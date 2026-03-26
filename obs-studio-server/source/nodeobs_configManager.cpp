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

#include "nodeobs_configManager.hpp"

#ifdef _WIN32
#include <ShlObj.h>
#include <windows.h>
#endif

#include <util/platform.h>
#include "shared.hpp"
#include "nodeobs_service.h"
#include "osn-encoders.hpp"

void ConfigManager::setAppdataPath(const std::string &path)
{
	appdata = path;
}

config_t *ConfigManager::getConfig(const std::string &name)
{
	config_t *config = nullptr;
	std::string file = appdata + name;

	int result = config_open(&config, file.c_str(), CONFIG_OPEN_EXISTING);

	if (result != CONFIG_SUCCESS) {
		config = config_create(file.c_str());
		if (config) {
			config_close(config);
			config = nullptr;

			result = config_open(&config, file.c_str(), CONFIG_OPEN_EXISTING);
			if (result != CONFIG_SUCCESS) {
				config = nullptr;
			}
		}
	}

	return config;
};

void initGlobalDefault(config_t *config)
{
	config_set_default_bool(config, "BasicWindow", "SnappingEnabled", true);
	config_set_default_double(config, "BasicWindow", "SnapDistance", 10);
	config_set_default_bool(config, "BasicWindow", "ScreenSnapping", true);
	config_set_default_bool(config, "BasicWindow", "SourceSnapping", true);
	config_set_default_bool(config, "BasicWindow", "CenterSnapping", false);
	config_set_default_bool(config, "General", "BrowserHWAccel", true);
	config_set_default_bool(config, "General", "fileCaching", true);
	config_set_default_string(config, "General", "ProcessPriority", "Normal");
	config_set_default_bool(config, "Audio", "LowLatencyAudioBuffering", false);

	config_save_safe(config, "tmp", nullptr);
}

static const double scaled_vals[] = {1.0, 1.25, (1.0 / 0.75), 1.5, (1.0 / 0.6), 1.75, 2.0, 2.25, 2.5, 2.75, 3.0, 0.0};

static inline std::string GetDefaultVideoSavePath()
{
#ifdef WIN32
	wchar_t path_utf16[MAX_PATH];
	char path_utf8[MAX_PATH] = {};

	SHGetFolderPathW(NULL, CSIDL_MYVIDEO, NULL, SHGFP_TYPE_CURRENT, path_utf16);

	os_wcs_to_utf8(path_utf16, wcslen(path_utf16), path_utf8, MAX_PATH);
	return std::string(path_utf8);
#else
	return g_util_osx->getDefaultVideoSavePath();
#endif
}

