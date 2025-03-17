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

#include "advanced-recording.hpp"
#include "utility.hpp"
#include "advanced-streaming.hpp"

Napi::FunctionReference osn::AdvancedRecording::constructor;

Napi::Object osn::AdvancedRecording::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func = DefineClass(
		env, "AdvancedRecording",
		{StaticMethod("create", &osn::AdvancedRecording::Create),
		 StaticMethod("destroy", &osn::AdvancedRecording::Destroy),

		 InstanceAccessor("path", &osn::AdvancedRecording::GetPath, &osn::AdvancedRecording::SetPath),
		 InstanceAccessor("format", &osn::AdvancedRecording::GetFormat, &osn::AdvancedRecording::SetFormat),
		 InstanceAccessor("muxerSettings", &osn::AdvancedRecording::GetMuxerSettings, &osn::AdvancedRecording::SetMuxerSettings),
		 InstanceAccessor("fileFormat", &osn::AdvancedRecording::GetFileFormat, &osn::AdvancedRecording::SetFileFormat),
		 InstanceAccessor("overwrite", &osn::AdvancedRecording::GetOverwrite, &osn::AdvancedRecording::SetOverwrite),
		 InstanceAccessor("noSpace", &osn::AdvancedRecording::GetNoSpace, &osn::AdvancedRecording::SetNoSpace),

		 InstanceAccessor("videoEncoder", &osn::AdvancedRecording::GetVideoEncoder, &osn::AdvancedRecording::SetVideoEncoder),
		 InstanceAccessor("signalHandler", &osn::AdvancedRecording::GetSignalHandler, &osn::AdvancedRecording::SetSignalHandler),
		 InstanceAccessor("mixer", &osn::AdvancedRecording::GetMixer, &osn::AdvancedRecording::SetMixer),
		 InstanceAccessor("rescaling", &osn::AdvancedRecording::GetRescaling, &osn::AdvancedRecording::SetRescaling),
		 InstanceAccessor("outputWidth", &osn::AdvancedRecording::GetOutputWidth, &osn::AdvancedRecording::SetOutputWidth),
		 InstanceAccessor("outputHeight", &osn::AdvancedRecording::GetOutputHeight, &osn::AdvancedRecording::SetOutputHeight),
		 InstanceAccessor("useStreamEncoders", &osn::AdvancedRecording::GetUseStreamEncoders, &osn::AdvancedRecording::SetUseStreamEncoders),
		 InstanceAccessor("enableFileSplit", &osn::AdvancedRecording::GetEnableFileSplit, &osn::AdvancedRecording::SetEnableFileSplit),
		 InstanceAccessor("splitType", &osn::AdvancedRecording::GetSplitType, &osn::AdvancedRecording::SetSplitType),
		 InstanceAccessor("splitTime", &osn::AdvancedRecording::GetSplitTime, &osn::AdvancedRecording::SetSplitTime),
		 InstanceAccessor("splitSize", &osn::AdvancedRecording::GetSplitSize, &osn::AdvancedRecording::SetSplitSize),
		 InstanceAccessor("fileResetTimestamps", &osn::AdvancedRecording::GetFileResetTimestamps, &osn::AdvancedRecording::SetFileResetTimestamps),
		 InstanceAccessor("video", &osn::AdvancedRecording::GetCanvas, &osn::AdvancedRecording::SetCanvas),

		 InstanceMethod("start", &osn::AdvancedRecording::Start),
		 InstanceMethod("stop", &osn::AdvancedRecording::Stop),
		 InstanceMethod("splitFile", &osn::AdvancedRecording::SplitFile),

		 StaticAccessor("legacySettings", &osn::AdvancedRecording::GetLegacySettings, &osn::AdvancedRecording::SetLegacySettings),
		 InstanceMethod("lastFile", &osn::AdvancedRecording::GetLastFile),
		 InstanceAccessor("streaming", &osn::AdvancedRecording::GetStreaming, &osn::AdvancedRecording::SetStreaming)});

	exports.Set("AdvancedRecording", func);
	osn::AdvancedRecording::constructor = Napi::Persistent(func);
	osn::AdvancedRecording::constructor.SuppressDestruct();

	return exports;
}

osn::AdvancedRecording::AdvancedRecording(const Napi::CallbackInfo &info) : Napi::ObjectWrap<osn::AdvancedRecording>(info)
{
	Napi::Env env = info.Env();
	Napi::HandleScope scope(env);
	size_t length = info.Length();

	if (length <= 0 || !info[0].IsNumber()) {
		Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
		return;
	}

	this->uid = (uint64_t)info[0].ToNumber().Int64Value();
	this->className = std::string("AdvancedRecording");
}

Napi::Value osn::AdvancedRecording::Create(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AdvancedRecording", "Create", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::AdvancedRecording::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::AdvancedRecording::Destroy(const Napi::CallbackInfo &info)
{
	if (info.Length() != 1)
		return;

	auto recording = Napi::ObjectWrap<osn::AdvancedRecording>::Unwrap(info[0].ToObject());

	recording->stopWorker();
	recording->cb.Reset();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	std::vector<ipc::value> response = conn->call_synchronous_helper("AdvancedRecording", "Destroy", {ipc::value(recording->uid)});

	if (!ValidateResponse(info, response))
		return;
}

Napi::Value osn::AdvancedRecording::GetMixer(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AdvancedRecording", "GetMixer", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void osn::AdvancedRecording::SetMixer(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper("AdvancedRecording", "SetMixer", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
}

Napi::Value osn::AdvancedRecording::GetRescaling(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AdvancedRecording", "GetRescaling", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void osn::AdvancedRecording::SetRescaling(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper("AdvancedRecording", "SetRescaling", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
}

Napi::Value osn::AdvancedRecording::GetOutputWidth(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AdvancedRecording", "GetOutputWidth", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void osn::AdvancedRecording::SetOutputWidth(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper("AdvancedRecording", "SetOutputWidth", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
}

Napi::Value osn::AdvancedRecording::GetOutputHeight(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AdvancedRecording", "GetOutputHeight", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void osn::AdvancedRecording::SetOutputHeight(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper("AdvancedRecording", "SetOutputHeight", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
}

Napi::Value osn::AdvancedRecording::GetUseStreamEncoders(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AdvancedRecording", "GetUseStreamEncoders", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void osn::AdvancedRecording::SetUseStreamEncoders(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper("AdvancedRecording", "SetUseStreamEncoders", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
}

Napi::Value osn::AdvancedRecording::GetLegacySettings(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AdvancedRecording", "GetLegacySettings", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::AdvancedRecording::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::AdvancedRecording::SetLegacySettings(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::AdvancedRecording *recording = Napi::ObjectWrap<osn::AdvancedRecording>::Unwrap(value.ToObject());

	if (!recording) {
		Napi::TypeError::New(info.Env(), "Invalid recording argument").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	std::vector<ipc::value> response = conn->call_synchronous_helper("AdvancedRecording", "SetLegacySettings", {recording->uid});

	if (!ValidateResponse(info, response))
		return;
}

Napi::Value osn::AdvancedRecording::GetStreaming(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper(className, "GetStreaming", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::AdvancedStreaming::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});
	return instance;
}

void osn::AdvancedRecording::SetStreaming(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::AdvancedStreaming *streaming = Napi::ObjectWrap<osn::AdvancedStreaming>::Unwrap(value.ToObject());

	if (!streaming) {
		Napi::TypeError::New(info.Env(), "Invalid streaming argument").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call(className, "SetStreaming", {ipc::value(this->uid), ipc::value(streaming->uid)});
}