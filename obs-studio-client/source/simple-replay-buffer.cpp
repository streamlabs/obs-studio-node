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

#include "simple-replay-buffer.hpp"
#include "utility.hpp"
#include "audio-encoder.hpp"
#include "simple-streaming.hpp"
#include "simple-recording.hpp"

Napi::FunctionReference osn::SimpleReplayBuffer::constructor;

Napi::Object osn::SimpleReplayBuffer::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func =
		DefineClass(env, "SimpleReplayBuffer",
			    {StaticMethod("create", &osn::SimpleReplayBuffer::Create),
			     StaticMethod("destroy", &osn::SimpleReplayBuffer::Destroy),

			     InstanceAccessor("path", &osn::SimpleReplayBuffer::GetPath, &osn::SimpleReplayBuffer::SetPath),
			     InstanceAccessor("format", &osn::SimpleReplayBuffer::GetFormat, &osn::SimpleReplayBuffer::SetFormat),
			     InstanceAccessor("muxerSettings", &osn::SimpleReplayBuffer::GetMuxerSettings, &osn::SimpleReplayBuffer::SetMuxerSettings),
			     InstanceAccessor("fileFormat", &osn::SimpleReplayBuffer::GetFileFormat, &osn::SimpleReplayBuffer::SetFileFormat),
			     InstanceAccessor("overwrite", &osn::SimpleReplayBuffer::GetOverwrite, &osn::SimpleReplayBuffer::SetOverwrite),
			     InstanceAccessor("noSpace", &osn::SimpleReplayBuffer::GetNoSpace, &osn::SimpleReplayBuffer::SetNoSpace),
			     InstanceAccessor("duration", &osn::SimpleReplayBuffer::GetDuration, &osn::SimpleReplayBuffer::SetDuration),
			     InstanceAccessor("prefix", &osn::SimpleReplayBuffer::GetPrefix, &osn::SimpleReplayBuffer::SetPrefix),
			     InstanceAccessor("suffix", &osn::SimpleReplayBuffer::GetSuffix, &osn::SimpleReplayBuffer::SetSuffix),
			     InstanceAccessor("signalHandler", &osn::SimpleReplayBuffer::GetSignalHandler, &osn::SimpleReplayBuffer::SetSignalHandler),
			     InstanceAccessor("usesStream", &osn::SimpleReplayBuffer::GetUsesStream, &osn::SimpleReplayBuffer::SetUsesStream),
			     InstanceAccessor("streaming", &osn::SimpleReplayBuffer::GetStreaming, &osn::SimpleReplayBuffer::SetStreaming),
			     InstanceAccessor("recording", &osn::SimpleReplayBuffer::GetRecording, &osn::SimpleReplayBuffer::SetRecording),
			     InstanceAccessor("video", &osn::SimpleReplayBuffer::GetCanvas, &osn::SimpleReplayBuffer::SetCanvas),

			     InstanceMethod("start", &osn::SimpleReplayBuffer::Start),
			     InstanceMethod("stop", &osn::SimpleReplayBuffer::Stop),

			     InstanceMethod("save", &osn::SimpleReplayBuffer::Save),
			     InstanceMethod("lastFile", &osn::SimpleReplayBuffer::GetLastFile),

			     StaticAccessor("legacySettings", &osn::SimpleReplayBuffer::GetLegacySettings, &osn::SimpleReplayBuffer::SetLegacySettings)});

	exports.Set("SimpleReplayBuffer", func);
	osn::SimpleReplayBuffer::constructor = Napi::Persistent(func);
	osn::SimpleReplayBuffer::constructor.SuppressDestruct();

	return exports;
}

osn::SimpleReplayBuffer::SimpleReplayBuffer(const Napi::CallbackInfo &info) : Napi::ObjectWrap<osn::SimpleReplayBuffer>(info)
{
	Napi::Env env = info.Env();
	Napi::HandleScope scope(env);
	size_t length = info.Length();

	if (length <= 0 || !info[0].IsNumber()) {
		Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
		return;
	}

	this->uid = (uint64_t)info[0].ToNumber().Int64Value();
	this->className = std::string("SimpleReplayBuffer");
}

void osn::SimpleReplayBuffer::Finalize(Napi::Env)
{
	ReleaseObjects();
}

void osn::SimpleReplayBuffer::ReleaseObjects()
{
	if (!parentOutputRef.IsEmpty())
		parentOutputRef.Reset();
}

