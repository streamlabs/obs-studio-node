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

#include "osn-encoders.hpp"
#include "obs.h"
#include <iostream>
#include <ipc-server.hpp>
#include <util/dstr.h>
#include "utility.hpp"

static bool codecListContains(const char **codecs, const char *codec);
static const char *getStreamOutputType(const obs_service_t *service);
static bool isNvencAvailableForSimpleMode();
static bool containerSupportsCodec(const std::string &container, const std::string &codec);
static void convert_nvenc_h264_presets(obs_data_t *data);
static void convert_nvenc_hevc_presets(obs_data_t *data);

const std::vector<osn::EncoderUtils::EncoderSettings> osn::EncoderUtils::videoEncoderOptions = {
	// Software x264
	{"Software (x264)", ADVANCED_ENCODER_X264, "Software (x264)", SIMPLE_ENCODER_X264, ADVANCED_ENCODER_X264, "", true, true, false, false, true, false,
	 DEFAULT_PRESET, FAMILY_OBS},
	// Software x264 low CPU (only for recording)
	{"", "", "Software (x264 low CPU usage preset, increases file size)", SIMPLE_ENCODER_X264_LOWCPU, ADVANCED_ENCODER_X264, "", true, false, false, false,
	 true, false, DEFAULT_PRESET, FAMILY_OBS},
	// QuickSync H.264 (v1, deprecated)
	// This line left here for reference
	// {"QuickSync H.264 (v1 deprecated)", ADVANCED_ENCODER_QSV, "(Deprecated v1) Hardware (QSV, H.264)", SIMPLE_ENCODER_QSV, ADVANCED_ENCODER_QSV, true, true, true, false, true, false},
	// QuickSync H.264 (v2, new)
	{"QuickSync H.264", ADVANCED_ENCODER_QSV_V2, "Hardware (QSV, H.264)", SIMPLE_ENCODER_QSV, ADVANCED_ENCODER_QSV_V2, "", true, true, true, false, true,
	 false, PRESET_QSV, FAMILY_QSV},
	// QuickSync AV1
	{"QuickSync AV1", ADVANCED_ENCODER_QSV_AV1, "Hardware (QSV, AV1)", SIMPLE_ENCODER_QSV_AV1, ADVANCED_ENCODER_QSV_AV1, "", true, true, true, true, true,
	 false, PRESET_QSV, FAMILY_QSV},
	// QuickSync HEVC
	{"QuickSync HEVC", ADVANCED_ENCODER_QSV_HEVC, "", "", "", "", true, true, true, false, true, false, PRESET_QSV, FAMILY_QSV},
	// NVIDIA NVENC H.264
	{"NVIDIA NVENC H.264", ADVANCED_ENCODER_NVENC, "NVIDIA NVENC H.264", SIMPLE_ENCODER_NVENC, ENCODER_NVENC_H264_TEX, ADVANCED_ENCODER_NVENC, true, true,
	 true, false, true, true, PRESET_NVENC, FAMILY_NVENC},
	// NVIDIA NVENC H.264 (new)
	{"NVIDIA NVENC H.264 (new)", ENCODER_NVENC_H264_TEX, "NVIDIA NVENC H.264 (new)", ENCODER_NVENC_H264_TEX, "", "", true, true, true, false, true, false,
	 PRESET_NVENC, FAMILY_NVENC},
	// NVIDIA NVENC HEVC
	{"NVIDIA NVENC HEVC", ENCODER_NVENC_HEVC_TEX, "Hardware (NVENC, HEVC)", SIMPLE_ENCODER_NVENC_HEVC, ENCODER_NVENC_HEVC_TEX, ADVANCED_ENCODER_NVENC_HEVC,
	 true, true, true, true, true, false, DEFAULT_PRESET, FAMILY_NVENC_HEVC},
	// NVIDIA NVENC AV1
	{"NVIDIA NVENC AV1", ENCODER_NVENC_AV1_TEX, "NVIDIA NVENC AV1", ENCODER_NVENC_AV1_TEX, "", "", true, true, true, true, true, false, PRESET_NVENC,
	 FAMILY_NVENC},
	// Apple VT H264 Hardware Encoder
	{"Apple VT H264 Hardware Encoder", APPLE_HARDWARE_VIDEO_ENCODER, "Hardware (Apple, H.264)", APPLE_HARDWARE_VIDEO_ENCODER, "", "", true, true, true,
	 false, true, false, PRESET_APPLE, FAMILY_APPLE},
	// Apple VT H264 Hardware Encoder - get_simple_output_encoder RETURNED M1 FOR SIMPLE_ENCODER_APPLE_H264 SO MAKE THAT THE SIMPLE NAME AND M1 INTERNAL NAME
	{"Apple VT H264 Hardware Encoder", APPLE_HARDWARE_VIDEO_ENCODER_M1, "Hardware (Apple, H.264)", SIMPLE_ENCODER_APPLE_H264,
	 APPLE_HARDWARE_VIDEO_ENCODER_M1, "", true, true, true, false, true, false, PRESET_APPLE, FAMILY_APPLE},
	// get_simple_output_encoder had Apple HEVC so add it here, never used with an advanced name but follow the pattern of M1 above
	{"Apple VT HEVC Hardware Encoder", APPLE_HARDWARE_VIDEO_ENCODER_HEVC, "Hardware (Apple, HEVC)", SIMPLE_ENCODER_APPLE_HEVC,
	 APPLE_HARDWARE_VIDEO_ENCODER_HEVC, "", true, true, true, true, true, false, PRESET_APPLE, FAMILY_APPLE},
	// AMD HW H.264
	{"AMD HW H.264", ADVANCED_ENCODER_AMD, "Hardware (AMD, H.264)", SIMPLE_ENCODER_AMD, ADVANCED_ENCODER_AMD, "", true, true, true, false, true, false,
	 PRESET_AMD, FAMILY_AMD},
	// AMD HW H.265 (HEVC)
	{"AMD HW H.265 (HEVC)", ADVANCED_ENCODER_AMD_HEVC, "Hardware (AMD, HEVC)", SIMPLE_ENCODER_AMD_HEVC, ADVANCED_ENCODER_AMD_HEVC, "", true, true, true,
	 true, true, false, PRESET_AMD, FAMILY_AMD},
	// AMD HW AV1
	{"AMD HW AV1", SIMPLE_ENCODER_AMD_AV1, "Hardware (AMD, AV1)", SIMPLE_ENCODER_AMD_AV1, ADVANCED_ENCODER_AMD_AV1, "", true, true, true, true, true, false,
	 PRESET_AMD, FAMILY_AMD},
	// AOM AV1
	{"AOM AV1", ENCODER_AV1_AOM_FFMPEG, "", "", "", "", true, true, true, true, true, false, DEFAULT_PRESET, FAMILY_FFMPEG},
	// SVT-AV1
	{"SVT-AV1", ENCODER_AV1_SVT_FFMPEG, "", "", "", "", true, true, true, true, true, false, DEFAULT_PRESET, FAMILY_FFMPEG}};

