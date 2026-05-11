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
#include "osn-audio-track.hpp"
#include "osn-error.hpp"
#include "shared.hpp"

#include "nodeobs_audio_encoders.h"
#include "nodeobs_configManager.hpp"

std::array<osn::AudioTrack *, NUM_AUDIO_TRACKS> osn::IAudioTrack::audioTrackConfigs = {};

void osn::IAudioTrack::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("AudioTrack");

	cls->register_function(std::make_shared<ipc::function>("Create", std::vector<ipc::type>{}, Create));

	cls->register_function(std::make_shared<ipc::function>("GetAudioTracks", std::vector<ipc::type>{}, GetAudioTracks));
	cls->register_function(std::make_shared<ipc::function>("GetAudioBitrates", std::vector<ipc::type>{}, GetAudioBitrates));
	cls->register_function(std::make_shared<ipc::function>("GetAtIndex", std::vector<ipc::type>{ipc::type::UInt32}, GetAtIndex));
	cls->register_function(std::make_shared<ipc::function>("SetAtIndex", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetAtIndex));
	cls->register_function(std::make_shared<ipc::function>("GetBitrate", std::vector<ipc::type>{ipc::type::UInt64}, GetBitrate));
	cls->register_function(std::make_shared<ipc::function>("SetBitrate", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetBitrate));
	cls->register_function(std::make_shared<ipc::function>("GetName", std::vector<ipc::type>{ipc::type::UInt64}, GetName));
	cls->register_function(std::make_shared<ipc::function>("SetName", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String}, SetName));
	cls->register_function(std::make_shared<ipc::function>("ImportLegacySettings", std::vector<ipc::type>{}, ImportLegacySettings));
	cls->register_function(std::make_shared<ipc::function>("SaveLegacySettings", std::vector<ipc::type>{}, SaveLegacySettings));

	srv.register_collection(cls);
}

void osn::IAudioTrack::Create(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	uint32_t bitrate = args[0].value_union.ui32;
	std::string name = args[1].value_str;
	uint64_t uid = osn::IAudioTrack::Manager::GetInstance().allocate(new AudioTrack(bitrate, name));
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::IAudioTrack::GetAudioTracks(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)NUM_AUDIO_TRACKS));
	for (const auto &audioTrack : audioTrackConfigs) {
		uint64_t uid = UINT64_MAX;
		if (audioTrack) {
			uid = osn::IAudioTrack::Manager::GetInstance().find(audioTrack);
		}
		rval.push_back(ipc::value(uid));
	}
	AUTO_DEBUG;
}

void osn::IAudioTrack::GetAudioBitrates(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	auto bitrates = GetAACEncoderBitrateMap();
	rval.push_back(ipc::value((uint32_t)bitrates.size()));
	for (const auto &bitrate : bitrates)
		rval.push_back(ipc::value((uint32_t)bitrate.first));
	AUTO_DEBUG;
}

void osn::IAudioTrack::GetAtIndex(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AudioTrack *audioTrack = GetTrackConfig(args[0].value_union.ui32);
	if (!audioTrack) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "AudioTrack reference is not valid.");
	}

	uint64_t uid = osn::IAudioTrack::Manager::GetInstance().find(audioTrack);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

osn::AudioTrack *osn::IAudioTrack::GetTrackConfig(uint32_t trackNumber)
{
	if (trackNumber == 0 || trackNumber > NUM_AUDIO_TRACKS)
		return nullptr;

	return audioTrackConfigs[trackNumber - 1];
}

uint32_t osn::IAudioTrack::GetMixerIndex(uint32_t trackNumber)
{
	return trackNumber - 1;
}

