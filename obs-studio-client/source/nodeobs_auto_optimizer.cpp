/******************************************************************************
    Copyright (C) 2016-2019 by Streamlabs (General Workings Inc)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

******************************************************************************/

#include "nodeobs_auto_optimizer.hpp"

#include "auto-optimizer-client-contract.hpp"
#include "controller.hpp"
#include "osn-error.hpp"
#include "polling-pacer.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using autoOptimizer::clientContract::RunState;

constexpr std::chrono::milliseconds sleepInterval(33);

std::atomic<bool> workerStop{true};
struct EventData {
	std::string json;
	std::string error;
	bool terminal = false;
};

void CallQueuedEvent(Napi::Env env, Napi::Function jsCallback, void *, EventData *event);
using EventThreadSafeFunction = Napi::TypedThreadSafeFunction<void, EventData, CallQueuedEvent>;

EventThreadSafeFunction jsThread;
bool jsThreadActive = false;
std::thread *workerThread = nullptr;
std::mutex sessionMutex;
std::mutex lifecycleMutex;
std::string activeSessionId;
std::shared_ptr<const autoOptimizer::clientContract::RequestContext> activeRequestContext;
int64_t lastEventSequence = -1;

enum class CallbackShutdownMode { Release, Abort };

std::string GetActiveSessionId()
{
	std::lock_guard<std::mutex> lock(sessionMutex);
	return activeSessionId;
}

void SetActiveSessionId(const std::string &sessionId)
{
	std::lock_guard<std::mutex> lock(sessionMutex);
	activeSessionId = sessionId;
	if (sessionId.empty())
		activeRequestContext.reset();
}

std::shared_ptr<const autoOptimizer::clientContract::RequestContext> GetActiveRequestContext()
{
	std::lock_guard<std::mutex> lock(sessionMutex);
	return activeRequestContext;
}

void SetActiveSession(const std::string &sessionId, std::shared_ptr<const autoOptimizer::clientContract::RequestContext> context)
{
	std::lock_guard<std::mutex> lock(sessionMutex);
	activeSessionId = sessionId;
	activeRequestContext = std::move(context);
}

std::string ResponseError(const std::vector<ipc::value> &response)
{
	if (response.empty())
		return "Failed to make IPC call, verify IPC status.";
	if (response.size() == 1 && response[0].type == ipc::type::Null)
		return response[0].value_str.empty() ? "Failed to make IPC call, verify IPC status." : response[0].value_str;

	const auto error = static_cast<ErrorCode>(response[0].value_union.ui64);
	if (error == ErrorCode::Ok)
		return {};
	if (response.size() >= 2 && !response[1].value_str.empty())
		return response[1].value_str;
	return "IPC received error code " + std::to_string(static_cast<uint64_t>(error)) + ", no additional description provided.";
}

std::vector<ipc::value> CallServer(const char *method, std::vector<ipc::value> arguments = {}, size_t minimumResponseSize = 1)
{
	auto connection = Controller::GetInstance().GetConnection();
	if (!connection)
		throw std::runtime_error("Failed to obtain IPC connection.");

	auto response = connection->call_synchronous_helper("AutoOptimizer", method, std::move(arguments));
	const std::string error = ResponseError(response);
	if (!error.empty())
		throw std::runtime_error(error);
	if (response.size() < minimumResponseSize)
		throw std::runtime_error(std::string(method) + " returned an incomplete response");
	return response;
}

void BestEffortServerCall(const std::shared_ptr<ipc::client> &connection, const char *method, const std::string &sessionId)
{
	if (!connection || sessionId.empty())
		return;
	try {
		connection->call_synchronous_helper("AutoOptimizer", method, {ipc::value(sessionId)});
	} catch (...) {
		// Disconnect and environment teardown must always continue locally.
	}
}

void CallQueuedEvent(Napi::Env env, Napi::Function jsCallback, void *, EventData *event)
{
	std::unique_ptr<EventData> ownedEvent(event);
	if (static_cast<napi_env>(env) == nullptr)
		return;

	try {
		jsCallback.Call({Napi::String::New(env, event->json), Napi::String::New(env, event->error), Napi::Boolean::New(env, event->terminal)});
	} catch (...) {
	}
	if (env.IsExceptionPending())
		env.GetAndClearPendingException();
}

