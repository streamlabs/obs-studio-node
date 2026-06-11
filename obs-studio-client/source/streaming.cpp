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

void osn::Streaming::ReleaseObjects()
{
	if (!videoEncoderRef.IsEmpty())
		videoEncoderRef.Reset();

	if (!serviceRef.IsEmpty())
		serviceRef.Reset();

	if (!delayRef.IsEmpty())
		delayRef.Reset();

	if (!reconnectRef.IsEmpty())
		reconnectRef.Reset();

	if (!networkRef.IsEmpty())
		networkRef.Reset();

	if (!audioEncoderRef.IsEmpty())
		audioEncoderRef.Reset();
}

Napi::Value osn::Streaming::GetService(const Napi::CallbackInfo &info)
{
	return serviceRef.IsEmpty() ? info.Env().Undefined() : serviceRef.Value();
}

void osn::Streaming::SetService(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	if (value.IsNull() || value.IsUndefined()) {
		if (!serviceRef.IsEmpty())
			serviceRef.Reset();
		auto response = conn->call_synchronous_helper(className, "SetService", {ipc::value(this->uid), ipc::value(UINT64_MAX)});
		ValidateResponse(info, response);
		return;
	}

	Napi::Object obj = value.As<Napi::Object>();
	if (!obj.InstanceOf(osn::Service::constructor.Value()))
		Napi::TypeError::New(info.Env(), "Object is not a Service").ThrowAsJavaScriptException();

	osn::Service *service = Napi::ObjectWrap<osn::Service>::Unwrap(obj);
	if (!service) {
		Napi::TypeError::New(info.Env(), "Invalid service argument").ThrowAsJavaScriptException();
		return;
	}

	auto response = conn->call_synchronous_helper(className, "SetService", {ipc::value(this->uid), ipc::value(service->uid)});
	if (!ValidateResponse(info, response))
		return;

	if (!serviceRef.IsEmpty())
		serviceRef.Reset();
	serviceRef = Napi::Persistent(obj);
}

