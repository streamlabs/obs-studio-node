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

#include "volmeter.hpp"
#include <iostream>
#include <iterator>
#include <vector>
#include "controller.hpp"
#include "osn-error.hpp"
#include "input.hpp"
#include "shared.hpp"
#include "utility-v8.hpp"
#include "utility.hpp"
#include "callback-manager.hpp"

Napi::FunctionReference osn::Volmeter::constructor;

Napi::Object osn::Volmeter::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func = DefineClass(env, "Volmeter",
					  {
						  StaticMethod("create", &osn::Volmeter::Create),

						  InstanceMethod("destroy", &osn::Volmeter::Destroy),
						  InstanceMethod("attach", &osn::Volmeter::Attach),
						  InstanceMethod("detach", &osn::Volmeter::Detach),
					  });
	exports.Set("Volmeter", func);
	osn::Volmeter::constructor = Napi::Persistent(func);
	osn::Volmeter::constructor.SuppressDestruct();
	return exports;
}

osn::Volmeter::Volmeter(const Napi::CallbackInfo &info) : Napi::ObjectWrap<osn::Volmeter>(info)
{
	Napi::Env env = info.Env();
	Napi::HandleScope scope(env);
	size_t length = info.Length();

	if (length <= 0 || !info[0].IsNumber()) {
		Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
		return;
	}

	this->m_uid = (uint64_t)info[0].ToNumber().Int64Value();
}

Napi::Value osn::Volmeter::Create(const Napi::CallbackInfo &info)
{
	int32_t type = info[0].ToNumber().Int32Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Volmeter", "Create",
									 {
										 ipc::value(type),
									 });

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::Volmeter::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});
	globalCallback::add_volmeter(response[1].value_union.ui64);

	return instance;
}

Napi::Value osn::Volmeter::Destroy(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	globalCallback::remove_volmeter(this->m_uid);

	conn->call("Volmeter", "Destroy", {ipc::value(this->m_uid)});

	return info.Env().Undefined();
}

Napi::Value osn::Volmeter::Attach(const Napi::CallbackInfo &info)
{
	osn::Input *input = Napi::ObjectWrap<osn::Input>::Unwrap(info[0].ToObject());

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("Volmeter", "Attach", {ipc::value(this->m_uid), ipc::value(input->sourceId)});
	return info.Env().Undefined();
}

Napi::Value osn::Volmeter::Detach(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("Volmeter", "Detach", {ipc::value(this->m_uid)});
	return info.Env().Undefined();
}
