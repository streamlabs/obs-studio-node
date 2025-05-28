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

#include "replay-buffer.hpp"
#include "utility.hpp"
#include "video-encoder.hpp"

Napi::Value osn::ReplayBuffer::GetDuration(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper(className, "GetDuration", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void osn::ReplayBuffer::SetDuration(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper(className, "SetDuration", {ipc::value(this->uid), ipc::value(value.ToNumber().Uint32Value())});
}

Napi::Value osn::ReplayBuffer::GetPrefix(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper(className, "GetPrefix", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

void osn::ReplayBuffer::SetPrefix(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper(className, "SetPrefix", {ipc::value(this->uid), ipc::value(value.ToString().Utf8Value())});
}

Napi::Value osn::ReplayBuffer::GetSuffix(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper(className, "GetSuffix", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::String::New(info.Env(), response[1].value_str);
}

void osn::ReplayBuffer::SetSuffix(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper(className, "SetSuffix", {ipc::value(this->uid), ipc::value(value.ToString().Utf8Value())});
}

Napi::Value osn::ReplayBuffer::GetUsesStream(const Napi::CallbackInfo &info)
{
	return Napi::Boolean::New(info.Env(), usesStream);
}

void osn::ReplayBuffer::SetUsesStream(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	//todo should be depricated
	auto conn = GetConnection(info);
	if (!conn)
		return;

	usesStream = value.ToBoolean().Value();
	conn->call_synchronous_helper(className, "SetUsesStream", {ipc::value(this->uid), ipc::value(value.ToBoolean().Value())});
}

Napi::Value osn::ReplayBuffer::GetSignalHandler(const Napi::CallbackInfo &info)
{
	if (this->cb.IsEmpty())
		return info.Env().Undefined();

	return this->cb.Value();
}

void osn::ReplayBuffer::SetSignalHandler(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	Napi::Function cb = value.As<Napi::Function>();
	if (cb.IsNull() || !cb.IsFunction())
		return;

	stopWorker();

	this->cb = Napi::Persistent(cb);
	this->cb.SuppressDestruct();
}

void osn::ReplayBuffer::Start(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	startWorker(info.Env(), this->cb.Value(), className, this->uid);

	conn->call(className, "Start", {ipc::value(this->uid)});
}

void osn::ReplayBuffer::Stop(const Napi::CallbackInfo &info)
{
	bool force = false;
	if (info.Length() == 1)
		force = info[0].ToBoolean().Value();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call(className, "Stop", {ipc::value(this->uid), ipc::value(force)});
}

void osn::ReplayBuffer::Save(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call(className, "Save", {ipc::value(this->uid)});
}