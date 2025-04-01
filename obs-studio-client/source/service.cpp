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

#include "service.hpp"
#include "controller.hpp"
#include "utility.hpp"
#include "utility-v8.hpp"
#include "properties.hpp"
#include "obs-property.hpp"

Napi::FunctionReference osn::Service::constructor;

Napi::Object osn::Service::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func =
		DefineClass(env, "Service",
			    {StaticMethod("types", &osn::Service::Types), StaticMethod("destroy", &osn::Service::Destroy),
			     StaticMethod("create", &osn::Service::Create), InstanceMethod("update", &osn::Service::Update),

			     InstanceAccessor("name", &osn::Service::GetName, nullptr), InstanceAccessor("properties", &osn::Service::GetProperties, nullptr),
			     InstanceAccessor("settings", &osn::Service::GetSettings, nullptr),

			     StaticAccessor("legacySettings", &osn::Service::GetLegacySettings, &osn::Service::SetLegacySettings)});
	exports.Set("Service", func);
	osn::Service::constructor = Napi::Persistent(func);
	osn::Service::constructor.SuppressDestruct();
	return exports;
}

osn::Service::Service(const Napi::CallbackInfo &info) : Napi::ObjectWrap<osn::Service>(info)
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

Napi::Value osn::Service::Types(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Service", "GetTypes", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	std::vector<std::string> types;

	for (size_t i = 1; i < response.size(); i++) {
		types.push_back(response[i].value_str);
	}

	return utilv8::ToValue<std::string>(info, types);
}

Napi::Value osn::Service::Create(const Napi::CallbackInfo &info)
{
	std::string type = info[0].ToString().Utf8Value();
	std::string name = info[1].ToString().Utf8Value();
	Napi::String settings = Napi::String::New(info.Env(), "");
	Napi::String hotkeys = Napi::String::New(info.Env(), "");

	Napi::Object json = info.Env().Global().Get("JSON").As<Napi::Object>();
	Napi::Function stringify = json.Get("stringify").As<Napi::Function>();

	if (info.Length() >= 4) {
		if (!info[3].IsUndefined()) {
			Napi::Object hksobj = info[3].ToObject();
			hotkeys = stringify.Call(json, {hksobj}).As<Napi::String>();
		}
	}
	if (info.Length() >= 3) {
		if (!info[2].IsUndefined()) {
			Napi::Object setobj = info[2].ToObject();
			settings = stringify.Call(json, {setobj}).As<Napi::String>();
		}
	}

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto params = std::vector<ipc::value>{ipc::value(type), ipc::value(name)};
	if (settings.Utf8Value().length() != 0) {
		std::string value;
		if (utilv8::FromValue(settings, value)) {
			params.push_back(ipc::value(value));
		}
	}
	if (hotkeys.Utf8Value().length() != 0) {
		std::string value;
		if (utilv8::FromValue(hotkeys, value)) {
			params.push_back(ipc::value(value));
		}
	}

	std::vector<ipc::value> response = conn->call_synchronous_helper("Service", "Create", {std::move(params)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::Service::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::Service::Destroy(const Napi::CallbackInfo &info)
{
	if (info.Length() != 1)
		return;

	auto service = Napi::ObjectWrap<osn::Service>::Unwrap(info[0].ToObject());

	auto conn = GetConnection(info);
	if (!conn)
		return;

	std::vector<ipc::value> response = conn->call_synchronous_helper("Service", "Destroy", {ipc::value(service->uid)});

	if (!ValidateResponse(info, response))
		return;
}

Napi::Value osn::Service::GetName(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Service", "GetName", {ipc::value((uint64_t)this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

Napi::Value osn::Service::GetProperties(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Service", "GetProperties", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	if (response.size() == 1)
		return info.Env().Null();

	osn::property_map_t pmap = osn::ProcessProperties(response, 1);

	std::shared_ptr<property_map_t> pSomeObject = std::make_shared<property_map_t>(pmap);
	auto prop_ptr = Napi::External<property_map_t>::New(info.Env(), pSomeObject.get());
	auto instance = osn::Properties::constructor.New({prop_ptr, Napi::Number::New(info.Env(), (uint32_t)this->uid)});
	return instance;
}

void osn::Service::Update(const Napi::CallbackInfo &info)
{
	Napi::Object jsonObj = info[0].ToObject();
	Napi::Object json = info.Env().Global().Get("JSON").As<Napi::Object>();
	Napi::Function stringify = json.Get("stringify").As<Napi::Function>();

	std::string jsondata = stringify.Call(json, {jsonObj}).As<Napi::String>();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	std::vector<ipc::value> response = conn->call_synchronous_helper("Service", "Update", {ipc::value(this->uid), ipc::value(jsondata)});

	if (!ValidateResponse(info, response))
		return;
}

Napi::Value osn::Service::GetSettings(const Napi::CallbackInfo &info)
{
	Napi::Object json = info.Env().Global().Get("JSON").As<Napi::Object>();
	Napi::Function parse = json.Get("parse").As<Napi::Function>();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Service", "GetSettings", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	Napi::String jsondata = Napi::String::New(info.Env(), response[1].value_str);
	Napi::Object jsonObj = parse.Call(json, {jsondata}).As<Napi::Object>();
	return jsonObj;
}

Napi::Value osn::Service::GetLegacySettings(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Service", "GetLegacySettings", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::Service::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::Service::SetLegacySettings(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::Service *service = Napi::ObjectWrap<osn::Service>::Unwrap(value.ToObject());

	if (!service) {
		Napi::TypeError::New(info.Env(), "Invalid service argument").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	std::vector<ipc::value> response = conn->call_synchronous_helper("Service", "SetLegacySettings", {service->uid});

	if (!ValidateResponse(info, response))
		return;
}
