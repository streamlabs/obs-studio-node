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
#include "isource.hpp"
#include "utility-v8.hpp"

namespace osn {
class Input : public Napi::ObjectWrap<osn::Input> {
public:
	uint64_t sourceId;

public:
	static Napi::FunctionReference constructor;
	static Napi::Object Init(Napi::Env env, Napi::Object exports);
	Input(const Napi::CallbackInfo &info);

	static Napi::Value Types(const Napi::CallbackInfo &info);
	static Napi::Value Create(const Napi::CallbackInfo &info);
	static Napi::Value CreatePrivate(const Napi::CallbackInfo &info);
	static Napi::Value FromName(const Napi::CallbackInfo &info);
	static Napi::Value GetPublicSources(const Napi::CallbackInfo &info);

	Napi::Value Duplicate(const Napi::CallbackInfo &info);
	Napi::Value AddFilter(const Napi::CallbackInfo &info);
	Napi::Value RemoveFilter(const Napi::CallbackInfo &info);
	Napi::Value SetFilterOrder(const Napi::CallbackInfo &info);
	Napi::Value FindFilter(const Napi::CallbackInfo &info);
	Napi::Value CopyFilters(const Napi::CallbackInfo &info);

	Napi::Value Active(const Napi::CallbackInfo &info);
	Napi::Value Showing(const Napi::CallbackInfo &info);
	Napi::Value Width(const Napi::CallbackInfo &info);
	Napi::Value Height(const Napi::CallbackInfo &info);
	Napi::Value GetVolume(const Napi::CallbackInfo &info);
	void SetVolume(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetSyncOffset(const Napi::CallbackInfo &info);
	void SetSyncOffset(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetAudioMixers(const Napi::CallbackInfo &info);
	void SetAudioMixers(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetMonitoringType(const Napi::CallbackInfo &info);
	void SetMonitoringType(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetDeinterlaceFieldOrder(const Napi::CallbackInfo &info);
	void SetDeinterlaceFieldOrder(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetDeinterlaceMode(const Napi::CallbackInfo &info);
	void SetDeinterlaceMode(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value Filters(const Napi::CallbackInfo &info);

	Napi::Value CallIsConfigurable(const Napi::CallbackInfo &info);
	Napi::Value CallGetProperties(const Napi::CallbackInfo &info);
	Napi::Value CallGetSettings(const Napi::CallbackInfo &info);
	Napi::Value CallGetSlowUncachedSettings(const Napi::CallbackInfo &info);

	Napi::Value CallGetType(const Napi::CallbackInfo &info);
	Napi::Value CallGetName(const Napi::CallbackInfo &info);
	void CallSetName(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value CallGetOutputFlags(const Napi::CallbackInfo &info);
	Napi::Value CallGetFlags(const Napi::CallbackInfo &info);
	void CallSetFlags(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value CallGetStatus(const Napi::CallbackInfo &info);
	Napi::Value CallGetId(const Napi::CallbackInfo &info);
	Napi::Value CallGetMuted(const Napi::CallbackInfo &info);
	void CallSetMuted(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value CallGetEnabled(const Napi::CallbackInfo &info);
	void CallSetEnabled(const Napi::CallbackInfo &info, const Napi::Value &value);

	Napi::Value CallRelease(const Napi::CallbackInfo &info);
	Napi::Value CallRemove(const Napi::CallbackInfo &info);
	Napi::Value CallUpdate(const Napi::CallbackInfo &info);
	Napi::Value CallSendMessage(const Napi::CallbackInfo &info);
	Napi::Value CallLoad(const Napi::CallbackInfo &info);
	Napi::Value CallSave(const Napi::CallbackInfo &info);

	Napi::Value CallSendMouseClick(const Napi::CallbackInfo &info);
	Napi::Value CallSendMouseMove(const Napi::CallbackInfo &info);
	Napi::Value CallSendMouseWheel(const Napi::CallbackInfo &info);
	Napi::Value CallSendFocus(const Napi::CallbackInfo &info);
	Napi::Value CallSendKeyClick(const Napi::CallbackInfo &info);

	Napi::Value GetDuration(const Napi::CallbackInfo &info);
	Napi::Value GetTime(const Napi::CallbackInfo &info);
	void SetTime(const Napi::CallbackInfo &info, const Napi::Value &value);
	void Play(const Napi::CallbackInfo &info);
	void Pause(const Napi::CallbackInfo &info);
	void Restart(const Napi::CallbackInfo &info);
	void Stop(const Napi::CallbackInfo &info);
	Napi::Value GetMediaState(const Napi::CallbackInfo &info);

	Napi::Value CallCallHandler(const Napi::CallbackInfo &info);
};
}
