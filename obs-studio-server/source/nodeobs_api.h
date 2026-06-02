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
#ifdef WIN32
#include <io.h>
#endif
#include <iostream>
#include <ipc-server.hpp>
#include <math.h>
#include <obs.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <deque>
#include "nodeobs_configManager.hpp"
#include "nodeobs_service.h"
#include "util-osx.hpp"

extern std::string g_moduleDirectory;

namespace util {
class CrashManager;
}

class OBS_API {
	friend util::CrashManager;

public:
	struct LogReport {
		static const int MaximumMessages = 150;

		void push(std::string message)
		{
			general.push_back(message);
			if (general.size() > MaximumMessages) {
				general.pop_front();
			}
		}

		std::deque<std::string> general;
	};

	struct OutputStats {
		double kbitsPerSec = 0;
		double dataOutput = 0;
		uint64_t lastBytesSent = 0;
		uint64_t lastBytesSentTime = 0;
	};

public:
	OBS_API();
	~OBS_API();

	static void Register(ipc::server &);

	static void OBS_API_initAPI(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void OBS_API_destroyOBS_API(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void OBS_API_getPerformanceStatistics(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetWorkingDirectory(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void StopCrashHandler(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void InformCrashHandler(const int crash_id);
	static void CrashModuleInfo(const std::string &moduleName, const std::string &binaryPath);
	static void QueryHotkeys(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void ProcessHotkeyStatus(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetUsername(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void OBS_API_forceCrash(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);

	static void GetBrowserAcceleration(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetBrowserAcceleration(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetBrowserAccelerationLegacy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetMediaFileCaching(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetMediaFileCaching(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetMediaFileCachingLegacy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetProcessPriority(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetProcessPriority(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetProcessPriorityLegacy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetSdrWhiteLevel(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetSdrWhiteLevel(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetSdrWhiteLevelLegacy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetHdrNominalPeakLevel(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetHdrNominalPeakLevel(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetHdrNominalPeakLevelLegacy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetLowLatencyAudioBuffering(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetLowLatencyAudioBuffering(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetLowLatencyAudioBufferingLegacy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetForceGPURendering(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetForceGPURendering(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetForceGPURenderingLegacy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);

	static bool getBrowserAcceleration();
	static bool getMediaFileCaching();

public:
	static void initAPI(void);
	static bool openAllModules(int &video_err);

	static double getCPU_Percentage(void);
	static int getNumberOfDroppedFrames(void);
	static double getDroppedFramesPercentage(void);
	static double getCurrentFrameRate(void);
	static double getAverageTimeToRenderFrame();
	static std::string getDiskSpaceAvailable();
	static double getMemoryUsage();
	static void getCurrentOutputStats(obs_output_t *output, OBS_API::OutputStats &outputStats);

	// Snapshot (and clear) the general log tail under logMutex. Uses try_lock — the crash
	// handler calls this and must never block on the logging thread. Returns oldest-first,
	// empty if the lock can't be acquired.
	static std::deque<std::string> snapshotOBSLogGeneral();

	static std::string getCurrentVersion();
	static std::string getUsername();

	static std::vector<std::string> exploreDirectory(std::string directory, std::string typeToReturn);

public:
	static std::string getPathConfigDirectory(void);
	static void setPathConfigDirectory(std::string newPathConfigDirectory);
	static std::string getOBS_currentProfile(void);
	static void setOBS_currentProfile(std::string profileName);
	static std::string getOBS_currentSceneCollection(void);
	static void setOBS_currentSceneCollection(std::string sceneCollectionName);
	static bool isOBS_configFilesUsed(void);
	static std::string getModuleDirectory(void);

	static std::vector<std::pair<uint32_t, uint32_t>> availableResolutions(void);

	static std::string getGlobalConfigPath(void);
	static std::string getBasicConfigPath(void);
	static std::string getServiceConfigPath(void);
	static std::string getContentConfigPath(void);

	static void setAudioDeviceMonitoring(void);
	static void SetProcessPriorityOld(const char *priority);
	static void destroyOBS_API(void);

	static void SetCrashHandlerPipe(const std::wstring &);
	static void CreateCrashHandlerExitPipe();
	static void WaitCrashHandlerClose(bool waitBeforeClosing);
};

class outdated_driver_error {
	static outdated_driver_error *inst;
	std::string line_1 = "";
	std::string line_2 = "";
	int lookup_enabled = 0;

public:
	static outdated_driver_error *instance();
	void set_active(bool state);
	std::string get_error();

	void catch_error(const char *msg);
};
