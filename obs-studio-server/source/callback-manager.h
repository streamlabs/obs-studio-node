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

#pragma once

#include <algorithm>
#include <iostream>
#include <ipc-server.hpp>
#include <map>
#include <mutex>
#include <obs.h>
#include <queue>
#include <string>
#include <thread>
#include <util/config-file.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <vector>
#include "nodeobs_api.h"

#include "nodeobs_audio_encoders.h"

struct SourceSizeInfo {
	obs_source_t *source;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t flags = 0;
};

struct TransitionInfo {
	enum EventType : uint32_t {
		START = 0,
		STOP = 1,
	};

	obs_source_t *transition;
	// All pending events to be processed. Might be empty.
	std::vector<EventType> events;
};

class CallbackManager {
public:
	CallbackManager(){};
	~CallbackManager(){};

	static void Register(ipc::server &);
	static void GlobalQuery(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);

	static void addSource(obs_source_t *source);
	static void removeSource(obs_source_t *source);
};
