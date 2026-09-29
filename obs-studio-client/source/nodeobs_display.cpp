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
#include "osn-common.hpp"
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

#include <chrono>
#include <cstring>
#include <memory>
#include <thread>

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

	uint64_t canvasId = osn::common::INVALID_ID;
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

	uint64_t canvasId = osn::common::INVALID_ID;
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

	if (!ValidateResponse(info, response)) {
		Napi::Error::New(info.Env(), response[1].value_str).ThrowAsJavaScriptException();
		return info.Env().Undefined();
	}

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

namespace {

// Extracts an IPC error without throwing, since a Submit or poll failure must
// reject the returned promise rather than throw synchronously.
bool ExtractCallError(const std::vector<ipc::value> &response, std::string &error)
{
	if (response.empty()) {
		error = "Failed to make IPC call, verify IPC status.";
		return false;
	}
	if (response.size() == 1 && response[0].type == ipc::type::Null) {
		error = response[0].value_str;
		return false;
	}
	if ((ErrorCode)response[0].value_union.ui64 != ErrorCode::Ok) {
		error = response.size() > 1 ? response[1].value_str : "Unknown screenshot error.";
		return false;
	}
	return true;
}

Napi::Object ScreenshotResultToObject(Napi::Env env, const std::string &path, uint32_t width, uint32_t height)
{
	Napi::Object object = Napi::Object::New(env);
	object.Set("path", Napi::String::New(env, path));
	object.Set("width", Napi::Number::New(env, width));
	object.Set("height", Napi::Number::New(env, height));
	return object;
}

// Mirrors ScreenshotManager::State on the server; the wire value is just the enum ordinal.
enum class ScreenshotJobState : uint32_t { Pending = 0, Done = 1, Failed = 2 };

// Polls OBS_content_getScreenshotResult instead of blocking the IPC thread on
// the server's own tick-driven capture. One worker waits for every job in a
// batch, so a single canvas failure rejects the whole call.
class ScreenshotWaitWorker : public Napi::AsyncWorker {
public:
	ScreenshotWaitWorker(Napi::Env env, Napi::Promise::Deferred deferred, std::vector<uint64_t> jobIds, std::vector<uint64_t> canvasIds, bool isArrayCall)
		: Napi::AsyncWorker(env),
		  deferred(deferred),
		  jobIds(std::move(jobIds)),
		  canvasIds(std::move(canvasIds)),
		  isArrayCall(isArrayCall),
		  results(this->jobIds.size())
	{
	}

	void Execute() override
	{
		auto conn = Controller::GetInstance().GetConnection();
		if (!conn) {
			SetError("Lost IPC connection while waiting for a screenshot.");
			return;
		}

		std::vector<bool> done(jobIds.size(), false);
		size_t remaining = jobIds.size();

		while (remaining > 0) {
			for (size_t i = 0; i < jobIds.size(); ++i) {
				if (done[i])
					continue;

				std::vector<ipc::value> response =
					conn->call_synchronous_helper("Display", "OBS_content_getScreenshotResult", {ipc::value(jobIds[i])});

				std::string error;
				if (!ExtractCallError(response, error)) {
					SetError("Screenshot failed for canvas " + std::to_string(canvasIds[i]) + ": " + error);
					return;
				}

				const auto state = ScreenshotJobState(response[1].value_union.ui32);
				if (state == ScreenshotJobState::Pending)
					continue;

				if (state == ScreenshotJobState::Failed) {
					SetError("Screenshot failed for canvas " + std::to_string(canvasIds[i]) + ": " + response[5].value_str);
					return;
				}

				results[i].path = response[2].value_str;
				results[i].width = response[3].value_union.ui32;
				results[i].height = response[4].value_union.ui32;
				done[i] = true;
				--remaining;
			}

			if (remaining > 0)
				std::this_thread::sleep_for(std::chrono::milliseconds(16));
		}
	}

	void OnOK() override
	{
		Napi::Env env = Env();
		if (!isArrayCall) {
			deferred.Resolve(ScreenshotResultToObject(env, results[0].path, results[0].width, results[0].height));
			return;
		}

		Napi::Array array = Napi::Array::New(env, results.size());
		for (uint32_t i = 0; i < results.size(); ++i)
			array.Set(i, ScreenshotResultToObject(env, results[i].path, results[i].width, results[i].height));
		deferred.Resolve(array);
	}

