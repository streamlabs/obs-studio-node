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

#include "nodeobs_service.hpp"
#include "controller.hpp"
#include "osn-error.hpp"
#include "utility-v8.hpp"

#include <node.h>
#include <sstream>
#include <string>
#include "shared.hpp"
#include "utility.hpp"
#include "video.hpp"

#ifdef WIN32

#include <shellapi.h>

#define TOTALBYTES 8192

enum VcamInstalledStatus : uint8_t { NotInstalled = 0, LegacyInstalled = 1, Installed = 2 };

#endif

bool service::isWorkerRunning = false;
bool service::worker_stop = true;
uint32_t service::sleepIntervalMS = 33;
std::thread *service::worker_thread = nullptr;
Napi::ThreadSafeFunction service::js_thread;
Napi::FunctionReference service::cb;

void service::start_worker(napi_env env, Napi::Function async_callback)
{
	if (isWorkerRunning)
		return;

	if (!worker_stop)
		return;

	worker_stop = false;
	js_thread = Napi::ThreadSafeFunction::New(env, async_callback, "NodeOBS_Service", 0, 1, [](Napi::Env) {});
	worker_thread = new std::thread(&service::worker);

	isWorkerRunning = true;
}

void service::stop_worker(void)
{
	if (!isWorkerRunning)
		return;

	if (worker_stop != false)
		return;

	worker_stop = true;
	if (worker_thread->joinable()) {
		worker_thread->join();
	}

	isWorkerRunning = false;
}

Napi::Value service::OBS_service_resetAudioContext(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("NodeOBS_Service", "OBS_service_resetAudioContext", {});
	return info.Env().Undefined();
}

Napi::Value service::OBS_service_resetVideoContext(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("NodeOBS_Service", "OBS_service_resetVideoContext", {});
	return info.Env().Undefined();
}

Napi::Value service::OBS_service_setVideoInfo(const Napi::CallbackInfo &info)
{
	osn::Video *canvas = Napi::ObjectWrap<osn::Video>::Unwrap(info[0].ToObject());
	if (!canvas) {
		Napi::TypeError::New(info.Env(), "Invalid canvas argument").ThrowAsJavaScriptException();
		return info.Env().Undefined();
	}

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("NodeOBS_Service", "OBS_service_setVideoInfo",
		   {ipc::value(canvas->canvasId), ipc::value(getServiceIdByName(info[1].ToString().Utf8Value()))});

	return info.Env().Undefined();
}

Napi::Value service::OBS_service_startStreaming(const Napi::CallbackInfo &info)
{
	start_worker(info.Env(), cb.Value());

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	if (info.Length() == 1) {
		conn->call("NodeOBS_Service", "OBS_service_startStreaming", {ipc::value(getServiceIdByName(info[0].ToString().Utf8Value()))});
	} else {
		conn->call("NodeOBS_Service", "OBS_service_startStreaming", {ipc::value(0)});
	}

	return info.Env().Undefined();
}

Napi::Value service::OBS_service_startRecording(const Napi::CallbackInfo &info)
{
	start_worker(info.Env(), cb.Value());

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("NodeOBS_Service", "OBS_service_startRecording", {});
	return info.Env().Undefined();
}

Napi::Value service::OBS_service_startReplayBuffer(const Napi::CallbackInfo &info)
{
	start_worker(info.Env(), cb.Value());

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("NodeOBS_Service", "OBS_service_startReplayBuffer", {});
	return info.Env().Undefined();
}

Napi::Value service::OBS_service_stopStreaming(const Napi::CallbackInfo &info)
{
	bool forceStop = info[0].ToBoolean().Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	if (info.Length() == 2) {
		conn->call("NodeOBS_Service", "OBS_service_stopStreaming",
			   {ipc::value(forceStop), ipc::value(getServiceIdByName(info[1].ToString().Utf8Value()))});
	} else {
		conn->call("NodeOBS_Service", "OBS_service_stopStreaming", {ipc::value(forceStop), ipc::value(0)});
	}

	return info.Env().Undefined();
}

Napi::Value service::OBS_service_stopRecording(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("NodeOBS_Service", "OBS_service_stopRecording", {});
	return info.Env().Undefined();
}

Napi::Value service::OBS_service_stopReplayBuffer(const Napi::CallbackInfo &info)
{
	bool forceStop = info[0].ToBoolean().Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("NodeOBS_Service", "OBS_service_stopReplayBuffer", {ipc::value(forceStop)});
	return info.Env().Undefined();
}

static v8::Persistent<v8::Object> serviceCallbackObject;

