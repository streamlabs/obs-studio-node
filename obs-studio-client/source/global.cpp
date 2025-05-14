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

#include "global.hpp"
#include <condition_variable>
#include <ipc-value.hpp>
#include <mutex>
#include "controller.hpp"
#include "osn-error.hpp"
#include "input.hpp"
#include "scene.hpp"
#include "transition.hpp"
#include "utility-v8.hpp"

Napi::FunctionReference osn::Global::constructor;

Napi::Object osn::Global::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func = DefineClass(env, "Global",
					  {
						  StaticMethod("getOutputSource", &osn::Global::getOutputSource),
						  StaticMethod("setOutputSource", &osn::Global::setOutputSource),

						  StaticMethod("addSceneToBackstage", &osn::Global::addSceneToBackstage),
						  StaticMethod("removeSceneFromBackstage", &osn::Global::removeSceneFromBackstage),

						  StaticMethod("getOutputFlagsFromId", &osn::Global::getOutputFlagsFromId),

						  StaticAccessor("laggedFrames", &osn::Global::GetLaggedFrames, nullptr),
						  StaticAccessor("totalFrames", &osn::Global::GetTotalFrames, nullptr),
						  StaticAccessor("locale", &osn::Global::getLocale, &osn::Global::setLocale),
						  StaticAccessor("multipleRendering", &osn::Global::getMultipleRendering, &osn::Global::setMultipleRendering),

						  StaticAccessor("cpuPercentage", &osn::Global::GetCPUPercentage, nullptr),
						  StaticAccessor("currentFrameRate", &osn::Global::GetCurrentFrameRate, nullptr),
						  StaticAccessor("averageFrameRenderTime", &osn::Global::GetAverageTimeToRenderFrame, nullptr),
						  StaticAccessor("diskSpaceAvailable", &osn::Global::GetDiskSpaceAvailable, nullptr),
						  StaticAccessor("memoryUsage", &osn::Global::GetMemoryUsage, nullptr),
					  });
	exports.Set("Global", func);
	osn::Global::constructor = Napi::Persistent(func);
	osn::Global::constructor.SuppressDestruct();
	return exports;
}

osn::Global::Global(const Napi::CallbackInfo &info) : Napi::ObjectWrap<osn::Global>(info)
{
	Napi::Env env = info.Env();
	Napi::HandleScope scope(env);
}

Napi::Value osn::Global::getOutputSource(const Napi::CallbackInfo &info)
{
	uint32_t channel = info[0].ToNumber().Uint32Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Global", "GetOutputSource", {ipc::value(channel)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	if (response[2].value_union.i32 == 0) {
		auto instance = osn::Input::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

		return instance;
	} else if (response[2].value_union.i32 == 2) {
		auto instance = osn::Transition::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

		return instance;
	} else if (response[2].value_union.i32 == 3) {
		auto instance = osn::Scene::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

		return instance;
	}
	return info.Env().Undefined();
}

Napi::Value osn::Global::setOutputSource(const Napi::CallbackInfo &info)
{
	uint32_t channel = info[0].ToNumber().Uint32Value();
	osn::Input *input = nullptr;

	if (info[1].IsObject())
		input = Napi::ObjectWrap<osn::Input>::Unwrap(info[1].ToObject());

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("Global", "SetOutputSource", {ipc::value(channel), ipc::value(input ? input->sourceId : UINT64_MAX)});

	return info.Env().Undefined();
}

Napi::Value osn::Global::addSceneToBackstage(const Napi::CallbackInfo &info)
{
	osn::Input *input = nullptr;

	if (info[0].IsObject())
		input = Napi::ObjectWrap<osn::Input>::Unwrap(info[0].ToObject());

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("Global", "AddSceneToBackstage", {ipc::value(input ? input->sourceId : UINT64_MAX)});

	return info.Env().Undefined();
}

Napi::Value osn::Global::removeSceneFromBackstage(const Napi::CallbackInfo &info)
{
	osn::Input *input = nullptr;

	if (info[0].IsObject())
		input = Napi::ObjectWrap<osn::Input>::Unwrap(info[0].ToObject());

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("Global", "RemoveSceneFromBackstage", {ipc::value(input ? input->sourceId : UINT64_MAX)});

	return info.Env().Undefined();
}

Napi::Value osn::Global::getOutputFlagsFromId(const Napi::CallbackInfo &info)
{
	std::string id = info[0].ToString().Utf8Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Global", "GetOutputFlagsFromId", {ipc::value(id)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

Napi::Value osn::Global::GetLaggedFrames(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Global", "GetLaggedFrames", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

Napi::Value osn::Global::GetTotalFrames(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Global", "GetTotalFrames", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

Napi::Value osn::Global::getLocale(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Global", "GetLocale", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

void osn::Global::setLocale(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("Global", "SetLocale", {ipc::value(value.ToString().Utf8Value())});
}

Napi::Value osn::Global::getMultipleRendering(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Global", "GetMultipleRendering", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.i32);
}

void osn::Global::setMultipleRendering(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("Global", "SetMultipleRendering", {ipc::value(value.ToBoolean().Value())});
}

Napi::Value osn::Global::GetCPUPercentage(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Global", "GetCPUPercentage", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

Napi::Value osn::Global::GetCurrentFrameRate(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Global", "GetCurrentFrameRate", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

Napi::Value osn::Global::GetAverageTimeToRenderFrame(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Global", "GetAverageTimeToRenderFrame", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

Napi::Value osn::Global::GetDiskSpaceAvailable(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Global", "GetDiskSpaceAvailable", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

Napi::Value osn::Global::GetMemoryUsage(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Global", "GetMemoryUsage", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}
