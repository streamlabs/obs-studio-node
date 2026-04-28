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
#include "shared.hpp"

bool autoConfig::isWorkerRunning = false;
bool autoConfig::worker_stop = true;
uint32_t autoConfig::sleepIntervalMS = 33;
Napi::ThreadSafeFunction autoConfig::js_thread;
std::thread *autoConfig::worker_thread = nullptr;
std::vector<std::thread *> autoConfig::ac_queue_task_workers;

#ifdef WIN32
const char *ac_sem_name = nullptr; // Not used on Windows
HANDLE ac_sem;
#else
const char *ac_sem_name = "autoconfig-semaphore";
sem_t *ac_sem;
#endif

void autoConfig::worker()
{
	size_t totalSleepMS = 0;

	while (!worker_stop) {
		auto tp_start = std::chrono::high_resolution_clock::now();

		auto conn = Controller::GetInstance().GetConnection();
		if (!conn) {
			goto do_sleep;
		}

		{
			std::vector<ipc::value> response = conn->call_synchronous_helper("AutoConfig", "Query", {});
			if (!response.size() || (response.size() == 1)) {
				goto do_sleep;
			}

			ErrorCode error = (ErrorCode)response[0].value_union.ui64;
			if (error == ErrorCode::Ok) {
				AutoConfigInfo *data = new AutoConfigInfo;

				data->event = response[1].value_str;
				data->description = response[2].value_str;
				data->percentage = response[3].value_union.fp64;
				ac_queue_task_workers.push_back(new std::thread(&autoConfig::queueTask, data));
			}
		}

	do_sleep:
		auto tp_end = std::chrono::high_resolution_clock::now();
		auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(tp_end - tp_start);
		totalSleepMS = sleepIntervalMS - dur.count();
		std::this_thread::sleep_for(std::chrono::milliseconds(totalSleepMS));
	}
	return;
}

void autoConfig::start_worker()
{
	if (!worker_stop)
		return;

	worker_stop = false;
	ac_sem = create_semaphore(ac_sem_name);
	worker_thread = new std::thread(&autoConfig::worker);
}

void autoConfig::stop_worker()
{
	if (worker_stop != false)
		return;

	worker_stop = true;
	if (worker_thread->joinable()) {
		worker_thread->join();
	}
	for (auto queue_worker : ac_queue_task_workers) {
		if (queue_worker->joinable()) {
			queue_worker->join();
		}
	}
	remove_semaphore(ac_sem, ac_sem_name);
	js_thread.Release();
}

Napi::Value autoConfig::InitializeAutoConfig(const Napi::CallbackInfo &info)
{
	Napi::Function async_callback = info[0].As<Napi::Function>();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AutoConfig", "InitializeAutoConfig", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	js_thread = Napi::ThreadSafeFunction::New(info.Env(), async_callback, "AutoConfig", 0, 1, [](Napi::Env) {});

	start_worker();
	isWorkerRunning = true;

	return Napi::Boolean::New(info.Env(), true);
}

Napi::Value autoConfig::StartBandwidthTest(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AutoConfig", "StartBandwidthTest", {});
	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return info.Env().Undefined();
}

Napi::Value autoConfig::StartStreamEncoderTest(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AutoConfig", "StartStreamEncoderTest", {});
	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return info.Env().Undefined();
}

Napi::Value autoConfig::StartRecordingEncoderTest(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AutoConfig", "StartRecordingEncoderTest", {});
	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return info.Env().Undefined();
}

void autoConfig::queueTask(AutoConfigInfo *data)
{
	wait_semaphore(ac_sem);

	auto sources_callback = [](Napi::Env env, Napi::Function jsCallback, AutoConfigInfo *event_data) {
		try {
			Napi::Object result = Napi::Object::New(env);

			result.Set(Napi::String::New(env, "event"), Napi::String::New(env, event_data->event));
			result.Set(Napi::String::New(env, "description"), Napi::String::New(env, event_data->description));

			if (event_data->event.compare("error") != 0) {
				result.Set(Napi::String::New(env, "percentage"), Napi::Number::New(env, event_data->percentage));
			}
			result.Set(Napi::String::New(env, "continent"), Napi::String::New(env, ""));

			jsCallback.Call({result});
		} catch (...) {
		}
		delete event_data;
	};

	napi_status status = js_thread.NonBlockingCall(data, sources_callback);
	if (status != napi_ok) {
		delete data;
	}
	release_semaphore(ac_sem);
}

Napi::Value autoConfig::StartCheckSettings(const Napi::CallbackInfo &info)
{
	AutoConfigInfo *startData = new AutoConfigInfo;
	startData->event = "starting_step";
	startData->description = "checking_settings";
	startData->percentage = 0;
	ac_queue_task_workers.push_back(new std::thread(&autoConfig::queueTask, startData));

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AutoConfig", "StartCheckSettings", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	bool success = (bool)response[1].value_union.ui32;
	AutoConfigInfo *stopData = new AutoConfigInfo;
	if (!success) {
		stopData->event = "error";
		stopData->description = "invalid_settings";
	} else {
		stopData->event = "stopping_step";
		stopData->description = "checking_settings";
	}

	stopData->percentage = 100;
	ac_queue_task_workers.push_back(new std::thread(&autoConfig::queueTask, stopData));

	return info.Env().Undefined();
}

Napi::Value autoConfig::StartSetDefaultSettings(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AutoConfig", "StartSetDefaultSettings", {});
	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return info.Env().Undefined();
}

Napi::Value autoConfig::StartSaveStreamSettings(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AutoConfig", "StartSaveStreamSettings", {});
	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return info.Env().Undefined();
}

Napi::Value autoConfig::StartSaveSettings(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AutoConfig", "StartSaveSettings", {});
	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return info.Env().Undefined();
}

Napi::Value autoConfig::TerminateAutoConfig(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AutoConfig", "TerminateAutoConfig", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	if (isWorkerRunning)
		stop_worker();

	return info.Env().Undefined();
}

void autoConfig::Init(Napi::Env env, Napi::Object exports)
{
	exports.Set(Napi::String::New(env, "InitializeAutoConfig"), Napi::Function::New(env, autoConfig::InitializeAutoConfig));
	exports.Set(Napi::String::New(env, "StartBandwidthTest"), Napi::Function::New(env, autoConfig::StartBandwidthTest));
	exports.Set(Napi::String::New(env, "StartStreamEncoderTest"), Napi::Function::New(env, autoConfig::StartStreamEncoderTest));
	exports.Set(Napi::String::New(env, "StartRecordingEncoderTest"), Napi::Function::New(env, autoConfig::StartRecordingEncoderTest));
	exports.Set(Napi::String::New(env, "StartCheckSettings"), Napi::Function::New(env, autoConfig::StartCheckSettings));
	exports.Set(Napi::String::New(env, "StartSetDefaultSettings"), Napi::Function::New(env, autoConfig::StartSetDefaultSettings));
	exports.Set(Napi::String::New(env, "StartSaveStreamSettings"), Napi::Function::New(env, autoConfig::StartSaveStreamSettings));
	exports.Set(Napi::String::New(env, "StartSaveSettings"), Napi::Function::New(env, autoConfig::StartSaveSettings));
	exports.Set(Napi::String::New(env, "TerminateAutoConfig"), Napi::Function::New(env, autoConfig::TerminateAutoConfig));
}