void initBasicDefault(config_t *config)
{
	// Base resolution
	uint32_t cx = 0;
	uint32_t cy = 0;

	/* ----------------------------------------------------- */
	/* move over mixer values in advanced if older config */
	if (config_has_user_value(config, "AdvOut", "RecTrackIndex") && !config_has_user_value(config, "AdvOut", "RecTracks")) {
		uint64_t track = config_get_uint(config, "AdvOut", "RecTrackIndex");
		track = 1ULL << (track - 1);
		config_set_uint(config, "AdvOut", "RecTracks", track);
		config_remove_value(config, "AdvOut", "RecTrackIndex");
		config_save_safe(config, "tmp", nullptr);
	}
	if (config_has_user_value(config, "AdvOut", "nameTrack3")) {
		std::string trackName = config_get_string(config, "AdvOut", "nameTrack3");
		config_set_string(config, "AdvOut", "Track3Name", trackName.c_str());
		config_remove_value(config, "AdvOut", "nameTrack3");
		config_save_safe(config, "tmp", nullptr);
	}
	if (config_has_user_value(config, "AdvOut", "nameTrack4")) {
		std::string trackName = config_get_string(config, "AdvOut", "nameTrack4");
		config_set_string(config, "AdvOut", "Track4Name", trackName.c_str());
		config_remove_value(config, "AdvOut", "nameTrack4");
		config_save_safe(config, "tmp", nullptr);
	}
	if (config_has_user_value(config, "AdvOut", "nameTrack5")) {
		std::string trackName = config_get_string(config, "AdvOut", "nameTrack5");
		config_set_string(config, "AdvOut", "Track5Name", trackName.c_str());
		config_remove_value(config, "AdvOut", "nameTrack5");
		config_save_safe(config, "tmp", nullptr);
	}

	config_set_default_string(config, "Output", "Mode", "Simple");
	std::string filePath = GetDefaultVideoSavePath();
	config_set_default_string(config, "SimpleOutput", "FilePath", filePath.c_str());
	config_set_default_string(config, "SimpleOutput", "RecFormat", "mp4");
	config_set_default_uint(config, "SimpleOutput", "VBitrate", 4500);
	config_set_default_string(config, "SimpleOutput", "StreamEncoder", SIMPLE_ENCODER_X264);

	config_set_default_uint(config, "SimpleOutput", "ABitrate", 160);
	config_set_default_bool(config, "SimpleOutput", "UseAdvanced", false);
	config_set_default_bool(config, "SimpleOutput", "EnforceBitrate", true);
	config_set_default_string(config, "SimpleOutput", "Preset", "veryfast");
	config_set_default_string(config, "SimpleOutput", "RecQuality", "Stream");
	config_set_default_string(config, "SimpleOutput", "RecEncoder", SIMPLE_ENCODER_X264);
	config_set_default_string(config, "SimpleOutput", "RecAEncoder", SIMPLE_AUDIO_ENCODER_AAC);

	config_set_default_bool(config, "SimpleOutput", "RecRB", true);
	config_set_default_int(config, "SimpleOutput", "RecRBTime", 20);
	config_set_default_int(config, "SimpleOutput", "RecRBSize", 512);
	config_set_default_string(config, "SimpleOutput", "RecRBPrefix", "Replay");
	config_set_default_bool(config, "SimpleOutput", "replayBufferUseStreamOutput", true);
	config_set_default_string(config, "SimpleOutput", "Profile", "main");

	config_set_default_bool(config, "AdvOut", "ApplyServiceSettings", true);
	config_set_default_bool(config, "AdvOut", "UseRescale", false);
	config_set_default_uint(config, "AdvOut", "TrackIndex", 1);
	config_set_default_uint(config, "AdvOut", "VodTrackIndex", 2);
	config_set_default_string(config, "AdvOut", "Encoder", ADVANCED_ENCODER_X264);

	config_set_default_string(config, "AdvOut", "RecType", "Standard");

	config_set_default_string(config, "AdvOut", "RecFilePath", GetDefaultVideoSavePath().c_str());
	config_set_default_string(config, "AdvOut", "RecFormat", "mp4");
	config_set_default_bool(config, "AdvOut", "RecUseRescale", false);
	config_set_default_uint(config, "AdvOut", "RecTracks", (1 << 0));
	config_set_default_string(config, "AdvOut", "RecEncoder", "none");

	config_set_default_bool(config, "AdvOut", "FFOutputToFile", true);
	config_set_default_string(config, "AdvOut", "FFFilePath", GetDefaultVideoSavePath().c_str());
	config_set_default_string(config, "AdvOut", "FFExtension", "mp4");
	config_set_default_uint(config, "AdvOut", "FFVBitrate", 4500);
	config_set_default_uint(config, "AdvOut", "FFVGOPSize", 250);
	config_set_default_bool(config, "AdvOut", "FFUseRescale", false);
	config_set_default_bool(config, "AdvOut", "FFIgnoreCompat", false);
	config_set_default_uint(config, "AdvOut", "FFABitrate", 160);
	config_set_default_uint(config, "AdvOut", "FFAudioTrack", 1);

	config_set_default_uint(config, "AdvOut", "Track1Bitrate", 160);
	config_set_default_uint(config, "AdvOut", "Track2Bitrate", 160);
	config_set_default_uint(config, "AdvOut", "Track3Bitrate", 160);
	config_set_default_uint(config, "AdvOut", "Track4Bitrate", 160);
	config_set_default_uint(config, "AdvOut", "Track5Bitrate", 160);
	config_set_default_uint(config, "AdvOut", "Track6Bitrate", 160);

	config_set_default_bool(config, "AdvOut", "RecSplitFile", false);
	config_set_default_string(config, "AdvOut", "RecSplitFileType", "Time");
	config_set_default_uint(config, "AdvOut", "RecSplitFileTime", 15);
	config_set_default_uint(config, "AdvOut", "RecSplitFileSize", 2048);
	config_set_default_bool(config, "AdvOut", "RecSplitFileResetTimestamps", true);
	config_set_default_string(config, "AdvOut", "RecAEncoder", SIMPLE_AUDIO_ENCODER_AAC);

	config_set_default_bool(config, "AdvOut", "RecRB", false);
	config_set_default_uint(config, "AdvOut", "RecRBTime", 20);
	config_set_default_int(config, "AdvOut", "RecRBSize", 512);
	config_set_default_bool(config, "AdvOut", "replayBufferUseStreamOutput", true);

	config_set_default_uint(config, "Video", "BaseCX", cx);
	config_set_default_uint(config, "Video", "BaseCY", cy);

	/* don't allow BaseCX/BaseCY to be susceptible to defaults changing */
	if (!config_has_user_value(config, "Video", "BaseCX") || !config_has_user_value(config, "Video", "BaseCY")) {
		config_set_uint(config, "Video", "BaseCX", cx);
		config_set_uint(config, "Video", "BaseCY", cy);
		config_save_safe(config, "tmp", nullptr);
	}

	config_set_default_bool(config, "Audio", "DisableAudioDucking", true);

	config_set_default_string(config, "Output", "FilenameFormatting", "%CCYY-%MM-%DD %hh-%mm-%ss");

	config_set_default_bool(config, "Output", "DelayEnable", false);
	config_set_default_uint(config, "Output", "DelaySec", 20);
	config_set_default_bool(config, "Output", "DelayPreserve", true);

	config_set_default_bool(config, "Output", "Reconnect", true);
	config_set_default_uint(config, "Output", "RetryDelay", 2);
	config_set_default_uint(config, "Output", "MaxRetries", 25);

	config_set_default_string(config, "Output", "BindIP", "default");
	config_set_default_bool(config, "Output", "DynamicBitrate", false);
	config_set_default_bool(config, "Output", "NewSocketLoopEnable", false);
	config_set_default_bool(config, "Output", "LowLatencyEnable", false);

	int i = 0;
	uint32_t scale_cx = 0;
	uint32_t scale_cy = 0;

	/* use a default scaled resolution that has a pixel count no higher
	* than 1280x720 */
	while (((scale_cx * scale_cy) > (1280 * 720)) && scaled_vals[i] > 0.0) {
		double scale = scaled_vals[i++];
		scale_cx = uint32_t(double(cx) / scale);
		scale_cy = uint32_t(double(cy) / scale);
	}

	config_set_default_uint(config, "Video", "OutputCX", scale_cx);
	config_set_default_uint(config, "Video", "OutputCY", scale_cy);

	/* don't allow OutputCX/OutputCY to be susceptible to defaults
	* changing */
	if (!config_has_user_value(config, "Video", "OutputCX") || !config_has_user_value(config, "Video", "OutputCY")) {
		config_set_uint(config, "Video", "OutputCX", scale_cx);
		config_set_uint(config, "Video", "OutputCY", scale_cy);
		config_save_safe(config, "tmp", nullptr);
	}

	config_set_default_uint(config, "Video", "FPSType", 0);
	config_set_default_string(config, "Video", "FPSCommon", "30");
	config_set_default_uint(config, "Video", "FPSInt", 30);
	config_set_default_uint(config, "Video", "FPSNum", 30);
	config_set_default_uint(config, "Video", "FPSDen", 1);
	config_set_default_string(config, "Video", "ScaleType", "bicubic");
	config_set_default_string(config, "Video", "ColorFormat", "NV12");
	config_set_default_string(config, "Video", "ColorSpace", "709");
	config_set_default_string(config, "Video", "ColorRange", "Partial");
	config_set_default_string(config, "AdvVideo", "ColorFormat", "NV12");
	config_set_default_string(config, "AdvVideo", "ColorSpace", "709");
	config_set_default_string(config, "AdvVideo", "ColorRange", "Partial");
	config_set_default_uint(config, "Video", "SdrWhiteLevel", 300);
	config_set_default_uint(config, "Video", "HdrNominalPeakLevel", 1000);
	config_set_default_bool(config, "Video", "ForceGPUAsRenderDevice", true);

	config_set_default_string(config, "Audio", "MonitoringDeviceId", "default");
	config_set_default_string(config, "Audio", "MonitoringDeviceName", "Default");
	config_set_default_bool(config, "Audio", "LowLatencyAudioBuffering", false);

	if (config_get_uint(config, "Audio", "SampleRate") == 0) {
		config_set_uint(config, "Audio", "SampleRate", 44100);
		config_save_safe(config, "tmp", nullptr);
	}
	config_set_default_uint(config, "Audio", "SampleRate", 44100);
	config_set_default_string(config, "Audio", "ChannelSetup", "Stereo");

	config_save_safe(config, "tmp", nullptr);
}

