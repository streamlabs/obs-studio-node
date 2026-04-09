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

#include "network.hpp"
#include "utility.hpp"

Napi::FunctionReference osn::Network::constructor;

Napi::Object osn::Network::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func =
		DefineClass(env, "Network",
			    {
				    StaticMethod("create", &osn::Network::Create),

				    InstanceAccessor("bindIP", &osn::Network::GetBindIP, &osn::Network::SetBindIP),
				    InstanceAccessor("networkInterfaces", &osn::Network::GetNetworkInterfaces, nullptr),
				    InstanceAccessor("enableDynamicBitrate", &osn::Network::GetEnableDynamicBitrate, &osn::Network::SetEnableDynamicBitrate),
				    InstanceAccessor("enableOptimizations", &osn::Network::GetEnableOptimizations, &osn::Network::SetEnableOptimizations),
				    InstanceAccessor("enableLowLatency", &osn::Network::GetEnableLowLatency, &osn::Network::SetEnableLowLatency),
			    });

	exports.Set("Network", func);
	osn::Network::constructor = Napi::Persistent(func);
	osn::Network::constructor.SuppressDestruct();

	return exports;
}

osn::Network::Network(const Napi::CallbackInfo &info) : Napi::ObjectWrap<osn::Network>(info)
{
	Napi::Env env = info.Env();
	Napi::HandleScope scope(env);
	size_t length = info.Length();
	this->uid = UINT64_MAX;
	this->connectionEpoch = 0;

	if (length <= 0 || !info[0].IsNumber()) {
		Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
		return;
	}

	this->uid = (uint64_t)info[0].ToNumber().Int64Value();
	this->connectionEpoch = Controller::GetInstance().GetConnectionEpoch();
}

Napi::Value osn::Network::Create(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Network", "Create", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::Network::constructor.New({Napi::Number::New(info.Env(), static_cast<uint32_t>(response[1].value_union.ui64))});

	return instance;
}

void osn::Network::Finalize(Napi::Env env)
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

	auto response = conn->call_synchronous_helper("Network", "Destroy", {ipc::value(this->uid)});
	ValidateResponse(env, response);

	this->uid = UINT64_MAX;
}

Napi::Value osn::Network::GetBindIP(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Network", "GetBindIP", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

void osn::Network::SetBindIP(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper("Network", "SetBindIP", {ipc::value(this->uid), ipc::value(value.ToString().Utf8Value())});
	ValidateResponse(info, response);
}

Napi::Value osn::Network::GetNetworkInterfaces(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Network", "GetNetworkInterfaces", {ipc::value()});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	uint32_t size = response[1].value_union.ui32;
	auto interfaces = Napi::Object::New(info.Env());

	for (uint32_t i = 2; i < size * 2 + 2; i += 2)
		interfaces.Set(response[i].value_str, response[i + 1].value_str);

	return interfaces;
}

Napi::Value osn::Network::GetEnableDynamicBitrate(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Network", "GetEnableDynamicBitrate", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void osn::Network::SetEnableDynamicBitrate(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper("Network", "SetEnableDynamicBitrate", {ipc::value(this->uid), ipc::value(value.ToBoolean().Value())});
	ValidateResponse(info, response);
}

Napi::Value osn::Network::GetEnableOptimizations(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Network", "GetEnableOptimizations", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void osn::Network::SetEnableOptimizations(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper("Network", "SetEnableOptimizations", {ipc::value(this->uid), ipc::value(value.ToBoolean().Value())});
	ValidateResponse(info, response);
}

Napi::Value osn::Network::GetEnableLowLatency(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Network", "GetEnableLowLatency", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void osn::Network::SetEnableLowLatency(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper("Network", "SetEnableLowLatency", {ipc::value(this->uid), ipc::value(value.ToBoolean().Value())});
	ValidateResponse(info, response);
}
