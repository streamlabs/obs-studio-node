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

#include "osn-simple-recording.hpp"
#include "osn-audio-encoder.hpp"
#include "osn-service.hpp"
#include "osn-error.hpp"
#include "shared.hpp"
#include "nodeobs_audio_encoders.h"
#include "osn-file-output.hpp"

void osn::ISimpleRecording::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("SimpleRecording");
	cls->register_function(std::make_shared<ipc::function>("Create", std::vector<ipc::type>{}, Create));
	cls->register_function(std::make_shared<ipc::function>("Destroy", std::vector<ipc::type>{ipc::type::UInt64}, Destroy));
	cls->register_function(std::make_shared<ipc::function>("GetVideoEncoder", std::vector<ipc::type>{ipc::type::UInt64}, GetVideoEncoder));
	cls->register_function(
		std::make_shared<ipc::function>("SetVideoEncoder", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetVideoEncoder));
	cls->register_function(std::make_shared<ipc::function>("GetAudioEncoder", std::vector<ipc::type>{ipc::type::UInt64}, GetAudioEncoder));
	cls->register_function(
		std::make_shared<ipc::function>("SetAudioEncoder", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetAudioEncoder));
	cls->register_function(std::make_shared<ipc::function>("GetQuality", std::vector<ipc::type>{ipc::type::UInt64}, GetQuality));
	cls->register_function(std::make_shared<ipc::function>("SetQuality", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetQuality));
	cls->register_function(std::make_shared<ipc::function>("Start", std::vector<ipc::type>{ipc::type::UInt64}, Start));
	cls->register_function(std::make_shared<ipc::function>("Stop", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, Stop));
	cls->register_function(std::make_shared<ipc::function>("Query", std::vector<ipc::type>{ipc::type::UInt64}, Query));
	cls->register_function(std::make_shared<ipc::function>("GetLowCPU", std::vector<ipc::type>{ipc::type::UInt64}, GetLowCPU));
	cls->register_function(std::make_shared<ipc::function>("SetLowCPU", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetLowCPU));
	cls->register_function(std::make_shared<ipc::function>("GetLegacySettings", std::vector<ipc::type>{}, GetLegacySettings));
	cls->register_function(std::make_shared<ipc::function>("SetLegacySettings", std::vector<ipc::type>{ipc::type::UInt64}, SetLegacySettings));
	cls->register_function(std::make_shared<ipc::function>("GetStreaming", std::vector<ipc::type>{ipc::type::UInt64}, GetStreaming));
	cls->register_function(std::make_shared<ipc::function>("SetStreaming", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetStreaming));
	cls->register_function(std::make_shared<ipc::function>("SplitFile", std::vector<ipc::type>{ipc::type::UInt64}, SplitFile));
	cls->register_function(std::make_shared<ipc::function>("GetEnableFileSplit", std::vector<ipc::type>{ipc::type::UInt64}, GetEnableFileSplit));
	cls->register_function(
		std::make_shared<ipc::function>("SetEnableFileSplit", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetEnableFileSplit));
	cls->register_function(std::make_shared<ipc::function>("GetSplitType", std::vector<ipc::type>{ipc::type::UInt64}, GetSplitType));
	cls->register_function(std::make_shared<ipc::function>("SetSplitType", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetSplitType));
	cls->register_function(std::make_shared<ipc::function>("GetSplitTime", std::vector<ipc::type>{ipc::type::UInt64}, GetSplitTime));
	cls->register_function(std::make_shared<ipc::function>("SetSplitTime", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetSplitTime));
	cls->register_function(std::make_shared<ipc::function>("GetSplitSize", std::vector<ipc::type>{ipc::type::UInt64}, GetSplitSize));
	cls->register_function(std::make_shared<ipc::function>("SetSplitSize", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetSplitSize));
	cls->register_function(std::make_shared<ipc::function>("GetFileResetTimestamps", std::vector<ipc::type>{ipc::type::UInt64}, GetFileResetTimestamps));
	cls->register_function(std::make_shared<ipc::function>("SetFileResetTimestamps", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32},
							       SetFileResetTimestamps));

	srv.register_collection(cls);
}

