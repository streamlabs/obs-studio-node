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

#include "nodeobs_autoconfig.hpp"
#include "polling-pacer.hpp"
#include "shared.hpp"
#include "utility-v8.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

namespace {
struct AutoConfigEvent {
	uint32_t schemaVersion = 0;
	std::string sessionId;
	uint64_t sequence = 0;
	std::string type;
	std::string phase;
	double progress = 0;
	std::string code;
	std::string legId;
	std::string measurementMode;
	std::string probeId;
	std::string provider;
	uint32_t targetBitrateKbps = 0;
	std::string encoderId;
	std::string encoderFamily;
	std::string encoderTitle;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t fpsNum = 0;
	uint32_t fpsDen = 0;
	uint32_t selectedBitrateKbps = 0;
	uint32_t availableBitrateKbps = 0;
};

std::atomic<bool> workerStop{true};
constexpr std::chrono::milliseconds sleepInterval(33);
Napi::ThreadSafeFunction jsThread;
bool jsThreadActive = false;
std::thread *workerThread = nullptr;
std::mutex sessionMutex;
std::mutex lifecycleMutex;
std::string activeSessionId;

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
}

uint64_t ReadUnsigned(const ipc::value &value)
{
	switch (value.type) {
	case ipc::type::UInt32:
		return value.value_union.ui32;
	case ipc::type::UInt64:
		return value.value_union.ui64;
	case ipc::type::Int32:
		return value.value_union.i32 < 0 ? 0 : static_cast<uint64_t>(value.value_union.i32);
	case ipc::type::Int64:
		return value.value_union.i64 < 0 ? 0 : static_cast<uint64_t>(value.value_union.i64);
	default:
		return 0;
	}
}

double ReadDouble(const ipc::value &value)
{
	switch (value.type) {
	case ipc::type::Float:
		return value.value_union.fp32;
	case ipc::type::Double:
		return value.value_union.fp64;
	case ipc::type::Int32:
		return value.value_union.i32;
	case ipc::type::Int64:
		return static_cast<double>(value.value_union.i64);
	case ipc::type::UInt32:
		return value.value_union.ui32;
	case ipc::type::UInt64:
		return static_cast<double>(value.value_union.ui64);
	default:
		return 0;
	}
}

bool GetSessionArgument(const Napi::CallbackInfo &info, const char *method, std::string &sessionId)
{
	if (info.Length() < 1 || !info[0].IsString()) {
		Napi::TypeError::New(info.Env(), std::string(method) + " expects (sessionId: string)").ThrowAsJavaScriptException();
		return false;
	}

	sessionId = info[0].As<Napi::String>().Utf8Value();
	if (sessionId.empty()) {
		Napi::TypeError::New(info.Env(), std::string(method) + " expects a non-empty sessionId").ThrowAsJavaScriptException();
		return false;
	}

	return true;
}

