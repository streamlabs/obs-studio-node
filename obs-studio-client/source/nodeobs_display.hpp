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

#include <napi.h>

namespace display {
void Init(Napi::Env env, Napi::Object exports);

Napi::Value OBS_content_setDayTheme(const Napi::CallbackInfo &info);
Napi::Value OBS_content_createDisplay(const Napi::CallbackInfo &info);
Napi::Value OBS_content_destroyDisplay(const Napi::CallbackInfo &info);
Napi::Value OBS_content_getDisplayPreviewOffset(const Napi::CallbackInfo &info);
Napi::Value OBS_content_getDisplayPreviewSize(const Napi::CallbackInfo &info);
Napi::Value OBS_content_createSourcePreviewDisplay(const Napi::CallbackInfo &info);
Napi::Value OBS_content_resizeDisplay(const Napi::CallbackInfo &info);
Napi::Value OBS_content_moveDisplay(const Napi::CallbackInfo &info);
Napi::Value OBS_content_setPaddingSize(const Napi::CallbackInfo &info);
Napi::Value OBS_content_setPaddingColor(const Napi::CallbackInfo &info);
Napi::Value OBS_content_setOutlineColor(const Napi::CallbackInfo &info);
Napi::Value OBS_content_setCropOutlineColor(const Napi::CallbackInfo &info);
Napi::Value OBS_content_setShouldDrawUI(const Napi::CallbackInfo &info);
Napi::Value OBS_content_setDrawGuideLines(const Napi::CallbackInfo &info);
Napi::Value OBS_content_setDrawRotationHandle(const Napi::CallbackInfo &info);
Napi::Value OBS_content_createIOSurface(const Napi::CallbackInfo &info);
Napi::Value OBS_content_takeScreenshot(const Napi::CallbackInfo &info);
}
