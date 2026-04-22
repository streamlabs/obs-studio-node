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
#include "osn-encoders.hpp"

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
	cls->register_function(std::make_shared<ipc::function>("GetAvailableEncoders", std::vector<ipc::type>{ipc::type::UInt64}, GetAvailableEncoders));

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
	blog(LOG_WARNING, "Function %s is deprecated", __func__);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::ISimpleRecording::SetAudioEncoder(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleRecording *recording = static_cast<SimpleRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	if (args[1].value_union.ui64 == UINT64_MAX) {
		recording->audioEncoder = nullptr;
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		AUTO_DEBUG;
		return;
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

	obs_output_set_mixers(recording->GetOutput(), 1);
	obs_output_update(recording->GetOutput(), settings);
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
	std::string encFamily = osn::EncoderUtils::getEncoderFamily(id.c_str());

	if (encFamily == FAMILY_OBS)
		settings = UpdateRecordingSettings_x264_crf(crf, recording->lowCPU);
	else if (encFamily == FAMILY_NVENC)
		settings = UpdateRecordingSettings_nvenc(crf);
	else if (encFamily == FAMILY_NVENC_HEVC)
		settings = UpdateRecordingSettings_nvenc_hevc(crf);
	else if (encFamily == FAMILY_QSV)
		settings = UpdateRecordingSettings_qsv11(crf, recording->videoEncoder);
	else if (encFamily == FAMILY_AMD)
		settings = UpdateRecordingSettings_amd_cqp(crf);
	else if (encFamily == FAMILY_APPLE)
		settings = UpdateRecordingSettings_apple(ultra_hq ? 70 : 50);
	else
		blog(LOG_WARNING, "Unable to update settings with unknown encoder family.");

	if (!settings)
		return;
	if (obs_get_multiple_rendering()) {
		obs_encoder_set_video_mix(recording->videoEncoder, obs_video_mix_get(recording->GetCanvas(), OBS_STREAMING_VIDEO_RENDERING));
	} else {
		obs_encoder_set_video_mix(recording->videoEncoder, obs_video_mix_get(recording->GetCanvas(), OBS_MAIN_VIDEO_RENDERING));
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
			obs_encoder_set_video_mix(videoEncoder, obs_video_mix_get(this->GetCanvas(), OBS_RECORDING_VIDEO_RENDERING));
		}
		break;
	}
	case RecQuality::HighQuality: {
		if (!videoEncoder)
			return;
		UpdateRecordingSettings_crf(RecQuality::HighQuality, this);
		break;
	}
	case RecQuality::HigherQuality: {
		if (!videoEncoder)
			return;
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
	if (!recording->GetOutput() || strcmp(obs_output_get_id(recording->GetOutput()), ffmpegMuxer) == 0)
		recording->CreateOutput("ffmpeg_muxer", "recording");

	if (!recording->GetOutput()) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Error while creating the recording output.");
	}

	std::string format = recording->format;
	std::string pathProperty = "path";

	if (recording->quality == RecQuality::Lossless) {
		recording->CreateOutput("ffmpeg_output", "recording");
		LoadLosslessPreset(recording);
		format = "avi";
		pathProperty = "url";
	} else {
		recording->UpdateEncoders();

		if (!recording->videoEncoder) {
			PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid video encoder.");
		}

		//verify the encoder is compatible before setting it - need config ID for simple mode in order to find correct settings
		const char *encID = "";
		const char *quality = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecQuality"));
		if (strcmp(quality, "Stream") == 0)
			encID = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "StreamEncoder"));
		else
			encID = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecEncoder"));
		if (!osn::EncoderUtils::isEncoderCompatibleRecording(encID, recording->format, true)) {
			//update config recording format = mkv because it supports all encoder types
			config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecFormat", "mkv");
			config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);
			PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "The specified video encoder is not valid for recording.");
		}

		if (!recording->audioEncoder) {
			PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid audio encoder.");
		}

		obs_encoder_set_audio(recording->audioEncoder, obs_get_audio());
		obs_output_set_audio_encoder(recording->GetOutput(), recording->audioEncoder, 0);
		obs_encoder_set_video_mix(recording->audioEncoder, obs_video_mix_get(recording->GetCanvas(), OBS_RECORDING_VIDEO_RENDERING));

		obs_output_set_video_encoder(recording->GetOutput(), recording->videoEncoder);
	}

	if (!recording->path.size()) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid recording path.");
	}

	std::string path = recording->path;

	char lastChar = path.back();
	if (lastChar != '/' && lastChar != '\\')
		path += "/";

	path += GenerateSpecifiedFilename(format, recording->noSpace, recording->fileFormat, recording->GetCanvas());

	if (!recording->overwrite)
		FindBestFilename(path, recording->noSpace);

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, pathProperty.c_str(), path.c_str());
	obs_data_set_string(settings, "muxer_settings", recording->muxerSettings.c_str());
	obs_output_update(recording->GetOutput(), settings);
	obs_data_release(settings);

	if (recording->enableFileSplit)
		recording->ConfigureRecFileSplitting();

	blog(LOG_INFO, "Start Recording using %s encoder.", obs_encoder_get_id(recording->videoEncoder));

	recording->StartOutput();

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::ISimpleRecording::Stop(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Recording *recording = static_cast<Recording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}
	if (!recording->GetOutput()) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid recording output.");
	}

	obs_output_stop(recording->GetOutput());

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

