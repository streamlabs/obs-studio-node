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

#include "video-encoder.hpp"
#include "utility.hpp"
#include "properties.hpp"
#include "obs-property.hpp"

Napi::FunctionReference osn::VideoEncoder::constructor;

Napi::Object osn::VideoEncoder::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func = DefineClass(env, "VideoEncoder",
					  {
						  StaticMethod("create", &osn::VideoEncoder::Create),
						  StaticMethod("types", &osn::VideoEncoder::GetTypes),

						  InstanceAccessor("name", &osn::VideoEncoder::GetName, &osn::VideoEncoder::SetName),
						  InstanceAccessor("type", &osn::VideoEncoder::GetType, nullptr),
						  InstanceAccessor("active", &osn::VideoEncoder::GetActive, nullptr),
						  InstanceAccessor("id", &osn::VideoEncoder::GetId, nullptr),
						  InstanceAccessor("lastError", &osn::VideoEncoder::GetLastError, nullptr),
						  InstanceAccessor("properties", &osn::VideoEncoder::GetProperties, nullptr),
						  InstanceAccessor("settings", &osn::VideoEncoder::GetSettings, nullptr),

						  InstanceMethod("update", &osn::VideoEncoder::Update),
						  InstanceMethod("release", &osn::VideoEncoder::Release),
					  });
	exports.Set("VideoEncoder", func);
	osn::VideoEncoder::constructor = Napi::Persistent(func);
	osn::VideoEncoder::constructor.SuppressDestruct();
	return exports;
}

osn::VideoEncoder::VideoEncoder(const Napi::CallbackInfo &info) : Napi::ObjectWrap<osn::VideoEncoder>(info)
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

Napi::Value osn::VideoEncoder::Create(const Napi::CallbackInfo &info)
{
	std::string id = info[0].ToString().Utf8Value();
	std::string name = info[1].ToString().Utf8Value();

	// Settings
	std::string settings = "";

	if (info.Length() > 2) {
		Napi::Object json = info.Env().Global().Get("JSON").As<Napi::Object>();
		Napi::Function stringify = json.Get("stringify").As<Napi::Function>();
		Napi::Object settingsObj = info[2].ToObject();
		settings = stringify.Call(json, {settingsObj}).As<Napi::String>().Utf8Value();
	}

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("VideoEncoder", "Create", {ipc::value(id), ipc::value(name), ipc::value(settings)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::VideoEncoder::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

Napi::Value osn::VideoEncoder::GetTypes(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("VideoEncoder", "GetTypes", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	Napi::Array types = Napi::Array::New(info.Env());
	for (int i = 1; i < response.size(); i++)
		types.Set(i - 1, Napi::String::New(info.Env(), response[i].value_str));

	return types;
}

Napi::Value osn::VideoEncoder::GetName(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("VideoEncoder", "GetName", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

void osn::VideoEncoder::SetName(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	std::string name = value.ToString().Utf8Value();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("VideoEncoder", "SetName", {ipc::value(this->uid), ipc::value(name)});
}

Napi::Value osn::VideoEncoder::GetType(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("VideoEncoder", "GetType", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

Napi::Value osn::VideoEncoder::GetActive(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("VideoEncoder", "GetActive", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

Napi::Value osn::VideoEncoder::GetId(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("VideoEncoder", "GetId", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

Napi::Value osn::VideoEncoder::GetLastError(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("VideoEncoder", "GetLastError", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

void osn::VideoEncoder::Release(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("VideoEncoder", "Release", {});
}

void osn::VideoEncoder::Update(const Napi::CallbackInfo &info)
{
	Napi::Object jsonObj = info[0].ToObject();
	Napi::Object json = info.Env().Global().Get("JSON").As<Napi::Object>();
	Napi::Function stringify = json.Get("stringify").As<Napi::Function>();

	std::string jsondata = stringify.Call(json, {jsonObj}).As<Napi::String>();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("VideoEncoder", "Update", {ipc::value(this->uid), ipc::value(jsondata)});
}

Napi::Value osn::VideoEncoder::GetProperties(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("VideoEncoder", "GetProperties", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	if (response.size() == 1)
		return info.Env().Undefined();

	osn::property_map_t pmap = osn::ProcessProperties(response, 1);

	std::shared_ptr<property_map_t> pSomeObject = std::make_shared<property_map_t>(pmap);
	auto prop_ptr = Napi::External<property_map_t>::New(info.Env(), pSomeObject.get());
	auto instance = osn::Properties::constructor.New({prop_ptr, Napi::Number::New(info.Env(), (uint32_t)this->uid)});

	return instance;
}

Napi::Value osn::VideoEncoder::GetSettings(const Napi::CallbackInfo &info)
{
	Napi::Object json = info.Env().Global().Get("JSON").As<Napi::Object>();
	Napi::Function parse = json.Get("parse").As<Napi::Function>();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("VideoEncoder", "GetSettings", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	Napi::String jsondata = Napi::String::New(info.Env(), response[1].value_str);
	Napi::Object jsonObj = parse.Call(json, {jsondata}).As<Napi::Object>();
	return jsonObj;
}
