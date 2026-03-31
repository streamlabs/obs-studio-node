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

#include "osn-global.hpp"
#include <osn-error.hpp>
#include <obs.h>
#include "osn-source.hpp"
#include "shared.hpp"
#include "nodeobs_configManager.hpp"

#define MBYTE (1024ULL * 1024ULL)
#define GBYTE (1024ULL * 1024ULL * 1024ULL)
#define TBYTE (1024ULL * 1024ULL * 1024ULL * 1024ULL)

os_cpu_usage_info_t *osn::Global::cpuUsage = nullptr;

void osn::Global::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("Global");
	cls->register_function(std::make_shared<ipc::function>("GetOutputSource", std::vector<ipc::type>{ipc::type::UInt32}, GetOutputSource));
	cls->register_function(
		std::make_shared<ipc::function>("SetOutputSource", std::vector<ipc::type>{ipc::type::UInt32, ipc::type::UInt64}, SetOutputSource));
	cls->register_function(std::make_shared<ipc::function>("AddSceneToBackstage", std::vector<ipc::type>{ipc::type::UInt64}, AddSceneToBackstage));
	cls->register_function(
		std::make_shared<ipc::function>("RemoveSceneFromBackstage", std::vector<ipc::type>{ipc::type::UInt64}, RemoveSceneFromBackstage));
	cls->register_function(std::make_shared<ipc::function>("GetOutputFlagsFromId", std::vector<ipc::type>{ipc::type::String}, GetOutputFlagsFromId));
	cls->register_function(std::make_shared<ipc::function>("GetLaggedFrames", std::vector<ipc::type>{}, GetLaggedFrames));
	cls->register_function(std::make_shared<ipc::function>("GetTotalFrames", std::vector<ipc::type>{}, GetTotalFrames));
	cls->register_function(std::make_shared<ipc::function>("GetLocale", std::vector<ipc::type>{}, GetLocale));
	cls->register_function(std::make_shared<ipc::function>("SetLocale", std::vector<ipc::type>{ipc::type::String}, SetLocale));
	cls->register_function(std::make_shared<ipc::function>("GetMultipleRendering", std::vector<ipc::type>{}, GetMultipleRendering));
	cls->register_function(std::make_shared<ipc::function>("SetMultipleRendering", std::vector<ipc::type>{ipc::type::Int32}, SetMultipleRendering));
	cls->register_function(std::make_shared<ipc::function>("GetCPUPercentage", std::vector<ipc::type>{}, GetCPUPercentage));
	cls->register_function(std::make_shared<ipc::function>("GetCurrentFrameRate", std::vector<ipc::type>{}, GetCurrentFrameRate));
	cls->register_function(std::make_shared<ipc::function>("GetAverageTimeToRenderFrame", std::vector<ipc::type>{}, GetAverageTimeToRenderFrame));
	cls->register_function(std::make_shared<ipc::function>("GetDiskSpaceAvailable", std::vector<ipc::type>{}, GetDiskSpaceAvailable));
	cls->register_function(std::make_shared<ipc::function>("GetMemoryUsage", std::vector<ipc::type>{}, GetMemoryUsage));
	srv.register_collection(cls);
}

void osn::Global::GetOutputSource(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *source = obs_get_output_source(args[0].value_union.ui32);
	if (!source) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		rval.push_back(ipc::value(UINT64_MAX));
		rval.push_back(ipc::value(-1));
		AUTO_DEBUG;
		return;
	}

	uint64_t uid = osn::Source::Manager::GetInstance().find(source);
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Source found but not indexed.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	rval.push_back(ipc::value(obs_source_get_type(source)));
	obs_source_release(source);
	AUTO_DEBUG;
}