bool osn::EncoderUtils::isEncoderRegistered(const std::string &encoder)
{
	const char *val;
	int i = 0;

	while (obs_enum_encoder_types(i++, &val)) {
		if (val == nullptr)
			continue;
		if (std::string(val) == encoder)
			return true;
	}

	return false;
}

bool osn::EncoderUtils::isCodecAvailableForService(const char *encoder, obs_service_t *service)
{
	if (!encoder || !service)
		return false;

	auto supportedCodecs = obs_service_get_supported_video_codecs(service);
	auto encoderCodec = obs_get_encoder_codec(encoder);

	if (!encoderCodec)
		return false;

	if (supportedCodecs)
		return codecListContains(supportedCodecs, encoderCodec);

	// Custom services do not expose codec lists, so mirror OBS and fall back to the output type.
	auto outputType = getStreamOutputType(service);
	if (!outputType)
		return false;

	auto outputSupportedCodecs = obs_get_output_supported_video_codecs(outputType);
	if (!outputSupportedCodecs)
		return false;

	auto splitOutputSupportedCodecs = strlist_split(outputSupportedCodecs, ';', false);
	bool supported = codecListContains((const char **)splitOutputSupportedCodecs, encoderCodec);
	strlist_free(splitOutputSupportedCodecs);

	return supported;
}

