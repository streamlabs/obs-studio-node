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

#pragma once
#include <napi.h>
#include "worker-signals.hpp"

namespace osn {
class Streaming : public WorkerSignals {
public:
	uint64_t uid;
	Streaming() : WorkerSignals() { uid = 0; };

protected:
	Napi::Function signalHandler;
	std::string className;

	Napi::Reference<Napi::Object> videoEncoderRef;
	Napi::Reference<Napi::Object> audioEncoderRef;
	Napi::Reference<Napi::Object> serviceRef;
	Napi::Reference<Napi::Object> delayRef;
	Napi::Reference<Napi::Object> reconnectRef;
	Napi::Reference<Napi::Object> networkRef;
	void ReleaseObjects();

	Napi::Value GetService(const Napi::CallbackInfo &info);
	void SetService(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetCanvas(const Napi::CallbackInfo &info);
	void SetCanvas(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetVideoEncoder(const Napi::CallbackInfo &info);
	void SetVideoEncoder(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetEnforceServiceBirate(const Napi::CallbackInfo &info);
	void SetEnforceServiceBirate(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetEnableTwitchVOD(const Napi::CallbackInfo &info);
	void SetEnableTwitchVOD(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetDelay(const Napi::CallbackInfo &info);
	void SetDelay(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetReconnect(const Napi::CallbackInfo &info);
	void SetReconnect(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetNetwork(const Napi::CallbackInfo &info);
	void SetNetwork(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetSignalHandler(const Napi::CallbackInfo &info);
	void SetSignalHandler(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetDroppedFrames(const Napi::CallbackInfo &info);
	Napi::Value GetTotalFrames(const Napi::CallbackInfo &info);
	Napi::Value GetKBitsPerSec(const Napi::CallbackInfo &info);
	Napi::Value GetDataOutput(const Napi::CallbackInfo &info);

	Napi::Value GetAvailableEncoders(const Napi::CallbackInfo &info);
	void Start(const Napi::CallbackInfo &info);
	void Stop(const Napi::CallbackInfo &info);
};
}