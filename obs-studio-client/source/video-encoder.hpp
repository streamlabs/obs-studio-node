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
#include <thread>

namespace osn {
class VideoEncoder : public Napi::ObjectWrap<osn::VideoEncoder> {
public:
	uint64_t uid;
	bool has_encoder;

public:
	static Napi::FunctionReference constructor;
	static Napi::Object Init(Napi::Env env, Napi::Object exports);
	VideoEncoder(const Napi::CallbackInfo &info);

	static Napi::Value Create(const Napi::CallbackInfo &info);
	void Finalize(Napi::Env env);
	static Napi::Value GetTypes(const Napi::CallbackInfo &info);

	Napi::Value GetName(const Napi::CallbackInfo &info);
	void SetName(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetType(const Napi::CallbackInfo &info);
	Napi::Value GetActive(const Napi::CallbackInfo &info);
	Napi::Value GetId(const Napi::CallbackInfo &info);
	Napi::Value GetLastError(const Napi::CallbackInfo &info);

	void Release(const Napi::CallbackInfo &info);
	void Update(const Napi::CallbackInfo &info);
	Napi::Value GetProperties(const Napi::CallbackInfo &info);
	Napi::Value GetSettings(const Napi::CallbackInfo &info);
};
}