static bool codecListContains(const char **codecs, const char *codec)
{
	if (!codecs || !codec)
		return false;

	while (*codecs) {
		if (strcmp(*codecs, codec) == 0)
			return true;
		codecs++;
	}

	return false;
}

// Resolves the OBS output type used by a streaming service.
// Returns a non-owned output type ID, such as "rtmp_output", or nullptr if no compatible output is registered.
static const char *getStreamOutputType(const obs_service_t *service)
{
	const char *protocol = obs_service_get_protocol(service);

	if (!protocol)
		return nullptr;

	if (!obs_is_output_protocol_registered(protocol))
		return nullptr;

	const char *output = obs_service_get_preferred_output_type(service);
	if (output && (obs_get_output_flags(output) & OBS_OUTPUT_SERVICE) != 0)
		return output;

	auto canUseOutput = [](const char *prot, const char *output, const char *prot_test1, const char *prot_test2 = nullptr) {
		return (strcmp(prot, prot_test1) == 0 || (prot_test2 && strcmp(prot, prot_test2) == 0)) &&
		       (obs_get_output_flags(output) & OBS_OUTPUT_SERVICE) != 0;
	};

	if (canUseOutput(protocol, "rtmp_output", "RTMP", "RTMPS")) {
		return "rtmp_output";
	} else if (canUseOutput(protocol, "ffmpeg_hls_muxer", "HLS")) {
		return "ffmpeg_hls_muxer";
	} else if (canUseOutput(protocol, "ffmpeg_mpegts_muxer", "SRT", "RIST")) {
		return "ffmpeg_mpegts_muxer";
	}

	auto returnFirstOutputId = [](void *data, const char *id) {
		const char **output = (const char **)data;

		*output = id;
		return false;
	};
	obs_enum_output_types_with_protocol(protocol, &output, returnFirstOutputId);
	return output;
}

bool osn::EncoderUtils::isEncoderCompatible(std::string encoderName, obs_service_t *service, bool simpleMode, bool recording, const std::string &container,
					    int checkIndex)
{
	if (encoderName.empty())
		return false;

	if (!recording && !videoEncoderOptions[checkIndex].streaming)
		return false;

	if (recording && !videoEncoderOptions[checkIndex].recording)
		return false;

	if (videoEncoderOptions[checkIndex].check_availability && !isEncoderRegistered(encoderName))
		return false;

	if (!recording && videoEncoderOptions[checkIndex].check_availability_streaming && !isCodecAvailableForService(encoderName.c_str(), service))
		return false;

	if (simpleMode) {
		if (videoEncoderOptions[checkIndex].only_for_reuse_simple && !isNvencAvailableForSimpleMode())
			return false;
	}

	if (recording && videoEncoderOptions[checkIndex].check_availability_format) {
		const char *codec = obs_get_encoder_codec(encoderName.c_str());
		if (!codec) {
			blog(LOG_DEBUG, "[ENCODER_SKIPPED] codec is null");
			return false;
		}
		if (!containerSupportsCodec(container, codec))
			return false;
	}

	return true;
}

bool osn::EncoderUtils::isEncoderCompatibleStreaming(obs_service_t *service, const char *encoderToFind, bool simpleMode)
{
	bool validEncoder = false;
	std::string curEncoder = "";

	//find the encoder in the set and then check compatibility
	for (int i = 0; i < videoEncoderOptions.size(); i++) {
		//simple mode: search by simple_name because that is what is in basic.ini
		curEncoder = simpleMode ? videoEncoderOptions[i].simple_name : videoEncoderOptions[i].advanced_name;
		if (curEncoder.compare(encoderToFind) == 0) {
			//simple mode: found simple name, get internal name to check compatibility
			if (simpleMode)
				curEncoder = videoEncoderOptions[i].getSimpleName();
			if (isEncoderCompatible(curEncoder, service, simpleMode, false, "", i)) {
				validEncoder = true;
			}
			break;
		}
	}

	return validEncoder;
}

