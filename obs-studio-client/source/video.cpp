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

#include "video.hpp"
#include "osn-error.hpp"
#include "controller.hpp"
#include "shared.hpp"
#include "utility-v8.hpp"
#include "utility.hpp"

Napi::FunctionReference osn::Video::constructor;

Napi::Object osn::Video::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func = DefineClass(env, "Video",
					  {
						  StaticMethod("create", &osn::Video::Create),
						  InstanceMethod("destroy", &osn::Video::Destroy),

						  InstanceAccessor("video", &osn::Video::get, &osn::Video::set),
						  InstanceAccessor("legacySettings", &osn::Video::GetLegacySettings, &osn::Video::SetLegacySettings),

						  InstanceAccessor("skippedFrames", &osn::Video::GetSkippedFrames, nullptr),
						  InstanceAccessor("encodedFrames", &osn::Video::GetEncodedFrames, nullptr),
					  });
	exports.Set("Video", func);
	osn::Video::constructor = Napi::Persistent(func);
	osn::Video::constructor.SuppressDestruct();
	return exports;
}

osn::Video::Video(const Napi::CallbackInfo &info) : Napi::ObjectWrap<osn::Video>(info)
{
	Napi::Env env = info.Env();
	Napi::HandleScope scope(env);

	this->canvasId = (uint64_t)info[0].ToNumber().Int64Value();
}

Napi::Value osn::Video::GetSkippedFrames(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Video", "GetSkippedFrames", {ipc::value((uint64_t)(this->canvasId))});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

Napi::Value osn::Video::GetEncodedFrames(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Video", "GetEncodedFrames", {ipc::value((uint64_t)(this->canvasId))});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return Napi::Number::New(info.Env(), response[1].value_union.ui32);
}

Napi::Value osn::Video::Create(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Video", "AddVideoContext", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	auto instance = osn::Video::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

void osn::Video::Destroy(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return;

	auto response = conn->call_synchronous_helper("Video", "RemoveVideoContext", {ipc::value((uint64_t)(this->canvasId))});
	ValidateResponse(info, response);
	isLastVideoValid = false;
	return;
}

inline void CreateVideo(const Napi::CallbackInfo &info, const std::vector<ipc::value> &response, Napi::Object &video, uint32_t index)
{
	video.Set("fpsNum", response.at(index++).value_union.ui32);
	video.Set("fpsDen", response.at(index++).value_union.ui32);
	video.Set("baseWidth", response.at(index++).value_union.ui32);
	video.Set("baseHeight", response.at(index++).value_union.ui32);
	video.Set("outputWidth", response.at(index++).value_union.ui32);
	video.Set("outputHeight", response.at(index++).value_union.ui32);
	video.Set("outputFormat", response.at(index++).value_union.ui32);
	video.Set("colorspace", response.at(index++).value_union.ui32);
	video.Set("range", response.at(index++).value_union.ui32);
	video.Set("scaleType", response.at(index++).value_union.ui32);
	if (response.size() >= 12)
		video.Set("fpsType", response.at(index++).value_union.ui32);
}

inline void SerializeVideoData(const Napi::Object &video, std::vector<ipc::value> &args)
{
	args.push_back(video.Get("fpsNum").ToNumber().Uint32Value());
	args.push_back(video.Get("fpsDen").ToNumber().Uint32Value());
	args.push_back(video.Get("baseWidth").ToNumber().Uint32Value());
	args.push_back(video.Get("baseHeight").ToNumber().Uint32Value());
	args.push_back(video.Get("outputWidth").ToNumber().Uint32Value());
	args.push_back(video.Get("outputHeight").ToNumber().Uint32Value());
	args.push_back(video.Get("outputFormat").ToNumber().Uint32Value());
	args.push_back(video.Get("colorspace").ToNumber().Uint32Value());
	args.push_back(video.Get("range").ToNumber().Uint32Value());
	args.push_back(video.Get("scaleType").ToNumber().Uint32Value());
	args.push_back(video.Get("fpsType").ToNumber().Uint32Value());
}

Napi::Value osn::Video::get(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	if (!isLastVideoValid) {
		lastVideo = conn->call_synchronous_helper("Video", "GetVideoContext", {ipc::value((uint64_t)this->canvasId)});

		if (!ValidateResponse(info, lastVideo))
			return info.Env().Undefined();

		if (!(lastVideo.size() == 11 || lastVideo.size() == 12))
			return info.Env().Undefined();
		isLastVideoValid = true;
	}

	Napi::Object video = Napi::Object::New(info.Env());
	CreateVideo(info, lastVideo, video, 1);

	return video;
}

void osn::Video::set(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	Napi::Object video = value.ToObject();
	if (!video || video.IsUndefined()) {
		Napi::Error::New(info.Env(), "The video context object passed is invalid.").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	std::vector<ipc::value> args;
	SerializeVideoData(video, args);

	args.push_back(this->canvasId);

	auto response = conn->call_synchronous_helper("Video", "SetVideoContext", args);

	lastVideo.resize(0);
	isLastVideoValid = false;
}

Napi::Value osn::Video::GetLegacySettings(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	auto response = conn->call_synchronous_helper("Video", "GetLegacySettings", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	if (response.size() != 12)
		return info.Env().Undefined();

	Napi::Object video = Napi::Object::New(info.Env());
	CreateVideo(info, response, video, 1);
	return video;
}

void osn::Video::SetLegacySettings(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	Napi::Object video = value.ToObject();
	if (!video || video.IsUndefined()) {
		Napi::Error::New(info.Env(), "The video context object passed is invalid.").ThrowAsJavaScriptException();
		return;
	}

	auto conn = GetConnection(info);
	if (!conn)
		return;

	std::vector<ipc::value> args;
	SerializeVideoData(video, args);
	conn->call("Video", "SetLegacySettings", args);
}
