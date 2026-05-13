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

#pragma once
#include <napi.h>
#include "utility-v8.hpp"

namespace osn {
class Video : public Napi::ObjectWrap<osn::Video> {
public:
	uint64_t canvasId = 0;
	constexpr static uint64_t nonCavasId = std::numeric_limits<uint64_t>::max();

	static Napi::FunctionReference constructor;
	static Napi::Object Init(Napi::Env env, Napi::Object exports);
	Video(const Napi::CallbackInfo &info);

	Napi::Value GetSkippedFrames(const Napi::CallbackInfo &info);
	Napi::Value GetEncodedFrames(const Napi::CallbackInfo &info);

	static Napi::Value Create(const Napi::CallbackInfo &info);
	void Destroy(const Napi::CallbackInfo &info);

	Napi::Value get(const Napi::CallbackInfo &info);
	void set(const Napi::CallbackInfo &info, const Napi::Value &value);

	Napi::Value GetLegacySettings(const Napi::CallbackInfo &info);
	void SetLegacySettings(const Napi::CallbackInfo &info, const Napi::Value &value);

	// Read-only accessor exposing the server-side canvas id (the same value the
	// server's osn::Video::Manager keys this object by). Required so the frontend
	// can refer to a canvas in APIs like autoconfig that take ids.
	Napi::Value GetCanvasId(const Napi::CallbackInfo &info);
};
}
