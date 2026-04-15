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

#include "recording.hpp"
#include "utility.hpp"
#include "video-encoder.hpp"

Napi::Value osn::Recording::GetVideoEncoder(const Napi::CallbackInfo &info)
{
	return videoEncoderRef.IsEmpty() ? info.Env().Undefined() : videoEncoderRef.Value();
}

void osn::Recording::SetVideoEncoder(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	if (value.IsNull() || value.IsUndefined()) {
		if (!videoEncoderRef.IsEmpty())
			videoEncoderRef.Reset();
		auto response = conn->call_synchronous_helper(className, "SetVideoEncoder", {ipc::value(this->uid), ipc::value(UINT64_MAX)});
		ValidateResponse(info, response);
		return;
	}

	Napi::Object obj = value.As<Napi::Object>();
	if (!obj.InstanceOf(osn::VideoEncoder::constructor.Value()))
		Napi::TypeError::New(info.Env(), "Object is not a VideoEncoder").ThrowAsJavaScriptException();

	osn::VideoEncoder *encoder = Napi::ObjectWrap<osn::VideoEncoder>::Unwrap(value.ToObject());

	if (!encoder) {
		Napi::TypeError::New(info.Env(), "Invalid encoder argument").ThrowAsJavaScriptException();
		return;
	}

	auto response = conn->call_synchronous_helper(className, "SetVideoEncoder", {ipc::value(this->uid), ipc::value(encoder->uid)});
	if (!ValidateResponse(info, response))
		return;

	if (!videoEncoderRef.IsEmpty())
		videoEncoderRef.Reset();
	videoEncoderRef = Napi::Persistent(obj);
}

Napi::Value osn::Recording::GetSignalHandler(const Napi::CallbackInfo &info)
{
	if (this->cb.IsEmpty())
		return info.Env().Undefined();

	return this->cb.Value();
}

void osn::Recording::SetSignalHandler(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	Napi::Function cb = value.As<Napi::Function>();
	if (cb.IsNull() || !cb.IsFunction())
		return;

	stopWorker();

	this->cb = Napi::Persistent(cb);
	this->cb.SuppressDestruct();
}

Napi::Value osn::Recording::GetAvailableEncoders(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper(className, "GetAvailableEncoders", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	Napi::Array arr = Napi::Array::New(info.Env());
	uint32_t idx = 0;
	for (size_t i = 1; i + 1 < response.size(); i += 2) {
		Napi::Object obj = Napi::Object::New(info.Env());
		obj.Set("title", Napi::String::New(info.Env(), response[i].value_str));
		obj.Set("name", Napi::String::New(info.Env(), response[i + 1].value_str));
		arr.Set(idx++, obj);
	}

	return arr;
}

void osn::Recording::Start(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	startWorker(info.Env(), this->cb.Value(), className, this->uid);

	auto response = conn->call_synchronous_helper(className, "Start", {ipc::value(this->uid)});
	ValidateResponse(info, response);
}

void osn::Recording::Stop(const Napi::CallbackInfo &info)
{
	bool force = false;
	if (info.Length() == 1)
		force = info[0].ToBoolean().Value();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper(className, "Stop", {ipc::value(this->uid), ipc::value(force)});
	ValidateResponse(info, response);
}

void osn::Recording::SplitFile(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper(className, "SplitFile", {ipc::value(this->uid)});
	ValidateResponse(info, response);
}

Napi::Value osn::Recording::GetEnableFileSplit(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper(className, "GetEnableFileSplit", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void osn::Recording::SetEnableFileSplit(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response =
		conn->call_synchronous_helper(className, "SetEnableFileSplit", {ipc::value(this->uid), ipc::value((uint32_t)value.ToBoolean().Value())});
	ValidateResponse(info, response);
}

Napi::Value osn::Recording::GetSplitType(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper(className, "GetSplitType", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void osn::Recording::SetSplitType(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper(className, "SetSplitType", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
	ValidateResponse(info, response);
}

Napi::Value osn::Recording::GetSplitTime(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper(className, "GetSplitTime", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void osn::Recording::SetSplitTime(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper(className, "SetSplitTime", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
	ValidateResponse(info, response);
}

Napi::Value osn::Recording::GetSplitSize(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper(className, "GetSplitSize", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void osn::Recording::SetSplitSize(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper(className, "SetSplitSize", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
	ValidateResponse(info, response);
}

Napi::Value osn::Recording::GetFileResetTimestamps(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper(className, "GetFileResetTimestamps", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void osn::Recording::SetFileResetTimestamps(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper(className, "SetFileResetTimestamps", {ipc::value(this->uid), ipc::value(value.ToBoolean().Value())});
	ValidateResponse(info, response);
}