void DispatchEvent(EventData *data)
{
	if (!jsThreadActive || jsThread.NonBlockingCall(data) != napi_ok)
		delete data;
}

void Worker()
{
	osn::PollingPacer pacer(sleepInterval);
	while (!workerStop.load()) {
		const auto cycleStart = std::chrono::high_resolution_clock::now();
		try {
			const std::string sessionId = GetActiveSessionId();
			const auto context = GetActiveRequestContext();
			if (!sessionId.empty() && context) {
				auto response = CallServer("QueryAutoOptimizerSession", {ipc::value(sessionId)}, 2);
				std::optional<std::string> eventJson;
				if (response[1].type != ipc::type::Null)
					eventJson = response[1].value_str;
				const auto polledEvent = autoOptimizer::clientContract::decodePolledEvent(eventJson, sessionId, lastEventSequence, *context);
				if (polledEvent) {
					const auto &envelope = *polledEvent;
					auto *event = new EventData{envelope.json, envelope.error, envelope.terminal};
					if (envelope.valid)
						lastEventSequence = static_cast<int64_t>(envelope.sequence);
					DispatchEvent(event);
				}
			}
		} catch (...) {
			// A lost peer is bounded by the caller's deadline. Shutdown or an
			// explicit cancellation will stop and join this poller.
		}

		const auto cycleEnd = std::chrono::high_resolution_clock::now();
		const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(cycleEnd - cycleStart);
		if (pacer.finishCycle(duration))
			std::this_thread::sleep_for(pacer.sleepDuration());
	}
}

bool IsWorkerRunning()
{
	return !workerStop.load();
}

void StartWorker()
{
	if (IsWorkerRunning())
		return;
	if (workerThread) {
		if (workerThread->joinable())
			workerThread->join();
		delete workerThread;
		workerThread = nullptr;
	}
	workerStop.store(false);
	workerThread = new std::thread(Worker);
}

void StopWorker(CallbackShutdownMode mode)
{
	workerStop.store(true);
	if (workerThread && workerThread->joinable())
		workerThread->join();
	delete workerThread;
	workerThread = nullptr;
	if (jsThreadActive) {
		if (mode == CallbackShutdownMode::Abort)
			jsThread.Abort();
		else
			jsThread.Release();
		jsThreadActive = false;
		jsThread = EventThreadSafeFunction();
	}
}

void StopLocalSession(const std::string &sessionId, CallbackShutdownMode mode, bool clearSession)
{
	if (GetActiveSessionId() != sessionId)
		return;
	StopWorker(mode);
	if (clearSession)
		SetActiveSessionId("");
}

Napi::Value ParseJson(Napi::Env env, const std::string &json)
{
	Napi::Object jsonObject = env.Global().Get("JSON").As<Napi::Object>();
	Napi::Function parse = jsonObject.Get("parse").As<Napi::Function>();
	return parse.Call(jsonObject, {Napi::String::New(env, json)});
}

std::string StringifyJson(Napi::Env env, const Napi::Value &value)
{
	Napi::Object jsonObject = env.Global().Get("JSON").As<Napi::Object>();
	Napi::Function stringify = jsonObject.Get("stringify").As<Napi::Function>();
	Napi::Value result = stringify.Call(jsonObject, {value});
	if (env.IsExceptionPending() || !result.IsString())
		return {};
	return result.As<Napi::String>().Utf8Value();
}

Napi::Promise ResolvedPromise(Napi::Env env)
{
	auto deferred = Napi::Promise::Deferred::New(env);
	deferred.Resolve(env.Undefined());
	return deferred.Promise();
}

struct FinishOutcome {
	std::string resultJson;
	std::string failure;
	std::string closeFailure;
};

class AutoOptimizerRun;

class FinishWorker final : public Napi::AsyncWorker {
public:
	FinishWorker(AutoOptimizerRun *run, std::string sessionId, std::shared_ptr<const autoOptimizer::clientContract::RequestContext> context, bool cancel,
		     bool readResult, std::string preferredError);
	void Execute() override;
	void OnOK() override;
	void OnError(const Napi::Error &error) override;

private:
	AutoOptimizerRun *run;
	std::string sessionId;
	std::shared_ptr<const autoOptimizer::clientContract::RequestContext> context;
	bool cancel;
	bool readResult;
	std::string preferredError;
	FinishOutcome outcome;
};

