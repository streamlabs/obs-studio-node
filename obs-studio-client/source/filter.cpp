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

#include "filter.hpp"
#include <condition_variable>
#include <mutex>
#include <string>
#include "controller.hpp"
#include "osn-error.hpp"
#include "ipc-value.hpp"
#include "shared.hpp"
#include "utility.hpp"

Napi::FunctionReference osn::Filter::constructor;

Napi::Object osn::Filter::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func = DefineClass(env, "Filter",
					  {
						  StaticMethod("types", &osn::Filter::Types),
						  StaticMethod("create", &osn::Filter::Create),

						  InstanceAccessor("configurable", &osn::Filter::CallIsConfigurable, nullptr),
						  InstanceAccessor("properties", &osn::Filter::CallGetProperties, nullptr),
						  InstanceAccessor("settings", &osn::Filter::CallGetSettings, nullptr),
						  InstanceAccessor("slowUncachedSettings", &osn::Filter::CallGetSlowUncachedSettings, nullptr),
						  InstanceAccessor("type", &osn::Filter::CallGetType, nullptr),
						  InstanceAccessor("name", &osn::Filter::CallGetName, &osn::Filter::CallSetName),
						  InstanceAccessor("outputFlags", &osn::Filter::CallGetOutputFlags, nullptr),
						  InstanceAccessor("flags", &osn::Filter::CallGetFlags, &osn::Filter::CallSetFlags),
						  InstanceAccessor("status", &osn::Filter::CallGetStatus, nullptr),
						  InstanceAccessor("id", &osn::Filter::CallGetId, nullptr),
						  InstanceAccessor("muted", &osn::Filter::CallGetMuted, &osn::Filter::CallSetMuted),
						  InstanceAccessor("enabled", &osn::Filter::CallGetEnabled, &osn::Filter::CallSetEnabled),

						  InstanceMethod("release", &osn::Filter::CallRelease),
						  InstanceMethod("remove", &osn::Filter::CallRemove),
						  InstanceMethod("update", &osn::Filter::CallUpdate),
						  InstanceMethod("load", &osn::Filter::CallLoad),
						  InstanceMethod("save", &osn::Filter::CallSave),
						  InstanceMethod("sendMouseClick", &osn::Filter::CallSendMouseClick),
						  InstanceMethod("sendMouseMove", &osn::Filter::CallSendMouseMove),
						  InstanceMethod("sendMouseWheel", &osn::Filter::CallSendMouseWheel),
						  InstanceMethod("sendFocus", &osn::Filter::CallSendFocus),
						  InstanceMethod("sendKeyClick", &osn::Filter::CallSendKeyClick),
						  InstanceMethod("callHandler", &osn::Filter::CallCallHandler),
					  });
	exports.Set("Filter", func);
	osn::Filter::constructor = Napi::Persistent(func);
	osn::Filter::constructor.SuppressDestruct();
	return exports;
}

osn::Filter::Filter(const Napi::CallbackInfo &info) : Napi::ObjectWrap<osn::Filter>(info)
{
	Napi::Env env = info.Env();
	Napi::HandleScope scope(env);
	size_t length = info.Length();

	if (length <= 0 || !info[0].IsNumber()) {
		Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
		return;
	}

	this->sourceId = (uint64_t)info[0].ToNumber().Int64Value();
}

Napi::Value osn::Filter::Types(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Filter", "Types", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	size_t count = response.size() - 1;
	Napi::Array types = Napi::Array::New(info.Env(), count);

	for (size_t idx = 0; idx < count; idx++)
		types.Set(static_cast<uint32_t>(idx), Napi::String::New(info.Env(), response[1 + idx].value_str));

	return types;
}