void osn::Global::SetOutputSource(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *source = nullptr;
	uint32_t channel = args[0].value_union.ui32;

	if (channel >= MAX_CHANNELS) {
		PRETTY_ERROR_RETURN(ErrorCode::OutOfBounds, "Invalid output channel.");
	}

	if (args[1].value_union.ui64 != UINT64_MAX) {
		source = osn::Source::Manager::GetInstance().find(args[1].value_union.ui64);
		if (!source) {
			PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
		}
	}

	obs_set_output_source(channel, source);
	if (source) {
		obs_source_t *newsource = obs_get_output_source(channel);
		if (newsource != source) {
			obs_source_release(newsource);
			PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to set output source.");
		} else {
			rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		}
		obs_source_release(newsource);
	} else {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	}
	AUTO_DEBUG;
}

void osn::Global::AddSceneToBackstage(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *source = nullptr;
	if (args[0].value_union.ui64 != UINT64_MAX) {
		source = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
		if (!source) {
			PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
		}
	}

	obs_activate_videos_on_backstage(source);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Global::RemoveSceneFromBackstage(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *source = nullptr;
	if (args[0].value_union.ui64 != UINT64_MAX) {
		source = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
		if (!source) {
			PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
		}
	}

	obs_deactivate_videos_on_backstage(source);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Global::GetOutputFlagsFromId(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	uint32_t flags = obs_get_source_output_flags(args[0].value_str.c_str());

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(flags));
	AUTO_DEBUG;
}

void osn::Global::GetLaggedFrames(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_get_lagged_frames()));
	AUTO_DEBUG;
}

void osn::Global::GetTotalFrames(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_get_total_frames()));
	AUTO_DEBUG;
}

void osn::Global::GetLocale(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_get_locale()));
	AUTO_DEBUG;
}

void osn::Global::SetLocale(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_set_locale(args[0].value_str.c_str());
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Global::GetMultipleRendering(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_get_multiple_rendering()));
	AUTO_DEBUG;
}

void osn::Global::SetMultipleRendering(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_set_multiple_rendering(args[0].value_union.i32);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Global::GetCPUPercentage(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	if (cpuUsage == nullptr) {
		cpuUsage = os_cpu_usage_info_start();
	}

	double cpuPercentage = os_cpu_usage_info_query(cpuUsage);

	cpuPercentage *= 10;
	cpuPercentage = trunc(cpuPercentage);
	cpuPercentage /= 10;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(cpuPercentage));
	AUTO_DEBUG;
}

void osn::Global::GetCurrentFrameRate(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_get_active_fps()));
	AUTO_DEBUG;
}

void osn::Global::GetAverageTimeToRenderFrame(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((double)obs_get_average_frame_time_ns() / 1000000.0));
	AUTO_DEBUG;
}

void osn::Global::GetDiskSpaceAvailable(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	const char *path = nullptr;
	const char *mode = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "Mode");

	if (strcmp(mode, "Advanced") == 0) {
		const char *advanced_mode = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecType");

		if (strcmp(advanced_mode, "FFmpeg") == 0) {
			path = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "FFFilePath");
		} else {
			path = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecFilePath");
		}
	} else {
		path = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "FilePath");
	}

	uint64_t bytes = os_get_free_disk_space(path);

	double free_bytes = 0;
	std::string type;

	if (bytes > TBYTE) {
		free_bytes = (double)bytes / TBYTE;
		type = " TB";
	} else if (bytes > GBYTE) {
		free_bytes = (double)bytes / GBYTE;
		type = " GB";
	} else {
		free_bytes = (double)bytes / MBYTE;
		type = " MB";
	}

	std::stringstream remainingHDSpace;
	remainingHDSpace << free_bytes << type;
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(remainingHDSpace.str()));
	AUTO_DEBUG;
}

void osn::Global::GetMemoryUsage(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((double)os_get_proc_resident_size() / (1024.0 * 1024.0)));
	AUTO_DEBUG;
}

//to be called from wherever OBS shutdown happens
void osn::Global::StopCPUMonitoring()
{
	if (cpuUsage != nullptr) {
		os_cpu_usage_info_destroy(cpuUsage);
	}
}
