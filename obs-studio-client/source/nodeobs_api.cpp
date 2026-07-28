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

#include "controller.hpp"
#include "osn-error.hpp"
#include "nodeobs_api.hpp"
#include <sstream>
#include <string>
#include "shared.hpp"
#include "utility.hpp"
#include "volmeter.hpp"
#include "callback-manager.hpp"

//api::Worker* worker = nullptr;

Napi::ThreadSafeFunction js_thread;

Napi::Value api::OBS_API_initAPI(const Napi::CallbackInfo &info)
{
	std::string path;
	std::string language;
	std::string version;
	std::string crashserverurl;

	ASSERT_GET_VALUE(info, info[0], language);
	ASSERT_GET_VALUE(info, info[1], path);
	ASSERT_GET_VALUE(info, info[2], version);
	if (info.Length() > 3)
		ASSERT_GET_VALUE(info, info[3], crashserverurl);

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->set_freeze_callback(ipc_freeze_callback, path);

	std::vector<ipc::value> response = conn->call_synchronous_helper(
		"API", "OBS_API_initAPI", {ipc::value(path), ipc::value(language), ipc::value(version), ipc::value(crashserverurl)});

	// The API init method will return a response error + graphical error
	// If there is a problem with the IPC the number of responses here will be zero so we must validate the
	// response.
	// If the method call was sucessfull we will have 2 arguments, also there is no need to validate the
	// response
	if (response.size() < 2) {
		if (!ValidateResponse(info, response)) {
			return info.Env().Undefined();
		}
	}

	return Napi::Number::New(info.Env(), response[1].value_union.i32);
}

Napi::Value api::OBS_API_destroyOBS_API(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("API", "OBS_API_destroyOBS_API", {});

#ifdef __APPLE__
	if (js_thread)
		js_thread.Release();
#endif

	return info.Env().Undefined();
}

