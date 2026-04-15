/******************************************************************************
    Copyright (C) 2016-2019 by Streamlabs (General Workings Inc)

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

#include "fader.hpp"
#include <iterator>
#include <vector>
#include "controller.hpp"
#include "osn-error.hpp"
#include "input.hpp"
#include "shared.hpp"
#include <iostream>

Napi::FunctionReference osn::Fader::constructor;

Napi::Object osn::Fader::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func = DefineClass(env, "Fader",
					  {
						  StaticMethod("create", &osn::Fader::Create),

						  InstanceMethod("destroy", &osn::Fader::Destroy),
						  InstanceMethod("attach", &osn::Fader::Attach),
						  InstanceMethod("detach", &osn::Fader::Detach),

						  InstanceAccessor("db", &osn::Fader::GetDeziBel, &osn::Fader::SetDezibel),
						  InstanceAccessor("deflection", &osn::Fader::GetDeflection, &osn::Fader::SetDeflection),
						  InstanceAccessor("mul", &osn::Fader::GetMultiplier, &osn::Fader::SetMultiplier),
					  });
	exports.Set("Fader", func);
	osn::Fader::constructor = Napi::Persistent(func);
	osn::Fader::constructor.SuppressDestruct();
	return exports;
}

osn::Fader::Fader(const Napi::CallbackInfo &info) : Napi::ObjectWrap<osn::Fader>(info)
{
	Napi::Env env = info.Env();
	Napi::HandleScope scope(env);
	size_t length = info.Length();

	if (length <= 0 || !info[0].IsNumber()) {
		Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
		return;
	}

	this->uid = (uint64_t)info[0].ToNumber().Int64Value();
}

Napi::Value osn::Fader::Create(const Napi::CallbackInfo &info)
{
	int32_t fader_type = info[0].ToNumber().Int32Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Fader", "Create",
						      {
							      ipc::value(fader_type),
						      });

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::Fader::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

Napi::Value osn::Fader::GetDeziBel(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Fader", "GetDeziBel",
						      {
							      ipc::value(this->uid),
						      });

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(Env(), response[1].value_union.fp32);
}

void osn::Fader::SetDezibel(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	float_t db = value.ToNumber().FloatValue();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper("Fader", "SetDeziBel", {ipc::value(this->uid), ipc::value(db)});
	ValidateResponse(info, response);
}

Napi::Value osn::Fader::GetDeflection(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Fader", "GetDeflection",
						      {
							      ipc::value(this->uid),
						      });

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(Env(), response[1].value_union.fp32);
}

void osn::Fader::SetDeflection(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	float_t deflection = value.ToNumber().FloatValue();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper("Fader", "SetDeflection", {ipc::value(this->uid), ipc::value(deflection)});
	ValidateResponse(info, response);
}

Napi::Value osn::Fader::GetMultiplier(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Fader", "GetMultiplier",
						      {
							      ipc::value(this->uid),
						      });

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(Env(), response[1].value_union.fp32);
}

void osn::Fader::SetMultiplier(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	float_t mul = value.ToNumber().FloatValue();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper("Fader", "SetMultiplier", {ipc::value(this->uid), ipc::value(mul)});
	ValidateResponse(info, response);
}

Napi::Value osn::Fader::Destroy(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Fader", "Destroy", {ipc::value(this->uid)});
	ValidateResponse(info, response);

	return info.Env().Undefined();
}

Napi::Value osn::Fader::Attach(const Napi::CallbackInfo &info)
{
	osn::Input *input = Napi::ObjectWrap<osn::Input>::Unwrap(info[0].ToObject());

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Fader", "Attach", {ipc::value(this->uid), ipc::value(input->sourceId)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return info.Env().Undefined();
}

Napi::Value osn::Fader::Detach(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Fader", "Detach", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return info.Env().Undefined();
}