struct RunInitialization {
	std::string sessionId;
	Napi::Function progressCallback;
	std::shared_ptr<const autoOptimizer::clientContract::RequestContext> context;
};

class AutoOptimizerRun final : public Napi::ObjectWrap<AutoOptimizerRun> {
public:
	static Napi::FunctionReference constructor;

	static void Init(Napi::Env env)
	{
		Napi::Function function = DefineClass(env, "AutoOptimizerRun",
						      {InstanceAccessor("result", &AutoOptimizerRun::GetResult, nullptr),
						       InstanceMethod("confirmProbeIngest", &AutoOptimizerRun::ConfirmProbeIngest),
						       InstanceMethod("cancel", &AutoOptimizerRun::Cancel)});
		constructor = Napi::Persistent(function);
		constructor.SuppressDestruct();
	}

	static Napi::Object New(Napi::Env env, RunInitialization &initialization)
	{
		return constructor.New({Napi::External<RunInitialization>::New(env, &initialization)});
	}

	explicit AutoOptimizerRun(const Napi::CallbackInfo &info) : Napi::ObjectWrap<AutoOptimizerRun>(info), resultDeferred(Napi::Promise::Deferred::New(info.Env()))
	{
		if (info.Length() != 1 || !info[0].IsExternal()) {
			Napi::TypeError::New(info.Env(), "AutoOptimizerRun cannot be constructed directly").ThrowAsJavaScriptException();
			return;
		}
		auto *initialization = info[0].As<Napi::External<RunInitialization>>().Data();
		sessionId = initialization->sessionId;
		context = initialization->context;
		progressCallback = Napi::Persistent(initialization->progressCallback);
		resultPromise = Napi::Persistent(resultDeferred.Promise());
	}

	~AutoOptimizerRun() override
	{
		progressCallback.Reset();
		resultPromise.Reset();
		finishPromise.Reset();
	}

	void Activate(Napi::Env env)
	{
		SetActiveSession(sessionId, context);
		Ref();
		callbackReference = true;
		try {
			Napi::Function dispatch = Napi::Function::New(env, DispatchToRun, "AutoOptimizerEvent", this);
			jsThread = EventThreadSafeFunction::New(
				env, dispatch, Value(), "AutoOptimizerSession", 0, 1, nullptr,
				[](Napi::Env, AutoOptimizerRun *run, void *) { run->ReleaseCallbackReference(); }, this);
			jsThreadActive = true;
			jsThread.Unref(env);
			lastEventSequence = -1;
		} catch (...) {
			if (jsThreadActive) {
				jsThread.Abort();
				jsThreadActive = false;
				jsThread = EventThreadSafeFunction();
			} else {
				ReleaseCallbackReference();
			}
			throw;
		}
	}

	Napi::Promise BeginFinish(Napi::Env env, bool cancel, const std::string &preferredError = {})
	{
		if (state.isClosed())
			return ResolvedPromise(env);
		if (state.isFinishing())
			return finishPromise.Value();

		finishDeferred = std::make_unique<Napi::Promise::Deferred>(Napi::Promise::Deferred::New(env));
		finishPromise = Napi::Persistent(finishDeferred->Promise());
		Napi::Promise attemptPromise = finishPromise.Value();
		// Automatic terminal cleanup does not expose this attempt promise. Mark
		// its rejection handled while returning the original promise unchanged
		// to explicit cancel() callers.
		Napi::Object promise = attemptPromise;
		Napi::Function catchFunction = promise.Get("catch").As<Napi::Function>();
		catchFunction.Call(promise, {Napi::Function::New(env, [](const Napi::CallbackInfo &info) { return info.Env().Undefined(); })});

		state.beginFinish();
		const bool readResult = !resultSettled && preferredError.empty();
		Ref();
		try {
			auto worker = std::make_unique<FinishWorker>(this, sessionId, context, cancel, readResult, preferredError);
			worker->Queue();
			worker.release();
		} catch (const std::exception &error) {
			HandleFinishSchedulingFailure(env, preferredError.empty() ? error.what() : preferredError);
		} catch (...) {
			HandleFinishSchedulingFailure(env, preferredError.empty() ? "Failed to schedule Auto Optimizer cleanup" : preferredError);
		}
		return attemptPromise;
	}

