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
#include <ipc-server.hpp>
#include <obs.h>
#include "utility.hpp"
#include "osn-delay.hpp"
#include "osn-reconnect.hpp"
#include "osn-network.hpp"
#include "osn-streaming.hpp"

namespace osn {
class SimpleStreaming : public Streaming {
public:
	SimpleStreaming() : Streaming()
	{
		audioEncoder = nullptr;
		useAdvanced = false;
		customEncSettings = "";
	}
	~SimpleStreaming() {}

public:
	obs_encoder_t *audioEncoder;
	bool useAdvanced;
	std::string customEncSettings;

	void UpdateEncoders();
};

class ISimpleStreaming : public IStreaming {
public:
	static void Register(ipc::server &);

	static void Create(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void Destroy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetAudioEncoder(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetAudioEncoder(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetUseAdvanced(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetUseAdvanced(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetCustomEncSettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetCustomEncSettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void Start(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void Stop(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetLegacySettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static obs_encoder_t *CreateLegacyVideoEncoder();
	static obs_encoder_t *CreateLegacyAudioEncoder();
	static void SetLegacySettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetLegacyVideoEncoderSettings(obs_encoder_t *encoder);
	static void SetLegacyAudioEncoderSettings(obs_encoder_t *encoder);
	static void GetAvailableEncoders(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
};
}
