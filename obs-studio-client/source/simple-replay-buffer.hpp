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
#include "replay-buffer.hpp"

namespace osn {
class SimpleReplayBuffer : public Napi::ObjectWrap<osn::SimpleReplayBuffer>, public osn::ReplayBuffer {
public:
	static Napi::FunctionReference constructor;
	static Napi::Object Init(Napi::Env env, Napi::Object exports);
	SimpleReplayBuffer(const Napi::CallbackInfo &info);
	void Finalize(Napi::Env);
	void ReleaseObjects();
	static Napi::Value Create(const Napi::CallbackInfo &info);
	static void Destroy(const Napi::CallbackInfo &info);

	static Napi::Value GetLegacySettings(const Napi::CallbackInfo &info);
	static void SetLegacySettings(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetStreaming(const Napi::CallbackInfo &info);
	void SetStreaming(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetRecording(const Napi::CallbackInfo &info);
	void SetRecording(const Napi::CallbackInfo &info, const Napi::Value &value);
};
}