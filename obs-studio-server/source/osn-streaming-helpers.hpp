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
#include <obs.h>

// Encoder defines - centralized to decouple from old API (nodeobs_service.h)
// These are used across multiple streaming/recording/encoder files
#ifdef WIN32
#define SIMPLE_ENCODER_X264 "x264"
#elif __APPLE__
#define SIMPLE_ENCODER_X264 "obs_x264"
#endif
#ifndef SIMPLE_ENCODER_X264
#define SIMPLE_ENCODER_X264 "x264"
#endif
#define SIMPLE_ENCODER_X264_LOWCPU "x264_lowcpu"
#define SIMPLE_ENCODER_QSV "qsv"
#define SIMPLE_ENCODER_QSV_AV1 "qsv_av1"
#define SIMPLE_ENCODER_NVENC "nvenc"
#define SIMPLE_ENCODER_NVENC_AV1 "nvenc_av1"
#define SIMPLE_ENCODER_NVENC_HEVC "nvenc_hevc"
#define SIMPLE_ENCODER_AMD "amd"
#define SIMPLE_ENCODER_AMD_HEVC "amd_hevc"
#define SIMPLE_ENCODER_AMD_AV1 "amd_av1"
#define SIMPLE_ENCODER_APPLE_H264 "apple_h264"
#define SIMPLE_ENCODER_APPLE_HEVC "apple_hevc"

#define ADVANCED_ENCODER_X264 "obs_x264"
#define ADVANCED_ENCODER_QSV "obs_qsv11"
#define ADVANCED_ENCODER_NVENC "ffmpeg_nvenc"
#define ADVANCED_ENCODER_AMD "h264_texture_amf"
#define ADVANCED_ENCODER_AMD_HEVC "h265_texture_amf"

#define ENCODER_NVENC_H264_TEX "obs_nvenc_h264_tex"
#define ENCODER_NVENC_HEVC_TEX "obs_nvenc_hevc_tex"
#define ENCODER_NVENC_AV1_TEX "obs_nvenc_av1_tex"

// Deprecated encoders
#define ENCODER_JIM_NVENC "jim_nvenc"
#define ENCODER_JIM_HEVC_NVENC "jim_hevc_nvenc"
#define ENCODER_JIM_AV1_NVENC "jim_av1_nvenc"

#define ENCODER_AV1_SVT_FFMPEG "ffmpeg_svt_av1"
#define ENCODER_AV1_AOM_FFMPEG "ffmpeg_aom_av1"

#define APPLE_SOFTWARE_VIDEO_ENCODER "com.apple.videotoolbox.videoencoder.h264"
#define APPLE_HARDWARE_VIDEO_ENCODER "com.apple.videotoolbox.videoencoder.h264.gva"
#define APPLE_HARDWARE_VIDEO_ENCODER_M1 "com.apple.videotoolbox.videoencoder.ave.avc"

namespace osn {
namespace streaming_helpers {

// Helper function to get stream output type from service
// Replaces OBS_service::getStreamOutputType to decouple from old API
inline const char *getStreamOutputType(obs_service_t *service)
{
	if (!service)
		return nullptr;
		
	const char *protocol = obs_service_get_protocol(service);
	if (!protocol) {
		blog(LOG_WARNING, "The service '%s' has no protocol set", obs_service_get_id(service));
		return nullptr;
	}

	if (!obs_is_output_protocol_registered(protocol)) {
		blog(LOG_WARNING, "The protocol '%s' is not registered", protocol);
		return nullptr;
	}

	// Check if the service has a preferred output type
	const char *output = obs_service_get_preferred_output_type(service);
	if (output && (obs_get_output_flags(output) & OBS_OUTPUT_SERVICE) != 0)
		return output;

	// Prefer first-party output types based on protocol
	if (strcmp(protocol, "RTMP") == 0 || strcmp(protocol, "RTMPS") == 0)
		return "rtmp_output";
	else if (strcmp(protocol, "HLS") == 0)
		return "ffmpeg_hls_muxer";
	else if (strcmp(protocol, "SRT") == 0 || strcmp(protocol, "RIST") == 0)
		return "ffmpeg_mpegts_muxer";

	// Default fallback
	return nullptr;
}

} // namespace streaming_helpers
} // namespace osn