obs_encoder_t *osn::IAudioTrack::CreateEncoderForTrack(uint32_t trackNumber, const std::string &name)
{
	AudioTrack *track = GetTrackConfig(trackNumber);
	if (!track)
		return nullptr;

	const uint32_t mixerIndex = GetMixerIndex(trackNumber);
	OBSData settings = obs_data_create();
	obs_data_set_int(settings, "bitrate", track->bitrate);
	return obs_audio_encoder_create(GetAACEncoderForBitrate(track->bitrate), name.c_str(), settings, mixerIndex, nullptr);
}

void osn::IAudioTrack::SetTrackConfigAtMixerIndex(AudioTrack *track, uint32_t mixerIndex)
{
	if (mixerIndex >= NUM_AUDIO_TRACKS)
		return;

	AudioTrack *oldTrack = audioTrackConfigs[mixerIndex];
	if (oldTrack)
		delete oldTrack;

	audioTrackConfigs[mixerIndex] = track;
}

void osn::IAudioTrack::SetAtIndex(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AudioTrack *audioTrack = osn::IAudioTrack::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!audioTrack) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "AudioTrack reference is not valid.");
	}
	uint32_t index = args[1].value_union.ui32;
	if (index == 0 || index > NUM_AUDIO_TRACKS) {
		PRETTY_ERROR_RETURN(ErrorCode::OutOfBounds, "AudioTrack index is invalid.");
	}

	SetTrackConfigAtMixerIndex(audioTrack, index - 1);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IAudioTrack::GetBitrate(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AudioTrack *audioTrack = osn::IAudioTrack::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!audioTrack) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "AudioTrack reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(audioTrack->bitrate));
	AUTO_DEBUG;
}

void osn::IAudioTrack::SetBitrate(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AudioTrack *audioTrack = osn::IAudioTrack::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!audioTrack) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "AudioTrack reference is not valid.");
	}

	audioTrack->bitrate = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IAudioTrack::GetName(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AudioTrack *audioTrack = osn::IAudioTrack::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!audioTrack) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "AudioTrack reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(audioTrack->name));
	AUTO_DEBUG;
}

void osn::IAudioTrack::SetName(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AudioTrack *audioTrack = osn::IAudioTrack::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!audioTrack) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "AudioTrack reference is not valid.");
	}

	audioTrack->name = args[1].value_str;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

osn::IAudioTrack::Manager &osn::IAudioTrack::Manager::GetInstance()
{
	static osn::IAudioTrack::Manager _inst;
	return _inst;
}

void osn::IAudioTrack::ImportLegacySettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	for (uint32_t i = 0; i < NUM_AUDIO_TRACKS; i++) {
		std::string prefix = "Track";
		prefix += std::to_string(i + 1);
		std::string bitrateParam = prefix;
		bitrateParam += "Bitrate";
		std::string nameParam = prefix;
		nameParam += "Name";
		auto track = new AudioTrack(160, "");
		auto uid = osn::IAudioTrack::Manager::GetInstance().allocate(track);
		if (uid != UINT64_MAX) {
			track->bitrate = static_cast<uint32_t>(config_get_uint(ConfigManager::getInstance().getBasic(), "AdvOut", bitrateParam.c_str()));
			track->name = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", nameParam.c_str());
			if (track->name.size())
				SetTrackConfigAtMixerIndex(track, i);
			else
				delete track;
		} else {
			delete track;
		}
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IAudioTrack::SaveLegacySettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	for (uint32_t i = 0; i < NUM_AUDIO_TRACKS; i++) {
		if (audioTrackConfigs[i]) {
			std::string prefix = "Track";
			prefix += std::to_string(i + 1);
			std::string bitrateParam = prefix;
			bitrateParam += "Bitrate";
			std::string nameParam = prefix;
			nameParam += "Name";
			config_set_uint(ConfigManager::getInstance().getBasic(), "AdvOut", bitrateParam.c_str(), audioTrackConfigs[i]->bitrate);
			config_set_string(ConfigManager::getInstance().getBasic(), "AdvOut", nameParam.c_str(), audioTrackConfigs[i]->name.c_str());
		}
	}

	config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}
