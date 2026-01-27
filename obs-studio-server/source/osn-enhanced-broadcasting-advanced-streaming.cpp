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

#include "osn-enhanced-broadcasting-advanced-streaming.hpp"
#include "osn-video-encoder.hpp"
#include "osn-service.hpp"
#include "osn-error.hpp"
#include "shared.hpp"
#include "nodeobs_audio_encoders.h"
#include "osn-audio-track.hpp"
#include <osn-video.hpp>

void osn::IEnhancedBroadcastingAdvancedStreaming::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("EnhancedBroadcastingAdvancedStreaming");
	cls->register_function(std::make_shared<ipc::function>("Create", std::vector<ipc::type>{}, Create));
	cls->register_function(std::make_shared<ipc::function>("Destroy", std::vector<ipc::type>{ipc::type::UInt64}, Destroy));
	cls->register_function(std::make_shared<ipc::function>("GetService", std::vector<ipc::type>{ipc::type::UInt64}, GetService));
	cls->register_function(std::make_shared<ipc::function>("SetService", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetService));
	cls->register_function(std::make_shared<ipc::function>("GetVideoEncoder", std::vector<ipc::type>{ipc::type::UInt64}, GetVideoEncoder));
	cls->register_function(
		std::make_shared<ipc::function>("SetVideoEncoder", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetVideoEncoder));
	cls->register_function(std::make_shared<ipc::function>("GetVideoCanvas", std::vector<ipc::type>{ipc::type::UInt64}, GetVideoCanvas));
	cls->register_function(std::make_shared<ipc::function>("SetVideoCanvas", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetVideoCanvas));
	cls->register_function(std::make_shared<ipc::function>("GetEnforceServiceBirate", std::vector<ipc::type>{ipc::type::UInt64}, GetEnforceServiceBirate));
	cls->register_function(std::make_shared<ipc::function>("SetEnforceServiceBirate", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32},
							       SetEnforceServiceBirate));
	cls->register_function(std::make_shared<ipc::function>("GetEnableTwitchVOD", std::vector<ipc::type>{ipc::type::UInt64}, GetEnableTwitchVOD));
	cls->register_function(
		std::make_shared<ipc::function>("SetEnableTwitchVOD", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetEnableTwitchVOD));
	cls->register_function(std::make_shared<ipc::function>("GetAudioTrack", std::vector<ipc::type>{ipc::type::UInt64}, GetAudioTrack));
	cls->register_function(std::make_shared<ipc::function>("SetAudioTrack", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetAudioTrack));
	cls->register_function(std::make_shared<ipc::function>("GetTwitchTrack", std::vector<ipc::type>{ipc::type::UInt64}, GetTwitchTrack));
	cls->register_function(std::make_shared<ipc::function>("SetTwitchTrack", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetTwitchTrack));
	cls->register_function(std::make_shared<ipc::function>("GetRescaling", std::vector<ipc::type>{ipc::type::UInt64}, GetRescaling));
	cls->register_function(std::make_shared<ipc::function>("SetRescaling", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetRescaling));
	cls->register_function(std::make_shared<ipc::function>("GetOutputWidth", std::vector<ipc::type>{ipc::type::UInt64}, GetOutputWidth));
	cls->register_function(std::make_shared<ipc::function>("SetOutputWidth", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetOutputWidth));
	cls->register_function(std::make_shared<ipc::function>("GetOutputHeight", std::vector<ipc::type>{ipc::type::UInt64}, GetOutputHeight));
	cls->register_function(
		std::make_shared<ipc::function>("SetOutputHeight", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetOutputHeight));
	cls->register_function(std::make_shared<ipc::function>("GetDelay", std::vector<ipc::type>{ipc::type::UInt64}, GetDelay));
	cls->register_function(std::make_shared<ipc::function>("SetDelay", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetDelay));
	cls->register_function(std::make_shared<ipc::function>("GetReconnect", std::vector<ipc::type>{ipc::type::UInt64}, GetReconnect));
	cls->register_function(std::make_shared<ipc::function>("SetReconnect", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetReconnect));
	cls->register_function(std::make_shared<ipc::function>("GetNetwork", std::vector<ipc::type>{ipc::type::UInt64}, GetNetwork));
	cls->register_function(std::make_shared<ipc::function>("SetNetwork", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetNetwork));
	cls->register_function(std::make_shared<ipc::function>("Start", std::vector<ipc::type>{ipc::type::UInt64}, Start));
	cls->register_function(std::make_shared<ipc::function>("Stop", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, Stop));
	cls->register_function(std::make_shared<ipc::function>("Query", std::vector<ipc::type>{ipc::type::UInt64}, Query));
	cls->register_function(std::make_shared<ipc::function>("GetLegacySettings", std::vector<ipc::type>{}, GetLegacySettings));
	cls->register_function(std::make_shared<ipc::function>("SetLegacySettings", std::vector<ipc::type>{ipc::type::UInt64}, SetLegacySettings));
	cls->register_function(std::make_shared<ipc::function>("GetDroppedFrames", std::vector<ipc::type>{ipc::type::UInt64}, GetDroppedFrames));
	cls->register_function(std::make_shared<ipc::function>("GetTotalFrames", std::vector<ipc::type>{ipc::type::UInt64}, GetTotalFrames));
	cls->register_function(std::make_shared<ipc::function>("GetKBitsPerSec", std::vector<ipc::type>{ipc::type::UInt64}, GetKBitsPerSec));
	cls->register_function(std::make_shared<ipc::function>("GetDataOutput", std::vector<ipc::type>{ipc::type::UInt64}, GetDataOutput));

	cls->register_function(
		std::make_shared<ipc::function>("GetAdditionalVideoCanvas", std::vector<ipc::type>{ipc::type::UInt64}, GetAdditionalVideoCanvas));
	cls->register_function(std::make_shared<ipc::function>("SetAdditionalVideoCanvas", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64},
							       SetAdditionalVideoCanvas));

	srv.register_collection(cls);
}

