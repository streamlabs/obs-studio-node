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

#include "isource.hpp"
#include "osn-error.hpp"
#include <functional>
#include "controller.hpp"
#include "shared.hpp"
#include "utility-v8.hpp"
#include "utility.hpp"

void osn::ISource::Release(const Napi::CallbackInfo &info, uint64_t id)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("Source", "Release", {ipc::value(id)});
}

void osn::ISource::Remove(const Napi::CallbackInfo &info, uint64_t id)
{
	CacheManager<SourceDataInfo *>::getInstance().Remove(id);

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("Source", "Remove", {ipc::value(id)});
}

Napi::Value osn::ISource::IsConfigurable(const Napi::CallbackInfo &info, uint64_t id)
{
	osn::ISource *source = Napi::ObjectWrap<osn::ISource>::Unwrap(info.This().ToObject());
	if (!source)
		return info.Env().Undefined();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Source", "IsConfigurable", {ipc::value(id)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), (bool)response[1].value_union.i32);
}

Napi::Value osn::ISource::GetProperties(const Napi::CallbackInfo &info, uint64_t id)
{
	osn::ISource *source = Napi::ObjectWrap<osn::ISource>::Unwrap(info.This().ToObject());
	if (!source)
		return info.Env().Undefined();

	SourceDataInfo *sdi = CacheManager<SourceDataInfo *>::getInstance().Retrieve(id);

	if (sdi && !sdi->propertiesChanged && sdi->properties.size() > 0) {
		std::shared_ptr<property_map_t> pSomeObject = std::make_shared<property_map_t>(sdi->properties);
		auto prop_ptr = Napi::External<property_map_t>::New(info.Env(), pSomeObject.get());
		auto instance = osn::Properties::constructor.New({prop_ptr, Napi::Number::New(info.Env(), (uint32_t)id)});
		return instance;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Source", "GetProperties", {ipc::value(id)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	if (response.size() == 1)
		return info.Env().Null();

	osn::property_map_t pmap = osn::ProcessProperties(response, 1);

	if (sdi) {
		sdi->properties = pmap;
		sdi->propertiesChanged = false;
	}
	std::shared_ptr<property_map_t> pSomeObject = std::make_shared<property_map_t>(pmap);
	auto prop_ptr = Napi::External<property_map_t>::New(info.Env(), pSomeObject.get());
	auto instance = osn::Properties::constructor.New({prop_ptr, Napi::Number::New(info.Env(), (uint32_t)id)});
	return instance;
}

Napi::Value osn::ISource::GetSlowUncachedSettings(const Napi::CallbackInfo &info, uint64_t id)
{
	osn::ISource *source = Napi::ObjectWrap<osn::ISource>::Unwrap(info.This().ToObject());
	if (!source)
		return info.Env().Undefined();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Source", "GetSettings", {ipc::value(id)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	Napi::Object json = info.Env().Global().Get("JSON").As<Napi::Object>();
	Napi::Function parse = json.Get("parse").As<Napi::Function>();

	Napi::String jsondata = Napi::String::New(info.Env(), response[1].value_str);
	Napi::Object jsonObj = parse.Call(json, {jsondata}).As<Napi::Object>();

	return jsonObj;
}

Napi::Value osn::ISource::GetSettings(const Napi::CallbackInfo &info, uint64_t id)
{
	osn::ISource *source = Napi::ObjectWrap<osn::ISource>::Unwrap(info.This().ToObject());
	if (!source)
		return info.Env().Undefined();

	Napi::Object json = info.Env().Global().Get("JSON").As<Napi::Object>();
	Napi::Function parse = json.Get("parse").As<Napi::Function>();

	SourceDataInfo *sdi = CacheManager<SourceDataInfo *>::getInstance().Retrieve(id);

	if (sdi && !sdi->settingsChanged && sdi->setting.size() > 0) {
		Napi::String jsondata = Napi::String::New(info.Env(), sdi->setting);
		Napi::Object jsonObj = parse.Call(json, {jsondata}).As<Napi::Object>();
		return jsonObj;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Source", "GetSettings", {ipc::value(id)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	Napi::String jsondata = Napi::String::New(info.Env(), response[1].value_str);
	Napi::Object jsonObj = parse.Call(json, {jsondata}).As<Napi::Object>();

	if (sdi) {
		sdi->setting = response[1].value_str;
		sdi->settingsChanged = false;
	}

	return jsonObj;
}

void osn::ISource::Update(const Napi::CallbackInfo &info, uint64_t id)
{
	Napi::Object jsonObj = info[0].ToObject();
	bool shouldUpdate = true;

	Napi::Object json = info.Env().Global().Get("JSON").As<Napi::Object>();
	Napi::Function stringify = json.Get("stringify").As<Napi::Function>();

	std::string jsondata = stringify.Call(json, {jsonObj}).As<Napi::String>();

	SourceDataInfo *sdi = CacheManager<SourceDataInfo *>::getInstance().Retrieve(id);

	if (sdi && sdi->setting.size() > 0) {
		auto newSettings = nlohmann::json::parse(jsondata);
		auto settings = nlohmann::json::parse(sdi->setting);

		nlohmann::json::iterator it = newSettings.begin();
		while (!shouldUpdate && it != newSettings.end()) {
			nlohmann::json::iterator item = settings.find(it.key());
			if (item != settings.end()) {
				if (it.value() != item.value()) {
					shouldUpdate = false;
				}
			}
			it++;
		}
	}

	if (shouldUpdate) {
		auto conn = GetConnection(info);
		if (!conn)
			return;

		std::vector<ipc::value> response = conn->call_synchronous_helper("Source", "Update", {ipc::value(id), ipc::value(jsondata)});

		if (!ValidateResponse(info, response))
			return;

		if (sdi) {
			sdi->setting = response[1].value_str;
			sdi->settingsChanged = false;
			sdi->propertiesChanged = true;
		}
	}
}

void osn::ISource::SendMessage(const Napi::CallbackInfo &info, uint64_t id)
{
	Napi::Object jsonObj = info[0].ToObject();

	Napi::Object json = info.Env().Global().Get("JSON").As<Napi::Object>();
	Napi::Function stringify = json.Get("stringify").As<Napi::Function>();

	std::string jsondata = stringify.Call(json, {jsonObj}).As<Napi::String>();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	std::vector<ipc::value> response = conn->call_synchronous_helper("Source", "SendMessage", {ipc::value(id), ipc::value(jsondata)});

	if (!ValidateResponse(info, response))
		return;
}

void osn::ISource::Load(const Napi::CallbackInfo &info, uint64_t id)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("Source", "Load", {ipc::value(id)});
}

void osn::ISource::Save(const Napi::CallbackInfo &info, uint64_t id)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("Source", "Save", {ipc::value(id)});
}