void ConfigManager::reloadConfig(void)
{
	if (basic) {
		config_close(basic);
		basic = nullptr;
	}
	if (global) {
		config_close(global);
		global = nullptr;
	}
}

config_t *ConfigManager::getGlobal()
{
	if (!global) {
#ifdef WIN32
		global = getConfig("\\global.ini");
#else
		global = getConfig("/global.ini");
#endif
		if (global) {
			initGlobalDefault(global);
		}
	}

	return global;
};
config_t *ConfigManager::getBasic()
{
	if (!basic) {
#ifdef WIN32
		basic = getConfig("\\basic.ini");
#else
		basic = getConfig("/basic.ini");
#endif
		if (basic) {
			initBasicDefault(basic);
		}
	}

	return basic;
};

std::string ConfigManager::getService(size_t index)
{
	if (index == 0) {
#ifdef WIN32
		return appdata + "\\service.json";
#else
		return appdata + "/service.json";
#endif
	} else {
#ifdef WIN32
		return appdata + "\\service1.json";
#else
		return appdata + "/service1.json";
#endif
	}
};
std::string ConfigManager::getStream()
{
#ifdef WIN32
	return appdata + "\\streamEncoder.json";
#else
	return appdata + "/streamEncoder.json";
#endif
};
std::string ConfigManager::getRecord()
{
#ifdef WIN32
	return appdata + "\\recordEncoder.json";
#else
	return appdata + "/recordEncoder.json";
#endif
};
