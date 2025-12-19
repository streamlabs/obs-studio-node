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

#include "osn-simple-replay-buffer.hpp"
#include "osn-audio-encoder.hpp"
#include "osn-error.hpp"
#include "shared.hpp"
#include "nodeobs_audio_encoders.h"

void osn::ISimpleReplayBuffer::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("SimpleReplayBuffer");
	cls->register_function(std::make_shared<ipc::function>("Create", std::vector<ipc::type>{}, Create));
	cls->register_function(std::make_shared<ipc::function>("Destroy", std::vector<ipc::type>{ipc::type::UInt64}, Destroy));
	cls->register_function(std::make_shared<ipc::function>("GetDuration", std::vector<ipc::type>{ipc::type::UInt64}, GetDuration));
	cls->register_function(std::make_shared<ipc::function>("SetDuration", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetDuration));
	cls->register_function(std::make_shared<ipc::function>("GetPrefix", std::vector<ipc::type>{ipc::type::UInt64}, GetPrefix));
	cls->register_function(std::make_shared<ipc::function>("SetPrefix", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String}, SetPrefix));
	cls->register_function(std::make_shared<ipc::function>("GetSuffix", std::vector<ipc::type>{ipc::type::UInt64}, GetSuffix));
	cls->register_function(std::make_shared<ipc::function>("SetSuffix", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String}, SetSuffix));
	cls->register_function(std::make_shared<ipc::function>("GetUsesStream", std::vector<ipc::type>{ipc::type::UInt64}, GetUsesStream));
	cls->register_function(std::make_shared<ipc::function>("SetUsesStream", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetUsesStream));
	cls->register_function(std::make_shared<ipc::function>("Save", std::vector<ipc::type>{ipc::type::UInt64}, Save));
	cls->register_function(std::make_shared<ipc::function>("Start", std::vector<ipc::type>{ipc::type::UInt64}, Start));
	cls->register_function(std::make_shared<ipc::function>("Stop", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, Stop));
	cls->register_function(std::make_shared<ipc::function>("Query", std::vector<ipc::type>{ipc::type::UInt64}, Query));
	cls->register_function(std::make_shared<ipc::function>("GetLegacySettings", std::vector<ipc::type>{}, GetLegacySettings));
	cls->register_function(std::make_shared<ipc::function>("SetLegacySettings", std::vector<ipc::type>{ipc::type::UInt64}, SetLegacySettings));
	cls->register_function(std::make_shared<ipc::function>("GetStreaming", std::vector<ipc::type>{ipc::type::UInt64}, GetStreaming));
	cls->register_function(std::make_shared<ipc::function>("SetStreaming", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetStreaming));
	cls->register_function(std::make_shared<ipc::function>("GetRecording", std::vector<ipc::type>{ipc::type::UInt64}, GetRecording));
	cls->register_function(std::make_shared<ipc::function>("SetRecording", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetRecording));

	srv.register_collection(cls);
}

void osn::ISimpleReplayBuffer::Create(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	uint64_t uid = osn::ISimpleReplayBuffer::Manager::GetInstance().allocate(new SimpleReplayBuffer());
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::ISimpleReplayBuffer::Destroy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleReplayBuffer *replayBuffer = static_cast<SimpleReplayBuffer *>(osn::ISimpleReplayBuffer::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!replayBuffer) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Replay buffer reference is not valid.");
	}

	osn::ISimpleReplayBuffer::Manager::GetInstance().free(replayBuffer);
	delete replayBuffer;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

static void remove_reserved_file_characters(std::string &s)
{
	replace(s.begin(), s.end(), '/', '_');
	replace(s.begin(), s.end(), '\\', '_');
	replace(s.begin(), s.end(), '*', '_');
	replace(s.begin(), s.end(), '?', '_');
	replace(s.begin(), s.end(), '"', '_');
	replace(s.begin(), s.end(), '|', '_');
	replace(s.begin(), s.end(), ':', '_');
	replace(s.begin(), s.end(), '>', '_');
	replace(s.begin(), s.end(), '<', '_');
}

