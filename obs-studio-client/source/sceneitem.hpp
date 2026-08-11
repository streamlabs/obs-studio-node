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
class SceneItem : public Napi::ObjectWrap<osn::SceneItem> {
public:
	uint64_t itemId;

public:
	static Napi::FunctionReference constructor;
	static Napi::Object Init(Napi::Env env, Napi::Object exports);
	SceneItem(const Napi::CallbackInfo &info);

	Napi::Value GetSource(const Napi::CallbackInfo &info);
	Napi::Value GetScene(const Napi::CallbackInfo &info);
	Napi::Value Remove(const Napi::CallbackInfo &info);
	Napi::Value IsVisible(const Napi::CallbackInfo &info);
	void SetVisible(const Napi::CallbackInfo &info, const Napi::Value &value);

	Napi::Value IsSelected(const Napi::CallbackInfo &info);
	void SetSelected(const Napi::CallbackInfo &info, const Napi::Value &value);

	Napi::Value IsStreamVisible(const Napi::CallbackInfo &info);
	void SetStreamVisible(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value IsRecordingVisible(const Napi::CallbackInfo &info);
	void SetRecordingVisible(const Napi::CallbackInfo &info, const Napi::Value &value);

	Napi::Value GetPosition(const Napi::CallbackInfo &info);
	void SetPosition(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetCanvas(const Napi::CallbackInfo &info);
	void SetCanvas(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetRotation(const Napi::CallbackInfo &info);
	void SetRotation(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetScale(const Napi::CallbackInfo &info);
	void SetScale(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetScaleFilter(const Napi::CallbackInfo &info);
	void SetScaleFilter(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetAlignment(const Napi::CallbackInfo &info);
	void SetAlignment(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetBounds(const Napi::CallbackInfo &info);
	void SetBounds(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetBoundsAlignment(const Napi::CallbackInfo &info);
	void SetBoundsAlignment(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetBoundsType(const Napi::CallbackInfo &info);
	void SetBoundsType(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetCrop(const Napi::CallbackInfo &info);
	void SetCrop(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetCropToBounds(const Napi::CallbackInfo &info);
	void SetCropToBounds(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetTransformInfo(const Napi::CallbackInfo &info);
	void SetTransformInfo(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetId(const Napi::CallbackInfo &info);
	Napi::Value MoveUp(const Napi::CallbackInfo &info);
	Napi::Value MoveDown(const Napi::CallbackInfo &info);
	Napi::Value MoveTop(const Napi::CallbackInfo &info);
	Napi::Value MoveBottom(const Napi::CallbackInfo &info);
	Napi::Value Move(const Napi::CallbackInfo &info);
	Napi::Value DeferUpdateBegin(const Napi::CallbackInfo &info);
	Napi::Value DeferUpdateEnd(const Napi::CallbackInfo &info);
	Napi::Value GetBlendingMethod(const Napi::CallbackInfo &info);
	void SetBlendingMethod(const Napi::CallbackInfo &info, const Napi::Value &value);
	Napi::Value GetBlendingMode(const Napi::CallbackInfo &info);
	void SetBlendingMode(const Napi::CallbackInfo &info, const Napi::Value &value);
};
}
