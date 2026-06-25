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

#include "advanced-replay-buffer.hpp"
#include "utility.hpp"
#include "audio-encoder.hpp"
#include "advanced-streaming.hpp"
#include "advanced-recording.hpp"
#include "enhanced-broadcasting-advanced-streaming.hpp"

Napi::FunctionReference osn::AdvancedReplayBuffer::constructor;

Napi::Object osn::AdvancedReplayBuffer::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func =
		DefineClass(env, "AdvancedReplayBuffer",
			    {StaticMethod("create", &osn::AdvancedReplayBuffer::Create),
			     StaticMethod("destroy", &osn::AdvancedReplayBuffer::Destroy),

			     InstanceAccessor("path", &osn::AdvancedReplayBuffer::GetPath, &osn::AdvancedReplayBuffer::SetPath),
			     InstanceAccessor("format", &osn::AdvancedReplayBuffer::GetFormat, &osn::AdvancedReplayBuffer::SetFormat),
			     InstanceAccessor("muxerSettings", &osn::AdvancedReplayBuffer::GetMuxerSettings, &osn::AdvancedReplayBuffer::SetMuxerSettings),
			     InstanceAccessor("fileFormat", &osn::AdvancedReplayBuffer::GetFileFormat, &osn::AdvancedReplayBuffer::SetFileFormat),
			     InstanceAccessor("overwrite", &osn::AdvancedReplayBuffer::GetOverwrite, &osn::AdvancedReplayBuffer::SetOverwrite),
			     InstanceAccessor("noSpace", &osn::AdvancedReplayBuffer::GetNoSpace, &osn::AdvancedReplayBuffer::SetNoSpace),
			     InstanceAccessor("duration", &osn::AdvancedReplayBuffer::GetDuration, &osn::AdvancedReplayBuffer::SetDuration),
			     InstanceAccessor("prefix", &osn::AdvancedReplayBuffer::GetPrefix, &osn::AdvancedReplayBuffer::SetPrefix),
			     InstanceAccessor("suffix", &osn::AdvancedReplayBuffer::GetSuffix, &osn::AdvancedReplayBuffer::SetSuffix),
			     InstanceAccessor("signalHandler", &osn::AdvancedReplayBuffer::GetSignalHandler, &osn::AdvancedReplayBuffer::SetSignalHandler),
			     InstanceAccessor("mixer", &osn::AdvancedReplayBuffer::GetMixer, &osn::AdvancedReplayBuffer::SetMixer),
			     InstanceAccessor("usesStream", &osn::AdvancedReplayBuffer::GetUsesStream, &osn::AdvancedReplayBuffer::SetUsesStream),
			     InstanceAccessor("video", &osn::AdvancedReplayBuffer::GetCanvas, &osn::AdvancedReplayBuffer::SetCanvas),

			     InstanceMethod("start", &osn::AdvancedReplayBuffer::Start),
			     InstanceMethod("stop", &osn::AdvancedReplayBuffer::Stop),

			     InstanceMethod("save", &osn::AdvancedReplayBuffer::Save),
			     InstanceMethod("lastFile", &osn::AdvancedReplayBuffer::GetLastFile),

			     StaticAccessor("legacySettings", &osn::AdvancedReplayBuffer::GetLegacySettings, &osn::AdvancedReplayBuffer::SetLegacySettings),
			     InstanceAccessor("streaming", &osn::AdvancedReplayBuffer::GetStreaming, &osn::AdvancedReplayBuffer::SetStreaming),
			     InstanceAccessor("recording", &osn::AdvancedReplayBuffer::GetRecording, &osn::AdvancedReplayBuffer::SetRecording)});

	exports.Set("AdvancedReplayBuffer", func);
	osn::AdvancedReplayBuffer::constructor = Napi::Persistent(func);
	osn::AdvancedReplayBuffer::constructor.SuppressDestruct();

	return exports;
}

osn::AdvancedReplayBuffer::AdvancedReplayBuffer(const Napi::CallbackInfo &info) : Napi::ObjectWrap<osn::AdvancedReplayBuffer>(info)
{
	Napi::Env env = info.Env();
	Napi::HandleScope scope(env);
	size_t length = info.Length();

	if (length <= 0 || !info[0].IsNumber()) {
		Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
		return;
	}

	this->uid = (uint64_t)info[0].ToNumber().Int64Value();
	this->className = std::string("AdvancedReplayBuffer");
}

void osn::AdvancedReplayBuffer::Finalize(Napi::Env)
{
	ReleaseObjects();
}

void osn::AdvancedReplayBuffer::ReleaseObjects()
{
	if (!parentOutputRef.IsEmpty())
		parentOutputRef.Reset();
}

