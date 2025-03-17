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

#include "reconnect.hpp"
#include "utility.hpp"

Napi::FunctionReference osn::Reconnect::constructor;

Napi::Object osn::Reconnect::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func = DefineClass(env, "Reconnect",
					  {
						  StaticMethod("create", &osn::Reconnect::Create),

						  InstanceAccessor("enabled", &osn::Reconnect::GetEnabled, &osn::Reconnect::SetEnabled),
						  InstanceAccessor("retryDelay", &osn::Reconnect::GetRetryDelay, &osn::Reconnect::SetRetryDelay),
						  InstanceAccessor("maxRetries", &osn::Reconnect::GetMaxRetries, &osn::Reconnect::SetMaxRetries),
					  });

	exports.Set("Reconnect", func);
	osn::Reconnect::constructor = Napi::Persistent(func);
	osn::Reconnect::constructor.SuppressDestruct();

	return exports;
}

osn::Reconnect::Reconnect(const Napi::CallbackInfo &info) : Napi::ObjectWrap<osn::Reconnect>(info)
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

Napi::Value osn::Reconnect::Create(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Reconnect", "Create", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::Reconnect::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

Napi::Value osn::Reconnect::GetEnabled(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Reconnect", "GetEnabled", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void osn::Reconnect::SetEnabled(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper("Reconnect", "SetEnabled", {ipc::value(this->uid), ipc::value(value.ToBoolean().Value())});
}

Napi::Value osn::Reconnect::GetRetryDelay(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Reconnect", "GetRetryDelay", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void osn::Reconnect::SetRetryDelay(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper("Reconnect", "SetRetryDelay", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
}

Napi::Value osn::Reconnect::GetMaxRetries(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Reconnect", "GetMaxRetries", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void osn::Reconnect::SetMaxRetries(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper("Reconnect", "SetMaxRetries", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
}