obs_encoder_t *osn::ISimpleRecording::CreateLegacyVideoEncoder()
{
	osn::EncoderUtils::convertOldJimNvencEncoder(ConfigManager::getInstance().getBasic(), "SimpleOutput", "StreamEncoder", "RecEncoder");

	obs_encoder_t *videoEncoder = nullptr;

	std::string simpleQuality = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecQuality"));

	const char *encId = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecEncoder"));
	std::string encIdOBS = osn::EncoderUtils::getInternalEncoderFromSimple(encId);

	videoEncoder = obs_video_encoder_create(encIdOBS.c_str(), "video-encoder", nullptr, nullptr);
	osn::VideoEncoder::Manager::GetInstance().allocate(videoEncoder);

	return videoEncoder;
}

obs_encoder_t *osn::ISimpleRecording::CreateLegacyAudioEncoder()
{
	obs_data_t *audioEncSettings = obs_data_create();
	obs_data_set_int(audioEncSettings, "bitrate", 192); // Hardcoded default value
	obs_encoder_t *audioEncoder = obs_audio_encoder_create("ffmpeg_aac", "audio-encoder", audioEncSettings, 0, nullptr);
	obs_data_release(audioEncSettings);

	osn::AudioEncoder::Manager::GetInstance().allocate(audioEncoder);

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
		recording->videoEncoder = CreateLegacyVideoEncoder();
	}

	recording->audioEncoder = CreateLegacyAudioEncoder();

	recording->enableFileSplit = config_get_bool(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFile");
	const char *splitFileType = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFileType");
	if (strcmp(splitFileType, "Time") == 0)
		recording->splitType = SplitFileType::TIME;
	else if (strcmp(splitFileType, "Size") == 0)
		recording->splitType = SplitFileType::SIZE;
	else
		recording->splitType = SplitFileType::MANUAL;

	recording->splitTime = static_cast<uint32_t>(config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFileTime"));
	recording->splitSize = static_cast<uint32_t>(config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFileSize"));
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
	blog(LOG_WARNING, "Function %s is deprecated", __func__);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::ISimpleRecording::SetStreaming(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleRecording *recording = static_cast<SimpleRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	if (args[1].value_union.ui64 == UINT64_MAX) {
		recording->streaming = nullptr;
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		AUTO_DEBUG;
		return;
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

	//don't save the encoder if using the streaming encoder, set it to empty string in that case
	if (recording->quality != RecQuality::Stream && recording->videoEncoder) {
		const char *encIdOBS = obs_encoder_get_id(recording->videoEncoder);
		std::string encId = osn::EncoderUtils::getSimpleEncoderFromInternal(encIdOBS);
		if (encId == SIMPLE_ENCODER_X264 && recording->lowCPU) {
			encId = SIMPLE_ENCODER_X264_LOWCPU;
		}
		config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecEncoder", encId.c_str());
	} else {
		config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecEncoder", "");
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

void osn::ISimpleRecording::GetAvailableEncoders(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleRecording *recording = static_cast<SimpleRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	osn::EncoderUtils::getAvailableEncoders(rval, nullptr, true, true, recording->format);
	AUTO_DEBUG;
}