	void CompleteFinish(Napi::Env env, FinishOutcome outcome)
	{
		const bool closeSucceeded = outcome.closeFailure.empty();
		state.finishAttempt(closeSucceeded);

		if (!resultSettled) {
			resultSettled = true;
			std::string failure = std::move(outcome.failure);
			if (failure.empty() && !closeSucceeded)
				failure = outcome.closeFailure;

			Napi::Value result = env.Undefined();
			if (failure.empty()) {
				try {
					result = ParseJson(env, outcome.resultJson);
				} catch (const std::exception &error) {
					failure = error.what();
				} catch (...) {
					failure = "Native Auto Optimizer returned malformed result JSON";
				}
				if (env.IsExceptionPending()) {
					env.GetAndClearPendingException();
					failure = "Native Auto Optimizer returned malformed result JSON";
				}
			}

			if (failure.empty())
				resultDeferred.Resolve(result);
			else
				resultDeferred.Reject(Napi::Error::New(env, failure).Value());
		}

		if (closeSucceeded)
			finishDeferred->Resolve(env.Undefined());
		else
			finishDeferred->Reject(Napi::Error::New(env, outcome.closeFailure).Value());

		finishDeferred.reset();
		finishPromise.Reset();
		// BeginFinish owns one reference until this asynchronous attempt has
		// settled. The callback reference is released separately by the TSFN
		// finalizer after every queued dispatch has drained.
		Unref();
	}

private:
	void HandleFinishSchedulingFailure(Napi::Env env, const std::string &error)
	{
		if (env.IsExceptionPending())
			env.GetAndClearPendingException();
		state.finishAttempt(false);
		if (!resultSettled) {
			resultSettled = true;
			resultDeferred.Reject(Napi::Error::New(env, error).Value());
		}
		finishDeferred->Reject(Napi::Error::New(env, error).Value());
		finishDeferred.reset();
		finishPromise.Reset();
		Unref();
	}

	static void DispatchToRun(const Napi::CallbackInfo &info)
	{
		auto *run = static_cast<AutoOptimizerRun *>(info.Data());
		if (!run || info.Length() < 3 || !info[0].IsString() || !info[1].IsString() || !info[2].IsBoolean())
			return;
		run->HandleEvent(info.Env(), info[0].As<Napi::String>().Utf8Value(), info[1].As<Napi::String>().Utf8Value(),
				 info[2].As<Napi::Boolean>().Value());
	}

	void ReleaseCallbackReference()
	{
		if (!callbackReference)
			return;
		callbackReference = false;
		Unref();
	}

	void HandleEvent(Napi::Env env, const std::string &json, const std::string &validationError, bool terminal)
	{
		if (resultSettled || state.isClosed() || state.isFinishing())
			return;
		if (!validationError.empty()) {
			BeginFinish(env, true, validationError);
			return;
		}

		Napi::Value event = env.Undefined();
		try {
			event = ParseJson(env, json);
		} catch (...) {
			if (env.IsExceptionPending())
				env.GetAndClearPendingException();
			BeginFinish(env, true, "Native Auto Optimizer returned malformed event JSON");
			return;
		}
		if (env.IsExceptionPending()) {
			env.GetAndClearPendingException();
			BeginFinish(env, true, "Native Auto Optimizer returned malformed event JSON");
			return;
		}

		try {
			progressCallback.Call({event});
		} catch (...) {
		}
		if (env.IsExceptionPending())
			env.GetAndClearPendingException();
		if (terminal)
			BeginFinish(env, false);
	}

	Napi::Value GetResult(const Napi::CallbackInfo &) { return resultPromise.Value(); }