Napi::Value service::OBS_service_connectOutputSignals(const Napi::CallbackInfo &info)
{
	Napi::Function async_callback = info[0].As<Napi::Function>();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("NodeOBS_Service", "OBS_service_connectOutputSignals", {});

	cb = Napi::Persistent(async_callback);
	cb.SuppressDestruct();
	return Napi::Boolean::New(info.Env(), true);
}

Napi::Value service::OBS_service_processReplayBufferHotkey(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("NodeOBS_Service", "OBS_service_processReplayBufferHotkey", {});
	return info.Env().Undefined();
}

Napi::Value service::OBS_service_getLastReplay(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("NodeOBS_Service", "OBS_service_getLastReplay", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response.at(1).value_str);
}

Napi::Value service::OBS_service_getLastRecording(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("NodeOBS_Service", "OBS_service_getLastRecording", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response.at(1).value_str);
}

void service::OBS_service_splitFile(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("NodeOBS_Service", "OBS_service_splitFile", {});
}

int service::getServiceIdByName(std::string serviceName)
{
	if (serviceName == "horizontal" || serviceName == "default") {
		return 0;
	} else if (serviceName == "vertical") {
		return 1;
	}
	return 0;
}

std::string service::getServiceNameById(int serviceId)
{
	if (serviceId == 0) {
		return "default";
	} else if (serviceId == 1) {
		return "vertical";
	}
	return "default";
}

void service::worker()
{
	const static int maximum_signals_in_queue = 100;
	auto callback = [](Napi::Env env, Napi::Function jsCallback, ServiceSignalInfo *data) {
		try {
			Napi::Object result = Napi::Object::New(env);

			result.Set(Napi::String::New(env, "type"), Napi::String::New(env, data->outputType));
			result.Set(Napi::String::New(env, "signal"), Napi::String::New(env, data->signal));
			result.Set(Napi::String::New(env, "code"), Napi::Number::New(env, data->code));
			result.Set(Napi::String::New(env, "error"), Napi::String::New(env, data->errorMessage));
			result.Set(Napi::String::New(env, "service"), Napi::String::New(env, getServiceNameById(data->service)));

			jsCallback.Call({result});
		} catch (...) {
			data->tosend = true;
			return;
		}
		data->sent = true;
	};
	size_t totalSleepMS = 0;
	std::vector<ServiceSignalInfo *> signalsList;
	while (!worker_stop) {
		auto tp_start = std::chrono::high_resolution_clock::now();

		// Validate Connection
		auto conn = Controller::GetInstance().GetConnection();
		if (conn) {
			std::vector<ipc::value> response = conn->call_synchronous_helper("NodeOBS_Service", "Query", {});
			if ((response.size() == 6) && signalsList.size() < maximum_signals_in_queue) {
				ErrorCode error = (ErrorCode)response[0].value_union.ui64;
				if (error == ErrorCode::Ok) {
					ServiceSignalInfo *data = new ServiceSignalInfo{"", "", 0, ""};
					data->outputType = response[1].value_str;
					data->signal = response[2].value_str;
					data->code = response[3].value_union.i32;
					data->errorMessage = response[4].value_str;
					data->service = response[5].value_union.i32;
					data->sent = false;
					data->tosend = true;
					signalsList.push_back(data);
				}
			}

			std::vector<ServiceSignalInfo *>::iterator i = signalsList.begin();
			while (i != signalsList.end()) {
				if ((*i)->tosend) {
					(*i)->tosend = false;
					napi_status status = js_thread.BlockingCall((*i), callback);
					if (status != napi_ok) {
						(*i)->tosend = true;
						break;
					}
				}
				if ((*i)->sent) {
					i = signalsList.erase(i);
				} else {
					i++;
				}
			}
		}

		auto tp_end = std::chrono::high_resolution_clock::now();
		auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(tp_end - tp_start);
		totalSleepMS = sleepIntervalMS - dur.count();
		std::this_thread::sleep_for(std::chrono::milliseconds(totalSleepMS));
	}

	for (auto &signalData : signalsList) {
		delete signalData;
	}
	return;
}

Napi::Value service::OBS_service_removeCallback(const Napi::CallbackInfo &info)
{
	stop_worker();
	return info.Env().Undefined();
}

Napi::Value service::OBS_service_startVirtualCam(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("NodeOBS_Service", "OBS_service_startVirtualCam", {});
	return info.Env().Undefined();
}

Napi::Value service::OBS_service_stopVirtualCam(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("NodeOBS_Service", "OBS_service_stopVirtualCam", {});
	return info.Env().Undefined();
}

