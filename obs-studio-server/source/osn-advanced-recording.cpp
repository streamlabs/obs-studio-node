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

#include "osn-advanced-recording.hpp"
#include "osn-error.hpp"
#include "shared.hpp"
#include "osn-audio-track.hpp"
#include "osn-file-output.hpp"
#include <osn-encoders.hpp>

void osn::IAdvancedRecording::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("AdvancedRecording");
	cls->register_function(std::make_shared<ipc::function>("Create", std::vector<ipc::type>{}, Create));
	cls->register_function(std::make_shared<ipc::function>("Destroy", std::vector<ipc::type>{ipc::type::UInt64}, Destroy));
	cls->register_function(std::make_shared<ipc::function>("GetVideoEncoder", std::vector<ipc::type>{ipc::type::UInt64}, GetVideoEncoder));
	cls->register_function(
		std::make_shared<ipc::function>("SetVideoEncoder", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetVideoEncoder));
	cls->register_function(std::make_shared<ipc::function>("GetVideoCanvas", std::vector<ipc::type>{ipc::type::UInt64}, GetVideoCanvas));
	cls->register_function(std::make_shared<ipc::function>("SetVideoCanvas", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetVideoCanvas));
	cls->register_function(std::make_shared<ipc::function>("GetMixer", std::vector<ipc::type>{ipc::type::UInt64}, GetMixer));
	cls->register_function(std::make_shared<ipc::function>("SetMixer", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetMixer));
	cls->register_function(std::make_shared<ipc::function>("GetRescaling", std::vector<ipc::type>{ipc::type::UInt64}, GetRescaling));
	cls->register_function(std::make_shared<ipc::function>("SetRescaling", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetRescaling));
	cls->register_function(std::make_shared<ipc::function>("GetOutputWidth", std::vector<ipc::type>{ipc::type::UInt64}, GetOutputWidth));
	cls->register_function(std::make_shared<ipc::function>("SetOutputWidth", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetOutputWidth));
	cls->register_function(std::make_shared<ipc::function>("GetOutputHeight", std::vector<ipc::type>{ipc::type::UInt64}, GetOutputHeight));
	cls->register_function(
		std::make_shared<ipc::function>("SetOutputHeight", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetOutputHeight));
	cls->register_function(std::make_shared<ipc::function>("GetUseStreamEncoders", std::vector<ipc::type>{ipc::type::UInt64}, GetUseStreamEncoders));
	cls->register_function(
		std::make_shared<ipc::function>("SetUseStreamEncoders", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetUseStreamEncoders));
	cls->register_function(std::make_shared<ipc::function>("Start", std::vector<ipc::type>{ipc::type::UInt64}, Start));
	cls->register_function(std::make_shared<ipc::function>("Stop", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, Stop));
	cls->register_function(std::make_shared<ipc::function>("Query", std::vector<ipc::type>{ipc::type::UInt64}, Query));
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

void osn::IAdvancedRecording::Create(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	uint64_t uid = osn::IAdvancedRecording::Manager::GetInstance().allocate(new AdvancedRecording());
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::IAdvancedRecording::Destroy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AdvancedRecording *recording = static_cast<AdvancedRecording *>(osn::IAdvancedRecording::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	osn::IAdvancedRecording::Manager::GetInstance().free(recording);
	delete recording;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::AdvancedRecording::ClearAudioEncoders()
{
	if (GetOutput()) {
		for (int idx = 0; idx < MAX_AUDIO_MIXES; idx++)
			obs_output_set_audio_encoder(GetOutput(), nullptr, idx);
	}

	for (auto *encoder : audioEncoders) {
		if (!encoder)
			continue;

		if (obs_encoder_active(encoder)) {
			blog(LOG_WARNING, "AdvancedRecording audio encoder is still active during cleanup; releasing owner reference.");
		}

		obs_encoder_release(encoder);
	}

	audioEncoders.clear();
}

osn::AdvancedRecording::~AdvancedRecording()
{
	DeleteOutput();
	ClearAudioEncoders();
}

void osn::IAdvancedRecording::GetRescaling(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AdvancedRecording *recording = static_cast<AdvancedRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(recording->rescaling));
	AUTO_DEBUG;
}

void osn::IAdvancedRecording::SetRescaling(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AdvancedRecording *recording = static_cast<AdvancedRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}

	recording->rescaling = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IAdvancedRecording::GetOutputWidth(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AdvancedRecording *recording = static_cast<AdvancedRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(recording->outputWidth));
	AUTO_DEBUG;
}

void osn::IAdvancedRecording::SetOutputWidth(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AdvancedRecording *recording = static_cast<AdvancedRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}

	recording->outputWidth = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IAdvancedRecording::GetOutputHeight(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AdvancedRecording *recording = static_cast<AdvancedRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(recording->outputHeight));
	AUTO_DEBUG;
}