void osn::ISimpleRecording::Create(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	uint64_t uid = osn::ISimpleRecording::Manager::GetInstance().allocate(new SimpleRecording());
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::ISimpleRecording::Destroy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleRecording *recording = static_cast<SimpleRecording *>(osn::ISimpleRecording::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	osn::ISimpleRecording::Manager::GetInstance().free(recording);
	delete recording;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::ISimpleRecording::GetQuality(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleRecording *recording = static_cast<SimpleRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)recording->quality));
	AUTO_DEBUG;
}

void osn::ISimpleRecording::SetQuality(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleRecording *recording = static_cast<SimpleRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}

	recording->quality = (RecQuality)args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::ISimpleRecording::GetAudioEncoder(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleRecording *recording = static_cast<SimpleRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	uint64_t uid = osn::AudioEncoder::Manager::GetInstance().find(recording->audioEncoder);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::ISimpleRecording::SetAudioEncoder(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleRecording *recording = static_cast<SimpleRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	obs_encoder_t *encoder = osn::AudioEncoder::Manager::GetInstance().find(args[1].value_union.ui64);
	if (!encoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Encoder reference is not valid.");
	}

	recording->audioEncoder = encoder;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

static void LoadLosslessPreset(osn::Recording *recording)
{
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "format_name", "avi");
	obs_data_set_string(settings, "video_encoder", "utvideo");
	obs_data_set_string(settings, "audio_encoder", "pcm_s16le");

	obs_output_set_mixers(recording->output, 1);
	obs_output_update(recording->output, settings);
	obs_data_release(settings);
}

static obs_data_t *UpdateRecordingSettings_x264_crf(int crf, bool lowCPU)
{
	obs_data_t *settings = obs_data_create();
	obs_data_set_int(settings, "crf", crf);
	obs_data_set_bool(settings, "use_bufsize", true);
	obs_data_set_string(settings, "rate_control", "CRF");
	obs_data_set_string(settings, "profile", "high");
	obs_data_set_string(settings, "preset", lowCPU ? "ultrafast" : "veryfast");
	return settings;
}

static obs_data_t *UpdateRecordingSettings_amd_cqp(int cqp)
{
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "rate_control", "CQP");
	obs_data_set_string(settings, "profile", "high");
	obs_data_set_string(settings, "preset", "quality");
	obs_data_set_int(settings, "cqp", cqp);
	return settings;
}

static obs_data_t *UpdateRecordingSettings_nvenc(int cqp)
{
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "rate_control", "CQP");
	obs_data_set_string(settings, "profile", "high");
	obs_data_set_string(settings, "preset", "hq");
	obs_data_set_int(settings, "cqp", cqp);
	obs_data_set_int(settings, "bitrate", 0);
	return settings;
}

static obs_data_t *UpdateRecordingSettings_nvenc_hevc(int cqp)
{
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "rate_control", "CQP");
	obs_data_set_string(settings, "profile", "main");
	obs_data_set_string(settings, "preset", "hq");
	obs_data_set_int(settings, "cqp", cqp);
	return settings;
}

static obs_data_t *UpdateRecordingSettings_apple(int quality)
{
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "rate_control", "CRF");
	obs_data_set_string(settings, "profile", "high");
	obs_data_set_int(settings, "quality", quality);
	return settings;
}

static bool icq_available(obs_encoder_t *encoder)
{
	obs_properties_t *props = obs_encoder_properties(encoder);
	obs_property_t *p = obs_properties_get(props, "rate_control");
	bool icq_found = false;

	size_t num = obs_property_list_item_count(p);
	for (size_t i = 0; i < num; i++) {
		const char *val = obs_property_list_item_string(p, i);
		if (strcmp(val, "ICQ") == 0) {
			icq_found = true;
			break;
		}
	}

	obs_properties_destroy(props);
	return icq_found;
}

