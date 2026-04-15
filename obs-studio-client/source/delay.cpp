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

#include "delay.hpp"
#include "utility.hpp"

Napi::FunctionReference osn::Delay::constructor;

Napi::Object osn::Delay::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func = DefineClass(env, "Delay",
					  {
						  StaticMethod("create", &osn::Delay::Create),

						  InstanceAccessor("enabled", &osn::Delay::GetEnabled, &osn::Delay::SetEnabled),
						  InstanceAccessor("delaySec", &osn::Delay::GetDelaySec, &osn::Delay::SetDelaySec),
						  InstanceAccessor("preserveDelay", &osn::Delay::GetPreserveDelay, &osn::Delay::SetPreserveDelay),
					  });

	exports.Set("Delay", func);
	osn::Delay::constructor = Napi::Persistent(func);
	osn::Delay::constructor.SuppressDestruct();

	return exports;
}

osn::Delay::Delay(const Napi::CallbackInfo &info) : Napi::ObjectWrap<osn::Delay>(info)
{
	Napi::Env env = info.Env();
	Napi::HandleScope scope(env);
	size_t length = info.Length();
	this->uid = 0;
	this->connectionEpoch = 0;

	if (length <= 0 || !info[0].IsNumber()) {
		Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
		return;
	}

	this->uid = (uint64_t)info[0].ToNumber().Int64Value();
	this->connectionEpoch = Controller::GetInstance().GetConnectionEpoch();
}

Napi::Value osn::Delay::Create(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Delay", "Create", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::Delay::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::Delay::Finalize(Napi::Env env)
{
	if (this->uid == UINT64_MAX)
		return;

	// If OBS was restarted/disconnected, skip IPC cleanup for this stale wrapper.
	if (this->connectionEpoch != Controller::GetInstance().GetConnectionEpoch()) {
		this->uid = UINT64_MAX;
		return;
	}

	auto conn = Controller::GetInstance().GetConnection();
	if (!conn) {
		this->uid = UINT64_MAX;
		return;
	}

	auto response = conn->call_synchronous_helper("Delay", "Destroy", {ipc::value(this->uid)});
	ValidateResponse(env, response);

	this->uid = UINT64_MAX;
}

Napi::Value osn::Delay::GetEnabled(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Delay", "GetEnabled", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void osn::Delay::SetEnabled(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper("Delay", "SetEnabled", {ipc::value(this->uid), ipc::value(value.ToBoolean().Value())});
	ValidateResponse(info, response);
}

Napi::Value osn::Delay::GetDelaySec(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Delay", "GetDelaySec", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void osn::Delay::SetDelaySec(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper("Delay", "SetDelaySec", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
	ValidateResponse(info, response);
}

Napi::Value osn::Delay::GetPreserveDelay(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Delay", "GetPreserveDelay", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void osn::Delay::SetPreserveDelay(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper("Delay", "SetPreserveDelay", {ipc::value(this->uid), ipc::value(value.ToBoolean().Value())});
	ValidateResponse(info, response);
}
