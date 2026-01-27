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

#include "simple-streaming-base.hpp"

#include "audio-encoder.hpp"

Napi::Value osn::SimpleStreamingBase::GetAudioEncoder(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("SimpleStreaming", "GetAudioEncoder", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::AudioEncoder::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});
	return instance;
}

Napi::Value osn::SimpleStreamingBase::GetUseAdvanced(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("SimpleStreaming", "GetUseAdvanced", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void osn::SimpleStreamingBase::SetUseAdvanced(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper("SimpleStreaming", "SetUseAdvanced", {ipc::value(this->uid), ipc::value(value.ToBoolean().Value())});
}

Napi::Value osn::SimpleStreamingBase::GetCustomEncSettings(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("SimpleStreaming", "GetCustomEncSettings", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str.c_str());
}

void osn::SimpleStreamingBase::SetCustomEncSettings(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper("SimpleStreaming", "SetCustomEncSettings", {ipc::value(this->uid), ipc::value(value.ToString().Utf8Value())});
}

void osn::SimpleStreamingBase::SetAudioEncoder(const Napi::CallbackInfo &info, const Napi::Value &value)
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