Napi::Value osn::ISource::GetType(const Napi::CallbackInfo &info, uint64_t id)
{
	osn::ISource *source = Napi::ObjectWrap<osn::ISource>::Unwrap(info.This().ToObject());
	if (!source)
		return info.Env().Undefined();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Source", "GetType", {ipc::value(id)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.i32);
}

Napi::Value osn::ISource::GetName(const Napi::CallbackInfo &info, uint64_t id)
{
	osn::ISource *source = Napi::ObjectWrap<osn::ISource>::Unwrap(info.This().ToObject());
	if (!source)
		return info.Env().Undefined();

	SourceDataInfo *sdi = CacheManager<SourceDataInfo *>::getInstance().Retrieve(id);

	if (sdi) {
		if (sdi->name.size() > 0) {
			return Napi::String::New(info.Env(), sdi->name);
		}
	}

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Source", "GetName", {ipc::value(id)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	if (sdi)
		sdi->name = response[1].value_str.c_str();

	return Napi::String::New(info.Env(), response[1].value_str);
}

void osn::ISource::SetName(const Napi::CallbackInfo &info, const Napi::Value &value, uint64_t id)
{
	std::string name = value.ToString().Utf8Value();

	osn::ISource *source = Napi::ObjectWrap<osn::ISource>::Unwrap(info.This().ToObject());
	if (!source)
		return;

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("Source", "SetName", {ipc::value(id), ipc::value(name)});
}

