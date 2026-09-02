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

	// Read-only ID used by OSN APIs that reference a registered video canvas,
	// including AutoConfig. It matches the key stored by osn::Video::Manager.
	Napi::Value GetCanvasId(const Napi::CallbackInfo &info);
};
}
