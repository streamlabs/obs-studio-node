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

namespace osn {
class Global : public Napi::ObjectWrap<osn::Global> {
public:
	static Napi::FunctionReference constructor;
	static Napi::Object Init(Napi::Env env, Napi::Object exports);
	Global(const Napi::CallbackInfo &info);

	static Napi::Value getOutputSource(const Napi::CallbackInfo &info);
	static Napi::Value setOutputSource(const Napi::CallbackInfo &info);
	static Napi::Value addSceneToBackstage(const Napi::CallbackInfo &info);
	static Napi::Value removeSceneFromBackstage(const Napi::CallbackInfo &info);
	static Napi::Value getOutputFlagsFromId(const Napi::CallbackInfo &info);
	static Napi::Value GetLaggedFrames(const Napi::CallbackInfo &info);
	static Napi::Value GetTotalFrames(const Napi::CallbackInfo &info);
	static Napi::Value getLocale(const Napi::CallbackInfo &info);
	static void setLocale(const Napi::CallbackInfo &info, const Napi::Value &value);
	static Napi::Value getMultipleRendering(const Napi::CallbackInfo &info);
	static void setMultipleRendering(const Napi::CallbackInfo &info, const Napi::Value &value);
	static Napi::Value GetCPUPercentage(const Napi::CallbackInfo &info);
	static Napi::Value GetCurrentFrameRate(const Napi::CallbackInfo &info);
	static Napi::Value GetAverageTimeToRenderFrame(const Napi::CallbackInfo &info);
	static Napi::Value GetDiskSpaceAvailable(const Napi::CallbackInfo &info);
	static Napi::Value GetMemoryUsage(const Napi::CallbackInfo &info);
};
}
