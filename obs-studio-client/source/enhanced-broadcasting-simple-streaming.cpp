/******************************************************************************
    Copyright (C) 2016-2022 by Streamlabs (General Workings Inc)

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

#include "enhanced-broadcasting-simple-streaming.hpp"
#include "utility.hpp"
#include "service.hpp"
#include "delay.hpp"
#include "reconnect.hpp"
#include "network.hpp"
#include "video.hpp"

Napi::FunctionReference osn::EnhancedBroadcastingSimpleStreaming::constructor;

Napi::Object osn::EnhancedBroadcastingSimpleStreaming::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func = DefineClass(
		env, "EnhancedBroadcastingSimpleStreaming",
		{StaticMethod("create", &osn::EnhancedBroadcastingSimpleStreaming::Create),
		 StaticMethod("destroy", &osn::EnhancedBroadcastingSimpleStreaming::Destroy),

		 InstanceAccessor("videoEncoder", &osn::EnhancedBroadcastingSimpleStreaming::GetVideoEncoder,
				  &osn::EnhancedBroadcastingSimpleStreaming::SetVideoEncoder),
		 InstanceAccessor("audioEncoder", &osn::EnhancedBroadcastingSimpleStreaming::GetAudioEncoder,
				  &osn::EnhancedBroadcastingSimpleStreaming::SetAudioEncoder),
		 InstanceAccessor("service", &osn::EnhancedBroadcastingSimpleStreaming::GetService, &osn::EnhancedBroadcastingSimpleStreaming::SetService),
		 InstanceAccessor("enforceServiceBitrate", &osn::EnhancedBroadcastingSimpleStreaming::GetEnforceServiceBirate,
				  &osn::EnhancedBroadcastingSimpleStreaming::SetEnforceServiceBirate),
		 InstanceAccessor("enableTwitchVOD", &osn::EnhancedBroadcastingSimpleStreaming::GetEnableTwitchVOD,
				  &osn::EnhancedBroadcastingSimpleStreaming::SetEnableTwitchVOD),
		 InstanceAccessor("audioEncoder", &osn::EnhancedBroadcastingSimpleStreaming::GetAudioEncoder,
				  &osn::EnhancedBroadcastingSimpleStreaming::SetAudioEncoder),
		 InstanceAccessor("useAdvanced", &osn::EnhancedBroadcastingSimpleStreaming::GetUseAdvanced,
				  &osn::EnhancedBroadcastingSimpleStreaming::SetUseAdvanced),
		 InstanceAccessor("customEncSettings", &osn::EnhancedBroadcastingSimpleStreaming::GetCustomEncSettings,
				  &osn::EnhancedBroadcastingSimpleStreaming::SetCustomEncSettings),
		 InstanceAccessor("signalHandler", &osn::EnhancedBroadcastingSimpleStreaming::GetSignalHandler,
				  &osn::EnhancedBroadcastingSimpleStreaming::SetSignalHandler),
		 InstanceAccessor("delay", &osn::EnhancedBroadcastingSimpleStreaming::GetDelay, &osn::EnhancedBroadcastingSimpleStreaming::SetDelay),
		 InstanceAccessor("reconnect", &osn::EnhancedBroadcastingSimpleStreaming::GetReconnect,
				  &osn::EnhancedBroadcastingSimpleStreaming::SetReconnect),
		 InstanceAccessor("network", &osn::EnhancedBroadcastingSimpleStreaming::GetNetwork, &osn::EnhancedBroadcastingSimpleStreaming::SetNetwork),
		 InstanceAccessor("video", &osn::EnhancedBroadcastingSimpleStreaming::GetCanvas, &osn::EnhancedBroadcastingSimpleStreaming::SetCanvas),
		 InstanceAccessor("additionalVideo", &osn::EnhancedBroadcastingSimpleStreaming::GetAdditionalCanvas,
				  &osn::EnhancedBroadcastingSimpleStreaming::SetAdditionalCanvas),
		 InstanceAccessor("droppedFrames", &osn::EnhancedBroadcastingSimpleStreaming::GetDroppedFrames, nullptr),
		 InstanceAccessor("totalFrames", &osn::EnhancedBroadcastingSimpleStreaming::GetTotalFrames, nullptr),
		 InstanceAccessor("kbitsPerSec", &osn::EnhancedBroadcastingSimpleStreaming::GetKBitsPerSec, nullptr),
		 InstanceAccessor("dataOutput", &osn::EnhancedBroadcastingSimpleStreaming::GetDataOutput, nullptr),

		 InstanceMethod("start", &osn::EnhancedBroadcastingSimpleStreaming::Start),
		 InstanceMethod("stop", &osn::EnhancedBroadcastingSimpleStreaming::Stop),

		 StaticAccessor("legacySettings", &osn::EnhancedBroadcastingSimpleStreaming::GetLegacySettings,
				&osn::EnhancedBroadcastingSimpleStreaming::SetLegacySettings)});

	exports.Set("EnhancedBroadcastingSimpleStreaming", func);
	osn::EnhancedBroadcastingSimpleStreaming::constructor = Napi::Persistent(func);
	osn::EnhancedBroadcastingSimpleStreaming::constructor.SuppressDestruct();

	return exports;
}

osn::EnhancedBroadcastingSimpleStreaming::EnhancedBroadcastingSimpleStreaming(const Napi::CallbackInfo &info)
	: Napi::ObjectWrap<osn::EnhancedBroadcastingSimpleStreaming>(info)
{
	Napi::Env env = info.Env();
	Napi::HandleScope scope(env);
	size_t length = info.Length();

	if (length <= 0 || !info[0].IsNumber()) {
		Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
		return;
	}

	this->uid = (uint64_t)info[0].ToNumber().Int64Value();
	this->className = std::string("EnhancedBroadcastingSimpleStreaming");
}

Napi::Value osn::EnhancedBroadcastingSimpleStreaming::Create(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("EnhancedBroadcastingSimpleStreaming", "Create", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance =
		osn::EnhancedBroadcastingSimpleStreaming::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::EnhancedBroadcastingSimpleStreaming::Destroy(const Napi::CallbackInfo &info)
{
	if (info.Length() != 1)
		return;

	auto stream = Napi::ObjectWrap<osn::EnhancedBroadcastingSimpleStreaming>::Unwrap(info[0].ToObject());

	stream->stopWorker();
	stream->cb.Reset();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	std::vector<ipc::value> response = conn->call_synchronous_helper("EnhancedBroadcastingSimpleStreaming", "Destroy", {ipc::value(stream->uid)});

	if (!ValidateResponse(info, response))
		return;
}

Napi::Value osn::EnhancedBroadcastingSimpleStreaming::GetAdditionalCanvas(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper(className, "GetAdditionalVideoCanvas", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::Video::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::EnhancedBroadcastingSimpleStreaming::SetAdditionalCanvas(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::Video *canvas = Napi::ObjectWrap<osn::Video>::Unwrap(value.ToObject());

	if (!canvas) {
		Napi::TypeError::New(info.Env(), "Invalid canvas argument").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call(className, "SetAdditionalVideoCanvas", {ipc::value(this->uid), ipc::value(canvas->canvasId)});
}

Napi::Value osn::EnhancedBroadcastingSimpleStreaming::GetLegacySettings(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("EnhancedBroadcastingSimpleStreaming", "GetLegacySettings", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance =
		osn::EnhancedBroadcastingSimpleStreaming::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::EnhancedBroadcastingSimpleStreaming::SetLegacySettings(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::EnhancedBroadcastingSimpleStreaming *streaming = Napi::ObjectWrap<osn::EnhancedBroadcastingSimpleStreaming>::Unwrap(value.ToObject());

	if (!streaming) {
		Napi::TypeError::New(info.Env(), "Invalid service argument").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	std::vector<ipc::value> response = conn->call_synchronous_helper("EnhancedBroadcastingSimpleStreaming", "SetLegacySettings", {streaming->uid});

	if (!ValidateResponse(info, response))
		return;
}