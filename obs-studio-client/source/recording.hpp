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
#include "file-output.hpp"

namespace osn {
class Recording : public WorkerSignals, public FileOutput {
public:
	Recording() : WorkerSignals(), FileOutput(){};

protected:
	Napi::Function signalHandler;
	std::string className;
	Napi::Reference<Napi::Object> videoEncoderRef;
	Napi::Reference<Napi::Object> streamingRef;

	Napi::Value GetVideoEncoder(const Napi::CallbackInfo &info);
	void SetVideoEncoder(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetSignalHandler(const Napi::CallbackInfo &info);
	void SetSignalHandler(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetEnableFileSplit(const Napi::CallbackInfo &info);
	void SetEnableFileSplit(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetSplitType(const Napi::CallbackInfo &info);
	void SetSplitType(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetSplitTime(const Napi::CallbackInfo &info);
	void SetSplitTime(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetSplitSize(const Napi::CallbackInfo &info);
	void SetSplitSize(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetFileResetTimestamps(const Napi::CallbackInfo &info);
	void SetFileResetTimestamps(const Napi::CallbackInfo &info, const Napi::Value &value);

	void Start(const Napi::CallbackInfo &info);
	void Stop(const Napi::CallbackInfo &info);
	void SplitFile(const Napi::CallbackInfo &info);
};
}