Napi::Value osn::SimpleReplayBuffer::Create(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("SimpleReplayBuffer", "Create", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::SimpleReplayBuffer::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::SimpleReplayBuffer::Destroy(const Napi::CallbackInfo &info)
{
	if (info.Length() != 1)
		return;

	auto replayBuffer = Napi::ObjectWrap<osn::SimpleReplayBuffer>::Unwrap(info[0].ToObject());

	replayBuffer->stopWorker();
	replayBuffer->cb.Reset();
	replayBuffer->ReleaseObjects();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper("SimpleReplayBuffer", "Destroy", {ipc::value(replayBuffer->uid)});

	if (!ValidateResponse(info, response))
		return;
}

Napi::Value osn::SimpleReplayBuffer::GetLegacySettings(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("SimpleReplayBuffer", "GetLegacySettings", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::SimpleReplayBuffer::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::SimpleReplayBuffer::SetLegacySettings(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::SimpleReplayBuffer *replayBuffer = Napi::ObjectWrap<osn::SimpleReplayBuffer>::Unwrap(value.ToObject());

	if (!replayBuffer) {
		Napi::TypeError::New(info.Env(), "Invalid replay buffer argument").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper("SimpleReplayBuffer", "SetLegacySettings", {replayBuffer->uid});

	if (!ValidateResponse(info, response))
		return;
}

Napi::Value osn::SimpleReplayBuffer::GetStreaming(const Napi::CallbackInfo &info)
{
	if (usesStream) {
		return parentOutputRef.IsEmpty() ? info.Env().Undefined() : parentOutputRef.Value();
	} else {
		return info.Env().Undefined();
	}
}

void osn::SimpleReplayBuffer::SetStreaming(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	if (!parentOutputRef.IsEmpty())
		parentOutputRef.Reset();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	if (value.IsNull() || value.IsUndefined()) {
		auto response = conn->call_synchronous_helper(className, "SetStreaming", {ipc::value(this->uid), ipc::value(UINT64_MAX)});
		ValidateResponse(info, response);
		usesStream = false;
		return;
	}

	Napi::Object obj = value.As<Napi::Object>();
	if (!obj.InstanceOf(osn::SimpleStreaming::constructor.Value()))
		Napi::TypeError::New(info.Env(), "Object is not a SimpleStreaming").ThrowAsJavaScriptException();

	osn::SimpleStreaming *streaming = Napi::ObjectWrap<osn::SimpleStreaming>::Unwrap(value.ToObject());

	if (!streaming) {
		Napi::TypeError::New(info.Env(), "Invalid streaming argument").ThrowAsJavaScriptException();
		return;
	}

	auto response = conn->call_synchronous_helper(className, "SetStreaming", {ipc::value(this->uid), ipc::value(streaming->uid)});
	if (!ValidateResponse(info, response))
		return;

	usesStream = true;
	parentOutputRef = Napi::Persistent(obj);
}

Napi::Value osn::SimpleReplayBuffer::GetRecording(const Napi::CallbackInfo &info)
{
	if (usesStream) {
		return info.Env().Undefined();
	} else {
		return parentOutputRef.IsEmpty() ? info.Env().Undefined() : parentOutputRef.Value();
	}
}

void osn::SimpleReplayBuffer::SetRecording(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	if (!parentOutputRef.IsEmpty())
		parentOutputRef.Reset();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	if (value.IsNull() || value.IsUndefined()) {
		auto response = conn->call_synchronous_helper(className, "SetRecording", {ipc::value(this->uid), ipc::value(UINT64_MAX)});
		ValidateResponse(info, response);
		usesStream = true;
		return;
	}

	Napi::Object obj = value.As<Napi::Object>();
	if (!obj.InstanceOf(osn::SimpleRecording::constructor.Value()))
		Napi::TypeError::New(info.Env(), "Object is not a Valid Recording").ThrowAsJavaScriptException();

	osn::SimpleRecording *recording = Napi::ObjectWrap<osn::SimpleRecording>::Unwrap(obj);
	if (!recording) {
		Napi::TypeError::New(info.Env(), "Invalid recording argument").ThrowAsJavaScriptException();
		return;
	}

	auto response = conn->call_synchronous_helper(className, "SetRecording", {ipc::value(this->uid), ipc::value(recording->uid)});
	if (!ValidateResponse(info, response))
		return;

	usesStream = false;
	parentOutputRef = Napi::Persistent(obj);
}