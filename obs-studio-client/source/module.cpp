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

#include <condition_variable>
#include <mutex>
#include <string>
#include "controller.hpp"
#include "osn-error.hpp"
#include "ipc-value.hpp"
#include "shared.hpp"
#include "utility.hpp"
#include "module.hpp"

Napi::FunctionReference osn::Module::constructor;

Napi::Object osn::Module::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func = DefineClass(env, "Module",
					  {
						  StaticMethod("open", &osn::Module::Open),
						  StaticMethod("modules", &osn::Module::Modules),

						  InstanceMethod("initialize", &osn::Module::Initialize),

						  InstanceAccessor("name", &osn::Module::Name, nullptr),
						  InstanceAccessor("fileName", &osn::Module::FileName, nullptr),
						  InstanceAccessor("author", &osn::Module::Author, nullptr),
						  InstanceAccessor("description", &osn::Module::Description, nullptr),
						  InstanceAccessor("binaryPath", &osn::Module::BinaryPath, nullptr),
						  InstanceAccessor("dataPath", &osn::Module::DataPath, nullptr),

					  });
	exports.Set("Module", func);
	osn::Module::constructor = Napi::Persistent(func);
	osn::Module::constructor.SuppressDestruct();
	return exports;
}

osn::Module::Module(const Napi::CallbackInfo &info) : Napi::ObjectWrap<osn::Module>(info)
{
	Napi::Env env = info.Env();
	Napi::HandleScope scope(env);
	size_t length = info.Length();
	this->moduleId = 0;

	if (length <= 0 || !info[0].IsNumber()) {
		Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
		return;
	}

	this->moduleId = (uint64_t)info[0].ToNumber().Int64Value();
}

Napi::Value osn::Module::Open(const Napi::CallbackInfo &info)
{
	std::string bin_path = info[0].ToString().Utf8Value();
	std::string data_path = info[1].ToString().Utf8Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Module", "Open", {ipc::value(bin_path), ipc::value(data_path)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::Module::constructor.New({Napi::Number::New(info.Env(), static_cast<uint32_t>(response[1].value_union.ui64))});

	return instance;
}

Napi::Value osn::Module::Modules(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Module", "Modules", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	uint64_t size = response[1].value_union.ui64;
	Napi::Array modules = Napi::Array::New(info.Env(), size);

	for (uint64_t i = 2; i < (size + 2); i++)
		modules.Set(static_cast<uint32_t>(i - 2), Napi::String::New(info.Env(), response.at(i).value_str.c_str()));

	return modules;
}

Napi::Value osn::Module::Initialize(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Module", "Initialize", {ipc::value(this->moduleId)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.i32);
}

Napi::Value osn::Module::Name(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Module", "GetName", {ipc::value(this->moduleId)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

Napi::Value osn::Module::FileName(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Module", "GetFileName", {ipc::value(this->moduleId)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

Napi::Value osn::Module::Description(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Module", "GetDescription", {ipc::value(this->moduleId)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

Napi::Value osn::Module::Author(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Module", "GetAuthor", {ipc::value(this->moduleId)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

Napi::Value osn::Module::BinaryPath(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Module", "GetBinaryPath", {ipc::value(this->moduleId)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

Napi::Value osn::Module::DataPath(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Module", "GetDataPath", {ipc::value(this->moduleId)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}