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

#include "enhanced-broadcasting-advanced-streaming.hpp"
#include "utility.hpp"
#include "service.hpp"
#include "delay.hpp"
#include "reconnect.hpp"
#include "network.hpp"
#include "video.hpp"

Napi::FunctionReference osn::EnhancedBroadcastingAdvancedStreaming::constructor;

Napi::Object osn::EnhancedBroadcastingAdvancedStreaming::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func = DefineClass(
		env, "EnhancedBroadcastingAdvancedStreaming",
		{StaticMethod("create", &osn::EnhancedBroadcastingAdvancedStreaming::Create),
		 StaticMethod("destroy", &osn::EnhancedBroadcastingAdvancedStreaming::Destroy),

		 InstanceAccessor("videoEncoder", &osn::EnhancedBroadcastingAdvancedStreaming::GetVideoEncoder,
				  &osn::EnhancedBroadcastingAdvancedStreaming::SetVideoEncoder),
		 InstanceAccessor("service", &osn::EnhancedBroadcastingAdvancedStreaming::GetService, &osn::EnhancedBroadcastingAdvancedStreaming::SetService),
		 InstanceAccessor("enforceServiceBitrate", &osn::EnhancedBroadcastingAdvancedStreaming::GetEnforceServiceBirate,
				  &osn::EnhancedBroadcastingAdvancedStreaming::SetEnforceServiceBirate),
		 InstanceAccessor("enableTwitchVOD", &osn::EnhancedBroadcastingAdvancedStreaming::GetEnableTwitchVOD,
				  &osn::EnhancedBroadcastingAdvancedStreaming::SetEnableTwitchVOD),
		 InstanceAccessor("signalHandler", &osn::EnhancedBroadcastingAdvancedStreaming::GetSignalHandler,
				  &osn::EnhancedBroadcastingAdvancedStreaming::SetSignalHandler),
		 InstanceAccessor("delay", &osn::EnhancedBroadcastingAdvancedStreaming::GetDelay, &osn::EnhancedBroadcastingAdvancedStreaming::SetDelay),
		 InstanceAccessor("reconnect", &osn::EnhancedBroadcastingAdvancedStreaming::GetReconnect,
				  &osn::EnhancedBroadcastingAdvancedStreaming::SetReconnect),
		 InstanceAccessor("network", &osn::EnhancedBroadcastingAdvancedStreaming::GetNetwork, &osn::EnhancedBroadcastingAdvancedStreaming::SetNetwork),
		 InstanceAccessor("video", &osn::EnhancedBroadcastingAdvancedStreaming::GetCanvas, &osn::EnhancedBroadcastingAdvancedStreaming::SetCanvas),
		 InstanceAccessor("additionalVideo", &osn::EnhancedBroadcastingAdvancedStreaming::GetAdditionalCanvas,
				  &osn::EnhancedBroadcastingAdvancedStreaming::SetAdditionalCanvas),
		 InstanceAccessor("droppedFrames", &osn::EnhancedBroadcastingAdvancedStreaming::GetDroppedFrames, nullptr),
		 InstanceAccessor("totalFrames", &osn::EnhancedBroadcastingAdvancedStreaming::GetTotalFrames, nullptr),
		 InstanceAccessor("kbitsPerSec", &osn::EnhancedBroadcastingAdvancedStreaming::GetKBitsPerSec, nullptr),
		 InstanceAccessor("dataOutput", &osn::EnhancedBroadcastingAdvancedStreaming::GetDataOutput, nullptr),

		 InstanceAccessor("audioTrack", &osn::EnhancedBroadcastingAdvancedStreaming::GetAudioTrack,
				  &osn::EnhancedBroadcastingAdvancedStreaming::SetAudioTrack),
		 InstanceAccessor("twitchTrack", &osn::EnhancedBroadcastingAdvancedStreaming::GetTwitchTrack,
				  &osn::EnhancedBroadcastingAdvancedStreaming::SetTwitchTrack),
		 InstanceAccessor("rescaling", &osn::EnhancedBroadcastingAdvancedStreaming::GetRescaling,
				  &osn::EnhancedBroadcastingAdvancedStreaming::SetRescaling),
		 InstanceAccessor("outputWidth", &osn::EnhancedBroadcastingAdvancedStreaming::GetOutputWidth,
				  &osn::EnhancedBroadcastingAdvancedStreaming::SetOutputWidth),
		 InstanceAccessor("outputHeight", &osn::EnhancedBroadcastingAdvancedStreaming::GetOutputHeight,
				  &osn::EnhancedBroadcastingAdvancedStreaming::SetOutputHeight),

		 InstanceMethod("start", &osn::EnhancedBroadcastingAdvancedStreaming::Start),
		 InstanceMethod("stop", &osn::EnhancedBroadcastingAdvancedStreaming::Stop),

		 StaticAccessor("legacySettings", &osn::EnhancedBroadcastingAdvancedStreaming::GetLegacySettings,
				&osn::EnhancedBroadcastingAdvancedStreaming::SetLegacySettings)});

	exports.Set("EnhancedBroadcastingAdvancedStreaming", func);
	osn::EnhancedBroadcastingAdvancedStreaming::constructor = Napi::Persistent(func);
	osn::EnhancedBroadcastingAdvancedStreaming::constructor.SuppressDestruct();

	return exports;
}

