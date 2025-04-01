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

#include "audio-track.hpp"
#include "utility.hpp"

Napi::FunctionReference osn::AudioTrack::constructor;

Napi::Object osn::AudioTrack::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func =
		DefineClass(env, "AudioTrack",
			    {StaticMethod("create", &osn::AudioTrack::Create), StaticAccessor("audioTracks", &osn::AudioTrack::GetAudioTracks, nullptr),
			     StaticAccessor("audioBitrates", &osn::AudioTrack::GetAudioBitrates, nullptr),
			     StaticMethod("getAtIndex", &osn::AudioTrack::GetAtIndex), StaticMethod("setAtIndex", &osn::AudioTrack::SetAtIndex),

			     InstanceAccessor("bitrate", &osn::AudioTrack::GetBitrate, &osn::AudioTrack::SetBitrate),
			     InstanceAccessor("name", &osn::AudioTrack::GetName, &osn::AudioTrack::SetName),
			     StaticMethod("importLegacySettings", &osn::AudioTrack::ImportLegacySettings),
			     StaticMethod("saveLegacySettings", &osn::AudioTrack::SaveLegacySettings)});

	exports.Set("AudioTrack", func);
	osn::AudioTrack::constructor = Napi::Persistent(func);
	osn::AudioTrack::constructor.SuppressDestruct();

	return exports;
}

osn::AudioTrack::AudioTrack(const Napi::CallbackInfo &info) : Napi::ObjectWrap<osn::AudioTrack>(info)
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

Napi::Value osn::AudioTrack::Create(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	uint32_t bitrate = info[0].ToNumber().Uint32Value();
	std::string name = info[1].ToString().Utf8Value();
	std::vector<ipc::value> response = conn->call_synchronous_helper("AudioTrack", "Create", {ipc::value(bitrate), ipc::value(name)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::AudioTrack::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

Napi::Value osn::AudioTrack::GetAudioTracks(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AudioTrack", "GetAudioTracks", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto tracks = Napi::Array::New(info.Env());
	uint32_t size = response[1].value_union.ui32;
	for (uint32_t i = 2; i < size + 2; i++) {
		uint64_t uid = response[i].value_union.ui64;
		if (uid == UINT64_MAX)
			tracks.Set(i - 2, info.Env().Undefined());
		else
			tracks.Set(i - 2, osn::AudioTrack::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(uid))}));
	}

	return tracks;
}

Napi::Value osn::AudioTrack::GetAudioBitrates(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AudioTrack", "GetAudioBitrates", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto bitrates = Napi::Array::New(info.Env());
	uint32_t size = response[1].value_union.ui32;
	for (uint32_t i = 2; i < size + 2; i++)
		bitrates.Set(i - 2, response[i].value_union.ui32);

	return bitrates;
}

Napi::Value osn::AudioTrack::GetAtIndex(const Napi::CallbackInfo &info)
{
	uint32_t index = info[0].ToNumber().Uint32Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AudioTrack", "GetAtIndex", {ipc::value(index)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::AudioTrack::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::AudioTrack::SetAtIndex(const Napi::CallbackInfo &info)
{
	osn::AudioTrack *audioTrack = Napi::ObjectWrap<osn::AudioTrack>::Unwrap(info[0].ToObject());
	uint32_t index = info[1].ToNumber().Uint32Value();

	if (!audioTrack) {
		Napi::TypeError::New(info.Env(), "Invalid audioTrack argument").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("AudioTrack", "SetAtIndex", {ipc::value(audioTrack->uid), ipc::value(index)});
}

Napi::Value osn::AudioTrack::GetBitrate(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AudioTrack", "GetBitrate", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void osn::AudioTrack::SetBitrate(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("AudioTrack", "SetBitrate", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
}

Napi::Value osn::AudioTrack::GetName(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("AudioTrack", "GetName", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

void osn::AudioTrack::SetName(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("AudioTrack", "SetName", {ipc::value(this->uid), ipc::value(value.ToString().Utf8Value())});
}

void osn::AudioTrack::ImportLegacySettings(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("AudioTrack", "ImportLegacySettings", {});
}

void osn::AudioTrack::SaveLegacySettings(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call("AudioTrack", "SaveLegacySettings", {});
}