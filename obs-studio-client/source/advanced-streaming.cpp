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

#include "advanced-streaming.hpp"
#include "utility.hpp"
#include "service.hpp"
#include "delay.hpp"
#include "reconnect.hpp"
#include "network.hpp"

Napi::FunctionReference osn::AdvancedStreaming::constructor;

Napi::Object osn::AdvancedStreaming::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func = DefineClass(
		env, "AdvancedStreaming",
		{StaticMethod("create", &osn::AdvancedStreaming::Create),
		 StaticMethod("destroy", &osn::AdvancedStreaming::Destroy),

		 InstanceAccessor("videoEncoder", &osn::AdvancedStreaming::GetVideoEncoder, &osn::AdvancedStreaming::SetVideoEncoder),
		 InstanceAccessor("service", &osn::AdvancedStreaming::GetService, &osn::AdvancedStreaming::SetService),
		 InstanceAccessor("enforceServiceBitrate", &osn::AdvancedStreaming::GetEnforceServiceBirate, &osn::AdvancedStreaming::SetEnforceServiceBirate),
		 InstanceAccessor("enableTwitchVOD", &osn::AdvancedStreaming::GetEnableTwitchVOD, &osn::AdvancedStreaming::SetEnableTwitchVOD),
		 InstanceAccessor("signalHandler", &osn::AdvancedStreaming::GetSignalHandler, &osn::AdvancedStreaming::SetSignalHandler),
		 InstanceAccessor("delay", &osn::AdvancedStreaming::GetDelay, &osn::AdvancedStreaming::SetDelay),
		 InstanceAccessor("reconnect", &osn::AdvancedStreaming::GetReconnect, &osn::AdvancedStreaming::SetReconnect),
		 InstanceAccessor("network", &osn::AdvancedStreaming::GetNetwork, &osn::AdvancedStreaming::SetNetwork),
		 InstanceAccessor("video", &osn::AdvancedStreaming::GetCanvas, &osn::AdvancedStreaming::SetCanvas),
		 InstanceAccessor("droppedFrames", &osn::AdvancedStreaming::GetDroppedFrames, nullptr),
		 InstanceAccessor("totalFrames", &osn::AdvancedStreaming::GetTotalFrames, nullptr),
		 InstanceAccessor("kbitsPerSec", &osn::AdvancedStreaming::GetKBitsPerSec, nullptr),
		 InstanceAccessor("dataOutput", &osn::AdvancedStreaming::GetDataOutput, nullptr),

		 InstanceAccessor("audioTrack", &osn::AdvancedStreaming::GetAudioTrack, &osn::AdvancedStreaming::SetAudioTrack),
		 InstanceAccessor("twitchTrack", &osn::AdvancedStreaming::GetTwitchTrack, &osn::AdvancedStreaming::SetTwitchTrack),
		 InstanceAccessor("rescaling", &osn::AdvancedStreaming::GetRescaling, &osn::AdvancedStreaming::SetRescaling),
		 InstanceAccessor("outputWidth", &osn::AdvancedStreaming::GetOutputWidth, &osn::AdvancedStreaming::SetOutputWidth),
		 InstanceAccessor("outputHeight", &osn::AdvancedStreaming::GetOutputHeight, &osn::AdvancedStreaming::SetOutputHeight),

		 InstanceMethod("start", &osn::AdvancedStreaming::Start),
		 InstanceMethod("stop", &osn::AdvancedStreaming::Stop),

		 StaticAccessor("legacySettings", &osn::AdvancedStreaming::GetLegacySettings, &osn::AdvancedStreaming::SetLegacySettings)});

	exports.Set("AdvancedStreaming", func);
	osn::AdvancedStreaming::constructor = Napi::Persistent(func);
	osn::AdvancedStreaming::constructor.SuppressDestruct();

	return exports;
}

osn::AdvancedStreaming::AdvancedStreaming(const Napi::CallbackInfo &info) : Napi::ObjectWrap<osn::AdvancedStreaming>(info)
{
	Napi::Env env = info.Env();
	Napi::HandleScope scope(env);
	size_t length = info.Length();

	if (length <= 0 || !info[0].IsNumber()) {
		Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
		return;
	}

	this->uid = (uint64_t)info[0].ToNumber().Int64Value();
	this->className = std::string("AdvancedStreaming");
}

Napi::Value osn::AdvancedStreaming::Create(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AdvancedStreaming", "Create", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::AdvancedStreaming::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::AdvancedStreaming::Destroy(const Napi::CallbackInfo &info)
{
	if (info.Length() != 1)
		return;

	auto stream = Napi::ObjectWrap<osn::AdvancedStreaming>::Unwrap(info[0].ToObject());

	stream->stopWorker();
	stream->cb.Reset();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	std::vector<ipc::value> response = conn->call_synchronous_helper("AdvancedStreaming", "Destroy", {ipc::value(stream->uid)});

	if (!ValidateResponse(info, response))
		return;
}

Napi::Value osn::AdvancedStreaming::GetLegacySettings(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AdvancedStreaming", "GetLegacySettings", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::AdvancedStreaming::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::AdvancedStreaming::SetLegacySettings(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::AdvancedStreaming *streaming = Napi::ObjectWrap<osn::AdvancedStreaming>::Unwrap(value.ToObject());

	if (!streaming) {
		Napi::TypeError::New(info.Env(), "Invalid service argument").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	std::vector<ipc::value> response = conn->call_synchronous_helper("AdvancedStreaming", "SetLegacySettings", {streaming->uid});

	if (!ValidateResponse(info, response))
		return;
}