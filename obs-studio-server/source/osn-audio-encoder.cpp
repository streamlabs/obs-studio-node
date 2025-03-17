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

#include "osn-audio-encoder.hpp"
#include "osn-error.hpp"
#include "shared.hpp"

void osn::AudioEncoder::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("AudioEncoder");
	cls->register_function(
		std::make_shared<ipc::function>("Create", std::vector<ipc::type>{ipc::type::String, ipc::type::String, ipc::type::String}, Create));
	cls->register_function(std::make_shared<ipc::function>("GetName", std::vector<ipc::type>{ipc::type::UInt64}, GetName));
	cls->register_function(std::make_shared<ipc::function>("SetName", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String}, SetName));
	cls->register_function(std::make_shared<ipc::function>("GetBitrate", std::vector<ipc::type>{ipc::type::UInt64}, GetBitrate));
	cls->register_function(std::make_shared<ipc::function>("SetBitrate", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetBitrate));

	srv.register_collection(cls);
}

void osn::AudioEncoder::Create(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_encoder_t *audioEncoder = obs_audio_encoder_create("ffmpeg_aac", "audio", nullptr, 0, nullptr);

	if (!audioEncoder) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to create the audio encoder.");
	}

	uint64_t uid = osn::AudioEncoder::Manager::GetInstance().allocate(audioEncoder);
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::AudioEncoder::GetName(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_encoder_t *audioEncoder = osn::AudioEncoder::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!audioEncoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Encoder reference is not valid.");
	}

	const char *name = obs_encoder_get_name(audioEncoder);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(name ? name : ""));
	AUTO_DEBUG;
}

void osn::AudioEncoder::SetName(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_encoder_t *audioEncoder = osn::AudioEncoder::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!audioEncoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Encoder reference is not valid.");
	}

	std::string name = args[1].value_str;
	obs_encoder_set_name(audioEncoder, name.c_str());
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::AudioEncoder::GetBitrate(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_encoder_t *audioEncoder = osn::AudioEncoder::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!audioEncoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Encoder reference is not valid.");
	}

	obs_data_t *settings = obs_encoder_get_settings(audioEncoder);
	uint32_t bitrate = static_cast<uint32_t>(obs_data_get_int(settings, "bitrate"));
	obs_data_release(settings);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(bitrate));
	AUTO_DEBUG;
}

void osn::AudioEncoder::SetBitrate(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_encoder_t *audioEncoder = osn::AudioEncoder::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!audioEncoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Encoder reference is not valid.");
	}

	obs_data_t *settings = obs_data_create();
	obs_data_set_int(settings, "bitrate", args[1].value_union.ui32);
	obs_encoder_update(audioEncoder, settings);
	obs_data_release(settings);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

osn::AudioEncoder::Manager &osn::AudioEncoder::Manager::GetInstance()
{
	static osn::AudioEncoder::Manager _inst;
	return _inst;
}