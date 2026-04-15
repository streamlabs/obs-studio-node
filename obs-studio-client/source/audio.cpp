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

#include "audio.hpp"
#include "utility.hpp"

Napi::FunctionReference osn::Audio::constructor;

Napi::Object osn::Audio::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func = DefineClass(env, "Audio",
					  {StaticAccessor("audioContext", &osn::Audio::GetAudioContext, &osn::Audio::SetAudioContext),
					   StaticAccessor("legacySettings", &osn::Audio::GetLegacySettings, &osn::Audio::SetLegacySettings),
					   StaticAccessor("monitoringDevice", &osn::Audio::GetMonitoringDevice, &osn::Audio::SetMonitoringDevice),
					   StaticAccessor("monitoringDeviceLegacy", &osn::Audio::GetMonitoringDeviceLegacy, nullptr),
					   StaticAccessor("monitoringDevices", &osn::Audio::GetMonitoringDevices, nullptr),
					   StaticAccessor("disableAudioDucking", &osn::Audio::GetDisableAudioDucking, &osn::Audio::SetDisableAudioDucking),
					   StaticAccessor("disableAudioDuckingLegacy", &osn::Audio::GetDisableAudioDuckingLegacy, nullptr)});
	exports.Set("Audio", func);
	osn::Audio::constructor = Napi::Persistent(func);
	osn::Audio::constructor.SuppressDestruct();
	return exports;
}

osn::Audio::Audio(const Napi::CallbackInfo &info) : Napi::ObjectWrap<osn::Audio>(info)
{
	Napi::Env env = info.Env();
	Napi::HandleScope scope(env);
}

void CreateAudio(const Napi::CallbackInfo &info, const std::vector<ipc::value> &response, Napi::Object &audio, uint32_t index)
{
	audio.Set("sampleRate", response[index++].value_union.ui32);
	audio.Set("speakers", response[index++].value_union.ui32);
}

void SerializeAudioData(const Napi::Object &audio, std::vector<ipc::value> &args)
{
	args.push_back(audio.Get("sampleRate").ToNumber().Uint32Value());
	args.push_back(audio.Get("speakers").ToNumber().Uint32Value());
}

Napi::Value osn::Audio::GetAudioContext(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Audio", "GetAudioContext", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	if (response.size() != 3)
		return info.Env().Undefined();

	Napi::Object audio = Napi::Object::New(info.Env());
	CreateAudio(info, response, audio, 1);
	return audio;
}

void osn::Audio::SetAudioContext(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	Napi::Object audio = value.ToObject();
	if (!audio || !audio.IsObject()) {
		Napi::Error::New(info.Env(), "The audio context object passed is invalid.").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	std::vector<ipc::value> args;
	SerializeAudioData(audio, args);
	auto response = conn->call_synchronous_helper("Audio", "SetAudioContext", args);
	ValidateResponse(info, response);
}

Napi::Value osn::Audio::GetLegacySettings(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Audio", "GetLegacySettings", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	if (response.size() != 3)
		return info.Env().Undefined();

	Napi::Object audio = Napi::Object::New(info.Env());
	CreateAudio(info, response, audio, 1);
	return audio;
}

void osn::Audio::SetLegacySettings(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	Napi::Object audio = value.ToObject();
	if (!audio || !audio.IsObject()) {
		Napi::Error::New(info.Env(), "The audio context object passed is invalid.").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	std::vector<ipc::value> args;
	SerializeAudioData(audio, args);
	conn->call("Audio", "SetLegacySettings", args);
}

Napi::Value osn::Audio::GetMonitoringDevice(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Audio", "GetMonitoringDevice", {});

	if (!ValidateResponse(info, response) || response.size() != 3)
		return info.Env().Undefined();

	auto device = Napi::Object::New(info.Env());
	device.Set("name", response[1].value_str);
	device.Set("id", response[2].value_str);
	return device;
}

void osn::Audio::SetMonitoringDevice(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto device = value.ToObject();
	auto name = device.Get("name").ToString().Utf8Value();
	auto id = device.Get("id").ToString().Utf8Value();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper("Audio", "SetMonitoringDevice", {ipc::value(name), ipc::value(id)});
	ValidateResponse(info, response);
}

Napi::Value osn::Audio::GetMonitoringDeviceLegacy(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Audio", "GetMonitoringDeviceLegacy", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto device = Napi::Object::New(info.Env());
	device.Set("name", response[1].value_str);
	device.Set("id", response[2].value_str);
	return device;
}

Napi::Value osn::Audio::GetMonitoringDevices(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Audio", "GetMonitoringDevices", {});
	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	uint32_t size = response[1].value_union.ui32;
	auto devices = Napi::Array::New(info.Env(), size);
	uint32_t index = 0;
	for (uint32_t i = 2; i < size * 2 + 2; i += 2) {
		auto device = Napi::Object::New(info.Env());
		device.Set("name", response[i].value_str);
		device.Set("id", response[i + 1].value_str);
		devices.Set(index++, device);
	}

	return devices;
}

Napi::Value osn::Audio::GetDisableAudioDucking(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Audio", "GetDisableAudioDucking", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void osn::Audio::SetDisableAudioDucking(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	bool disableAudioDucking = value.ToBoolean().Value();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("Audio", "SetDisableAudioDucking", {ipc::value(disableAudioDucking)});
}

Napi::Value osn::Audio::GetDisableAudioDuckingLegacy(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Audio", "GetDisableAudioDuckingLegacy", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}