Napi::Value api::OBS_API_getModuleLoadFailures(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("API", "OBS_API_getModuleLoadFailures", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	if (response.size() < 2) {
		Napi::Error::New(info.Env(), "Malformed module load failure response.").ThrowAsJavaScriptException();
		return info.Env().Undefined();
	}

	const uint32_t count = response[1].value_union.ui32;
	const size_t expectedSize = 2 + static_cast<size_t>(count) * 3;
	if (response.size() < expectedSize) {
		Napi::Error::New(info.Env(), "Malformed module load failure response.").ThrowAsJavaScriptException();
		return info.Env().Undefined();
	}

	Napi::Array failures = Napi::Array::New(info.Env(), count);
	size_t responseIndex = 2;

	for (uint32_t i = 0; i < count; i++) {
		Napi::Object failure = Napi::Object::New(info.Env());
		failure.Set("module", Napi::String::New(info.Env(), response[responseIndex++].value_str));
		failure.Set("code", Napi::String::New(info.Env(), response[responseIndex++].value_str));
		failure.Set("message", Napi::String::New(info.Env(), response[responseIndex++].value_str));
		failures.Set(i, failure);
	}

	return failures;
}

Napi::Value api::OBS_API_getPerformanceStatistics(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("API", "OBS_API_getPerformanceStatistics", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	Napi::Object statistics = Napi::Object::New(info.Env());

	statistics.Set(Napi::String::New(info.Env(), "CPU"), Napi::Number::New(info.Env(), response[1].value_union.fp64));
	statistics.Set(Napi::String::New(info.Env(), "numberDroppedFrames"), Napi::Number::New(info.Env(), response[2].value_union.i32));
	statistics.Set(Napi::String::New(info.Env(), "percentageDroppedFrames"), Napi::Number::New(info.Env(), response[3].value_union.fp64));
	statistics.Set(Napi::String::New(info.Env(), "streamingBandwidth"), Napi::Number::New(info.Env(), response[4].value_union.fp64));
	statistics.Set(Napi::String::New(info.Env(), "streamingDataOutput"), Napi::Number::New(info.Env(), response[5].value_union.fp64));
	statistics.Set(Napi::String::New(info.Env(), "recordingBandwidth"), Napi::Number::New(info.Env(), response[6].value_union.fp64));
	statistics.Set(Napi::String::New(info.Env(), "recordingDataOutput"), Napi::Number::New(info.Env(), response[7].value_union.fp64));
	statistics.Set(Napi::String::New(info.Env(), "frameRate"), Napi::Number::New(info.Env(), response[8].value_union.fp64));
	statistics.Set(Napi::String::New(info.Env(), "averageTimeToRenderFrame"), Napi::Number::New(info.Env(), response[9].value_union.fp64));
	statistics.Set(Napi::String::New(info.Env(), "memoryUsage"), Napi::Number::New(info.Env(), response[10].value_union.fp64));

	std::string diskSpaceAvailable; // workaround for a strlen crash
	if (response.size() < 12 || response[11].type != ipc::type::String || response[11].value_str.c_str() == nullptr || response[11].value_str.empty()) {
		diskSpaceAvailable = "0 MB";
	} else {
		diskSpaceAvailable = response[11].value_str;
	}
	statistics.Set(Napi::String::New(info.Env(), "diskSpaceAvailable"), Napi::String::New(info.Env(), diskSpaceAvailable));

	statistics.Set(Napi::String::New(info.Env(), "streamingBandwidthSecond"), Napi::Number::New(info.Env(), response[12].value_union.fp64));
	statistics.Set(Napi::String::New(info.Env(), "streamingDataOutputSecond"), Napi::Number::New(info.Env(), response[13].value_union.fp64));
	return statistics;
}

Napi::Value api::SetWorkingDirectory(const Napi::CallbackInfo &info)
{
	std::string path = info[0].ToString().Utf8Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("API", "SetWorkingDirectory", {ipc::value(path)});
	return info.Env().Undefined();
}

Napi::Value api::InitShutdownSequence(const Napi::CallbackInfo &info)
{
	globalCallback::m_all_workers_stop = true;

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("API", "StopCrashHandler", {});

	// This is a shutdown operation, no response validation needed
	// ValidateResponse(info, response);

	return info.Env().Undefined();
}

Napi::Value api::OBS_API_QueryHotkeys(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("API", "OBS_API_QueryHotkeys", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	Napi::Array hotkeyInfos = Napi::Array::New(info.Env());

	// For each hotkey info that we need to fill
	for (int i = 0; i < (response.size() - 1) / 5; i++) {
		int responseIndex = i * 5 + 1;
		Napi::Object object = Napi::Object::New(info.Env());
		std::string objectName = response[responseIndex + 0].value_str;
		uint32_t objectType = response[responseIndex + 1].value_union.ui32;
		std::string hotkeyName = response[responseIndex + 2].value_str;
		std::string hotkeyDesc = response[responseIndex + 3].value_str;
		uint64_t hotkeyId = response[responseIndex + 4].value_union.ui64;

		object.Set(Napi::String::New(info.Env(), "ObjectName"), Napi::String::New(info.Env(), objectName));

		object.Set(Napi::String::New(info.Env(), "ObjectType"), Napi::Number::New(info.Env(), objectType));

		object.Set(Napi::String::New(info.Env(), "HotkeyName"), Napi::String::New(info.Env(), hotkeyName));

		object.Set(Napi::String::New(info.Env(), "HotkeyDesc"), Napi::String::New(info.Env(), hotkeyDesc));

		object.Set(Napi::String::New(info.Env(), "HotkeyId"), Napi::Number::New(info.Env(), static_cast<double>(hotkeyId)));

		hotkeyInfos.Set(i, object);
	}

	return hotkeyInfos;
}

Napi::Value api::OBS_API_ProcessHotkeyStatus(const Napi::CallbackInfo &info)
{
	uint64_t hotkeyId;
	bool press;

	ASSERT_GET_VALUE(info, info[0], hotkeyId);
	ASSERT_GET_VALUE(info, info[1], press);

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("API", "OBS_API_ProcessHotkeyStatus", {ipc::value(hotkeyId), ipc::value(press)});

	return info.Env().Undefined();
}

Napi::Value api::SetUsername(const Napi::CallbackInfo &info)
{
	std::string username;

	ASSERT_GET_VALUE(info, info[0], username);

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("API", "SetUsername", {ipc::value(username)});

	return info.Env().Undefined();
}

Napi::Value api::GetPermissionsStatus(const Napi::CallbackInfo &info)
{
#ifdef __APPLE__
	bool webcam, mic;
	g_util_osx->getPermissionsStatus(webcam, mic);

	Napi::Object perms = Napi::Object::New(info.Env());
	perms.Set(Napi::String::New(info.Env(), "webcamPermission"), Napi::Boolean::New(info.Env(), webcam));
	perms.Set(Napi::String::New(info.Env(), "micPermission"), Napi::Boolean::New(info.Env(), mic));

	return perms;
#endif
	return info.Env().Undefined();
}

Napi::Value api::RequestPermissions(const Napi::CallbackInfo &info)
{
#ifdef __APPLE__
	Napi::Function async_callback = info[0].As<Napi::Function>();
	js_thread = Napi::ThreadSafeFunction::New(info.Env(), async_callback, "RequestPermissionsThread", 0, 1, [](Napi::Env) {});

	auto cb = [](void *data, bool webcam, bool mic) {
		Napi::ThreadSafeFunction *worker = reinterpret_cast<Napi::ThreadSafeFunction *>(data);

		auto callback = [](Napi::Env env, Napi::Function jsCallback, Permissions *data) {
			Napi::Object result = Napi::Object::New(env);

			result.Set(Napi::String::New(env, "webcamPermission"), Napi::Boolean::New(env, data->webcam));
			result.Set(Napi::String::New(env, "micPermission"), Napi::Boolean::New(env, data->mic));

			jsCallback.Call({result});
			delete data;
		};
		Permissions *perms_status = new Permissions();
		perms_status->webcam = webcam;
		perms_status->mic = mic;
		napi_status status = js_thread.BlockingCall(perms_status, callback);
		if (status != napi_ok) {
			delete perms_status;
		}
	};

	g_util_osx->requestPermissions(js_thread, cb);
#endif
	return info.Env().Undefined();
}

Napi::Value api::GetBrowserAcceleration(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("API", "GetBrowserAcceleration", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void api::SetBrowserAcceleration(const Napi::CallbackInfo &info)
{
	bool browserAceel = info[0].ToBoolean().Value();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("API", "SetBrowserAcceleration", {ipc::value(browserAceel)});
}

Napi::Value api::GetBrowserAccelerationLegacy(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("API", "GetBrowserAccelerationLegacy", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

Napi::Value api::GetMediaFileCaching(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("API", "GetMediaFileCaching", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void api::SetMediaFileCaching(const Napi::CallbackInfo &info)
{
	bool mediaFileCaching = info[0].ToBoolean().Value();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("API", "SetMediaFileCaching", {ipc::value(mediaFileCaching)});
}

Napi::Value api::GetMediaFileCachingLegacy(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("API", "GetMediaFileCachingLegacy", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

Napi::Value api::GetProcessPriority(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("API", "GetProcessPriority", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

void api::SetProcessPriority(const Napi::CallbackInfo &info)
{
	std::string processPriority = info[0].ToString().Utf8Value();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("API", "SetProcessPriority", {ipc::value(processPriority)});
}

Napi::Value api::GetProcessPriorityLegacy(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("API", "GetProcessPriorityLegacy", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

Napi::Value api::OBS_API_forceCrash(const Napi::CallbackInfo &info)
{
	bool backendCrash;
	ASSERT_GET_VALUE(info, info[0], backendCrash);

	if (!backendCrash)
		throw std::runtime_error("Simulated crash to test crash handling functionality");

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("API", "OBS_API_forceCrash", {});

	return info.Env().Undefined();
}

Napi::Value api::GetSdrWhiteLevel(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("API", "GetSdrWhiteLevel", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void api::SetSdrWhiteLevel(const Napi::CallbackInfo &info)
{
	uint32_t sdrWhiteLevel = info[0].ToNumber().Uint32Value();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("API", "SetSdrWhiteLevel", {ipc::value(sdrWhiteLevel)});
}

Napi::Value api::GetSdrWhiteLevelLegacy(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("API", "GetSdrWhiteLevelLegacy", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

Napi::Value api::GetHdrNominalPeakLevel(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("API", "GetHdrNominalPeakLevel", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void api::SetHdrNominalPeakLevel(const Napi::CallbackInfo &info)
{
	uint32_t hdrNominalPeakLevel = info[0].ToNumber().Uint32Value();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("API", "SetHdrNominalPeakLevel", {ipc::value(hdrNominalPeakLevel)});
}

Napi::Value api::GetHdrNominalPeakLevelLegacy(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("API", "GetHdrNominalPeakLevelLegacy", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

Napi::Value api::GetLowLatencyAudioBuffering(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("API", "GetLowLatencyAudioBuffering", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void api::SetLowLatencyAudioBuffering(const Napi::CallbackInfo &info)
{
	bool lowLatencyAudioBuffering = info[0].ToBoolean().Value();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("API", "SetLowLatencyAudioBuffering", {ipc::value(lowLatencyAudioBuffering)});
}

Napi::Value api::GetLowLatencyAudioBufferingLegacy(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("API", "GetLowLatencyAudioBufferingLegacy", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

Napi::Value api::GetForceGPURendering(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("API", "GetForceGPURendering", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void api::SetForceGPURendering(const Napi::CallbackInfo &info)
{
	bool forceGPURendering = info[0].ToBoolean().Value();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("API", "SetForceGPURendering", {ipc::value(forceGPURendering)});
}

Napi::Value api::GetForceGPURenderingLegacy(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("API", "GetForceGPURenderingLegacy", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void api::Init(Napi::Env env, Napi::Object exports)
{
	exports.Set(Napi::String::New(env, "OBS_API_initAPI"), Napi::Function::New(env, api::OBS_API_initAPI));
	exports.Set(Napi::String::New(env, "OBS_API_destroyOBS_API"), Napi::Function::New(env, api::OBS_API_destroyOBS_API));
	exports.Set(Napi::String::New(env, "OBS_API_getPerformanceStatistics"), Napi::Function::New(env, api::OBS_API_getPerformanceStatistics));
	exports.Set(Napi::String::New(env, "OBS_API_getModuleLoadFailures"), Napi::Function::New(env, api::OBS_API_getModuleLoadFailures));
	exports.Set(Napi::String::New(env, "SetWorkingDirectory"), Napi::Function::New(env, api::SetWorkingDirectory));
	exports.Set(Napi::String::New(env, "InitShutdownSequence"), Napi::Function::New(env, api::InitShutdownSequence));
	exports.Set(Napi::String::New(env, "OBS_API_QueryHotkeys"), Napi::Function::New(env, api::OBS_API_QueryHotkeys));
	exports.Set(Napi::String::New(env, "OBS_API_ProcessHotkeyStatus"), Napi::Function::New(env, api::OBS_API_ProcessHotkeyStatus));
	exports.Set(Napi::String::New(env, "SetUsername"), Napi::Function::New(env, api::SetUsername));
	exports.Set(Napi::String::New(env, "GetPermissionsStatus"), Napi::Function::New(env, api::GetPermissionsStatus));
	exports.Set(Napi::String::New(env, "RequestPermissions"), Napi::Function::New(env, api::RequestPermissions));
	exports.Set(Napi::String::New(env, "SetBrowserAcceleration"), Napi::Function::New(env, api::SetBrowserAcceleration));
	exports.Set(Napi::String::New(env, "GetBrowserAccelerationLegacy"), Napi::Function::New(env, api::GetBrowserAccelerationLegacy));
	exports.Set(Napi::String::New(env, "GetBrowserAcceleration"), Napi::Function::New(env, api::GetBrowserAcceleration));
	exports.Set(Napi::String::New(env, "SetMediaFileCaching"), Napi::Function::New(env, api::SetMediaFileCaching));
	exports.Set(Napi::String::New(env, "GetMediaFileCachingLegacy"), Napi::Function::New(env, api::GetMediaFileCachingLegacy));
	exports.Set(Napi::String::New(env, "GetMediaFileCaching"), Napi::Function::New(env, api::GetMediaFileCaching));
	exports.Set(Napi::String::New(env, "SetProcessPriority"), Napi::Function::New(env, api::SetProcessPriority));
	exports.Set(Napi::String::New(env, "GetProcessPriorityLegacy"), Napi::Function::New(env, api::GetProcessPriorityLegacy));
	exports.Set(Napi::String::New(env, "GetProcessPriority"), Napi::Function::New(env, api::GetProcessPriority));
	exports.Set(Napi::String::New(env, "OBS_API_forceCrash"), Napi::Function::New(env, api::OBS_API_forceCrash));
	exports.Set(Napi::String::New(env, "GetSdrWhiteLevel"), Napi::Function::New(env, api::GetSdrWhiteLevel));
	exports.Set(Napi::String::New(env, "SetSdrWhiteLevel"), Napi::Function::New(env, api::SetSdrWhiteLevel));
	exports.Set(Napi::String::New(env, "GetSdrWhiteLevelLegacy"), Napi::Function::New(env, api::GetSdrWhiteLevelLegacy));
	exports.Set(Napi::String::New(env, "GetHdrNominalPeakLevel"), Napi::Function::New(env, api::GetHdrNominalPeakLevel));
	exports.Set(Napi::String::New(env, "SetHdrNominalPeakLevel"), Napi::Function::New(env, api::SetHdrNominalPeakLevel));
	exports.Set(Napi::String::New(env, "GetHdrNominalPeakLevelLegacy"), Napi::Function::New(env, api::GetHdrNominalPeakLevelLegacy));
	exports.Set(Napi::String::New(env, "GetLowLatencyAudioBuffering"), Napi::Function::New(env, api::GetLowLatencyAudioBuffering));
	exports.Set(Napi::String::New(env, "SetLowLatencyAudioBuffering"), Napi::Function::New(env, api::SetLowLatencyAudioBuffering));
	exports.Set(Napi::String::New(env, "GetLowLatencyAudioBufferingLegacy"), Napi::Function::New(env, api::GetLowLatencyAudioBufferingLegacy));
	exports.Set(Napi::String::New(env, "GetForceGPURendering"), Napi::Function::New(env, api::GetForceGPURendering));
	exports.Set(Napi::String::New(env, "SetForceGPURendering"), Napi::Function::New(env, api::SetForceGPURendering));
	exports.Set(Napi::String::New(env, "GetForceGPURenderingLegacy"), Napi::Function::New(env, api::GetForceGPURenderingLegacy));
}
