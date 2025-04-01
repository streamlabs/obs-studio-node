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

#include "audio-encoder.hpp"
#include "utility.hpp"
#include "properties.hpp"
#include "obs-property.hpp"

Napi::FunctionReference osn::AudioEncoder::constructor;

Napi::Object osn::AudioEncoder::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func = DefineClass(env, "AudioEncoder",
					  {
						  StaticMethod("create", &osn::AudioEncoder::Create),

						  InstanceAccessor("name", &osn::AudioEncoder::GetName, &osn::AudioEncoder::SetName),
						  InstanceAccessor("bitrate", &osn::AudioEncoder::GetBitrate, &osn::AudioEncoder::SetBitrate),
					  });
	exports.Set("AudioEncoder", func);
	osn::AudioEncoder::constructor = Napi::Persistent(func);
	osn::AudioEncoder::constructor.SuppressDestruct();
	return exports;
}

osn::AudioEncoder::AudioEncoder(const Napi::CallbackInfo &info) : Napi::ObjectWrap<osn::AudioEncoder>(info)
{
	Napi::Env env = info.Env();
	Napi::HandleScope scope(env);
	size_t length = info.Length();
	this->uid = 0;

	if (length <= 0 || !info[0].IsNumber()) {
		Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
		return;
	}

	this->uid = (uint64_t)info[0].ToNumber().Int64Value();
}

Napi::Value osn::AudioEncoder::Create(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AudioEncoder", "Create", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::AudioEncoder::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

Napi::Value osn::AudioEncoder::GetName(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AudioEncoder", "GetName", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

void osn::AudioEncoder::SetName(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	std::string name = value.ToString().Utf8Value();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("AudioEncoder", "SetName", {ipc::value(this->uid), ipc::value(name)});
}

Napi::Value osn::AudioEncoder::GetBitrate(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AudioEncoder", "GetBitrate", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void osn::AudioEncoder::SetBitrate(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("AudioEncoder", "SetBitrate", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
}