void osn::IAdvancedRecording::SetOutputHeight(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AdvancedRecording *recording = static_cast<AdvancedRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}

	recording->outputHeight = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

bool osn::AdvancedRecording::UpdateEncoders()
{
	if (useStreamEncoders) {
		if (!streaming)
			return false;
		streaming->UpdateEncoders();
		videoEncoder = streaming->videoEncoder;

		if (obs_get_multiple_rendering()) {
			videoEncoder = osn::IRecording::duplicate_encoder(videoEncoder);
		}
	}

	if (!videoEncoder)
		return false;

	if (obs_get_multiple_rendering()) {
		obs_encoder_set_video_mix(videoEncoder, obs_video_mix_get(this->GetCanvas(), OBS_RECORDING_VIDEO_RENDERING));
	} else {
		obs_encoder_set_video_mix(videoEncoder, obs_video_mix_get(this->GetCanvas(), OBS_MAIN_VIDEO_RENDERING));
	}

	return true;
}

void osn::IAdvancedRecording::Start(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AdvancedRecording *recording = static_cast<AdvancedRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}

	if (!recording->GetOutput())
		recording->CreateOutput("ffmpeg_muxer", "recording");

	if (!recording->GetOutput()) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Error while creating the recording output.");
	}

	recording->ClearAudioEncoders();

	int outputEncoderIndex = 0;
	for (int mixerIndex = 0; mixerIndex < MAX_AUDIO_MIXES; mixerIndex++) {
		if ((recording->mixer & (1 << mixerIndex)) == 0)
			continue;

		const uint32_t trackNumber = mixerIndex + 1;
		std::string encoderName = "audio-encoder-recording-track";
		encoderName += std::to_string(trackNumber);
		obs_encoder_t *audioEncoder = osn::IAudioTrack::CreateEncoderForTrack(trackNumber, encoderName);
		if (!audioEncoder)
			continue;

		recording->audioEncoders.push_back(audioEncoder);
		obs_encoder_set_audio(audioEncoder, obs_get_audio());
		obs_output_set_audio_encoder(recording->GetOutput(), audioEncoder, outputEncoderIndex);
		obs_encoder_set_video_mix(audioEncoder, obs_video_mix_get(recording->GetCanvas(), OBS_RECORDING_VIDEO_RENDERING));
		outputEncoderIndex++;
	}

	if (!recording->UpdateEncoders() || !recording->videoEncoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid video encoder.");
	}

	if (!osn::EncoderUtils::isEncoderCompatibleRecording(obs_encoder_get_id(recording->videoEncoder), recording->format, false)) {
		//update config recording format = mkv because it supports all encoder types
		config_set_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecFormat", "mkv");
		config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "The specified video encoder is not valid for recording.");
	}

	uint32_t cx = 0;
	uint32_t cy = 0;
	if (recording->rescaling && recording->outputWidth > 0 && recording->outputHeight > 0) {
		cx = recording->outputWidth;
		cy = recording->outputHeight;
	}
	obs_encoder_set_scaled_size(recording->videoEncoder, cx, cy);
	obs_encoder_set_gpu_scale_type(recording->videoEncoder, recording->rescaling ? OBS_SCALE_BILINEAR : OBS_SCALE_DISABLE);

	obs_output_set_video_encoder(recording->GetOutput(), recording->videoEncoder);

	std::string path = recording->path;

	char lastChar = path.back();
	if (lastChar != '/' && lastChar != '\\')
		path += "/";

	path += GenerateSpecifiedFilename(recording->format, recording->noSpace, recording->fileFormat, recording->GetCanvas());

	if (!recording->overwrite)
		FindBestFilename(path, recording->noSpace);

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "path", path.c_str());
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

void osn::IAdvancedRecording::Stop(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AdvancedRecording *recording = static_cast<AdvancedRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}

	obs_output_stop(recording->GetOutput());

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IAdvancedRecording::GetMixer(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AdvancedRecording *recording = static_cast<AdvancedRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(recording->mixer));
	AUTO_DEBUG;
}

void osn::IAdvancedRecording::SetMixer(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AdvancedRecording *recording = static_cast<AdvancedRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}

	recording->mixer = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IAdvancedRecording::GetUseStreamEncoders(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AdvancedRecording *recording = static_cast<AdvancedRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(recording->useStreamEncoders));
	AUTO_DEBUG;
}