static obs_data_t *UpdateRecordingSettings_qsv11(int crf, obs_encoder_t *encoder)
{
	bool icq = icq_available(encoder);
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "profile", "high");
	if (icq) {
		obs_data_set_string(settings, "rate_control", "ICQ");
		obs_data_set_int(settings, "icq_quality", crf);
	} else {
		obs_data_set_string(settings, "rate_control", "CQP");
		obs_data_set_int(settings, "qpi", crf);
		obs_data_set_int(settings, "qpp", crf);
		obs_data_set_int(settings, "qpb", crf);
	}
	return settings;
}

#define CROSS_DIST_CUTOFF 2000.0
static int CalcCRF(int crf, bool lowCPU = false)
{
	obs_video_info ovi = {0};
	obs_get_video_info(&ovi);
	uint64_t cx = ovi.output_width;
	uint64_t cy = ovi.output_height;
	double fCX = double(cx);
	double fCY = double(cy);

	if (lowCPU)
		crf -= 2;

	double crossDist = sqrt(fCX * fCX + fCY * fCY);
	double crfResReduction = fmin(CROSS_DIST_CUTOFF, crossDist) / CROSS_DIST_CUTOFF;
	crfResReduction = (1.0 - crfResReduction) * 10.0;

	return crf - int(crfResReduction);
}

static void UpdateRecordingSettings_crf(enum osn::RecQuality quality, osn::SimpleRecording *recording)
{
	std::string id = obs_encoder_get_id(recording->videoEncoder);
	bool ultra_hq = (quality == osn::RecQuality::HigherQuality);
	int crf = CalcCRF(ultra_hq ? 16 : 23);

	obs_data_t *settings = nullptr;
	if (id.compare(SIMPLE_ENCODER_X264) == 0 || id.compare(ADVANCED_ENCODER_X264) == 0 || id.compare(SIMPLE_ENCODER_X264_LOWCPU) == 0) {
		settings = UpdateRecordingSettings_x264_crf(CalcCRF(crf, recording->lowCPU), recording->lowCPU);
	} else if (id.compare(SIMPLE_ENCODER_NVENC) == 0 || id.compare(ADVANCED_ENCODER_NVENC) == 0 || id.compare(ENCODER_NEW_NVENC) == 0) {
		settings = UpdateRecordingSettings_nvenc(CalcCRF(crf));
	} else if (id.compare(SIMPLE_ENCODER_NVENC_HEVC) == 0) {
		settings = UpdateRecordingSettings_nvenc_hevc(CalcCRF(crf));
	} else if (id.compare(SIMPLE_ENCODER_QSV) == 0 || id.compare(ADVANCED_ENCODER_QSV) == 0) {
		settings = UpdateRecordingSettings_qsv11(CalcCRF(crf), recording->videoEncoder);
	} else if (id.compare(SIMPLE_ENCODER_AMD) == 0 || id.compare(SIMPLE_ENCODER_AMD_HEVC) == 0 || id.compare(ADVANCED_ENCODER_AMD) == 0) {
		settings = UpdateRecordingSettings_amd_cqp(CalcCRF(crf));
	} else if (id.compare(APPLE_SOFTWARE_VIDEO_ENCODER) == 0 || id.compare(APPLE_HARDWARE_VIDEO_ENCODER) == 0 ||
		   id.compare(APPLE_HARDWARE_VIDEO_ENCODER_M1) == 0) {
		/* These are magic numbers. 0 - 100, more is better. */
		UpdateRecordingSettings_apple(ultra_hq ? 70 : 50);
	} else {
		return;
	}

	if (!settings)
		return;
	if (obs_get_multiple_rendering()) {
		obs_encoder_set_video_mix(recording->videoEncoder, obs_video_mix_get(recording->canvas, OBS_STREAMING_VIDEO_RENDERING));
	} else {
		obs_encoder_set_video_mix(recording->videoEncoder, obs_video_mix_get(recording->canvas, OBS_MAIN_VIDEO_RENDERING));
	}
	obs_encoder_update(recording->videoEncoder, settings);
	obs_data_release(settings);
}