void DispatchEvent(AutoConfigEvent *event)
{
	auto callback = [](Napi::Env env, Napi::Function jsCallback, AutoConfigEvent *eventData) {
		try {
			Napi::Object result = Napi::Object::New(env);
			result.Set("schemaVersion", Napi::Number::New(env, eventData->schemaVersion));
			result.Set("sessionId", Napi::String::New(env, eventData->sessionId));
			result.Set("sequence", Napi::Number::New(env, static_cast<double>(eventData->sequence)));
			result.Set("type", Napi::String::New(env, eventData->type));
			result.Set("phase", Napi::String::New(env, eventData->phase));
			result.Set("progress", Napi::Number::New(env, eventData->progress));

			if (!eventData->code.empty())
				result.Set("code", Napi::String::New(env, eventData->code));
			if (!eventData->legId.empty())
				result.Set("legId", Napi::String::New(env, eventData->legId));
			if (!eventData->measurementMode.empty())
				result.Set("measurementMode", Napi::String::New(env, eventData->measurementMode));
			if (!eventData->probeId.empty())
				result.Set("probeId", Napi::String::New(env, eventData->probeId));
			if (!eventData->provider.empty())
				result.Set("provider", Napi::String::New(env, eventData->provider));
			if (eventData->targetBitrateKbps > 0)
				result.Set("targetBitrateKbps", Napi::Number::New(env, eventData->targetBitrateKbps));
			if (!eventData->encoderId.empty())
				result.Set("encoderId", Napi::String::New(env, eventData->encoderId));
			if (!eventData->encoderFamily.empty())
				result.Set("encoderFamily", Napi::String::New(env, eventData->encoderFamily));
			if (!eventData->encoderTitle.empty())
				result.Set("encoderTitle", Napi::String::New(env, eventData->encoderTitle));
			if (eventData->width > 0)
				result.Set("width", Napi::Number::New(env, eventData->width));
			if (eventData->height > 0)
				result.Set("height", Napi::Number::New(env, eventData->height));
			if (eventData->fpsNum > 0)
				result.Set("fpsNum", Napi::Number::New(env, eventData->fpsNum));
			if (eventData->fpsDen > 0)
				result.Set("fpsDen", Napi::Number::New(env, eventData->fpsDen));
			if (eventData->selectedBitrateKbps > 0)
				result.Set("selectedBitrateKbps", Napi::Number::New(env, eventData->selectedBitrateKbps));
			if (eventData->availableBitrateKbps > 0)
				result.Set("availableBitrateKbps", Napi::Number::New(env, eventData->availableBitrateKbps));

			jsCallback.Call({result});
		} catch (...) {
		}
		delete eventData;
	};

	if (jsThread.NonBlockingCall(event, callback) != napi_ok)
		delete event;
}

