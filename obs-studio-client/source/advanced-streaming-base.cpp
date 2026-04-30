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

#include "advanced-streaming-base.hpp"

Napi::Value osn::AdvancedStreamingBase::GetAudioTrack(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("AdvancedStreaming", "GetAudioTrack", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void osn::AdvancedStreamingBase::SetAudioTrack(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response =
		conn->call_synchronous_helper("AdvancedStreaming", "SetAudioTrack", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
	ValidateResponse(info, response);
}

Napi::Value osn::AdvancedStreamingBase::GetTwitchTrack(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("AdvancedStreaming", "GetTwitchTrack", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void osn::AdvancedStreamingBase::SetTwitchTrack(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response =
		conn->call_synchronous_helper("AdvancedStreaming", "SetTwitchTrack", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
	ValidateResponse(info, response);
}

Napi::Value osn::AdvancedStreamingBase::GetRescaling(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("AdvancedStreaming", "GetRescaling", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void osn::AdvancedStreamingBase::SetRescaling(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper("AdvancedStreaming", "SetRescaling", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
	ValidateResponse(info, response);
}

Napi::Value osn::AdvancedStreamingBase::GetRescaleFilter(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("AdvancedStreaming", "GetRescaleFilter", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void osn::AdvancedStreamingBase::SetRescaleFilter(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response =
		conn->call_synchronous_helper("AdvancedStreaming", "SetRescaleFilter", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
	ValidateResponse(info, response);
}

Napi::Value osn::AdvancedStreamingBase::GetOutputWidth(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("AdvancedStreaming", "GetOutputWidth", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void osn::AdvancedStreamingBase::SetOutputWidth(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response =
		conn->call_synchronous_helper("AdvancedStreaming", "SetOutputWidth", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
	ValidateResponse(info, response);
}

Napi::Value osn::AdvancedStreamingBase::GetOutputHeight(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("AdvancedStreaming", "GetOutputHeight", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void osn::AdvancedStreamingBase::SetOutputHeight(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response =
		conn->call_synchronous_helper("AdvancedStreaming", "SetOutputHeight", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
	ValidateResponse(info, response);
}
