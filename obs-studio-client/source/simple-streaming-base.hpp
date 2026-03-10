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
#include "streaming.hpp"

namespace osn {

class SimpleStreamingBase : public osn::Streaming {
public:
	Napi::Value GetAudioEncoder(const Napi::CallbackInfo &info);
	void SetAudioEncoder(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetUseAdvanced(const Napi::CallbackInfo &info);
	void SetUseAdvanced(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetCustomEncSettings(const Napi::CallbackInfo &info);
	void SetCustomEncSettings(const Napi::CallbackInfo &info, const Napi::Value &value);
};

}