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

#include "simple-recording.hpp"
#include "utility.hpp"
#include "audio-encoder.hpp"
#include "service.hpp"
#include "delay.hpp"
#include "reconnect.hpp"
#include "network.hpp"
#include "simple-streaming.hpp"

Napi::FunctionReference osn::SimpleRecording::constructor;

Napi::Object osn::SimpleRecording::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func = DefineClass(
		env, "SimpleRecording",
		{
			StaticMethod("create", &osn::SimpleRecording::Create),
			StaticMethod("destroy", &osn::SimpleRecording::Destroy),

			InstanceAccessor("path", &osn::SimpleRecording::GetPath, &osn::SimpleRecording::SetPath),
			InstanceAccessor("format", &osn::SimpleRecording::GetFormat, &osn::SimpleRecording::SetFormat),
			InstanceAccessor("muxerSettings", &osn::SimpleRecording::GetMuxerSettings, &osn::SimpleRecording::SetMuxerSettings),
			InstanceAccessor("videoEncoder", &osn::SimpleRecording::GetVideoEncoder, &osn::SimpleRecording::SetVideoEncoder),
			InstanceAccessor("signalHandler", &osn::SimpleRecording::GetSignalHandler, &osn::SimpleRecording::SetSignalHandler),
			InstanceAccessor("quality", &osn::SimpleRecording::GetQuality, &osn::SimpleRecording::SetQuality),
			InstanceAccessor("audioEncoder", &osn::SimpleRecording::GetAudioEncoder, &osn::SimpleRecording::SetAudioEncoder),
			InstanceAccessor("fileFormat", &osn::SimpleRecording::GetFileFormat, &osn::SimpleRecording::SetFileFormat),
			InstanceAccessor("overwrite", &osn::SimpleRecording::GetOverwrite, &osn::SimpleRecording::SetOverwrite),
			InstanceAccessor("noSpace", &osn::SimpleRecording::GetNoSpace, &osn::SimpleRecording::SetNoSpace),
			InstanceAccessor("lowCPU", &osn::SimpleRecording::GetLowCPU, &osn::SimpleRecording::SetLowCPU),
			InstanceAccessor("streaming", &osn::SimpleRecording::GetStreaming, &osn::SimpleRecording::SetStreaming),
			InstanceAccessor("enableFileSplit", &osn::SimpleRecording::GetEnableFileSplit, &osn::SimpleRecording::SetEnableFileSplit),
			InstanceAccessor("splitType", &osn::SimpleRecording::GetSplitType, &osn::SimpleRecording::SetSplitType),
			InstanceAccessor("splitTime", &osn::SimpleRecording::GetSplitTime, &osn::SimpleRecording::SetSplitTime),
			InstanceAccessor("splitSize", &osn::SimpleRecording::GetSplitSize, &osn::SimpleRecording::SetSplitSize),
			InstanceAccessor("fileResetTimestamps", &osn::SimpleRecording::GetFileResetTimestamps, &osn::SimpleRecording::SetFileResetTimestamps),
			InstanceAccessor("video", &osn::SimpleRecording::GetCanvas, &osn::SimpleRecording::SetCanvas),

			InstanceMethod("start", &osn::SimpleRecording::Start),
			InstanceMethod("stop", &osn::SimpleRecording::Stop),
			InstanceMethod("splitFile", &osn::SimpleRecording::SplitFile),

			StaticAccessor("legacySettings", &osn::SimpleRecording::GetLegacySettings, &osn::SimpleRecording::SetLegacySettings),
			InstanceMethod("lastFile", &osn::SimpleRecording::GetLastFile),
		});

	exports.Set("SimpleRecording", func);
	osn::SimpleRecording::constructor = Napi::Persistent(func);
	osn::SimpleRecording::constructor.SuppressDestruct();

	return exports;
}

osn::SimpleRecording::SimpleRecording(const Napi::CallbackInfo &info) : Napi::ObjectWrap<osn::SimpleRecording>(info)
{
	Napi::Env env = info.Env();
	Napi::HandleScope scope(env);
	size_t length = info.Length();

	if (length <= 0 || !info[0].IsNumber()) {
		Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
		return;
	}

	this->uid = (uint64_t)info[0].ToNumber().Int64Value();
	this->className = std::string("SimpleRecording");
}

Napi::Value osn::SimpleRecording::Create(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("SimpleRecording", "Create", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::SimpleRecording::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::SimpleRecording::Destroy(const Napi::CallbackInfo &info)
{
	if (info.Length() != 1)
		return;

	auto recording = Napi::ObjectWrap<osn::SimpleRecording>::Unwrap(info[0].ToObject());

	recording->stopWorker();
	recording->cb.Reset();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	std::vector<ipc::value> response = conn->call_synchronous_helper("SimpleRecording", "Destroy", {ipc::value(recording->uid)});

	if (!ValidateResponse(info, response))
		return;
}

Napi::Value osn::SimpleRecording::GetQuality(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("SimpleRecording", "GetQuality", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void osn::SimpleRecording::SetQuality(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper("SimpleRecording", "SetQuality", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
}

Napi::Value osn::SimpleRecording::GetAudioEncoder(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("SimpleRecording", "GetAudioEncoder", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::AudioEncoder::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});
	return instance;
}

void osn::SimpleRecording::SetAudioEncoder(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::AudioEncoder *encoder = Napi::ObjectWrap<osn::AudioEncoder>::Unwrap(value.ToObject());

	if (!encoder) {
		Napi::TypeError::New(info.Env(), "Invalid encoder argument").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call(className, "SetAudioEncoder", {ipc::value(this->uid), ipc::value(encoder->uid)});
}

Napi::Value osn::SimpleRecording::GetLowCPU(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("SimpleRecording", "GetLowCPU", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void osn::SimpleRecording::SetLowCPU(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper("SimpleRecording", "SetLowCPU", {ipc::value(this->uid), ipc::value(value.ToBoolean().Value())});
}

Napi::Value osn::SimpleRecording::GetLegacySettings(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("SimpleRecording", "GetLegacySettings", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::SimpleRecording::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::SimpleRecording::SetLegacySettings(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::SimpleRecording *recording = Napi::ObjectWrap<osn::SimpleRecording>::Unwrap(value.ToObject());

	if (!recording) {
		Napi::TypeError::New(info.Env(), "Invalid recording argument").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	std::vector<ipc::value> response = conn->call_synchronous_helper("SimpleRecording", "SetLegacySettings", {recording->uid});

	if (!ValidateResponse(info, response))
		return;
}

Napi::Value osn::SimpleRecording::GetStreaming(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper(className, "GetStreaming", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::SimpleStreaming::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});
	return instance;
}

void osn::SimpleRecording::SetStreaming(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::SimpleStreaming *streaming = Napi::ObjectWrap<osn::SimpleStreaming>::Unwrap(value.ToObject());

	if (!streaming) {
		Napi::TypeError::New(info.Env(), "Invalid streaming argument").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call(className, "SetStreaming", {ipc::value(this->uid), ipc::value(streaming->uid)});
}