	void OnError(const Napi::Error &error) override { deferred.Reject(Napi::Error::New(Env(), error.Message()).Value()); }

private:
	struct Result {
		std::string path;
		uint32_t width = 0;
		uint32_t height = 0;
	};

	Napi::Promise::Deferred deferred;
	std::vector<uint64_t> jobIds;
	std::vector<uint64_t> canvasIds;
	bool isArrayCall;
	std::vector<Result> results;
};

} // namespace

Napi::Value display::OBS_content_takeScreenshot(const Napi::CallbackInfo &info)
{
	Napi::Env env = info.Env();

	if (info.Length() < 3 || !info[0].IsObject() || !info[1].IsString() || !info[2].IsString()) {
		Napi::TypeError::New(env, "OBS_content_takeScreenshot(video, directory, filenameFormat, noSpace?) expects a Video object "
					  "(or an array of them) and two strings.")
			.ThrowAsJavaScriptException();
		return env.Undefined();
	}

	const bool isArrayCall = info[0].IsArray();
	std::vector<uint64_t> canvasIds;

	if (isArrayCall) {
		Napi::Array videos = info[0].As<Napi::Array>();
		if (videos.Length() == 0) {
			Napi::TypeError::New(env, "OBS_content_takeScreenshot: video array must not be empty.").ThrowAsJavaScriptException();
			return env.Undefined();
		}

		canvasIds.reserve(videos.Length());
		for (uint32_t i = 0; i < videos.Length(); ++i) {
			Napi::Value item = videos.Get(i);
			osn::Video *video = item.IsObject() ? Napi::ObjectWrap<osn::Video>::Unwrap(item.ToObject()) : nullptr;
			if (!video) {
				Napi::TypeError::New(env, "OBS_content_takeScreenshot: video array must only contain Video objects.")
					.ThrowAsJavaScriptException();
				return env.Undefined();
			}
			canvasIds.push_back(video->canvasId);
		}
	} else {
		osn::Video *video = Napi::ObjectWrap<osn::Video>::Unwrap(info[0].ToObject());
		if (!video) {
			Napi::TypeError::New(env, "OBS_content_takeScreenshot: first argument is not a Video object.").ThrowAsJavaScriptException();
			return env.Undefined();
		}
		canvasIds.push_back(video->canvasId);
	}

	std::string directory = info[1].ToString().Utf8Value();
	std::string filenameFormat = info[2].ToString().Utf8Value();
	/* ipc::value has no bool constructor; a bare bool would promote to int32 and mismatch the registered UInt32. */
	uint32_t noSpace = (info.Length() > 3 && !info[3].IsUndefined() && info[3].ToBoolean().Value()) ? 1 : 0;

	std::vector<char> canvasIdBytes(canvasIds.size() * sizeof(uint64_t));
	memcpy(canvasIdBytes.data(), canvasIds.data(), canvasIdBytes.size());

	auto conn = GetConnection(info);
	if (!conn)
		return env.Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper(
		"Display", "OBS_content_takeScreenshot", {ipc::value(canvasIdBytes), ipc::value(directory), ipc::value(filenameFormat), ipc::value(noSpace)});

	auto deferred = Napi::Promise::Deferred::New(env);

	std::string submitError;
	if (!ExtractCallError(response, submitError)) {
		deferred.Reject(Napi::Error::New(env, submitError).Value());
		return deferred.Promise();
	}

	const std::vector<char> &jobIdBytes = response[1].value_bin;
	std::vector<uint64_t> jobIds(jobIdBytes.size() / sizeof(uint64_t));
	memcpy(jobIds.data(), jobIdBytes.data(), jobIdBytes.size());

	auto worker = std::make_unique<ScreenshotWaitWorker>(env, deferred, std::move(jobIds), std::move(canvasIds), isArrayCall);
	worker->Queue();
	worker.release();

	return deferred.Promise();
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
	exports.Set(Napi::String::New(env, "OBS_content_takeScreenshot"), Napi::Function::New(env, display::OBS_content_takeScreenshot));
}
