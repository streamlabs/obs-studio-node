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

#include <iostream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "obs.h"
#include "nodeobs_configManager.hpp"

//obs-x264 plugin
#ifdef WIN32
#define SIMPLE_ENCODER_X264 "x264"
#elif __APPLE__
#define SIMPLE_ENCODER_X264 "obs_x264"
#endif
#define ADVANCED_ENCODER_X264 "obs_x264"

//special case for recording
#define SIMPLE_ENCODER_X264_LOWCPU "x264_lowcpu"

//generic values for simple mode to convert to specific encoders
#define SIMPLE_ENCODER_NVENC "nvenc"           //h264 - obs-nvenc plugin
#define SIMPLE_ENCODER_NVENC_AV1 "nvenc_av1"   //av1 - obs-nvenc plugin
#define SIMPLE_ENCODER_NVENC_HEVC "nvenc_hevc" //hevc - obs-nvenc plugin
#define SIMPLE_ENCODER_AMD "amd"               //h264 - obs-ffmpeg plugin
#define SIMPLE_ENCODER_AMD_HEVC "amd_hevc"     //hevc - obs-ffmpeg plugin
#define SIMPLE_ENCODER_AMD_AV1 "amd_av1"       //av1 - obs-ffmpeg plugin
#define SIMPLE_ENCODER_QSV "qsv"               //h264 - obs-qsv11 plugin
#define SIMPLE_ENCODER_QSV_AV1 "qsv_av1"       //av1 - obs-qsv11 plugin
#define SIMPLE_ENCODER_APPLE_H264 "apple_h264" //h264 - apple encoder
#define SIMPLE_ENCODER_APPLE_HEVC "apple_hevc" //hevc - apple encoder

//obs-qsv11 plugin (QuickSync)
#define ADVANCED_ENCODER_QSV "obs_qsv11"           //h264
#define ADVANCED_ENCODER_QSV_V2 "obs_qsv11_v2"     //h264
#define ADVANCED_ENCODER_QSV_AV1 "obs_qsv11_av1"   //av1
#define ADVANCED_ENCODER_QSV_HEVC "obs_qsv11_hevc" //hevc

//obs-nvenc plugin - deprecated jim encoders
#define ENCODER_JIM_NVENC "jim_nvenc"           //h264
#define ENCODER_JIM_HEVC_NVENC "jim_hevc_nvenc" //hevc
#define ENCODER_JIM_AV1_NVENC "jim_av1_nvenc"   //av1

//obs-nvenc plugin (NVIDIA)
#define ENCODER_NVENC_H264_TEX "obs_nvenc_h264_tex"     //h264
#define ENCODER_NVENC_HEVC_TEX "obs_nvenc_hevc_tex"     //hevc
#define ENCODER_NVENC_AV1_TEX "obs_nvenc_av1_tex"       //av1
#define ADVANCED_ENCODER_NVENC "ffmpeg_nvenc"           //h264 - if REGISTER_FFMPEG_IDS
#define ADVANCED_ENCODER_NVENC_HEVC "ffmpeg_hevc_nvenc" //hevc - if REGISTER_FFMPEG_IDS and ENABLE_HEVC

//obs-ffmpeg plugin
#define ENCODER_AV1_SVT_FFMPEG "ffmpeg_svt_av1"      //av1
#define ENCODER_AV1_AOM_FFMPEG "ffmpeg_aom_av1"      //av1
#define ADVANCED_ENCODER_AMD "h264_texture_amf"      //h264
#define ADVANCED_ENCODER_AMD_HEVC "h265_texture_amf" //hevc
#define ADVANCED_ENCODER_AMD_AV1 "av1_texture_amf"   //av1

//Apple encoders
#define APPLE_SOFTWARE_VIDEO_ENCODER "com.apple.videotoolbox.videoencoder.h264"
#define APPLE_HARDWARE_VIDEO_ENCODER "com.apple.videotoolbox.videoencoder.h264.gva"
#define APPLE_HARDWARE_VIDEO_ENCODER_M1 "com.apple.videotoolbox.videoencoder.ave.avc"
#define APPLE_HARDWARE_VIDEO_ENCODER_HEVC "com.apple.videotoolbox.videoencoder.ave.hevc"

