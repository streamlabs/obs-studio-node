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

#include "nodeobs_display.hpp"
#include "controller.hpp"
#include "osn-error.hpp"
#include "utility-v8.hpp"

#pragma warning(push, 0)
#include <node.h>
#pragma warning(pop)
//#include <node.h>
#include <sstream>
#include <string>
#include "shared.hpp"
#include "utility.hpp"
#include "callback-manager.hpp"
#include "video.hpp"

#ifdef WIN32
static BOOL CALLBACK EnumChromeWindowsProc(HWND hwnd, LPARAM lParam)
{
	char buf[256];
	if (GetClassNameA(hwnd, buf, sizeof(buf) / sizeof(*buf))) {
		if (strstr(buf, "Intermediate D3D Window")) {
			LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
			if ((style & WS_CLIPSIBLINGS) == 0) {
				style |= WS_CLIPSIBLINGS;
				SetWindowLongPtr(hwnd, GWL_STYLE, style);
			}
		}
	}
	return TRUE;
}

static void FixChromeD3DIssue(HWND chromeWindow)
{
	(void)EnumChildWindows(chromeWindow, EnumChromeWindowsProc, (LPARAM)NULL);

	LONG_PTR style = GetWindowLongPtr(chromeWindow, GWL_STYLE);
	if ((style & WS_CLIPCHILDREN) == 0) {
		style |= WS_CLIPCHILDREN;
		SetWindowLongPtr(chromeWindow, GWL_STYLE, style);
	}
}
#endif

Napi::Value display::OBS_content_setDayTheme(const Napi::CallbackInfo &info)
{
	bool dayTheme = info[0].ToBoolean().Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("Display", "OBS_content_setDayTheme", {ipc::value(dayTheme)});
	return info.Env().Undefined();
}