void osn::IEnhancedBroadcastingAdvancedStreaming::Create(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	uint64_t uid = osn::IEnhancedBroadcastingAdvancedStreaming::Manager::GetInstance().allocate(new EnhancedBroadcastingAdvancedStreaming());
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::IEnhancedBroadcastingAdvancedStreaming::Destroy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	EnhancedBroadcastingAdvancedStreaming *streaming = static_cast<EnhancedBroadcastingAdvancedStreaming *>(
		osn::IEnhancedBroadcastingAdvancedStreaming::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	osn::IEnhancedBroadcastingAdvancedStreaming::Manager::GetInstance().free(streaming);
	delete streaming;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IEnhancedBroadcastingAdvancedStreaming::Start(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	EnhancedBroadcastingAdvancedStreaming *streaming = static_cast<EnhancedBroadcastingAdvancedStreaming *>(
		osn::IEnhancedBroadcastingAdvancedStreaming::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	if (!streaming->service) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid service.");
	}

	auto vod_track_mixer = (streaming->twitchVODSupported && streaming->enableTwitchVOD) ? std::optional{streaming->twitchTrack} : std::nullopt;
	streaming->StartEnhancedBroadcastingStream(vod_track_mixer);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IEnhancedBroadcastingAdvancedStreaming::Stop(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	EnhancedBroadcastingAdvancedStreaming *streaming = static_cast<EnhancedBroadcastingAdvancedStreaming *>(
		osn::IEnhancedBroadcastingAdvancedStreaming::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	streaming->StopEnhancedBroadcastingStream();

	IAdvancedStreaming::Stop(data, id, args, rval);

	AUTO_DEBUG;
}

void osn::IEnhancedBroadcastingAdvancedStreaming::GetAdditionalVideoCanvas(void *data, const int64_t id, const std::vector<ipc::value> &args,
									   std::vector<ipc::value> &rval)
{
	EnhancedBroadcastingAdvancedStreaming *streaming = static_cast<EnhancedBroadcastingAdvancedStreaming *>(
		osn::IEnhancedBroadcastingAdvancedStreaming::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	const uint64_t uid = osn::Video::Manager::GetInstance().find(streaming->GetAdditionalVideoCanvas());

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));

	AUTO_DEBUG;
}

void osn::IEnhancedBroadcastingAdvancedStreaming::SetAdditionalVideoCanvas(void *data, const int64_t id, const std::vector<ipc::value> &args,
									   std::vector<ipc::value> &rval)
{
	EnhancedBroadcastingAdvancedStreaming *streaming = static_cast<EnhancedBroadcastingAdvancedStreaming *>(
		osn::IEnhancedBroadcastingAdvancedStreaming::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	obs_video_info *canvas = osn::Video::Manager::GetInstance().find(args[1].value_union.ui64);
	if (!canvas) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Canvas reference is not valid.");
	}

	streaming->SetAdditionalVideoCanvas(canvas);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}