void osn::SimpleRecording::UpdateEncoders()
{
	if (videoEncoder && obs_encoder_active(videoEncoder))
		return;

	if (audioEncoder && obs_encoder_active(audioEncoder))
		return;

	switch (quality) {
	case RecQuality::Stream: {
		if (!streaming)
			return;
		streaming->UpdateEncoders();
		videoEncoder = streaming->videoEncoder;
		audioEncoder = streaming->audioEncoder;
		if (obs_get_multiple_rendering()) {
			obs_encoder_t *videoEncDup = osn::IRecording::duplicate_encoder(videoEncoder);
			videoEncoder = videoEncDup;
		}
		break;
	}
	case RecQuality::HighQuality: {
		UpdateRecordingSettings_crf(RecQuality::HighQuality, this);
		break;
	}
	case RecQuality::HigherQuality: {
		UpdateRecordingSettings_crf(RecQuality::HigherQuality, this);
		break;
	}
	default: {
		break;
	}
	}
}

void osn::ISimpleRecording::Start(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleRecording *recording = static_cast<SimpleRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}

	const char *ffmpegMuxer = "ffmpeg_muxer";
	if (!recording->output || strcmp(obs_output_get_id(recording->output), ffmpegMuxer) == 0)
		recording->createOutput("ffmpeg_muxer", "recording");

	if (!recording->output) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Error while creating the recording output.");
	}

	std::string format = recording->format;
	std::string pathProperty = "path";

	if (recording->quality == RecQuality::Lossless) {
		recording->createOutput("ffmpeg_output", "recording");
		LoadLosslessPreset(recording);
		format = "avi";
		pathProperty = "url";
	} else {
		recording->UpdateEncoders();

		if (!recording->videoEncoder) {
			PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid video encoder.");
		}

		if (!recording->audioEncoder) {
			PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid audio encoder.");
		}

		obs_encoder_set_audio(recording->audioEncoder, obs_get_audio());
		obs_output_set_audio_encoder(recording->output, recording->audioEncoder, 0);
		obs_encoder_set_video_mix(recording->audioEncoder, obs_video_mix_get(recording->canvas, OBS_RECORDING_VIDEO_RENDERING));

		obs_output_set_video_encoder(recording->output, recording->videoEncoder);
	}

	if (!recording->path.size()) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid recording path.");
	}

	std::string path = recording->path;

	char lastChar = path.back();
	if (lastChar != '/' && lastChar != '\\')
		path += "/";

	path += GenerateSpecifiedFilename(format, recording->noSpace, recording->fileFormat, recording->canvas->base_width, recording->canvas->base_height);

	if (!recording->overwrite)
		FindBestFilename(path, recording->noSpace);

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, pathProperty.c_str(), path.c_str());
	obs_data_set_string(settings, "muxer_settings", recording->muxerSettings.c_str());
	obs_output_update(recording->output, settings);
	obs_data_release(settings);

	if (recording->enableFileSplit)
		recording->ConfigureRecFileSplitting();

	recording->startOutput();

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::ISimpleRecording::Stop(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Recording *recording = static_cast<Recording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}
	if (!recording->output) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid recording output.");
	}

	obs_output_stop(recording->output);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::ISimpleRecording::GetLowCPU(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleRecording *recording = static_cast<SimpleRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)recording->lowCPU));
	AUTO_DEBUG;
}

void osn::ISimpleRecording::SetLowCPU(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleRecording *recording = static_cast<SimpleRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}

	recording->lowCPU = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

