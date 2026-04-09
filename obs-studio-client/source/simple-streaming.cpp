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

#include "simple-streaming.hpp"
#include "utility.hpp"
#include "audio-encoder.hpp"
#include "service.hpp"
#include "delay.hpp"
#include "reconnect.hpp"
#include "network.hpp"

Napi::FunctionReference osn::SimpleStreaming::constructor;

Napi::Object osn::SimpleStreaming::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func = DefineClass(
		env, "SimpleStreaming",
		{StaticMethod("create", &osn::SimpleStreaming::Create),
		 StaticMethod("destroy", &osn::SimpleStreaming::Destroy),

		 InstanceAccessor("videoEncoder", &osn::SimpleStreaming::GetVideoEncoder, &osn::SimpleStreaming::SetVideoEncoder),
		 InstanceAccessor("service", &osn::SimpleStreaming::GetService, &osn::SimpleStreaming::SetService),
		 InstanceAccessor("enforceServiceBitrate", &osn::SimpleStreaming::GetEnforceServiceBirate, &osn::SimpleStreaming::SetEnforceServiceBirate),
		 InstanceAccessor("enableTwitchVOD", &osn::SimpleStreaming::GetEnableTwitchVOD, &osn::SimpleStreaming::SetEnableTwitchVOD),
		 InstanceAccessor("audioEncoder", &osn::SimpleStreaming::GetAudioEncoder, &osn::SimpleStreaming::SetAudioEncoder),
		 InstanceAccessor("useAdvanced", &osn::SimpleStreaming::GetUseAdvanced, &osn::SimpleStreaming::SetUseAdvanced),
		 InstanceAccessor("customEncSettings", &osn::SimpleStreaming::GetCustomEncSettings, &osn::SimpleStreaming::SetCustomEncSettings),
		 InstanceAccessor("signalHandler", &osn::SimpleStreaming::GetSignalHandler, &osn::SimpleStreaming::SetSignalHandler),
		 InstanceAccessor("delay", &osn::SimpleStreaming::GetDelay, &osn::SimpleStreaming::SetDelay),
		 InstanceAccessor("reconnect", &osn::SimpleStreaming::GetReconnect, &osn::SimpleStreaming::SetReconnect),
		 InstanceAccessor("network", &osn::SimpleStreaming::GetNetwork, &osn::SimpleStreaming::SetNetwork),
		 InstanceAccessor("video", &osn::SimpleStreaming::GetCanvas, &osn::SimpleStreaming::SetCanvas),
		 InstanceAccessor("droppedFrames", &osn::SimpleStreaming::GetDroppedFrames, nullptr),
		 InstanceAccessor("totalFrames", &osn::SimpleStreaming::GetTotalFrames, nullptr),
		 InstanceAccessor("kbitsPerSec", &osn::SimpleStreaming::GetKBitsPerSec, nullptr),
		 InstanceAccessor("dataOutput", &osn::SimpleStreaming::GetDataOutput, nullptr),

		 InstanceMethod("start", &osn::SimpleStreaming::Start),
		 InstanceMethod("stop", &osn::SimpleStreaming::Stop),

		 StaticAccessor("legacySettings", &osn::SimpleStreaming::GetLegacySettings, &osn::SimpleStreaming::SetLegacySettings)});

	exports.Set("SimpleStreaming", func);
	osn::SimpleStreaming::constructor = Napi::Persistent(func);
	osn::SimpleStreaming::constructor.SuppressDestruct();

	return exports;
}

osn::SimpleStreaming::SimpleStreaming(const Napi::CallbackInfo &info) : Napi::ObjectWrap<osn::SimpleStreaming>(info)
{
	Napi::Env env = info.Env();
	Napi::HandleScope scope(env);
	size_t length = info.Length();

	if (length <= 0 || !info[0].IsNumber()) {
		Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
		return;
	}

	this->uid = (uint64_t)info[0].ToNumber().Int64Value();
	this->className = std::string("SimpleStreaming");
}

void osn::SimpleStreaming::Finalize(Napi::Env)
{
	ReleaseObjects();
}

void osn::SimpleStreaming::ReleaseObjects()
{
	osn::Streaming::ReleaseObjects();
}

Napi::Value osn::SimpleStreaming::Create(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("SimpleStreaming", "Create", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::SimpleStreaming::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::SimpleStreaming::Destroy(const Napi::CallbackInfo &info)
{
	if (info.Length() != 1)
		return;

	auto stream = Napi::ObjectWrap<osn::SimpleStreaming>::Unwrap(info[0].ToObject());

	stream->stopWorker();
	stream->cb.Reset();

	stream->ReleaseObjects();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper("SimpleStreaming", "Destroy", {ipc::value(stream->uid)});

	if (!ValidateResponse(info, response))
		return;
}

Napi::Value osn::SimpleStreaming::GetLegacySettings(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("SimpleStreaming", "GetLegacySettings", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::SimpleStreaming::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::SimpleStreaming::SetLegacySettings(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::SimpleStreaming *streaming = Napi::ObjectWrap<osn::SimpleStreaming>::Unwrap(value.ToObject());

	if (!streaming) {
		Napi::TypeError::New(info.Env(), "Invalid streaming argument").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper("SimpleStreaming", "SetLegacySettings", {streaming->uid});

	if (!ValidateResponse(info, response))
		return;
}