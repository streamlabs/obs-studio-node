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
#include <obs.h>
#include "utility.hpp"
#include "osn-delay.hpp"
#include "osn-simple-streaming.hpp"
#include "osn-reconnect.hpp"
#include "osn-network.hpp"
#include "osn-output.hpp"
#include "osn-video-encoder.hpp"
#include "osn-multitrack-video.hpp"
#include "osn-enhanced-broadcasting.hpp"

#include "nodeobs_configManager.hpp"

namespace osn {

class EnhancedBroadcastingSimpleStreaming : public EnhancedBroadcasting<SimpleStreaming> {
public:
	EnhancedBroadcastingSimpleStreaming() : EnhancedBroadcasting<SimpleStreaming>() {}
	~EnhancedBroadcastingSimpleStreaming() {}
};

class IEnhancedBroadcastingSimpleStreaming : public ISimpleStreaming {
public:
	static void Register(ipc::server &);

	static void Create(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void Destroy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);

	static void Start(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void Stop(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);

	static void GetAdditionalVideoCanvas(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetAdditionalVideoCanvas(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
};
}