obs_encoder_t *osn::ISimpleRecording::GetLegacyVideoEncoderSettings()
{
	obs_encoder_t *videoEncoder = nullptr;

	std::string simpleQuality = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecQuality"));

	const char *encId = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecEncoder"));
	const char *encIdOBS = nullptr;
	if (strcmp(encId, SIMPLE_ENCODER_X264) == 0 || strcmp(encId, ADVANCED_ENCODER_X264) == 0) {
		encIdOBS = "obs_x264";
	} else if (strcmp(encId, SIMPLE_ENCODER_X264_LOWCPU) == 0) {
		encIdOBS = "obs_x264";
	} else if (strcmp(encId, SIMPLE_ENCODER_QSV) == 0 || strcmp(encId, ADVANCED_ENCODER_QSV) == 0) {
		encIdOBS = "obs_qsv11";
	} else if (strcmp(encId, SIMPLE_ENCODER_AMD) == 0 || strcmp(encId, ADVANCED_ENCODER_AMD) == 0) {
		encIdOBS = "h264_texture_amf";
	} else if (strcmp(encId, SIMPLE_ENCODER_NVENC) == 0 || strcmp(encId, ADVANCED_ENCODER_NVENC) == 0) {
		encIdOBS = "ffmpeg_nvenc";
	} else if (strcmp(encId, ENCODER_NEW_NVENC) == 0) {
		encIdOBS = "jim_nvenc";
	}

	if (simpleQuality.compare("Stream") != 0) {
		videoEncoder = obs_video_encoder_create(encIdOBS, "video-encoder", nullptr, nullptr);
	}

	return videoEncoder;
}

obs_encoder_t *osn::ISimpleRecording::GetLegacyAudioEncoderSettings()
{
	obs_data_t *audioEncSettings = obs_data_create();
	obs_data_set_int(audioEncSettings, "bitrate", 192); // Hardcoded default value
	obs_encoder_t *audioEncoder = obs_audio_encoder_create("ffmpeg_aac", "audio-encoder", audioEncSettings, 0, nullptr);
	obs_data_release(audioEncSettings);

	return audioEncoder;
}