#define SIMPLE_AUDIO_ENCODER_AAC "ffmpeg_aac"
#define SIMPLE_AUDIO_ENCODER_OPUS "ffmpeg_opus"

//presets
#define PRESET_NVENC "NVENCPreset2"
#define PRESET_NVENC_DEP "NVENCPreset"
#define PRESET_QSV "QSVPreset"
#define PRESET_AMD "AMDPreset"
#define PRESET_APPLE "Profile"
#define DEFAULT_PRESET "Preset"

//encoder families
#define FAMILY_OBS "family_obs"
#define FAMILY_QSV "family_qsv"
#define FAMILY_NVENC "family_nvenc"
#define FAMILY_NVENC_HEVC "family_nvenc_hevc"
#define FAMILY_AMD "family_amd"
#define FAMILY_APPLE "family_apple"
#define FAMILY_FFMPEG "family_ffmpeg"

namespace osn {
namespace EncoderUtils {

bool isEncoderRegistered(const std::string &encoder);
bool isCodecAvailableForService(const char *encoder, obs_service_t *service);
bool isInvalidAppleEncoder(const char *encoderID);
std::string getInternalEncoderFromSimple(const char *encoder);
std::string getSimpleEncoderFromInternal(const char *encoder);
std::string getEncoderFamily(const char *encoder);
std::string getEncoderPreset(const char *encoder);
bool isOldJimNvencEncoder(const std::string &encoderId);
void convertOldJimNvencEncoder(config_t *config, const std::string &configSection, const std::string &streamEncoderSetting,
			       const std::string &recordingEncoderSetting);
bool isEncoderCompatible(std::string encoderName, obs_service_t *service, bool simpleMode, bool recording, const std::string &container, int checkIndex);
bool isEncoderCompatibleStreaming(obs_service_t *service, const char *encoderToFind, bool simpleMode);
bool isEncoderCompatibleRecording(const char *encoderToFind, const std::string &container, bool simpleMode);
bool updateNvencPresets(obs_data_t *data, const char *encoderId);
const char *convertNvencSimplePreset(const char *old_preset);

class EncoderSettings {
public:
	std::string advanced_title;
	std::string advanced_name;
	std::string simple_title;
	std::string simple_name;
	std::string simple_internal_name;
	std::string backup;
	bool recording;
	bool streaming;
	bool check_availability;
	bool check_availability_streaming;
	bool check_availability_format;
	bool only_for_reuse_simple;
	std::string preset;
	std::string family;
	const std::string getSimpleName() const { return simple_internal_name.empty() ? simple_name : simple_internal_name; }
};

extern const std::vector<EncoderSettings> videoEncoderOptions;

// Codect/Container support check.
// from OBS code UI\window-basic-settings.cpp
static const std::unordered_map<std::string, std::unordered_set<std::string>> codecsForContainers = {
	// Technically our muxer supports HEVC and AV1 as well, but nothing else does
	{"flv", {"h264", "aac"}},
	{"mpegts", {"h264", "hevc", "aac", "opus"}},
	{"hls", {"h264", "hevc", "aac"}}, // Also using MPEG-TS, but no Opus support
	{"mov", {"h264", "hevc", "prores", "aac", "alac", "pcm_s16le", "pcm_s24le", "pcm_f32le"}},
	{"mp4", {"h264", "hevc", "av1", "aac", "opus", "alac", "flac"}},
	{"fragmented_mov", {"h264", "hevc", "prores", "aac", "alac", "pcm_s16le", "pcm_s24le", "pcm_f32le"}},
	{"fragmented_mp4", {"h264", "hevc", "av1", "aac", "opus", "alac", "flac"}},
	// MKV supports everything
	{"mkv", {}},
};

}
}