bool osn::EncoderUtils::isEncoderCompatibleRecording(const char *encoderToFind, const std::string &container, bool simpleMode)
{
	bool validEncoder = false;

	std::string curEncoder = "";

	//find the encoder in the set and then check compatibility
	for (int i = 0; i < videoEncoderOptions.size(); i++) {
		//search by simple_name to check settings because that is what is in basic.ini and because multiple encoders may have same internal_simple_name
		curEncoder = simpleMode ? videoEncoderOptions[i].simple_name : videoEncoderOptions[i].advanced_name;
		if (curEncoder.compare(encoderToFind) == 0) {
			//simple mode: found simple name, get internal name to check compatibility
			if (simpleMode)
				curEncoder = videoEncoderOptions[i].getSimpleName();
			if (isEncoderCompatible(curEncoder, NULL, simpleMode, true, container, i)) {
				validEncoder = true;
			}
			break;
		}
	}

	return validEncoder;
}

void osn::EncoderUtils::getAvailableEncoders(std::vector<ipc::value> &rval, obs_service_t *service, bool simpleMode, bool recording,
					     const std::string &container)
{
	for (int i = 0; i < videoEncoderOptions.size(); i++) {
		const auto &opt = videoEncoderOptions[i];
		const std::string &title = simpleMode ? opt.simple_title : opt.advanced_title;
		const std::string &name = simpleMode ? opt.simple_name : opt.advanced_name;
		if (title.empty() || name.empty())
			continue;
		std::string encoderName = simpleMode ? opt.getSimpleName() : opt.advanced_name;
		if (isEncoderCompatible(encoderName, service, simpleMode, recording, container, i)) {
			rval.push_back(ipc::value(title));
			rval.push_back(ipc::value(name));
		}
	}
}

bool osn::EncoderUtils::isInvalidAppleEncoder(const char *encoderID)
{
#if defined(__APPLE__)
	// disable this encoder; not functioning properly
	return strcmp(encoderID, APPLE_SOFTWARE_VIDEO_ENCODER) == 0;
#else
	return false;
#endif
}

//replacing logic of get_simple_output_encoder
std::string osn::EncoderUtils::getInternalEncoderFromSimple(const char *encoder)
{
	std::string encoderName = ADVANCED_ENCODER_X264;
	bool found = false;

	for (const auto curEnc : videoEncoderOptions) {
		//if there is a backup, check if simple_internal_name is available, return backup if not
		//else if no simple_internal_name, return simple_name
		//else return simple_internal_name
		if (encoder == curEnc.simple_name) {
			if (!curEnc.backup.empty() && !isEncoderRegistered(curEnc.simple_internal_name))
				encoderName = curEnc.backup;
			else {
				if (curEnc.simple_internal_name.empty())
					encoderName = curEnc.simple_name;
				else
					encoderName = curEnc.simple_internal_name;
			}
			found = true;
			break;
		}
	}
	if (!found)
		blog(LOG_WARNING, "getInternalEncoderFromSimple - encoder %s is not found, returning default encoder.", encoder);

	return encoderName;
}

std::string osn::EncoderUtils::getSimpleEncoderFromInternal(const char *encoder)
{
	//this defaults to advanced b/c that's how it's done in osn-simple-streaming where this is used
	std::string encoderName = ADVANCED_ENCODER_X264;
	bool found = false;

	for (const auto curEnc : videoEncoderOptions) {
		if ((encoder == curEnc.simple_internal_name) || (!curEnc.backup.empty() && encoder == curEnc.backup)) {
			encoderName = curEnc.simple_name;
			found = true;
			break;
		}
	}
	if (!found)
		blog(LOG_WARNING, "GetSimpleEncoderFromAdvanced - encoder %s is not found, returning default encoder.", encoder);

	return encoderName;
}

std::string osn::EncoderUtils::getEncoderPreset(const char *encoder)
{
	std::string preset = DEFAULT_PRESET;
	bool found = false;

	for (const auto curEnc : videoEncoderOptions) {
		if ((encoder == curEnc.advanced_name) || (encoder == curEnc.simple_name)) {
			preset = curEnc.preset;
			found = true;
			break;
		}
	}

	if (!found)
		blog(LOG_WARNING, "GetEncoderPreset - encoder %s is not found, returning default preset.", encoder);

	return preset;
}