Napi::Value osn::Streaming::GetCanvas(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper(className, "GetVideoCanvas", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::Video::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

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

	auto response = conn->call_synchronous_helper(className, "SetVideoCanvas", {ipc::value(this->uid), ipc::value(canvas->canvasId)});
	ValidateResponse(info, response);
}

Napi::Value osn::Streaming::GetVideoEncoder(const Napi::CallbackInfo &info)
{
	return videoEncoderRef.IsEmpty() ? info.Env().Undefined() : videoEncoderRef.Value();
}

void osn::Streaming::SetVideoEncoder(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	if (value.IsNull() || value.IsUndefined()) {
		if (!videoEncoderRef.IsEmpty())
			videoEncoderRef.Reset();
		auto response = conn->call_synchronous_helper(className, "SetVideoEncoder", {ipc::value(this->uid), ipc::value(UINT64_MAX)});
		ValidateResponse(info, response);
		return;
	}

	Napi::Object obj = value.As<Napi::Object>();
	if (!obj.InstanceOf(osn::VideoEncoder::constructor.Value()))
		Napi::TypeError::New(info.Env(), "Object is not a VideoEncoder").ThrowAsJavaScriptException();

	osn::VideoEncoder *encoder = Napi::ObjectWrap<osn::VideoEncoder>::Unwrap(value.ToObject());

	if (!encoder) {
		Napi::TypeError::New(info.Env(), "Invalid encoder argument").ThrowAsJavaScriptException();
		return;
	}

	auto response = conn->call_synchronous_helper(className, "SetVideoEncoder", {ipc::value(this->uid), ipc::value(encoder->uid)});
	if (!ValidateResponse(info, response))
		return;

	if (!videoEncoderRef.IsEmpty())
		videoEncoderRef.Reset();
	videoEncoderRef = Napi::Persistent(obj);
}

Napi::Value osn::Streaming::GetEnforceServiceBirate(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper(className, "GetEnforceServiceBirate", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void osn::Streaming::SetEnforceServiceBirate(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper(className, "SetEnforceServiceBirate", {ipc::value(this->uid), ipc::value(value.ToBoolean().Value())});
	ValidateResponse(info, response);
}

Napi::Value osn::Streaming::GetEnableTwitchVOD(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper(className, "GetEnableTwitchVOD", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Boolean::New(info.Env(), response[1].value_union.ui32);
}

void osn::Streaming::SetEnableTwitchVOD(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper(className, "SetEnableTwitchVOD", {ipc::value(this->uid), ipc::value(value.ToBoolean().Value())});
	ValidateResponse(info, response);
}

Napi::Value osn::Streaming::GetDelay(const Napi::CallbackInfo &info)
{
	return delayRef.IsEmpty() ? info.Env().Undefined() : delayRef.Value();
}

void osn::Streaming::SetDelay(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	if (value.IsNull() || value.IsUndefined()) {
		if (!delayRef.IsEmpty())
			delayRef.Reset();
		auto response = conn->call_synchronous_helper(className, "SetDelay", {ipc::value(this->uid), ipc::value(UINT64_MAX)});
		ValidateResponse(info, response);
		return;
	}

	Napi::Object obj = value.As<Napi::Object>();
	if (!obj.InstanceOf(osn::Delay::constructor.Value()))
		Napi::TypeError::New(info.Env(), "Object is not a Delay").ThrowAsJavaScriptException();

	osn::Delay *delay = Napi::ObjectWrap<osn::Delay>::Unwrap(obj);
	if (!delay) {
		Napi::TypeError::New(info.Env(), "Invalid delay argument").ThrowAsJavaScriptException();
		return;
	}

	auto response = conn->call_synchronous_helper(className, "SetDelay", {ipc::value(this->uid), ipc::value(delay->uid)});
	if (!ValidateResponse(info, response))
		return;

	if (!delayRef.IsEmpty())
		delayRef.Reset();
	delayRef = Napi::Persistent(obj);
}

Napi::Value osn::Streaming::GetReconnect(const Napi::CallbackInfo &info)
{
	return reconnectRef.IsEmpty() ? info.Env().Undefined() : reconnectRef.Value();
}

void osn::Streaming::SetReconnect(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	if (value.IsNull() || value.IsUndefined()) {
		if (!reconnectRef.IsEmpty())
			reconnectRef.Reset();
		auto response = conn->call_synchronous_helper(className, "SetReconnect", {ipc::value(this->uid), ipc::value(UINT64_MAX)});
		ValidateResponse(info, response);
		return;
	}

	Napi::Object obj = value.As<Napi::Object>();
	if (!obj.InstanceOf(osn::Reconnect::constructor.Value()))
		Napi::TypeError::New(info.Env(), "Object is not a Reconnect").ThrowAsJavaScriptException();

	osn::Reconnect *reconnect = Napi::ObjectWrap<osn::Reconnect>::Unwrap(obj);
	if (!reconnect) {
		Napi::TypeError::New(info.Env(), "Invalid reconnect argument").ThrowAsJavaScriptException();
		return;
	}

	auto response = conn->call_synchronous_helper(className, "SetReconnect", {ipc::value(this->uid), ipc::value(reconnect->uid)});
	if (!ValidateResponse(info, response))
		return;

	if (!reconnectRef.IsEmpty())
		reconnectRef.Reset();
	reconnectRef = Napi::Persistent(obj);
}

Napi::Value osn::Streaming::GetNetwork(const Napi::CallbackInfo &info)
{
	return networkRef.IsEmpty() ? info.Env().Undefined() : networkRef.Value();
}

void osn::Streaming::SetNetwork(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	if (value.IsNull() || value.IsUndefined()) {
		if (!networkRef.IsEmpty())
			networkRef.Reset();
		auto response = conn->call_synchronous_helper(className, "SetNetwork", {ipc::value(this->uid), ipc::value(UINT64_MAX)});
		ValidateResponse(info, response);
		return;
	}

	Napi::Object obj = value.As<Napi::Object>();
	if (!obj.InstanceOf(osn::Network::constructor.Value()))
		Napi::TypeError::New(info.Env(), "Object is not a Network").ThrowAsJavaScriptException();

	osn::Network *network = Napi::ObjectWrap<osn::Network>::Unwrap(obj);
	if (!network) {
		Napi::TypeError::New(info.Env(), "Invalid network argument").ThrowAsJavaScriptException();
		return;
	}

	auto response = conn->call_synchronous_helper(className, "SetNetwork", {ipc::value(this->uid), ipc::value(network->uid)});
	if (!ValidateResponse(info, response))
		return;

	if (!networkRef.IsEmpty())
		networkRef.Reset();
	networkRef = Napi::Persistent(obj);
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

Napi::Value osn::Streaming::GetAvailableEncoders(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper(className, "GetAvailableEncoders", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	Napi::Array arr = Napi::Array::New(info.Env());
	uint32_t idx = 0;
	for (size_t i = 1; i + 7 < response.size(); i += 8) {
		Napi::Object obj = Napi::Object::New(info.Env());
		obj.Set("title", Napi::String::New(info.Env(), response[i].value_str));
		obj.Set("name", Napi::String::New(info.Env(), response[i + 1].value_str));
		obj.Set("id", Napi::String::New(info.Env(), response[i + 2].value_str));
		obj.Set("family", Napi::String::New(info.Env(), response[i + 3].value_str));
		obj.Set("preset", Napi::String::New(info.Env(), response[i + 4].value_str));
		obj.Set("codec", Napi::String::New(info.Env(), response[i + 5].value_str));
		obj.Set("streaming", Napi::Boolean::New(info.Env(), response[i + 6].value_union.ui32 != 0));
		obj.Set("recording", Napi::Boolean::New(info.Env(), response[i + 7].value_union.ui32 != 0));
		arr.Set(idx++, obj);
	}

	return arr;
}

void osn::Streaming::Start(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	startWorker(info.Env(), this->cb.Value(), className, this->uid);

	auto response = conn->call_synchronous_helper(className, "Start", {ipc::value(this->uid)});
	ValidateResponse(info, response);
}

void osn::Streaming::Stop(const Napi::CallbackInfo &info)
{
	bool force = false;
	if (info.Length() == 1)
		force = info[0].ToBoolean().Value();

	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper(className, "Stop", {ipc::value(this->uid), ipc::value(force)});
	ValidateResponse(info, response);
}

Napi::Value osn::Streaming::GetDroppedFrames(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper(className, "GetDroppedFrames", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

Napi::Value osn::Streaming::GetTotalFrames(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper(className, "GetTotalFrames", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

Napi::Value osn::Streaming::GetKBitsPerSec(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper(className, "GetKBitsPerSec", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.fp64);
}

Napi::Value osn::Streaming::GetDataOutput(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper(className, "GetDataOutput", {ipc::value(this->uid)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.fp64);
}
