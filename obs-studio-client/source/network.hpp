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

namespace osn {
class Network : public Napi::ObjectWrap<osn::Network> {
public:
	uint64_t uid;
	uint64_t connectionEpoch;

public:
	static Napi::FunctionReference constructor;
	static Napi::Object Init(Napi::Env env, Napi::Object exports);
	Network(const Napi::CallbackInfo &info);

	static Napi::Value Create(const Napi::CallbackInfo &info);
	void Finalize(Napi::Env env);
	Napi::Value GetBindIP(const Napi::CallbackInfo &info);
	void SetBindIP(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetNetworkInterfaces(const Napi::CallbackInfo &info);
	Napi::Value GetEnableDynamicBitrate(const Napi::CallbackInfo &info);
	void SetEnableDynamicBitrate(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetEnableOptimizations(const Napi::CallbackInfo &info);
	void SetEnableOptimizations(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetEnableLowLatency(const Napi::CallbackInfo &info);
	void SetEnableLowLatency(const Napi::CallbackInfo &info, const Napi::Value &value);
};
}