	Napi::Value ConfirmProbeIngest(const Napi::CallbackInfo &info)
	{
		if (state.isClosed() || sessionId.empty()) {
			Napi::Error::New(info.Env(), "Auto Optimizer run is already closed").ThrowAsJavaScriptException();
			return info.Env().Undefined();
		}
		if (info.Length() != 2 || !info[0].IsString() || !info[1].IsBoolean()) {
			Napi::TypeError::New(info.Env(), "confirmProbeIngest expects (probeId: string, received: boolean)").ThrowAsJavaScriptException();
			return info.Env().Undefined();
		}
		const std::string probeId = info[0].As<Napi::String>().Utf8Value();
		if (probeId.empty()) {
			Napi::TypeError::New(info.Env(), "confirmProbeIngest expects a non-empty probeId").ThrowAsJavaScriptException();
			return info.Env().Undefined();
		}

		try {
			CallServer("ConfirmAutoOptimizerProbeIngest", {ipc::value(sessionId), ipc::value(probeId),
								    ipc::value(static_cast<uint32_t>(info[1].As<Napi::Boolean>().Value() ? 1 : 0))});
		} catch (const std::exception &error) {
			Napi::Error::New(info.Env(), error.what()).ThrowAsJavaScriptException();
		}
		return info.Env().Undefined();
	}

	Napi::Value Cancel(const Napi::CallbackInfo &info) { return BeginFinish(info.Env(), true); }

	std::string sessionId;
	std::shared_ptr<const autoOptimizer::clientContract::RequestContext> context;
	Napi::FunctionReference progressCallback;
	Napi::Promise::Deferred resultDeferred;
	Napi::Reference<Napi::Promise> resultPromise;
	std::unique_ptr<Napi::Promise::Deferred> finishDeferred;
	Napi::Reference<Napi::Promise> finishPromise;
	RunState state;
	bool resultSettled = false;
	bool callbackReference = false;
};

Napi::FunctionReference AutoOptimizerRun::constructor;

FinishWorker::FinishWorker(AutoOptimizerRun *run, std::string sessionId, std::shared_ptr<const autoOptimizer::clientContract::RequestContext> context, bool cancel,
			   bool readResult, std::string preferredError)
	: Napi::AsyncWorker(run->Env()),
	  run(run),
	  sessionId(std::move(sessionId)),
	  context(std::move(context)),
	  cancel(cancel),
	  readResult(readResult),
	  preferredError(std::move(preferredError))
{
}

void FinishWorker::Execute()
{
	try {
		std::lock_guard<std::mutex> lock(lifecycleMutex);
		outcome.failure = preferredError;
		if (cancel) {
			try {
				CallServer("CancelAutoOptimizerSession", {ipc::value(sessionId)});
			} catch (const std::exception &error) {
				if (outcome.failure.empty())
					outcome.failure = error.what();
			}
		}

		if (readResult) {
			try {
				auto response = CallServer("GetAutoOptimizerResult", {ipc::value(sessionId)}, 2);
				const auto projected = autoOptimizer::clientContract::projectResult(response[1].value_str, sessionId, *context);
				outcome.resultJson = projected.json;
				if (!projected.valid && outcome.failure.empty())
					outcome.failure = projected.error;
			} catch (const std::exception &error) {
				if (outcome.failure.empty())
					outcome.failure = error.what();
			}
		}

		try {
			CallServer("CloseAutoOptimizerSession", {ipc::value(sessionId)});
		} catch (const std::exception &error) {
			outcome.closeFailure = error.what();
		}
		StopLocalSession(sessionId, CallbackShutdownMode::Release, outcome.closeFailure.empty());
	} catch (const std::exception &error) {
		if (outcome.closeFailure.empty())
			outcome.closeFailure = error.what();
	} catch (...) {
		if (outcome.closeFailure.empty())
			outcome.closeFailure = "Unexpected Auto Optimizer cleanup failure";
	}
}

void FinishWorker::OnOK()
{
	run->CompleteFinish(Env(), std::move(outcome));
}

void FinishWorker::OnError(const Napi::Error &error)
{
	if (outcome.closeFailure.empty())
		outcome.closeFailure = error.Message();
	run->CompleteFinish(Env(), std::move(outcome));
}