void Worker()
{
	osn::PollingPacer pacer(sleepInterval);

	while (!workerStop.load()) {
		const auto cycleStart = std::chrono::high_resolution_clock::now();
		try {
			auto conn = Controller::GetInstance().GetConnection();
			const std::string sessionId = GetActiveSessionId();

			if (conn && !sessionId.empty()) {
				std::vector<ipc::value> response =
					conn->call_synchronous_helper("AutoConfig", "QueryAutoConfigSession", {ipc::value(sessionId)});
				if (response.size() >= 12 && static_cast<ErrorCode>(response[0].value_union.ui64) == ErrorCode::Ok) {
					auto *event = new AutoConfigEvent;
					event->schemaVersion = static_cast<uint32_t>(ReadUnsigned(response[1]));
					event->sessionId = response[2].value_str;
					event->sequence = ReadUnsigned(response[3]);
					event->type = response[4].value_str;
					event->phase = response[5].value_str;
					event->progress = ReadDouble(response[6]);
					event->code = response[7].value_str;
					event->legId = response[8].value_str;
					event->measurementMode = response[9].value_str;
					event->probeId = response[10].value_str;
					event->provider = response[11].value_str;
					if (response.size() >= 13)
						event->targetBitrateKbps = static_cast<uint32_t>(ReadUnsigned(response[12]));
					if (response.size() >= 22) {
						event->encoderId = response[13].value_str;
						event->encoderFamily = response[14].value_str;
						event->encoderTitle = response[15].value_str;
						event->width = static_cast<uint32_t>(ReadUnsigned(response[16]));
						event->height = static_cast<uint32_t>(ReadUnsigned(response[17]));
						event->fpsNum = static_cast<uint32_t>(ReadUnsigned(response[18]));
						event->fpsDen = static_cast<uint32_t>(ReadUnsigned(response[19]));
						event->selectedBitrateKbps = static_cast<uint32_t>(ReadUnsigned(response[20]));
						event->availableBitrateKbps = static_cast<uint32_t>(ReadUnsigned(response[21]));
					}

					if (event->sessionId == sessionId)
						DispatchEvent(event);
					else
						delete event;
				}
			}
		} catch (...) {
			// A peer disappearing must never escape the native polling thread.
			// Explicit IPC disconnect and environment teardown set workerStop and
			// perform the corresponding join/ThreadSafeFunction abort.
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
		jsThread = Napi::ThreadSafeFunction();
	}
}

void StopLocalSession(const std::string &sessionId, CallbackShutdownMode mode)
{
	std::lock_guard<std::mutex> lock(lifecycleMutex);
	if (GetActiveSessionId() != sessionId)
		return;
	StopWorker(mode);
	SetActiveSessionId("");
}

void BestEffortServerCall(const std::shared_ptr<ipc::client> &conn, const char *method, const std::string &sessionId)
{
	if (!conn || sessionId.empty())
		return;
	try {
		conn->call_synchronous_helper("AutoConfig", method, {ipc::value(sessionId)});
	} catch (...) {
		// Disconnect and environment teardown must always continue locally.
	}
}

Napi::Value GetAutoConfigCapabilities(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AutoConfig", "GetAutoConfigCapabilities", {});
	if (!ValidateResponse(info, response) || response.size() < 2)
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

Napi::Value CreateAutoConfigSession(const Napi::CallbackInfo &info)
{
	if (info.Length() < 2 || !info[0].IsString() || !info[1].IsFunction()) {
		Napi::TypeError::New(info.Env(), "CreateAutoConfigSession expects (requestJson: string, callback)").ThrowAsJavaScriptException();
		return info.Env().Undefined();
	}

	if (IsWorkerRunning()) {
		Napi::Error::New(info.Env(), "An AutoConfig session is already active").ThrowAsJavaScriptException();
		return info.Env().Undefined();
	}

	const std::string requestJson = info[0].As<Napi::String>().Utf8Value();
	if (requestJson.empty()) {
		Napi::TypeError::New(info.Env(), "CreateAutoConfigSession expects non-empty request JSON").ThrowAsJavaScriptException();
		return info.Env().Undefined();
	}

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AutoConfig", "CreateAutoConfigSession", {ipc::value(requestJson)});
	if (!ValidateResponse(info, response) || response.size() < 2)
		return info.Env().Undefined();

	const std::string sessionId = response[1].value_str;
	if (sessionId.empty()) {
		Napi::Error::New(info.Env(), "CreateAutoConfigSession returned an empty sessionId").ThrowAsJavaScriptException();
		return info.Env().Undefined();
	}

	jsThread = Napi::ThreadSafeFunction::New(info.Env(), info[1].As<Napi::Function>(), "AutoConfigSession", 0, 1, [](Napi::Env) {});
	// The poller must not keep a renderer's Node environment alive. Its cleanup
	// hook below owns the final abort/join if JavaScript never closes the session.
	jsThread.Unref(info.Env());
	jsThreadActive = true;
	SetActiveSessionId(sessionId);
	StartWorker();

	return Napi::String::New(info.Env(), sessionId);
}

Napi::Value StartAutoConfigSession(const Napi::CallbackInfo &info)
{
	std::string sessionId;
	if (!GetSessionArgument(info, "StartAutoConfigSession", sessionId))
		return info.Env().Undefined();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AutoConfig", "StartAutoConfigSession", {ipc::value(sessionId)});
	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return info.Env().Undefined();
}

Napi::Value ConfirmAutoConfigProbeIngest(const Napi::CallbackInfo &info)
{
	if (info.Length() < 3 || !info[0].IsString() || !info[1].IsString() || !info[2].IsBoolean()) {
		Napi::TypeError::New(info.Env(), "ConfirmAutoConfigProbeIngest expects (sessionId: string, probeId: string, received: boolean)")
			.ThrowAsJavaScriptException();
		return info.Env().Undefined();
	}
	const std::string sessionId = info[0].As<Napi::String>().Utf8Value();
	const std::string probeId = info[1].As<Napi::String>().Utf8Value();
	if (sessionId.empty() || probeId.empty()) {
		Napi::TypeError::New(info.Env(), "ConfirmAutoConfigProbeIngest expects non-empty sessionId and probeId").ThrowAsJavaScriptException();
		return info.Env().Undefined();
	}
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();
	std::vector<ipc::value> response = conn->call_synchronous_helper("AutoConfig", "ConfirmAutoConfigProbeIngest",
									 {ipc::value(sessionId), ipc::value(probeId),
									  ipc::value((uint32_t)(info[2].As<Napi::Boolean>().Value() ? 1 : 0))});
	if (!ValidateResponse(info, response))
		return info.Env().Undefined();
	return info.Env().Undefined();
}

Napi::Value GetAutoConfigResult(const Napi::CallbackInfo &info)
{
	std::string sessionId;
	if (!GetSessionArgument(info, "GetAutoConfigResult", sessionId))
		return info.Env().Undefined();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AutoConfig", "GetAutoConfigResult", {ipc::value(sessionId)});
	if (!ValidateResponse(info, response) || response.size() < 2)
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

Napi::Value CancelAutoConfigSession(const Napi::CallbackInfo &info)
{
	std::string sessionId;
	if (!GetSessionArgument(info, "CancelAutoConfigSession", sessionId))
		return info.Env().Undefined();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AutoConfig", "CancelAutoConfigSession", {ipc::value(sessionId)});
	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	// Cancellation is awaitable: polling continues until cleanup emits its
	// terminal event, after which the caller closes the session.
	return info.Env().Undefined();
}

Napi::Value CloseAutoConfigSession(const Napi::CallbackInfo &info)
{
	std::string sessionId;
	if (!GetSessionArgument(info, "CloseAutoConfigSession", sessionId))
		return info.Env().Undefined();

	auto conn = GetConnection(info);
	if (!conn) {
		StopLocalSession(sessionId, CallbackShutdownMode::Release);
		return info.Env().Undefined();
	}

	try {
		std::vector<ipc::value> response = conn->call_synchronous_helper("AutoConfig", "CloseAutoConfigSession", {ipc::value(sessionId)});
		const bool responseIsValid = ValidateResponse(info, response);
		StopLocalSession(sessionId, CallbackShutdownMode::Release);
		if (!responseIsValid)
			return info.Env().Undefined();
	} catch (const std::exception &error) {
		StopLocalSession(sessionId, CallbackShutdownMode::Release);
		Napi::Error::New(info.Env(), error.what()).ThrowAsJavaScriptException();
		return info.Env().Undefined();
	} catch (...) {
		StopLocalSession(sessionId, CallbackShutdownMode::Release);
		Napi::Error::New(info.Env(), "CloseAutoConfigSession IPC call failed").ThrowAsJavaScriptException();
		return info.Env().Undefined();
	}

	return info.Env().Undefined();
}
}

void autoConfig::Shutdown()
{
	std::lock_guard<std::mutex> lock(lifecycleMutex);
	const std::string sessionId = GetActiveSessionId();
	if (sessionId.empty()) {
		StopWorker(CallbackShutdownMode::Abort);
		return;
	}

	// Prevent new poll cycles while the server performs awaitable cancellation.
	// An already-running query may finish, so local joining remains mandatory.
	workerStop.store(true);
	auto conn = Controller::GetInstance().GetConnection();
	BestEffortServerCall(conn, "CancelAutoConfigSession", sessionId);
	BestEffortServerCall(conn, "CloseAutoConfigSession", sessionId);

	StopWorker(CallbackShutdownMode::Abort);
	SetActiveSessionId("");
}

void autoConfig::Init(Napi::Env env, Napi::Object exports)
{
	env.AddCleanupHook([]() { autoConfig::Shutdown(); });
	exports.Set("GetAutoConfigCapabilities", Napi::Function::New(env, GetAutoConfigCapabilities));
	exports.Set("CreateAutoConfigSession", Napi::Function::New(env, CreateAutoConfigSession));
	exports.Set("StartAutoConfigSession", Napi::Function::New(env, StartAutoConfigSession));
	exports.Set("ConfirmAutoConfigProbeIngest", Napi::Function::New(env, ConfirmAutoConfigProbeIngest));
	exports.Set("GetAutoConfigResult", Napi::Function::New(env, GetAutoConfigResult));
	exports.Set("CancelAutoConfigSession", Napi::Function::New(env, CancelAutoConfigSession));
	exports.Set("CloseAutoConfigSession", Napi::Function::New(env, CloseAutoConfigSession));
}