void osn::ISimpleReplayBuffer::Start(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleReplayBuffer *replayBuffer = static_cast<SimpleReplayBuffer *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!replayBuffer) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple replay buffer reference is not valid.");
	}

	if (!replayBuffer->GetOutput())
		replayBuffer->CreateOutput("replay_buffer", "replay-buffer");

	obs_encoder_t *audioEncoder = nullptr;
	obs_encoder_t *videoEncoder = nullptr;

	if (obs_get_multiple_rendering() && replayBuffer->usesStream) {
		if (!replayBuffer->streaming)
			return;
		replayBuffer->streaming->UpdateEncoders();
		audioEncoder = replayBuffer->streaming->audioEncoder;
		videoEncoder = replayBuffer->streaming->videoEncoder;
	} else {
		if (!replayBuffer->recording)
			return;
		replayBuffer->recording->UpdateEncoders();
		audioEncoder = replayBuffer->recording->audioEncoder;
		videoEncoder = replayBuffer->recording->videoEncoder;
	}

	if (!audioEncoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid audio encoder.");
	}

	obs_encoder_set_audio(audioEncoder, obs_get_audio());
	obs_output_set_audio_encoder(replayBuffer->GetOutput(), audioEncoder, 0);

	if (!videoEncoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid video encoder.");
	}

	obs_output_set_video_encoder(replayBuffer->GetOutput(), videoEncoder);

	if (!replayBuffer->path.size()) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid recording path.");
	}

	const char *rbPrefix = replayBuffer->prefix.c_str();
	const char *rbSuffix = replayBuffer->suffix.c_str();
	int64_t rbSize = replayBuffer->usesStream ? 0 : 512;

	std::string f;
	if (rbPrefix && *rbPrefix) {
		f += rbPrefix;
		if (f.back() != ' ')
			f += " ";
	}
	f += replayBuffer->fileFormat.c_str();
	if (rbSuffix && *rbSuffix) {
		if (*rbSuffix != ' ')
			f += " ";
		f += rbSuffix;
	}
	remove_reserved_file_characters(f);

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "directory", replayBuffer->path.c_str());
	obs_data_set_string(settings, "format", f.c_str());
	obs_data_set_string(settings, "extension", replayBuffer->format.c_str());
	obs_data_set_bool(settings, "allow_spaces", !replayBuffer->noSpace);
	obs_data_set_int(settings, "max_time_sec", replayBuffer->duration);
	obs_data_set_int(settings, "max_size_mb", rbSize);
	obs_output_update(replayBuffer->GetOutput(), settings);
	obs_data_release(settings);

	replayBuffer->StartOutput();

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::ISimpleReplayBuffer::Stop(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleReplayBuffer *replayBuffer = static_cast<SimpleReplayBuffer *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!replayBuffer) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple replay buffer reference is not valid.");
	}
	if (!replayBuffer->GetOutput()) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid replay buffer output.");
	}

	obs_output_stop(replayBuffer->GetOutput());

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::ISimpleReplayBuffer::GetLegacySettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	osn::SimpleReplayBuffer *replayBuffer = new osn::SimpleReplayBuffer();

	replayBuffer->path = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "FilePath"));
	replayBuffer->format = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecFormat"));
	replayBuffer->muxerSettings = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "MuxerCustom"));
	replayBuffer->noSpace = config_get_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "FileNameWithoutSpace");
	replayBuffer->fileFormat = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "Output", "FilenameFormatting"));
	replayBuffer->overwrite = config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "OverwriteIfExists");

	replayBuffer->prefix = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecRBPrefix"));
	replayBuffer->suffix = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecRBSuffix"));
	replayBuffer->duration = static_cast<uint32_t>(config_get_int(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecRBTime"));

	replayBuffer->usesStream = config_get_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "replayBufferUseStreamOutput");

	uint64_t uid = osn::ISimpleReplayBuffer::Manager::GetInstance().allocate(replayBuffer);
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::ISimpleReplayBuffer::SetLegacySettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleReplayBuffer *replayBuffer = static_cast<SimpleReplayBuffer *>(osn::ISimpleReplayBuffer::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!replayBuffer) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Replay buffer reference is not valid.");
	}

	config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "FilePath", replayBuffer->path.c_str());
	config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecFormat", replayBuffer->format.c_str());
	config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "MuxerCustom", replayBuffer->muxerSettings.c_str());
	config_set_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "FileNameWithoutSpace", replayBuffer->noSpace);
	config_set_string(ConfigManager::getInstance().getBasic(), "Output", "FilenameFormatting", replayBuffer->fileFormat.c_str());
	config_set_bool(ConfigManager::getInstance().getBasic(), "Output", "OverwriteIfExists", replayBuffer->overwrite);
	config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecRBPrefix", replayBuffer->prefix.c_str());
	config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecRBSuffix", replayBuffer->suffix.c_str());
	config_set_int(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecRBTime", replayBuffer->duration);
	config_set_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "replayBufferUseStreamOutput", replayBuffer->usesStream);

	config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::ISimpleReplayBuffer::GetStreaming(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleReplayBuffer *replayBuffer = static_cast<SimpleReplayBuffer *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!replayBuffer) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple replay buffer reference is not valid.");
	}

	uint64_t uid = osn::ISimpleStreaming::Manager::GetInstance().find(replayBuffer->streaming);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::ISimpleReplayBuffer::SetStreaming(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleReplayBuffer *replayBuffer = static_cast<SimpleReplayBuffer *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!replayBuffer) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple replay buffer reference is not valid.");
	}

	SimpleStreaming *streaming = static_cast<SimpleStreaming *>(osn::ISimpleStreaming::Manager::GetInstance().find(args[1].value_union.ui64));
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	replayBuffer->streaming = streaming;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::ISimpleReplayBuffer::GetRecording(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleReplayBuffer *replayBuffer = static_cast<SimpleReplayBuffer *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!replayBuffer) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple replay buffer reference is not valid.");
	}

	uint64_t uid = osn::ISimpleRecording::Manager::GetInstance().find(replayBuffer->recording);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::ISimpleReplayBuffer::SetRecording(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleReplayBuffer *replayBuffer = static_cast<SimpleReplayBuffer *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!replayBuffer) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple replay buffer reference is not valid.");
	}

	SimpleRecording *recording = static_cast<SimpleRecording *>(osn::ISimpleRecording::Manager::GetInstance().find(args[1].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	replayBuffer->recording = recording;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}