void osn::ISimpleRecording::GetLegacySettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	osn::SimpleRecording *recording = new osn::SimpleRecording();

	std::string simpleQuality = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecQuality"));
	if (simpleQuality.compare("Stream") == 0) {
		recording->quality = RecQuality::Stream;
	} else if (simpleQuality.compare("Small") == 0) {
		recording->quality = RecQuality::HighQuality;
	} else if (simpleQuality.compare("HQ") == 0) {
		recording->quality = RecQuality::HigherQuality;
	} else if (simpleQuality.compare("Lossless") == 0) {
		recording->quality = RecQuality::Lossless;
	} else {
		recording->quality = RecQuality::Stream;
	}

	recording->path = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "FilePath"));
	recording->format = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecFormat"));
	recording->muxerSettings = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "MuxerCustom"));
	recording->noSpace = config_get_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "FileNameWithoutSpace");
	recording->fileFormat = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "Output", "FilenameFormatting"));
	recording->overwrite = config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "OverwriteIfExists");
	recording->muxerSettings = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "MuxerCustom"));

	const char *encId = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecEncoder"));
	recording->lowCPU = false;
	if (strcmp(encId, SIMPLE_ENCODER_X264_LOWCPU) == 0)
		recording->lowCPU = true;

	if (recording->quality != RecQuality::Stream) {
		recording->videoEncoder = GetLegacyVideoEncoderSettings();
		osn::VideoEncoder::Manager::GetInstance().allocate(recording->videoEncoder);
	}

	recording->audioEncoder = GetLegacyAudioEncoderSettings();
	osn::AudioEncoder::Manager::GetInstance().allocate(recording->audioEncoder);

	recording->enableFileSplit = config_get_bool(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFile");
	const char *splitFileType = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFileType");
	if (strcmp(splitFileType, "Time") == 0)
		recording->splitType = SplitFileType::TIME;
	else if (strcmp(splitFileType, "Size") == 0)
		recording->splitType = SplitFileType::SIZE;
	else
		recording->splitType = SplitFileType::MANUAL;

	recording->splitTime = config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFileTime");
	recording->splitSize = config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFileSize");
	recording->fileResetTimestamps = config_get_bool(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFileResetTimestamps");

	uint64_t uid = osn::ISimpleRecording::Manager::GetInstance().allocate(recording);
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::ISimpleRecording::GetStreaming(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleRecording *recording = static_cast<SimpleRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	uint64_t uid = osn::ISimpleStreaming::Manager::GetInstance().find(recording->streaming);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::ISimpleRecording::SetStreaming(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleRecording *recording = static_cast<SimpleRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	SimpleStreaming *streaming = static_cast<SimpleStreaming *>(osn::ISimpleStreaming::Manager::GetInstance().find(args[1].value_union.ui64));
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	recording->streaming = streaming;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::ISimpleRecording::SetLegacySettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleRecording *recording = static_cast<SimpleRecording *>(osn::ISimpleRecording::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	std::string recQuality = "";
	switch (recording->quality) {
	case RecQuality::Stream: {
		recQuality = "Stream";
		break;
	}
	case RecQuality::HighQuality: {
		recQuality = "Small";
		break;
	}
	case RecQuality::HigherQuality: {
		recQuality = "HQ";
		break;
	}
	case RecQuality::Lossless: {
		recQuality = "Lossless";
		break;
	}
	default: {
		recQuality = "Stream";
		break;
	}
	}
	config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecQuality", recQuality.c_str());

	config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "FilePath", recording->path.c_str());
	config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecFormat", recording->format.c_str());
	config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "MuxerCustom", recording->muxerSettings.c_str());
	config_set_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "FileNameWithoutSpace", recording->noSpace);
	config_set_string(ConfigManager::getInstance().getBasic(), "Output", "FilenameFormatting", recording->fileFormat.c_str());
	config_set_bool(ConfigManager::getInstance().getBasic(), "Output", "OverwriteIfExists", recording->overwrite);
	config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "MuxerCustom", recording->muxerSettings.c_str());

	if (recording->videoEncoder) {
		const char *encId = nullptr;
		const char *encIdOBS = obs_encoder_get_id(recording->videoEncoder);
		if (strcmp(encIdOBS, "obs_x264") == 0 && !recording->lowCPU) {
			encId = SIMPLE_ENCODER_X264;
		} else if (strcmp(encIdOBS, "obs_x264") == 0 && recording->lowCPU) {
			encId = SIMPLE_ENCODER_X264_LOWCPU;
		} else if (strcmp(encIdOBS, "obs_qsv11") == 0) {
			encId = SIMPLE_ENCODER_QSV;
		} else if (strcmp(encIdOBS, "h264_texture_amf") == 0) {
			encId = SIMPLE_ENCODER_AMD;
		} else if (strcmp(encIdOBS, "ffmpeg_nvenc") == 0) {
			encId = SIMPLE_ENCODER_NVENC;
		} else if (strcmp(encIdOBS, "jim_nvenc") == 0) {
			encId = ENCODER_NEW_NVENC;
		}

		config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecEncoder", encId);
	}

	config_set_bool(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFile", recording->enableFileSplit);

	switch (recording->splitType) {
	case SplitFileType::TIME: {
		config_set_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFileType", "Time");
		break;
	}
	case SplitFileType::SIZE: {
		config_set_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFileType", "Size");
		break;
	}
	default: {
		config_set_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFileType", "Manual");
		break;
	}
	}

	config_set_int(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFileTime", recording->splitTime);
	config_set_int(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFileSize", recording->splitSize);
	config_set_bool(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFileResetTimestamps", recording->fileResetTimestamps);

	config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}