Napi::Value service::OBS_service_updateVirtualCam(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	const int sourceType = info[0].ToNumber().Int32Value();
	std::string sourceName;

	if (info.Length() == 2)
		sourceName = info[1].ToString().Utf8Value();

	conn->call("NodeOBS_Service", "OBS_service_updateVirtualCam", {ipc::value(sourceType), ipc::value(sourceName)});
	return info.Env().Undefined();
}

Napi::Value service::OBS_service_installVirtualCamPlugin(const Napi::CallbackInfo &info)
{
#ifdef WIN32
	SHELLEXECUTEINFO ShExecInfo = {0};
	ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
	ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
	ShExecInfo.hwnd = NULL;
	ShExecInfo.lpVerb = L"runas";
	ShExecInfo.lpFile = L"regsvr32.exe";
	ShExecInfo.lpDirectory = NULL;
	ShExecInfo.nShow = SW_HIDE;
	ShExecInfo.hInstApp = NULL;

	std::wstring pathToRegFile64 = L"/s /n /i:\"1\" \"" + utfWorkingDir;
	pathToRegFile64 += L"\\data\\obs-plugins\\win-dshow\\obs-virtualcam-module64.dll\"";
	ShExecInfo.lpParameters = pathToRegFile64.c_str();
	ShellExecuteEx(&ShExecInfo);
	WaitForSingleObject(ShExecInfo.hProcess, INFINITE);
	CloseHandle(ShExecInfo.hProcess);

	std::wstring pathToRegFile32 = L"/s /n /i:\"1\" \"" + utfWorkingDir;
	pathToRegFile32 += L"\\data\\obs-plugins\\win-dshow\\obs-virtualcam-module32.dll\"";
	ShExecInfo.lpParameters = pathToRegFile32.c_str();
	ShellExecuteEx(&ShExecInfo);
	WaitForSingleObject(ShExecInfo.hProcess, INFINITE);
	CloseHandle(ShExecInfo.hProcess);
#elif __APPLE__
	g_util_osx->installPlugin();
#endif
	return info.Env().Undefined();
}

Napi::Value service::OBS_service_uninstallVirtualCamPlugin(const Napi::CallbackInfo &info)
{
#ifdef WIN32
	SHELLEXECUTEINFO ShExecInfo = {0};
	ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
	ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
	ShExecInfo.hwnd = NULL;
	ShExecInfo.lpVerb = L"runas";
	ShExecInfo.lpFile = L"regsvr32.exe";
	ShExecInfo.lpDirectory = NULL;
	ShExecInfo.nShow = SW_HIDE;
	ShExecInfo.hInstApp = NULL;

	std::wstring pathToRegFile64 = L"/u \"" + utfWorkingDir;
	pathToRegFile64 += L"\\data\\obs-plugins\\win-dshow\\obs-virtualcam-module64.dll\"";
	ShExecInfo.lpParameters = pathToRegFile64.c_str();
	ShellExecuteEx(&ShExecInfo);
	WaitForSingleObject(ShExecInfo.hProcess, INFINITE);
	CloseHandle(ShExecInfo.hProcess);

	std::wstring pathToRegFile32 = L"/u \"" + utfWorkingDir;
	pathToRegFile32 += L"\\data\\obs-plugins\\win-dshow\\obs-virtualcam-module32.dll\"";
	ShExecInfo.lpParameters = pathToRegFile32.c_str();
	ShellExecuteEx(&ShExecInfo);
	WaitForSingleObject(ShExecInfo.hProcess, INFINITE);
	CloseHandle(ShExecInfo.hProcess);
#elif __APPLE__
	g_util_osx->uninstallPlugin();
#endif
	return info.Env().Undefined();
}