void osn::IAdvancedRecording::SetUseStreamEncoders(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AdvancedRecording *recording = static_cast<AdvancedRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}

	recording->useStreamEncoders = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IAdvancedRecording::GetLegacySettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	osn::AdvancedRecording *recording = new osn::AdvancedRecording();

	recording->path = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecFilePath"));
	recording->format = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecFormat"));
	recording->muxerSettings = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecMuxerCustom"));
	recording->noSpace = config_get_bool(ConfigManager::getInstance().getBasic(), "AdvOut", "RecFileNameWithoutSpace");
	recording->fileFormat = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "Output", "FilenameFormatting"));
	recording->overwrite = config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "OverwriteIfExists");
	recording->muxerSettings = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecMuxerCustom"));

	recording->rescaling = config_get_bool(ConfigManager::getInstance().getBasic(), "AdvOut", "RecRescale");
	const char *rescaleRes = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecRescaleRes"));
	unsigned int cx = 0;
	unsigned int cy = 0;
	if (recording->rescaling && rescaleRes) {
		if (sscanf(rescaleRes, "%ux%u", &cx, &cy) != 2) {
			cx = 0;
			cy = 0;
		}
		recording->outputWidth = cx;
		recording->outputHeight = cy;
	}

	recording->mixer = static_cast<uint32_t>(config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "RecTracks"));

	std::string encId = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecEncoder"));
	recording->useStreamEncoders = encId.compare("") == 0 || encId.compare("none") == 0;

	if (!recording->useStreamEncoders) {
		obs_data_t *existingVideoEncSettings = obs_data_create_from_json_file_safe(ConfigManager::getInstance().getRecord().c_str(), "bak");
		obs_data_t *newSettings = obs_encoder_defaults(encId.c_str());

		//old API gets defaults, reads recordEncoder.json if exists, converts if it does, then creates - need to handle null settings from missing config file
		if (existingVideoEncSettings != nullptr) {
			osn::EncoderUtils::updateNvencPresets(existingVideoEncSettings, encId.c_str());
			obs_data_apply(newSettings, existingVideoEncSettings);
		}
		recording->videoEncoder = obs_video_encoder_create(encId.c_str(), "video-encoder", newSettings, nullptr);
		osn::VideoEncoder::Manager::GetInstance().allocate(recording->videoEncoder);
	}

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

	uint64_t uid = osn::IAdvancedRecording::Manager::GetInstance().allocate(recording);
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::IAdvancedRecording::SetLegacySettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AdvancedRecording *recording = static_cast<AdvancedRecording *>(osn::IAdvancedRecording::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	config_set_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecFilePath", recording->path.c_str());
	config_set_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecFormat", recording->format.c_str());
	config_set_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecMuxerCustom", recording->muxerSettings.c_str());
	config_set_bool(ConfigManager::getInstance().getBasic(), "AdvOut", "RecFileNameWithoutSpace", recording->noSpace);
	config_set_string(ConfigManager::getInstance().getBasic(), "Output", "FilenameFormatting", recording->fileFormat.c_str());
	config_set_bool(ConfigManager::getInstance().getBasic(), "Output", "OverwriteIfExists", recording->overwrite);
	config_set_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecMuxerCustom", recording->muxerSettings.c_str());

	config_set_bool(ConfigManager::getInstance().getBasic(), "AdvOut", "RecRescale", recording->rescaling);
	std::string rescaledRes = std::to_string(recording->outputWidth);
	rescaledRes += 'x';
	rescaledRes += std::to_string(recording->outputHeight);
	config_set_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecRescaleRes", rescaledRes.c_str());

	config_set_int(ConfigManager::getInstance().getBasic(), "AdvOut", "RecTracks", recording->mixer);

	if (recording->useStreamEncoders) {
		config_set_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecEncoder", "none");
	} else if (!recording->useStreamEncoders && recording->videoEncoder) {
		config_set_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecEncoder", obs_encoder_get_id(recording->videoEncoder));

		obs_data_t *settings = obs_encoder_get_settings(recording->videoEncoder);

		if (!obs_data_save_json_safe(settings, ConfigManager::getInstance().getRecord().c_str(), "tmp", "bak")) {
			blog(LOG_ERROR, "Failed to save encoder %s", ConfigManager::getInstance().getStream().c_str());
		}
		obs_data_release(settings);
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

void osn::IAdvancedRecording::GetStreaming(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	blog(LOG_WARNING, "Function %s is deprecated", __func__);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IAdvancedRecording::SetStreaming(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AdvancedRecording *recording = static_cast<AdvancedRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	if (args[1].value_union.ui64 == UINT64_MAX) {
		recording->streaming = nullptr;
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		AUTO_DEBUG;
		return;
	}

	AdvancedStreaming *streaming = static_cast<AdvancedStreaming *>(osn::IAdvancedStreaming::Manager::GetInstance().find(args[1].value_union.ui64));
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	recording->streaming = streaming;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IAdvancedRecording::GetAvailableEncoders(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AdvancedRecording *recording = static_cast<AdvancedRecording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Advanced recording reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	osn::EncoderUtils::getAvailableEncoders(rval, nullptr, false, true, recording->format);
	AUTO_DEBUG;
}