osn::EnhancedBroadcastingAdvancedStreaming::EnhancedBroadcastingAdvancedStreaming(const Napi::CallbackInfo &info)
	: Napi::ObjectWrap<osn::EnhancedBroadcastingAdvancedStreaming>(info)
{
	Napi::Env env = info.Env();
	Napi::HandleScope scope(env);
	size_t length = info.Length();

	if (length <= 0 || !info[0].IsNumber()) {
		Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
		return;
	}

	this->uid = (uint64_t)info[0].ToNumber().Int64Value();
	this->className = std::string("EnhancedBroadcastingAdvancedStreaming");
}

Napi::Value osn::EnhancedBroadcastingAdvancedStreaming::Create(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("EnhancedBroadcastingAdvancedStreaming", "Create", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance =
		osn::EnhancedBroadcastingAdvancedStreaming::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::EnhancedBroadcastingAdvancedStreaming::Destroy(const Napi::CallbackInfo &info)
{
	if (info.Length() != 1)
		return;

	auto stream = Napi::ObjectWrap<osn::EnhancedBroadcastingAdvancedStreaming>::Unwrap(info[0].ToObject());

	stream->stopWorker();
	stream->cb.Reset();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	std::vector<ipc::value> response = conn->call_synchronous_helper("EnhancedBroadcastingAdvancedStreaming", "Destroy", {ipc::value(stream->uid)});

	if (!ValidateResponse(info, response))
		return;
}

Napi::Value osn::EnhancedBroadcastingAdvancedStreaming::GetAdditionalCanvas(const Napi::CallbackInfo &info)
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

void osn::EnhancedBroadcastingAdvancedStreaming::SetAdditionalCanvas(const Napi::CallbackInfo &info, const Napi::Value &value)
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

Napi::Value osn::EnhancedBroadcastingAdvancedStreaming::GetLegacySettings(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("EnhancedBroadcastingAdvancedStreaming", "GetLegacySettings", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance =
		osn::EnhancedBroadcastingAdvancedStreaming::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::EnhancedBroadcastingAdvancedStreaming::SetLegacySettings(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::EnhancedBroadcastingAdvancedStreaming *streaming = Napi::ObjectWrap<osn::EnhancedBroadcastingAdvancedStreaming>::Unwrap(value.ToObject());

	if (!streaming) {
		Napi::TypeError::New(info.Env(), "Invalid service argument").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	std::vector<ipc::value> response = conn->call_synchronous_helper("EnhancedBroadcastingAdvancedStreaming", "SetLegacySettings", {streaming->uid});

	if (!ValidateResponse(info, response))
		return;
}