Napi::Value service::OBS_service_isVirtualCamPluginInstalled(const Napi::CallbackInfo &info)
{
#ifdef WIN32
	HKEY OpenResult;
	LONG error;

	error = RegOpenKeyEx(HKEY_CLASSES_ROOT, L"CLSID\\{27B05C2D-93DC-474A-A5DA-9BBA34CB2A9C}", 0, KEY_READ | KEY_WOW64_64KEY, &OpenResult);
	if (error != ERROR_SUCCESS) {
		return Napi::Number::New(info.Env(), VcamInstalledStatus::NotInstalled);
	}

	error = RegOpenKeyEx(HKEY_CLASSES_ROOT, L"CLSID\\{27B05C2D-93DC-474A-A5DA-9BBA34CB2A9C}", 0, KEY_READ | KEY_WOW64_32KEY, &OpenResult);
	if (error != ERROR_SUCCESS) {
		return Napi::Number::New(info.Env(), VcamInstalledStatus::NotInstalled);
	}

	DWORD dwRet = 0;
	DWORD cbData;
	TCHAR buf[TOTALBYTES] = {0};
	DWORD dwBufSize = sizeof(buf);
	error = RegOpenKeyEx(HKEY_CLASSES_ROOT, L"CLSID\\{27B05C2D-93DC-474A-A5DA-9BBA34CB2A9C}\\InprocServer32", 0, KEY_READ | KEY_QUERY_VALUE, &OpenResult);
	if (error == ERROR_SUCCESS && OpenResult) {
		dwRet = RegQueryValueExW(OpenResult, TEXT(""), NULL, NULL, (LPBYTE)buf, &dwBufSize);
		if (dwRet == ERROR_SUCCESS) {
			std::wstring fileName = std::wstring(buf);
			fileName = fileName.substr(fileName.find_last_of(L"/\\") + 1);
			if (fileName.compare(L"obs-virtualsource.dll") == 0)
				return Napi::Number::New(info.Env(), VcamInstalledStatus::LegacyInstalled);
			else
				return Napi::Number::New(info.Env(), VcamInstalledStatus::Installed);
		}
	}

	return Napi::Number::New(info.Env(), VcamInstalledStatus::NotInstalled);
#elif __APPLE__
	// Not implemented
	return info.Env().Undefined();
#endif
}

void service::Init(Napi::Env env, Napi::Object exports)
{
	exports.Set(Napi::String::New(env, "OBS_service_resetAudioContext"), Napi::Function::New(env, service::OBS_service_resetAudioContext));
	exports.Set(Napi::String::New(env, "OBS_service_resetVideoContext"), Napi::Function::New(env, service::OBS_service_resetVideoContext));
	exports.Set(Napi::String::New(env, "OBS_service_setVideoInfo"), Napi::Function::New(env, service::OBS_service_setVideoInfo));
	exports.Set(Napi::String::New(env, "OBS_service_startStreaming"), Napi::Function::New(env, service::OBS_service_startStreaming));
	exports.Set(Napi::String::New(env, "OBS_service_startRecording"), Napi::Function::New(env, service::OBS_service_startRecording));
	exports.Set(Napi::String::New(env, "OBS_service_startReplayBuffer"), Napi::Function::New(env, service::OBS_service_startReplayBuffer));
	exports.Set(Napi::String::New(env, "OBS_service_stopRecording"), Napi::Function::New(env, service::OBS_service_stopRecording));
	exports.Set(Napi::String::New(env, "OBS_service_stopStreaming"), Napi::Function::New(env, service::OBS_service_stopStreaming));
	exports.Set(Napi::String::New(env, "OBS_service_stopReplayBuffer"), Napi::Function::New(env, service::OBS_service_stopReplayBuffer));
	exports.Set(Napi::String::New(env, "OBS_service_connectOutputSignals"), Napi::Function::New(env, service::OBS_service_connectOutputSignals));
	exports.Set(Napi::String::New(env, "OBS_service_removeCallback"), Napi::Function::New(env, service::OBS_service_removeCallback));
	exports.Set(Napi::String::New(env, "OBS_service_processReplayBufferHotkey"), Napi::Function::New(env, service::OBS_service_processReplayBufferHotkey));
	exports.Set(Napi::String::New(env, "OBS_service_getLastReplay"), Napi::Function::New(env, service::OBS_service_getLastReplay));
	exports.Set(Napi::String::New(env, "OBS_service_getLastRecording"), Napi::Function::New(env, service::OBS_service_getLastRecording));
	exports.Set(Napi::String::New(env, "OBS_service_splitFile"), Napi::Function::New(env, service::OBS_service_splitFile));
	exports.Set(Napi::String::New(env, "OBS_service_startVirtualCam"), Napi::Function::New(env, service::OBS_service_startVirtualCam));
	exports.Set(Napi::String::New(env, "OBS_service_stopVirtualCam"), Napi::Function::New(env, service::OBS_service_stopVirtualCam));
	exports.Set(Napi::String::New(env, "OBS_service_updateVirtualCam"), Napi::Function::New(env, service::OBS_service_updateVirtualCam));
	exports.Set(Napi::String::New(env, "OBS_service_installVirtualCamPlugin"), Napi::Function::New(env, service::OBS_service_installVirtualCamPlugin));
	exports.Set(Napi::String::New(env, "OBS_service_uninstallVirtualCamPlugin"), Napi::Function::New(env, service::OBS_service_uninstallVirtualCamPlugin));
	exports.Set(Napi::String::New(env, "OBS_service_isVirtualCamPluginInstalled"),
		    Napi::Function::New(env, service::OBS_service_isVirtualCamPluginInstalled));
}