Napi::Value Run(const Napi::CallbackInfo &info)
{
	if (info.Length() != 2 || !info[0].IsObject() || !info[1].IsFunction()) {
		Napi::TypeError::New(info.Env(), "AutoOptimizer.run expects (request: object, onProgress: function)").ThrowAsJavaScriptException();
		return info.Env().Undefined();
	}

	std::string publicRequestJson;
	try {
		publicRequestJson = StringifyJson(info.Env(), info[0]);
	} catch (...) {
	}
	if (info.Env().IsExceptionPending())
		return info.Env().Undefined();
	if (publicRequestJson.empty()) {
		Napi::TypeError::New(info.Env(), "AutoOptimizer.run expects a JSON-serializable request object").ThrowAsJavaScriptException();
		return info.Env().Undefined();
	}
	auto prepared = autoOptimizer::clientContract::prepareRequest(publicRequestJson);
	publicRequestJson.clear();
	if (!prepared.valid) {
		Napi::TypeError::New(info.Env(), prepared.error).ThrowAsJavaScriptException();
		return info.Env().Undefined();
	}
	std::string requestJson = std::move(prepared.wireJson);
	auto context = std::make_shared<const autoOptimizer::clientContract::RequestContext>(std::move(prepared.context));

	std::string startFailure;
	std::string createdSessionId;
	Napi::Object instance;
	{
		std::lock_guard<std::mutex> lock(lifecycleMutex);
		if (!GetActiveSessionId().empty() || IsWorkerRunning()) {
			Napi::Error::New(info.Env(), "An Auto Optimizer session is already active").ThrowAsJavaScriptException();
			return info.Env().Undefined();
		}

		try {
			auto response = CallServer("CreateAutoOptimizerSession", {ipc::value(requestJson)}, 2);
			requestJson.clear();
			createdSessionId = response[1].value_str;
			if (createdSessionId.empty())
				throw std::runtime_error("CreateAutoOptimizerSession returned an empty sessionId");
		} catch (const std::exception &error) {
			Napi::Error::New(info.Env(), error.what()).ThrowAsJavaScriptException();
			return info.Env().Undefined();
		}

		RunInitialization initialization{createdSessionId, info[1].As<Napi::Function>(), context};
		try {
			instance = AutoOptimizerRun::New(info.Env(), initialization);
			auto *run = AutoOptimizerRun::Unwrap(instance);
			run->Activate(info.Env());
			CallServer("StartAutoOptimizerSession", {ipc::value(createdSessionId)});
			StartWorker();
		} catch (const std::exception &error) {
			startFailure = error.what();
		} catch (...) {
			startFailure = "Failed to start Auto Optimizer session";
		}
	}

	if (info.Env().IsExceptionPending()) {
		const auto error = info.Env().GetAndClearPendingException();
		if (startFailure.empty())
			startFailure = error.Message();
	}
	if (!startFailure.empty()) {
		if (instance.IsEmpty()) {
			std::lock_guard<std::mutex> lock(lifecycleMutex);
			auto connection = Controller::GetInstance().GetConnection();
			BestEffortServerCall(connection, "CancelAutoOptimizerSession", createdSessionId);
			BestEffortServerCall(connection, "CloseAutoOptimizerSession", createdSessionId);
			StopLocalSession(createdSessionId, CallbackShutdownMode::Release, true);
			Napi::Error::New(info.Env(), startFailure).ThrowAsJavaScriptException();
			return info.Env().Undefined();
		}
		AutoOptimizerRun::Unwrap(instance)->BeginFinish(info.Env(), true, startFailure);
	}

	return instance;
}
} // namespace

void autoOptimizer::Shutdown()
{
	std::lock_guard<std::mutex> lock(lifecycleMutex);
	const std::string sessionId = GetActiveSessionId();
	if (sessionId.empty()) {
		StopWorker(CallbackShutdownMode::Abort);
		return;
	}

	workerStop.store(true);
	auto connection = Controller::GetInstance().GetConnection();
	BestEffortServerCall(connection, "CancelAutoOptimizerSession", sessionId);
	BestEffortServerCall(connection, "CloseAutoOptimizerSession", sessionId);
	StopWorker(CallbackShutdownMode::Abort);
	SetActiveSessionId("");
}

void autoOptimizer::Init(Napi::Env env, Napi::Object exports)
{
	env.AddCleanupHook([]() { autoOptimizer::Shutdown(); });
	AutoOptimizerRun::Init(env);
	Napi::Object api = Napi::Object::New(env);
	api.Set("run", Napi::Function::New(env, Run, "run"));
	exports.Set("AutoOptimizer", api);
}