Napi::Value osn::ISource::GetOutputFlags(const Napi::CallbackInfo &info, uint64_t id)
{
	osn::ISource *source = Napi::ObjectWrap<osn::ISource>::Unwrap(info.This().ToObject());
	if (!source)
		return info.Env().Undefined();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Source", "GetOutputFlags", {ipc::value(id)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

Napi::Value osn::ISource::GetFlags(const Napi::CallbackInfo &info, uint64_t id)
{
	osn::ISource *source = Napi::ObjectWrap<osn::ISource>::Unwrap(info.This().ToObject());
	if (!source)
		return info.Env().Undefined();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Source", "GetFlags", {ipc::value(id)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void osn::ISource::SetFlags(const Napi::CallbackInfo &info, const Napi::Value &value, uint64_t id)
{
	uint32_t flags = value.ToNumber().Uint32Value();

	osn::ISource *source = Napi::ObjectWrap<osn::ISource>::Unwrap(info.This().ToObject());
	if (!source)
		return;

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("Source", "SetFlags", {ipc::value(id), ipc::value(flags)});
}

Napi::Value osn::ISource::GetStatus(const Napi::CallbackInfo &info, uint64_t id)
{
	osn::ISource *source = Napi::ObjectWrap<osn::ISource>::Unwrap(info.This().ToObject());
	if (!source)
		return info.Env().Undefined();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Source", "GetStatus", {ipc::value(id)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

Napi::Value osn::ISource::GetId(const Napi::CallbackInfo &info, uint64_t id)
{
	osn::ISource *source = Napi::ObjectWrap<osn::ISource>::Unwrap(info.This().ToObject());
	if (!source)
		return info.Env().Undefined();

	SourceDataInfo *sdi = CacheManager<SourceDataInfo *>::getInstance().Retrieve(id);

	if (sdi) {
		if (sdi->obs_sourceId.size() > 0)
			return Napi::String::New(info.Env(), sdi->obs_sourceId);
	}

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Source", "GetId", {ipc::value(id)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	if (sdi) {
		sdi->obs_sourceId = response[1].value_str;
	}

	return Napi::String::New(info.Env(), response[1].value_str);
}

Napi::Value osn::ISource::GetMuted(const Napi::CallbackInfo &info, uint64_t id)
{
	osn::ISource *source = Napi::ObjectWrap<osn::ISource>::Unwrap(info.This().ToObject());
	if (!source)
		return info.Env().Undefined();

	SourceDataInfo *sdi = CacheManager<SourceDataInfo *>::getInstance().Retrieve(id);

	if (sdi) {
		if (sdi && !sdi->mutedChanged)
			return Napi::Boolean::New(info.Env(), sdi->isMuted);
	}

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Source", "GetMuted", {ipc::value(id)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	if (sdi) {
		sdi->isMuted = (bool)response[1].value_union.i32;
		sdi->mutedChanged = false;
	}

	return Napi::Boolean::New(info.Env(), (bool)response[1].value_union.i32);
}

void osn::ISource::SetMuted(const Napi::CallbackInfo &info, const Napi::Value &value, uint64_t id)
{
	bool muted = value.ToBoolean().Value();

	osn::ISource *source = Napi::ObjectWrap<osn::ISource>::Unwrap(info.This().ToObject());
	if (!source)
		return;

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("Source", "SetMuted", {ipc::value(id), ipc::value(muted)});

	SourceDataInfo *sdi = CacheManager<SourceDataInfo *>::getInstance().Retrieve(id);
	if (sdi)
		sdi->mutedChanged = true;
}

Napi::Object osn::ISource::CallHandler(const Napi::CallbackInfo &info, uint64_t id)
{
	Napi::Object result = Napi::Object::New(info.Env());
	Napi::String fuction_name = info[0].ToString();
	Napi::String fuction_input = info[1].ToString();

	auto conn = GetConnection(info);

	if (!conn) {
		Napi::TypeError::New(info.Env(), "IPC Connection Failed").ThrowAsJavaScriptException();
		return result;
	}

	std::vector<ipc::value> response =
		conn->call_synchronous_helper("Source", "CallHandler", {ipc::value(id), ipc::value(fuction_name), ipc::value(fuction_input)});

	if (!ValidateResponse(info, response)) {
		Napi::TypeError::New(info.Env(), "Invalid IPC Response").ThrowAsJavaScriptException();
		return result;
	}

	result.Set("output", Napi::String::New(info.Env(), response[1].value_str.c_str()));
	return result;
}

Napi::Value osn::ISource::GetEnabled(const Napi::CallbackInfo &info, uint64_t id)
{
	osn::ISource *source = Napi::ObjectWrap<osn::ISource>::Unwrap(info.This().ToObject());
	if (!source)
		return info.Env().Undefined();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Source", "GetEnabled", {ipc::value(id)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), (bool)response[1].value_union.i32);
}

void osn::ISource::SetEnabled(const Napi::CallbackInfo &info, const Napi::Value &value, uint64_t id)
{
	bool enabled = value.ToBoolean().Value();

	osn::ISource *source = Napi::ObjectWrap<osn::ISource>::Unwrap(info.This().ToObject());
	if (!source)
		return;

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("Source", "SetEnabled", {ipc::value(id), ipc::value(enabled)});
}

void osn::ISource::SendMouseClick(const Napi::CallbackInfo &info, uint64_t id)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	Napi::Object mouse_event_obj = info[0].ToObject();
	uint32_t type = info[1].ToNumber().Uint32Value();
	bool mouse_up = info[2].ToBoolean().Value();
	uint32_t click_count = info[3].ToNumber().Uint32Value();

	uint32_t modifiers = mouse_event_obj.Get("modifiers").ToNumber().Uint32Value();
	uint32_t x = mouse_event_obj.Get("x").ToNumber().Uint32Value();
	uint32_t y = mouse_event_obj.Get("y").ToNumber().Uint32Value();

	conn->call("Source", "SendMouseClick",
		   {ipc::value(id), ipc::value(modifiers), ipc::value(x), ipc::value(y), ipc::value(type), ipc::value(mouse_up), ipc::value(click_count)});
}

