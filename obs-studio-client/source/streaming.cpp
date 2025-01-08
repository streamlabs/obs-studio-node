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

#include "streaming.hpp"
#include "utility.hpp"
#include "video-encoder.hpp"
#include "service.hpp"
#include "delay.hpp"
#include "reconnect.hpp"
#include "network.hpp"
#include "video.hpp"

Napi::Value osn::Streaming::GetService(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper(className, "GetService", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::Service::constructor.New({Napi::Number::New(info.Env(), response[1].value_union.ui64)});

	return instance;
}

void osn::Streaming::SetService(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::Service *service = Napi::ObjectWrap<osn::Service>::Unwrap(value.ToObject());

	if (!service) {
		Napi::TypeError::New(info.Env(), "Invalid service argument").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call(className, "SetService", {ipc::value(this->uid), ipc::value(service->uid)});
}

Napi::Value osn::Streaming::GetCanvas(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper(className, "GetVideoCanvas", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::Video::constructor.New({Napi::Number::New(info.Env(), response[1].value_union.ui64)});

	return instance;
}

void osn::Streaming::SetCanvas(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::Video *canvas = Napi::ObjectWrap<osn::Video>::Unwrap(value.ToObject());

	if (!canvas) {
		Napi::TypeError::New(info.Env(), "Invalid canvas argument").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call(className, "SetVideoCanvas", {ipc::value(this->uid), ipc::value(canvas->canvasId)});
}
Napi::Value osn::Streaming::GetVideoEncoder(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper(className, "GetVideoEncoder", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::VideoEncoder::constructor.New({Napi::Number::New(info.Env(), response[1].value_union.ui64)});

	return instance;
}

void osn::Streaming::SetVideoEncoder(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::VideoEncoder *encoder = Napi::ObjectWrap<osn::VideoEncoder>::Unwrap(value.ToObject());

	if (!encoder) {
		Napi::TypeError::New(info.Env(), "Invalid encoder argument").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call(className, "SetVideoEncoder", {ipc::value(this->uid), ipc::value(encoder->uid)});
}

Napi::Value osn::Streaming::GetEnforceServiceBirate(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper(className, "GetEnforceServiceBirate", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void osn::Streaming::SetEnforceServiceBirate(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper(className, "SetEnforceServiceBirate", {ipc::value(this->uid), ipc::value(value.ToBoolean().Value())});
}

Napi::Value osn::Streaming::GetEnableTwitchVOD(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper(className, "GetEnableTwitchVOD", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void osn::Streaming::SetEnableTwitchVOD(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call_synchronous_helper(className, "SetEnableTwitchVOD", {ipc::value(this->uid), ipc::value(value.ToBoolean().Value())});
}

Napi::Value osn::Streaming::GetDelay(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper(className, "GetDelay", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::Delay::constructor.New({Napi::Number::New(info.Env(), response[1].value_union.ui64)});

	return instance;
}

void osn::Streaming::SetDelay(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::Delay *delay = Napi::ObjectWrap<osn::Delay>::Unwrap(value.ToObject());

	if (!delay) {
		Napi::TypeError::New(info.Env(), "Invalid delay argument").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call(className, "SetDelay", {ipc::value(this->uid), ipc::value(delay->uid)});
}

Napi::Value osn::Streaming::GetReconnect(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper(className, "GetReconnect", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::Reconnect::constructor.New({Napi::Number::New(info.Env(), response[1].value_union.ui64)});

	return instance;
}

void osn::Streaming::SetReconnect(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::Reconnect *reconnect = Napi::ObjectWrap<osn::Reconnect>::Unwrap(value.ToObject());

	if (!reconnect) {
		Napi::TypeError::New(info.Env(), "Invalid reconnect argument").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call(className, "SetReconnect", {ipc::value(this->uid), ipc::value(reconnect->uid)});
}

Napi::Value osn::Streaming::GetNetwork(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper(className, "GetNetwork", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::Network::constructor.New({Napi::Number::New(info.Env(), response[1].value_union.ui64)});

	return instance;
}

void osn::Streaming::SetNetwork(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::Network *network = Napi::ObjectWrap<osn::Network>::Unwrap(value.ToObject());

	if (!network) {
		Napi::TypeError::New(info.Env(), "Invalid network argument").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call(className, "SetNetwork", {ipc::value(this->uid), ipc::value(network->uid)});
}

Napi::Value osn::Streaming::GetSignalHandler(const Napi::CallbackInfo &info)
{
	if (this->cb.IsEmpty())
		return info.Env().Undefined();

	return this->cb.Value();
}

void osn::Streaming::SetSignalHandler(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	Napi::Function cb = value.As<Napi::Function>();
	if (cb.IsNull() || !cb.IsFunction())
		return;

	stopWorker();

	this->cb = Napi::Persistent(cb);
	this->cb.SuppressDestruct();
}

void osn::Streaming::Start(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	startWorker(info.Env(), this->cb.Value(), className, this->uid);

	conn->call(className, "Start", {ipc::value(this->uid)});
}

void osn::Streaming::Stop(const Napi::CallbackInfo &info)
{
	bool force = false;
	if (info.Length() == 1)
		force = info[0].ToBoolean().Value();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	conn->call(className, "Stop", {ipc::value(this->uid), ipc::value(force)});
}