Napi::Value osn::AdvancedReplayBuffer::Create(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("AdvancedReplayBuffer", "Create", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::AdvancedReplayBuffer::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::AdvancedReplayBuffer::Destroy(const Napi::CallbackInfo &info)
{
	if (info.Length() != 1)
		return;

	auto replayBuffer = Napi::ObjectWrap<osn::AdvancedReplayBuffer>::Unwrap(info[0].ToObject());

	replayBuffer->stopWorker();
	replayBuffer->cb.Reset();
	replayBuffer->ReleaseObjects();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper("AdvancedReplayBuffer", "Destroy", {ipc::value(replayBuffer->uid)});

	if (!ValidateResponse(info, response))
		return;
}

Napi::Value osn::AdvancedReplayBuffer::GetMixer(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper(className, "GetMixer", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void osn::AdvancedReplayBuffer::SetMixer(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper(className, "SetMixer", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
	ValidateResponse(info, response);
}

Napi::Value osn::AdvancedReplayBuffer::GetLegacySettings(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("AdvancedReplayBuffer", "GetLegacySettings", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::AdvancedReplayBuffer::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::AdvancedReplayBuffer::SetLegacySettings(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::AdvancedReplayBuffer *replayBuffer = Napi::ObjectWrap<osn::AdvancedReplayBuffer>::Unwrap(value.ToObject());

	if (!replayBuffer) {
		Napi::TypeError::New(info.Env(), "Invalid replay buffer argument").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper("AdvancedReplayBuffer", "SetLegacySettings", {replayBuffer->uid});

	if (!ValidateResponse(info, response))
		return;
}

Napi::Value osn::AdvancedReplayBuffer::GetStreaming(const Napi::CallbackInfo &info)
{
	if (usesStream) {
		return parentOutputRef.IsEmpty() ? info.Env().Undefined() : parentOutputRef.Value();
	} else {
		return info.Env().Undefined();
	}
}

void osn::AdvancedReplayBuffer::SetStreaming(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	if (value.IsNull() || value.IsUndefined()) {
		if (!parentOutputRef.IsEmpty())
			parentOutputRef.Reset();
		auto response = conn->call_synchronous_helper(className, "SetStreaming", {ipc::value(this->uid), ipc::value(UINT64_MAX)});
		ValidateResponse(info, response);
		usesStream = false;
		return;
	}

	if (!value.IsObject()) {
		Napi::TypeError::New(info.Env(), "Object is not a valid Streaming").ThrowAsJavaScriptException();
		return;
	}

	Napi::Object obj = value.As<Napi::Object>();
	uint64_t streamingUid = UINT64_MAX;
	if (obj.InstanceOf(osn::AdvancedStreaming::constructor.Value())) {
		osn::AdvancedStreaming *streaming = Napi::ObjectWrap<osn::AdvancedStreaming>::Unwrap(obj);
		if (!streaming) {
			Napi::TypeError::New(info.Env(), "Invalid streaming argument").ThrowAsJavaScriptException();
			return;
		}

		streamingUid = streaming->uid;
	} else if (obj.InstanceOf(osn::EnhancedBroadcastingAdvancedStreaming::constructor.Value())) {
		osn::EnhancedBroadcastingAdvancedStreaming *streaming = Napi::ObjectWrap<osn::EnhancedBroadcastingAdvancedStreaming>::Unwrap(obj);
		if (!streaming) {
			Napi::TypeError::New(info.Env(), "Invalid streaming argument").ThrowAsJavaScriptException();
			return;
		}

		streamingUid = streaming->uid;
	} else {
		Napi::TypeError::New(info.Env(), "Object is not a valid Streaming").ThrowAsJavaScriptException();
		return;
	}

	if (streamingUid == UINT64_MAX) {
		Napi::TypeError::New(info.Env(), "Invalid streaming argument").ThrowAsJavaScriptException();
		return;
	}

	auto response = conn->call_synchronous_helper(className, "SetStreaming", {ipc::value(this->uid), ipc::value(streamingUid)});
	if (!ValidateResponse(info, response))
		return;

	usesStream = true;
	if (!parentOutputRef.IsEmpty())
		parentOutputRef.Reset();
	parentOutputRef = Napi::Persistent(obj);
}

Napi::Value osn::AdvancedReplayBuffer::GetRecording(const Napi::CallbackInfo &info)
{
	if (usesStream) {
		return info.Env().Undefined();
	} else {
		return parentOutputRef.IsEmpty() ? info.Env().Undefined() : parentOutputRef.Value();
	}
}

void osn::AdvancedReplayBuffer::SetRecording(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	if (value.IsNull() || value.IsUndefined()) {
		if (!parentOutputRef.IsEmpty())
			parentOutputRef.Reset();
		auto response = conn->call_synchronous_helper(className, "SetRecording", {ipc::value(this->uid), ipc::value(UINT64_MAX)});
		ValidateResponse(info, response);
		usesStream = false;
		return;
	}

	Napi::Object obj = value.As<Napi::Object>();
	if (!obj.InstanceOf(osn::AdvancedRecording::constructor.Value()))
		Napi::TypeError::New(info.Env(), "Object is not a Valid Recording").ThrowAsJavaScriptException();

	osn::AdvancedRecording *recording = Napi::ObjectWrap<osn::AdvancedRecording>::Unwrap(obj);
	if (!recording) {
		Napi::TypeError::New(info.Env(), "Invalid recording argument").ThrowAsJavaScriptException();
		return;
	}

	auto response = conn->call_synchronous_helper(className, "SetRecording", {ipc::value(this->uid), ipc::value(recording->uid)});
	if (!ValidateResponse(info, response))
		return;

	usesStream = false;
	if (!parentOutputRef.IsEmpty())
		parentOutputRef.Reset();
	parentOutputRef = Napi::Persistent(obj);
}