void osn::ISource::SendMouseMove(const Napi::CallbackInfo &info, uint64_t id)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	Napi::Object mouse_event_obj = info[0].ToObject();
	bool mouse_leave = info[1].ToBoolean().Value();

	uint32_t modifiers = mouse_event_obj.Get("modifiers").ToNumber().Uint32Value();
	uint32_t x = mouse_event_obj.Get("x").ToNumber().Uint32Value();
	uint32_t y = mouse_event_obj.Get("y").ToNumber().Uint32Value();

	conn->call("Source", "SendMouseMove", {ipc::value(id), ipc::value(modifiers), ipc::value(x), ipc::value(y), ipc::value(mouse_leave)});
}

void osn::ISource::SendMouseWheel(const Napi::CallbackInfo &info, uint64_t id)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	Napi::Object mouse_event_obj = info[0].ToObject();
	int32_t x_delta = info[1].ToNumber().Int32Value();
	int32_t y_delta = info[2].ToNumber().Int32Value();

	uint32_t modifiers = mouse_event_obj.Get("modifiers").ToNumber().Uint32Value();
	uint32_t x = mouse_event_obj.Get("x").ToNumber().Uint32Value();
	uint32_t y = mouse_event_obj.Get("y").ToNumber().Uint32Value();

	conn->call("Source", "SendMouseWheel", {ipc::value(id), ipc::value(modifiers), ipc::value(x), ipc::value(y), ipc::value(x_delta), ipc::value(y_delta)});
}

void osn::ISource::SendFocus(const Napi::CallbackInfo &info, uint64_t id)
{
	bool focus = info[0].ToBoolean().Value();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("Source", "SendFocus", {ipc::value(id), ipc::value(focus)});
}

void osn::ISource::SendKeyClick(const Napi::CallbackInfo &info, uint64_t id)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;
	Napi::Object key_event_obj = info[0].ToObject();
	bool key_up = info[1].ToBoolean().Value();

	uint32_t modifiers = key_event_obj.Get("modifiers").ToNumber().Uint32Value();
	uint32_t native_modifiers = key_event_obj.Get("nativeModifiers").ToNumber().Uint32Value();
	uint32_t native_scancode = key_event_obj.Get("nativeScancode").ToNumber().Uint32Value();
	uint32_t native_vkey = key_event_obj.Get("nativeVkey").ToNumber().Uint32Value();
	std::string text = key_event_obj.Get("text").ToString().Utf8Value();

	conn->call("Source", "SendKeyClick",
		   {ipc::value(id), ipc::value(modifiers), ipc::value(text), ipc::value(native_modifiers), ipc::value(native_scancode), ipc::value(native_vkey),
		    ipc::value((int32_t)key_up)});
}