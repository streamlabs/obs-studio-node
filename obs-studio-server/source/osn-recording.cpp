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

#include "osn-recording.hpp"
#include "osn-video-encoder.hpp"
#include "osn-audio-encoder.hpp"
#include "osn-error.hpp"
#include "shared.hpp"
#include "util/platform.h"
#include "osn-encoders.hpp"

extern char *osn_generate_formatted_filename(const char *extension, bool space, const char *format, int width, int height);

osn::Recording::~Recording()
{
	deleteOutput();
}

void osn::IRecording::GetVideoEncoder(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Recording *recording = static_cast<Recording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}

	uint64_t uid = osn::VideoEncoder::Manager::GetInstance().find(recording->videoEncoder);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::IRecording::SetVideoEncoder(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Recording *recording = static_cast<Recording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple recording reference is not valid.");
	}

	obs_encoder_t *encoder = osn::VideoEncoder::Manager::GetInstance().find(args[1].value_union.ui64);
	if (!encoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Encoder reference is not valid.");
	}

	//verify the encoder is compatible before setting it
	if (!osn::EncoderUtils::isEncoderCompatibleRecording(obs_encoder_get_id(encoder), recording->format, recording->simple)) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "The specified video encoder is not valid for recording.");
	}

	recording->videoEncoder = encoder;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IRecording::Query(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Recording *recording = static_cast<Recording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	std::unique_lock<std::mutex> ulock(recording->signalsMtx);
	if (recording->signalsReceived.empty()) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		AUTO_DEBUG;
		return;
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

	auto signal = recording->signalsReceived.front();
	rval.push_back(ipc::value("recording"));
	rval.push_back(ipc::value(signal.signal));
	rval.push_back(ipc::value(signal.code));
	rval.push_back(ipc::value(signal.errorMessage));

	recording->signalsReceived.pop();

	AUTO_DEBUG;
}

std::string osn::IRecording::GenerateSpecifiedFilename(const std::string &extension, bool noSpace, const std::string &format, int width, int height)
{
	char *filename = osn_generate_formatted_filename(extension.c_str(), !noSpace, format.c_str(), width, height);
	if (filename == nullptr) {
		throw "Invalid filename";
	}

	std::string result(filename);

	bfree(filename);

	return result;
}

void osn::IRecording::FindBestFilename(std::string &strPath, bool noSpace)
{
	int num = 2;

	if (!os_file_exists(strPath.c_str()))
		return;

	const char *ext = strrchr(strPath.c_str(), '.');
	if (!ext)
		return;

	int extStart = int(ext - strPath.c_str());
	for (;;) {
		std::string testPath = strPath;
		std::string numStr;

		numStr = noSpace ? "_" : " (";
		numStr += std::to_string(num++);
		if (!noSpace)
			numStr += ")";

		testPath.insert(extStart, numStr);

		if (!os_file_exists(testPath.c_str())) {
			strPath = testPath;
			break;
		}
	}
}

obs_encoder_t *osn::IRecording::duplicate_encoder(obs_encoder_t *src, uint64_t trackIndex)
{
	if (!src)
		return nullptr;

	obs_encoder_t *dst = nullptr;
	std::string name = obs_encoder_get_name(src);
	name += "-duplicate";

	if (obs_encoder_get_type(src) == OBS_ENCODER_AUDIO) {
		dst = obs_audio_encoder_create(obs_encoder_get_id(src), name.c_str(), obs_encoder_get_settings(src), trackIndex, nullptr);
		//this allows shutdown code in nodeobs_api to release the object
		osn::AudioEncoder::Manager::GetInstance().allocate(dst);
	} else if (obs_encoder_get_type(src) == OBS_ENCODER_VIDEO) {
		dst = obs_video_encoder_create(obs_encoder_get_id(src), name.c_str(), obs_encoder_get_settings(src), nullptr);
		//this allows shutdown code in nodeobs_api to release the object
		osn::VideoEncoder::Manager::GetInstance().allocate(dst);
	}

	return dst;
}

void osn::IRecording::SplitFile(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Recording *recording = static_cast<Recording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording || !recording->output) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	calldata_t cd = {0};

	proc_handler_t *ph = obs_output_get_proc_handler(recording->output);
	proc_handler_call(ph, "split_file", &cd);
	calldata_free(&cd);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IRecording::GetEnableFileSplit(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Recording *recording = static_cast<Recording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)recording->enableFileSplit));
	AUTO_DEBUG;
}

void osn::IRecording::SetEnableFileSplit(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Recording *recording = static_cast<Recording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	recording->enableFileSplit = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IRecording::GetSplitType(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Recording *recording = static_cast<Recording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)recording->splitType));
	AUTO_DEBUG;
}

void osn::IRecording::SetSplitType(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Recording *recording = static_cast<Recording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	recording->splitType = (enum SplitFileType)args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IRecording::GetSplitTime(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Recording *recording = static_cast<Recording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)recording->splitTime));
	AUTO_DEBUG;
}

void osn::IRecording::SetSplitTime(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Recording *recording = static_cast<Recording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	recording->splitTime = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IRecording::GetSplitSize(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Recording *recording = static_cast<Recording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)recording->splitSize));
	AUTO_DEBUG;
}

void osn::IRecording::SetSplitSize(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Recording *recording = static_cast<Recording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	recording->splitSize = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IRecording::GetFileResetTimestamps(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Recording *recording = static_cast<Recording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)recording->fileResetTimestamps));
	AUTO_DEBUG;
}

void osn::IRecording::SetFileResetTimestamps(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Recording *recording = static_cast<Recording *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!recording) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Recording reference is not valid.");
	}

	recording->fileResetTimestamps = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Recording::ConfigureRecFileSplitting()
{
	obs_data_t *settings = obs_data_create();

	if (splitType == SplitFileType::TIME)
		obs_data_set_int(settings, "max_time_sec", splitTime); // * 60);
	else if (splitType == SplitFileType::SIZE)
		obs_data_set_int(settings, "max_size_mb", splitSize);

	obs_data_set_string(settings, "directory", path.c_str());
	obs_data_set_string(settings, "format", fileFormat.c_str());
	obs_data_set_string(settings, "extension", format.c_str());
	obs_data_set_bool(settings, "allow_spaces", !noSpace);
	obs_data_set_bool(settings, "allow_overwrite", overwrite);
	obs_data_set_bool(settings, "split_file", true);

	obs_data_set_bool(settings, "reset_timestamps", fileResetTimestamps);

	obs_output_update(output, settings);
	obs_data_release(settings);
}