Napi::Value osn::Filter::Create(const Napi::CallbackInfo &info)
{
	std::string type = info[0].ToString().Utf8Value();
	std::string name = info[1].ToString().Utf8Value();
	Napi::String settings = Napi::String::New(info.Env(), "");

	if (info.Length() >= 3) {
		Napi::Object json = info.Env().Global().Get("JSON").As<Napi::Object>();
		Napi::Function stringify = json.Get("stringify").As<Napi::Function>();

		Napi::Object setobj = info[2].ToObject();
		settings = stringify.Call(json, {setobj}).As<Napi::String>();
	}

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto params = std::vector<ipc::value>{ipc::value(type), ipc::value(name)};
	std::string settings_str = settings.Utf8Value();
	if (settings_str.size() != 0) {
		params.push_back(ipc::value(settings_str));
	}

	std::vector<ipc::value> response = conn->call_synchronous_helper("Filter", "Create", {std::move(params)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	SourceDataInfo *sdi = new SourceDataInfo;
	sdi->name = name;
	sdi->obs_sourceId = type;
	sdi->id = response[1].value_union.ui64;

	CacheManager<SourceDataInfo *>::getInstance().Store(response[1].value_union.ui64, name, sdi);

	auto instance = osn::Filter::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

Napi::Value osn::Filter::CallIsConfigurable(const Napi::CallbackInfo &info)
{
	return osn::ISource::IsConfigurable(info, this->sourceId);
}

Napi::Value osn::Filter::CallGetProperties(const Napi::CallbackInfo &info)
{
	return osn::ISource::GetProperties(info, this->sourceId);
}

Napi::Value osn::Filter::CallGetSettings(const Napi::CallbackInfo &info)
{
	Napi::Value ret = osn::ISource::GetSettings(info, this->sourceId);

	SourceDataInfo *sdi = CacheManager<SourceDataInfo *>::getInstance().Retrieve(this->sourceId);
	if (sdi && sdi->obs_sourceId.compare("vst_filter") == 0) {
		sdi->settingsChanged = true;
	}
	return ret;
}

Napi::Value osn::Filter::CallGetSlowUncachedSettings(const Napi::CallbackInfo &info)
{
	return osn::ISource::GetSlowUncachedSettings(info, this->sourceId);
}

Napi::Value osn::Filter::CallGetType(const Napi::CallbackInfo &info)
{
	return osn::ISource::GetType(info, this->sourceId);
}

Napi::Value osn::Filter::CallGetName(const Napi::CallbackInfo &info)
{
	return osn::ISource::GetName(info, this->sourceId);
}

void osn::Filter::CallSetName(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::ISource::SetName(info, value, this->sourceId);
}

Napi::Value osn::Filter::CallGetOutputFlags(const Napi::CallbackInfo &info)
{
	return osn::ISource::GetOutputFlags(info, this->sourceId);
}

Napi::Value osn::Filter::CallGetFlags(const Napi::CallbackInfo &info)
{
	return osn::ISource::GetFlags(info, this->sourceId);
}

void osn::Filter::CallSetFlags(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::ISource::SetFlags(info, value, this->sourceId);
}

Napi::Value osn::Filter::CallGetStatus(const Napi::CallbackInfo &info)
{
	return osn::ISource::GetStatus(info, this->sourceId);
}

Napi::Value osn::Filter::CallGetId(const Napi::CallbackInfo &info)
{
	return osn::ISource::GetId(info, this->sourceId);
}

Napi::Value osn::Filter::CallGetMuted(const Napi::CallbackInfo &info)
{
	return osn::ISource::GetMuted(info, this->sourceId);
}

void osn::Filter::CallSetMuted(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::ISource::SetMuted(info, value, this->sourceId);
}

Napi::Value osn::Filter::CallGetEnabled(const Napi::CallbackInfo &info)
{
	return osn::ISource::GetEnabled(info, this->sourceId);
}

void osn::Filter::CallSetEnabled(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::ISource::SetEnabled(info, value, this->sourceId);
}

Napi::Value osn::Filter::CallRelease(const Napi::CallbackInfo &info)
{
	osn::ISource::Release(info, this->sourceId);

	return info.Env().Undefined();
}

Napi::Value osn::Filter::CallCallHandler(const Napi::CallbackInfo &info)
{
	return osn::ISource::CallHandler(info, this->sourceId);
}

Napi::Value osn::Filter::CallRemove(const Napi::CallbackInfo &info)
{
	osn::ISource::Remove(info, this->sourceId);
	this->sourceId = UINT64_MAX;

	return info.Env().Undefined();
}

Napi::Value osn::Filter::CallUpdate(const Napi::CallbackInfo &info)
{
	osn::ISource::Update(info, this->sourceId);

	SourceDataInfo *sdi = CacheManager<SourceDataInfo *>::getInstance().Retrieve(this->sourceId);
	if (sdi && sdi->obs_sourceId.compare("vst_filter") == 0) {
		sdi->settingsChanged = true;
	}

	return info.Env().Undefined();
}

Napi::Value osn::Filter::CallLoad(const Napi::CallbackInfo &info)
{
	osn::ISource::Load(info, this->sourceId);

	return info.Env().Undefined();
}

Napi::Value osn::Filter::CallSave(const Napi::CallbackInfo &info)
{
	osn::ISource::Save(info, this->sourceId);

	return info.Env().Undefined();
}

Napi::Value osn::Filter::CallSendMouseClick(const Napi::CallbackInfo &info)
{
	osn::ISource::SendMouseClick(info, this->sourceId);

	return info.Env().Undefined();
}

Napi::Value osn::Filter::CallSendMouseMove(const Napi::CallbackInfo &info)
{
	osn::ISource::SendMouseMove(info, this->sourceId);

	return info.Env().Undefined();
}

Napi::Value osn::Filter::CallSendMouseWheel(const Napi::CallbackInfo &info)
{
	osn::ISource::SendMouseWheel(info, this->sourceId);

	return info.Env().Undefined();
}

Napi::Value osn::Filter::CallSendFocus(const Napi::CallbackInfo &info)
{
	osn::ISource::SendFocus(info, this->sourceId);

	return info.Env().Undefined();
}

Napi::Value osn::Filter::CallSendKeyClick(const Napi::CallbackInfo &info)
{
	osn::ISource::SendKeyClick(info, this->sourceId);

	return info.Env().Undefined();
}