Napi::Value display::OBS_content_createDisplay(const Napi::CallbackInfo &info)
{
	Napi::Buffer<void *> bufferData = info[0].As<Napi::Buffer<void *>>();
	uint64_t *windowHandle = static_cast<uint64_t *>(*reinterpret_cast<void **>(bufferData.Data()));

#ifdef WIN32
	FixChromeD3DIssue((HWND)windowHandle);
#endif

	std::string key = info[1].ToString().Utf8Value();
	int32_t mode = info[2].ToNumber().Int32Value();

	bool renderAtBottom = (info.Length() > 3) ? info[3].ToBoolean().Value() : false;

	uint64_t canvasId = osn::Video::nonCavasId;
	if (info.Length() > 4) {
		osn::Video *video = Napi::ObjectWrap<osn::Video>::Unwrap(info[4].ToObject());
		if (video)
			canvasId = video->canvasId;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("Display", "OBS_content_createDisplay",
		   {ipc::value((uint64_t)windowHandle), ipc::value(key), ipc::value(mode), ipc::value(renderAtBottom), ipc::value(canvasId)});

	return info.Env().Undefined();
}

Napi::Value display::OBS_content_destroyDisplay(const Napi::CallbackInfo &info)
{
	std::string key = info[0].ToString().Utf8Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Display", "OBS_content_destroyDisplay", {ipc::value(key)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return info.Env().Undefined();
}

Napi::Value display::OBS_content_getDisplayPreviewOffset(const Napi::CallbackInfo &info)
{
	std::string key = info[0].ToString().Utf8Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Display", "OBS_content_getDisplayPreviewOffset", {ipc::value(key)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	Napi::Object previewOffset = Napi::Object::New(info.Env());
	previewOffset.Set("x", Napi::Number::New(info.Env(), response[1].value_union.i32));
	previewOffset.Set("y", Napi::Number::New(info.Env(), response[2].value_union.i32));
	return previewOffset;
}

Napi::Value display::OBS_content_getDisplayPreviewSize(const Napi::CallbackInfo &info)
{
	std::string key = info[0].ToString().Utf8Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Display", "OBS_content_getDisplayPreviewSize", {ipc::value(key)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	Napi::Object previewSize = Napi::Object::New(info.Env());
	previewSize.Set("width", Napi::Number::New(info.Env(), response[1].value_union.i32));
	previewSize.Set("height", Napi::Number::New(info.Env(), response[2].value_union.i32));
	return previewSize;
}

Napi::Value display::OBS_content_createSourcePreviewDisplay(const Napi::CallbackInfo &info)
{
	Napi::Buffer<void *> bufferData = info[0].As<Napi::Buffer<void *>>();
	uint64_t *windowHandle = static_cast<uint64_t *>(*reinterpret_cast<void **>(bufferData.Data()));

#ifdef WIN32
	FixChromeD3DIssue((HWND)windowHandle);
#endif

	std::string sourceName = info[1].ToString().Utf8Value();
	std::string key = info[2].ToString().Utf8Value();
	bool renderAtBottom = (info.Length() > 3) ? info[3].ToBoolean().Value() : false;

	uint64_t canvasId = osn::Video::nonCavasId;
	if (info.Length() > 4) {
		osn::Video *video = Napi::ObjectWrap<osn::Video>::Unwrap(info[4].ToObject());
		if (video)
			canvasId = video->canvasId;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("Display", "OBS_content_createSourcePreviewDisplay",
		   {ipc::value((uint64_t)windowHandle), ipc::value(sourceName), ipc::value(key), ipc::value(renderAtBottom), ipc::value(canvasId)});

	return info.Env().Undefined();
}

Napi::Value display::OBS_content_resizeDisplay(const Napi::CallbackInfo &info)
{
	std::string key = info[0].ToString().Utf8Value();
	uint32_t width = info[1].ToNumber().Uint32Value();
	uint32_t height = info[2].ToNumber().Uint32Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("Display", "OBS_content_resizeDisplay", {ipc::value(key), ipc::value(width), ipc::value(height)});

	return info.Env().Undefined();
}

Napi::Value display::OBS_content_moveDisplay(const Napi::CallbackInfo &info)
{
	std::string key = info[0].ToString().Utf8Value();
	uint32_t x = info[1].ToNumber().Uint32Value();
	uint32_t y = info[2].ToNumber().Uint32Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("Display", "OBS_content_moveDisplay", {ipc::value(key), ipc::value(x), ipc::value(y)});
	return info.Env().Undefined();
}

Napi::Value display::OBS_content_setPaddingSize(const Napi::CallbackInfo &info)
{
	std::string key = info[0].ToString().Utf8Value();
	uint32_t paddingSize = info[1].ToNumber().Uint32Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("Display", "OBS_content_setPaddingSize", {ipc::value(key), ipc::value(paddingSize)});
	return info.Env().Undefined();
}

Napi::Value display::OBS_content_setPaddingColor(const Napi::CallbackInfo &info)
{
	std::string key = info[0].ToString().Utf8Value();
	uint32_t r = info[1].ToNumber().Uint32Value();
	uint32_t g = info[2].ToNumber().Uint32Value();
	uint32_t b = info[3].ToNumber().Uint32Value();
	uint32_t a = 255;

	if (info.Length() > 4)
		a = info[4].ToNumber().Uint32Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("Display", "OBS_content_setPaddingColor", {ipc::value(key), ipc::value(r), ipc::value(g), ipc::value(b), ipc::value(a)});
	return info.Env().Undefined();
}

Napi::Value display::OBS_content_setOutlineColor(const Napi::CallbackInfo &info)
{
	std::string key = info[0].ToString().Utf8Value();
	uint32_t r = info[1].ToNumber().Uint32Value();
	uint32_t g = info[2].ToNumber().Uint32Value();
	uint32_t b = info[3].ToNumber().Uint32Value();
	uint32_t a = 255;

	if (info.Length() > 4)
		a = info[4].ToNumber().Uint32Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("Display", "OBS_content_setOutlineColor", {ipc::value(key), ipc::value(r), ipc::value(g), ipc::value(b), ipc::value(a)});
	return info.Env().Undefined();
}

Napi::Value display::OBS_content_setCropOutlineColor(const Napi::CallbackInfo &info)
{
	std::string key = info[0].ToString().Utf8Value();
	uint32_t r = info[1].ToNumber().Uint32Value();
	uint32_t g = info[2].ToNumber().Uint32Value();
	uint32_t b = info[3].ToNumber().Uint32Value();
	uint32_t a = 255;

	if (info.Length() > 4)
		a = info[4].ToNumber().Uint32Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("Display", "OBS_content_setCropOutlineColor", {ipc::value(key), ipc::value(r), ipc::value(g), ipc::value(b), ipc::value(a)});
	return info.Env().Undefined();
}

Napi::Value display::OBS_content_setShouldDrawUI(const Napi::CallbackInfo &info)
{
	std::string key = info[0].ToString().Utf8Value();
	bool drawUI = info[1].ToBoolean().Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("Display", "OBS_content_setShouldDrawUI", {ipc::value(key), ipc::value(drawUI)});
	return info.Env().Undefined();
}

Napi::Value display::OBS_content_setDrawGuideLines(const Napi::CallbackInfo &info)
{
	std::string key = info[0].ToString().Utf8Value();
	bool drawGuideLines = info[1].ToBoolean().Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("Display", "OBS_content_setDrawGuideLines", {ipc::value(key), ipc::value(drawGuideLines)});
	return info.Env().Undefined();
}

Napi::Value display::OBS_content_setDrawRotationHandle(const Napi::CallbackInfo &info)
{
	std::string key = info[0].ToString().Utf8Value();
	bool drawRotationHandle = info[1].ToBoolean().Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("Display", "OBS_content_setDrawRotationHandle", {ipc::value(key), ipc::value(drawRotationHandle)});
	return info.Env().Undefined();
}

Napi::Value display::OBS_content_createIOSurface(const Napi::CallbackInfo &info)
{
	std::string key = info[0].ToString().Utf8Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Display", "OBS_content_createIOSurface", {ipc::value(key)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

void display::Init(Napi::Env env, Napi::Object exports)
{
	exports.Set(Napi::String::New(env, "OBS_content_setDayTheme"), Napi::Function::New(env, display::OBS_content_setDayTheme));
	exports.Set(Napi::String::New(env, "OBS_content_createDisplay"), Napi::Function::New(env, display::OBS_content_createDisplay));
	exports.Set(Napi::String::New(env, "OBS_content_destroyDisplay"), Napi::Function::New(env, display::OBS_content_destroyDisplay));
	exports.Set(Napi::String::New(env, "OBS_content_getDisplayPreviewOffset"), Napi::Function::New(env, display::OBS_content_getDisplayPreviewOffset));
	exports.Set(Napi::String::New(env, "OBS_content_getDisplayPreviewSize"), Napi::Function::New(env, display::OBS_content_getDisplayPreviewSize));
	exports.Set(Napi::String::New(env, "OBS_content_createSourcePreviewDisplay"),
		    Napi::Function::New(env, display::OBS_content_createSourcePreviewDisplay));
	exports.Set(Napi::String::New(env, "OBS_content_resizeDisplay"), Napi::Function::New(env, display::OBS_content_resizeDisplay));
	exports.Set(Napi::String::New(env, "OBS_content_moveDisplay"), Napi::Function::New(env, display::OBS_content_moveDisplay));
	exports.Set(Napi::String::New(env, "OBS_content_setPaddingSize"), Napi::Function::New(env, display::OBS_content_setPaddingSize));
	exports.Set(Napi::String::New(env, "OBS_content_setPaddingColor"), Napi::Function::New(env, display::OBS_content_setPaddingColor));
	exports.Set(Napi::String::New(env, "OBS_content_setCropOutlineColor"), Napi::Function::New(env, display::OBS_content_setCropOutlineColor));
	exports.Set(Napi::String::New(env, "OBS_content_setShouldDrawUI"), Napi::Function::New(env, display::OBS_content_setShouldDrawUI));
	exports.Set(Napi::String::New(env, "OBS_content_setDrawGuideLines"), Napi::Function::New(env, display::OBS_content_setDrawGuideLines));
	exports.Set(Napi::String::New(env, "OBS_content_setDrawRotationHandle"), Napi::Function::New(env, display::OBS_content_setDrawRotationHandle));
	exports.Set(Napi::String::New(env, "OBS_content_createIOSurface"), Napi::Function::New(env, display::OBS_content_createIOSurface));
}