std::string osn::EncoderUtils::getEncoderFamily(const char *encoder)
{
	std::string family = "";
	bool found = false;

	//match on advanced_name or simple_name...backup or internal?
	for (const auto curEnc : videoEncoderOptions) {
		if ((encoder == curEnc.advanced_name) || (encoder == curEnc.simple_name)) {
			family = curEnc.family;
			found = true;
			break;
		}
	}

	if (!found)
		blog(LOG_WARNING, "GetEncoderFamily - encoder %s is not found.", encoder);

	return family;
}

bool osn::EncoderUtils::isOldJimNvencEncoder(const std::string &encoderId)
{
	return encoderId == ENCODER_JIM_NVENC || encoderId == ENCODER_JIM_HEVC_NVENC || encoderId == ENCODER_JIM_AV1_NVENC;
}

// This code should be removed when JIM_ encoders will be removed from OBS
void osn::EncoderUtils::convertOldJimNvencEncoder(config_t *config, const std::string &configSection, const std::string &streamEncoderSetting,
						  const std::string &recordingEncoderSetting)
{
	const std::string streamEncoder = utility::GetSafeString(config_get_string(config, configSection.c_str(), streamEncoderSetting.c_str()));
	if (osn::EncoderUtils::isOldJimNvencEncoder(streamEncoder)) {
		blog(LOG_INFO, "Converting stream encoder for mode '%s' from encoder '%s' to '%s'", configSection.c_str(), streamEncoder.c_str(),
		     ENCODER_NVENC_H264_TEX);
		config_set_string(config, configSection.c_str(), streamEncoderSetting.c_str(), ENCODER_NVENC_H264_TEX);
	}

	const std::string recordingEncoder = utility::GetSafeString(config_get_string(config, configSection.c_str(), recordingEncoderSetting.c_str()));
	if (osn::EncoderUtils::isOldJimNvencEncoder(recordingEncoder)) {
		blog(LOG_INFO, "Converting recording encoder for mode '%s' from encoder '%s' to '%s'", configSection.c_str(), recordingEncoder.c_str(),
		     ENCODER_NVENC_H264_TEX);
		config_set_string(config, configSection.c_str(), recordingEncoderSetting.c_str(), ENCODER_NVENC_H264_TEX);
	}
}

bool osn::EncoderUtils::updateNvencPresets(obs_data_t *data, const char *encoderId)
{
	bool modified = false;
	if (astrcmpi(encoderId, ENCODER_NVENC_H264_TEX) == 0 || astrcmpi(encoderId, ADVANCED_ENCODER_NVENC) == 0) {
		if (obs_data_has_user_value(data, "preset") && !obs_data_has_user_value(data, "preset2")) {
			convert_nvenc_h264_presets(data);

			modified = true;
		}
	} else if (astrcmpi(encoderId, ENCODER_NVENC_HEVC_TEX) == 0 || astrcmpi(encoderId, ADVANCED_ENCODER_NVENC_HEVC) == 0) {

		if (obs_data_has_user_value(data, "preset") && !obs_data_has_user_value(data, "preset2")) {
			convert_nvenc_hevc_presets(data);

			modified = true;
		}
	}
	if (modified)
		blog(LOG_INFO, "Updated nvenc preset for %s", encoderId);

	return modified;
}

const char *osn::EncoderUtils::convertNvencSimplePreset(const char *old_preset)
{
	if (astrcmpi(old_preset, "mq") == 0) {
		return "p5";
	} else if (astrcmpi(old_preset, "hq") == 0) {
		return "p5";
	} else if (astrcmpi(old_preset, "default") == 0) {
		return "p3";
	} else if (astrcmpi(old_preset, "hp") == 0) {
		return "p1";
	} else if (astrcmpi(old_preset, "ll") == 0) {
		return "p3";
	} else if (astrcmpi(old_preset, "llhq") == 0) {
		return "p4";
	} else if (astrcmpi(old_preset, "llhp") == 0) {
		return "p2";
	}
	return "p5";
}

