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
#include <chrono>
#include <iostream>
#include <mutex>
#include <obs.h>
#include <sstream>
#include <stdio.h>
#include <string>
#include <thread>
#include <vector>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <ctime>
#include <fstream>
#include <map>
#include "nodeobs_api.h"
#include "nodeobs_display.h"

struct SourceInfo {
	uint32_t fader;
	uint32_t volmeter;
};

class OBS_content {
public:
	OBS_content();
	~OBS_content();

	static void Register(ipc::server &);

	static void OBS_content_setDayTheme(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void OBS_content_createDisplay(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void OBS_content_destroyDisplay(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void OBS_content_shutdownDisplays();
	static void OBS_content_getDisplayPreviewOffset(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void OBS_content_getDisplayPreviewSize(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void OBS_content_createSourcePreviewDisplay(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void OBS_content_resizeDisplay(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void OBS_content_moveDisplay(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void OBS_content_setPaddingSize(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void OBS_content_setPaddingColor(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void OBS_content_setBackgroundColor(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void OBS_content_setOutlineColor(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void OBS_content_setCropOutlineColor(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void OBS_content_setShouldDrawUI(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void OBS_content_setDrawGuideLines(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void OBS_content_setDrawRotationHandle(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void OBS_content_createIOSurface(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void OBS_content_takeScreenshot(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
};