static bool isNvencAvailableForSimpleMode()
{
	// Only available if config already uses it
	const char *current_stream_encoder = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "StreamEncoder");
	const char *current_rec_encoder = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecEncoder");
	bool nvenc_used_streaming = (current_stream_encoder && strcmp(current_stream_encoder, SIMPLE_ENCODER_NVENC) == 0);
	bool nvenc_used_recording = (current_rec_encoder && strcmp(current_rec_encoder, SIMPLE_ENCODER_NVENC) == 0);

	return (nvenc_used_streaming || nvenc_used_recording) && osn::EncoderUtils::isEncoderRegistered(ADVANCED_ENCODER_NVENC);
}

static bool containerSupportsCodec(const std::string &container, const std::string &codec)
{
	auto iter = osn::EncoderUtils::codecsForContainers.find(container);
	if (iter == osn::EncoderUtils::codecsForContainers.end())
		return false;
	auto codecs = iter->second;
	// Assume everything is supported
	if (codecs.empty())
		return true;
	return codecs.count(codec) > 0;
}

static void convert_nvenc_h264_presets(obs_data_t *data)
{
	const char *preset = obs_data_get_string(data, "preset");
	const char *rc = obs_data_get_string(data, "rate_control");

	// If already using SDK10+ preset, return early.
	if (astrcmpi_n(preset, "p", 1) == 0) {
		obs_data_set_string(data, "preset2", preset);
		return;
	}

	if (astrcmpi(rc, "lossless") == 0 && astrcmpi(preset, "mq")) {
		obs_data_set_string(data, "preset2", "p3");
		obs_data_set_string(data, "tune", "lossless");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(rc, "lossless") == 0 && astrcmpi(preset, "hp")) {
		obs_data_set_string(data, "preset2", "p2");
		obs_data_set_string(data, "tune", "lossless");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "mq") == 0) {
		obs_data_set_string(data, "preset2", "p5");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "qres");

	} else if (astrcmpi(preset, "hq") == 0) {
		obs_data_set_string(data, "preset2", "p5");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "default") == 0) {
		obs_data_set_string(data, "preset2", "p3");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "hp") == 0) {
		obs_data_set_string(data, "preset2", "p1");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "ll") == 0) {
		obs_data_set_string(data, "preset2", "p3");
		obs_data_set_string(data, "tune", "ll");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "llhq") == 0) {
		obs_data_set_string(data, "preset2", "p4");
		obs_data_set_string(data, "tune", "ll");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "llhp") == 0) {
		obs_data_set_string(data, "preset2", "p2");
		obs_data_set_string(data, "tune", "ll");
		obs_data_set_string(data, "multipass", "disabled");
	}
}

static void convert_nvenc_hevc_presets(obs_data_t *data)
{
	const char *preset = obs_data_get_string(data, "preset");
	const char *rc = obs_data_get_string(data, "rate_control");

	// If already using SDK10+ preset, return early.
	if (astrcmpi_n(preset, "p", 1) == 0) {
		obs_data_set_string(data, "preset2", preset);
		return;
	}

	if (astrcmpi(rc, "lossless") == 0 && astrcmpi(preset, "mq")) {
		obs_data_set_string(data, "preset2", "p5");
		obs_data_set_string(data, "tune", "lossless");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(rc, "lossless") == 0 && astrcmpi(preset, "hp")) {
		obs_data_set_string(data, "preset2", "p3");
		obs_data_set_string(data, "tune", "lossless");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "mq") == 0) {
		obs_data_set_string(data, "preset2", "p6");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "qres");

	} else if (astrcmpi(preset, "hq") == 0) {
		obs_data_set_string(data, "preset2", "p6");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "default") == 0) {
		obs_data_set_string(data, "preset2", "p5");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "hp") == 0) {
		obs_data_set_string(data, "preset2", "p1");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "ll") == 0) {
		obs_data_set_string(data, "preset2", "p3");
		obs_data_set_string(data, "tune", "ll");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "llhq") == 0) {
		obs_data_set_string(data, "preset2", "p4");
		obs_data_set_string(data, "tune", "ll");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "llhp") == 0) {
		obs_data_set_string(data, "preset2", "p2");
		obs_data_set_string(data, "tune", "ll");
		obs_data_set_string(data, "multipass", "disabled");
	}
}
