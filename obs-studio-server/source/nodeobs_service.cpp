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

#include "nodeobs_service.h"
#ifdef WIN32
#include <ShlObj.h>
#include <windows.h>
#include <filesystem>
#endif
#include "osn-error.hpp"
#include "shared.hpp"
#include "utility.hpp"
#include <osn-video.hpp>
#include "osn-vcam.hpp"
#include "osn-encoders.hpp"

#include "osn-multitrack-video.hpp"

#ifdef __APPLE__
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include "util-crashmanager.h"
#include "util-metricsprovider.h"
#include <exception>
#include <optional>

extern bool isConfiguredStreamingEncoderValid(StreamServiceId serviceId);

std::string GetFormatExt(const std::string container);

std::vector<obs_output_t *> streamingOutput = {nullptr, nullptr};

obs_output_t *recordingOutput = nullptr;
obs_output_t *replayBufferOutput = nullptr;

// Virtual cam
OBSOutputAutoRelease virtualCam;
bool vcamEnabled = false;
bool virtualCamActive = false;
VCamConfig vcamConfig;
obs_view_t *virtualCamView = nullptr;
video_t *virtualCamVideo = nullptr;
obs_scene_t *vCamSourceScene = nullptr;
obs_sceneitem_t *vCamSourceSceneItem = nullptr;
obs_source_t *vCamActiveScene = nullptr;

obs_encoder_t *audioSimpleRecordingEncoder = nullptr;
std::vector<obs_encoder_t *> audioStreamingEncoder = {nullptr, nullptr};
std::vector<obs_encoder_t *> videoStreamingEncoder = {nullptr, nullptr};
std::vector<obs_video_info_t *> videoInfo = {nullptr, nullptr};
obs_encoder_t *videoRecordingEncoder = nullptr;
std::vector<obs_service_t *> services = {nullptr, nullptr};

obs_encoder_t *streamArchiveEncVod = nullptr;

obs_encoder_t *AdvancedRecordingAudioTracks[MAX_AUDIO_MIXES];
std::string AdvancedRecordingAudioEncodersID[MAX_AUDIO_MIXES];

obs_video_info *base_canvas = nullptr;

// Audio encoders
const std::string ffmpeg_aac_id = "ffmpeg_aac";
const std::string ffmpeg_opus_id = "ffmpeg_opus";
std::string audioSimpleRecEncID;

std::string videoEncoder;
std::string videoQuality;
bool usingRecordingPreset = true;
bool recordingConfigured = false;
bool ffmpegOutput = false;
bool lowCPUx264 = false;
std::vector<bool> isStreaming = {false, false};
bool isRecording = false;
bool isReplayBufferActive = false;
bool rpUsesRec = false;
bool rpUsesStream = false;

std::mutex signalMutex;
std::queue<SignalInfo> outputSignal;
std::thread releaseWorker;

static std::optional<osn::EnhancedBroadcastOutputObjects> enhancedBroadcastContext;

OBS_service::OBS_service() {}
OBS_service::~OBS_service() {}

static void logVCamChanged(const VCamConfig &config, bool starting)
{
	const char *action = starting ? "Starting" : "Changing";

	switch (config.type) {
	case VCamOutputType::Invalid:
		break;
	case VCamOutputType::ProgramView:
		blog(LOG_INFO, "%s Virtual Camera output to Program", action);
		break;
	case VCamOutputType::PreviewOutput:
		blog(LOG_INFO, "%s Virtual Camera output to Preview", action);
		break;
	case VCamOutputType::SceneOutput:
		blog(LOG_INFO, "%s Virtual Camera output to Scene : %s", action, config.scene.c_str());
		break;
	case VCamOutputType::SourceOutput:
		blog(LOG_INFO, "%s Virtual Camera output to Source : %s", action, config.source.c_str());
		break;
	}
}

void OBS_service::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("NodeOBS_Service");

	cls->register_function(std::make_shared<ipc::function>("OBS_service_resetAudioContext", std::vector<ipc::type>{}, OBS_service_resetAudioContext));
	cls->register_function(std::make_shared<ipc::function>("OBS_service_resetVideoContext", std::vector<ipc::type>{}, OBS_service_resetVideoContext));
	cls->register_function(std::make_shared<ipc::function>("OBS_service_setVideoInfo", std::vector<ipc::type>{}, OBS_service_setVideoInfo));
	cls->register_function(std::make_shared<ipc::function>("OBS_service_startStreaming", std::vector<ipc::type>{}, OBS_service_startStreaming));
	cls->register_function(std::make_shared<ipc::function>("OBS_service_startRecording", std::vector<ipc::type>{}, OBS_service_startRecording));
	cls->register_function(std::make_shared<ipc::function>("OBS_service_startReplayBuffer", std::vector<ipc::type>{}, OBS_service_startReplayBuffer));
	cls->register_function(
		std::make_shared<ipc::function>("OBS_service_stopStreaming", std::vector<ipc::type>{ipc::type::Int32}, OBS_service_stopStreaming));
	cls->register_function(std::make_shared<ipc::function>("OBS_service_stopRecording", std::vector<ipc::type>{}, OBS_service_stopRecording));
	cls->register_function(
		std::make_shared<ipc::function>("OBS_service_stopReplayBuffer", std::vector<ipc::type>{ipc::type::Int32}, OBS_service_stopReplayBuffer));
	cls->register_function(std::make_shared<ipc::function>("OBS_service_connectOutputSignals", std::vector<ipc::type>{}, OBS_service_connectOutputSignals));
	cls->register_function(std::make_shared<ipc::function>("Query", std::vector<ipc::type>{}, Query));
	cls->register_function(
		std::make_shared<ipc::function>("OBS_service_processReplayBufferHotkey", std::vector<ipc::type>{}, OBS_service_processReplayBufferHotkey));
	cls->register_function(std::make_shared<ipc::function>("OBS_service_splitFile", std::vector<ipc::type>{}, OBS_service_splitFile));
	cls->register_function(std::make_shared<ipc::function>("OBS_service_getLastReplay", std::vector<ipc::type>{}, OBS_service_getLastReplay));
	cls->register_function(std::make_shared<ipc::function>("OBS_service_getLastRecording", std::vector<ipc::type>{}, OBS_service_getLastRecording));

	cls->register_function(std::make_shared<ipc::function>("OBS_service_createVirtualCam", std::vector<ipc::type>{}, OBS_service_createVirtualCam));
	cls->register_function(std::make_shared<ipc::function>("OBS_service_startVirtualCam", std::vector<ipc::type>{}, OBS_service_startVirtualCam));
	cls->register_function(std::make_shared<ipc::function>("OBS_service_stopVirtualCam", std::vector<ipc::type>{}, OBS_service_stopVirtualCam));
	cls->register_function(std::make_shared<ipc::function>("OBS_service_updateVirtualCam", std::vector<ipc::type>{ipc::type::Int32, ipc::type::String},
							       OBS_service_updateVirtualCam));

	srv.register_collection(cls);
}

void OBS_service::OBS_service_resetAudioContext(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	if (!resetAudioContext(true)) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed OBS_service_resetAudioContext.");
	} else {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	}
	AUTO_DEBUG;
}

void OBS_service::OBS_service_resetVideoContext(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	int result = resetVideoContext(true);
	if (result == OBS_VIDEO_SUCCESS) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	} else {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Error));
		rval.push_back(ipc::value(result));
	}

	AUTO_DEBUG;
}

void OBS_service::OBS_service_setVideoInfo(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_video_info *canvas = osn::Video::Manager::GetInstance().find(args[0].value_union.ui64);
	setVideoInfo(canvas, static_cast<StreamServiceId>(args[1].value_union.i64));
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

	AUTO_DEBUG;
}
void OBS_service::OBS_service_startStreaming(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	StreamServiceId serviceid = static_cast<StreamServiceId>(args[0].value_union.i64);
	blog(LOG_INFO, "OBS_service_startStreaming - serviceid: %d", serviceid);

	bool dualStreamingMode = false;
	if (serviceid == StreamServiceId::Both) {
		// Both is the special mode than needs to be implemented as a full scale service one day.
		// For now for simplicity and compatibility we just fall back to the Main.
		serviceid = StreamServiceId::Main;
		dualStreamingMode = true;
	}

	if (isStreamingOutputActive(serviceid)) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		AUTO_DEBUG;
		return;
	}

	if (!startStreaming(serviceid, dualStreamingMode)) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to start streaming!");
	} else {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	}

#if !defined(_WIN32)
	util::CrashManager::UpdateBriefCrashInfoAppState();
#endif

	AUTO_DEBUG;
}

void OBS_service::OBS_service_startRecording(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	if (isRecordingOutputActive()) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		AUTO_DEBUG;
		return;
	}

	if (!startRecording()) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to start recording!");
	} else {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	}

#if !defined(_WIN32)
	util::CrashManager::UpdateBriefCrashInfoAppState();
#endif

	AUTO_DEBUG;
}

void OBS_service::OBS_service_startReplayBuffer(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	if (isReplayBufferOutputActive()) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		AUTO_DEBUG;
		return;
	}

	if (!startReplayBuffer()) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to start the replay buffer!");
	} else {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	}

#if !defined(_WIN32)
	util::CrashManager::UpdateBriefCrashInfoAppState();
#endif

	AUTO_DEBUG;
}

void OBS_service::OBS_service_stopStreaming(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	stopStreaming((bool)args[0].value_union.i32, static_cast<StreamServiceId>(args[1].value_union.i64));
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

#if !defined(_WIN32)
	util::CrashManager::UpdateBriefCrashInfoAppState();
#endif

	AUTO_DEBUG;
}

void OBS_service::OBS_service_stopRecording(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	stopRecording();
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

#if !defined(_WIN32)
	util::CrashManager::UpdateBriefCrashInfoAppState();
#endif

	AUTO_DEBUG;
}

void OBS_service::OBS_service_stopReplayBuffer(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	stopReplayBuffer((bool)args[0].value_union.i32);
	rpUsesRec = false;
	rpUsesStream = false;
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

#if !defined(_WIN32)
	util::CrashManager::UpdateBriefCrashInfoAppState();
#endif

	AUTO_DEBUG;
}

bool OBS_service::resetAudioContext(bool reload)
{
	struct obs_audio_info2 ai = {};

	if (reload)
		ConfigManager::getInstance().reloadConfig();

	ai.samples_per_sec = static_cast<uint32_t>(config_get_uint(ConfigManager::getInstance().getBasic(), "Audio", "SampleRate"));
	const char *channelSetupStr = config_get_string(ConfigManager::getInstance().getBasic(), "Audio", "ChannelSetup");

	if (strcmp(channelSetupStr, "Mono") == 0)
		ai.speakers = SPEAKERS_MONO;
	else if (strcmp(channelSetupStr, "2.1") == 0)
		ai.speakers = SPEAKERS_2POINT1;
	else if (strcmp(channelSetupStr, "4.0") == 0)
		ai.speakers = SPEAKERS_4POINT0;
	else if (strcmp(channelSetupStr, "4.1") == 0)
		ai.speakers = SPEAKERS_4POINT1;
	else if (strcmp(channelSetupStr, "5.1") == 0)
		ai.speakers = SPEAKERS_5POINT1;
	else if (strcmp(channelSetupStr, "7.1") == 0)
		ai.speakers = SPEAKERS_7POINT1;
	else
		ai.speakers = SPEAKERS_STEREO;

	bool lowLatencyAudioBuffering = config_get_bool(ConfigManager::getInstance().getGlobal(), "Audio", "LowLatencyAudioBuffering");
	if (lowLatencyAudioBuffering) {
		ai.max_buffering_ms = 20;
		ai.fixed_buffering = true;
	}

	return obs_reset_audio2(&ai);
}

static uint64_t basicConfigGetUInt(const char *section, const char *name, bool defaultConf)
{
	return (defaultConf) ? config_get_default_uint(ConfigManager::getInstance().getBasic(), section, name)
			     : config_get_uint(ConfigManager::getInstance().getBasic(), section, name);
}

static const char *basicConfigGetString(const char *section, const char *name, bool defaultConf)
{
	return (defaultConf) ? config_get_default_string(ConfigManager::getInstance().getBasic(), section, name)
			     : config_get_string(ConfigManager::getInstance().getBasic(), section, name);
}

static inline enum video_format GetVideoFormatFromName(const char *name)
{
	if (astrcmpi(name, "I420") == 0)
		return VIDEO_FORMAT_I420;
	else if (astrcmpi(name, "NV12") == 0)
		return VIDEO_FORMAT_NV12;
	else if (astrcmpi(name, "I444") == 0)
		return VIDEO_FORMAT_I444;
	else if (astrcmpi(name, "I010") == 0)
		return VIDEO_FORMAT_I010;
	else if (astrcmpi(name, "P010") == 0)
		return VIDEO_FORMAT_P010;
#if 0 //currently unsupported
	else if (astrcmpi(name, "YVYU") == 0)
		return VIDEO_FORMAT_YVYU;
	else if (astrcmpi(name, "YUY2") == 0)
		return VIDEO_FORMAT_YUY2;
	else if (astrcmpi(name, "UYVY") == 0)
		return VIDEO_FORMAT_UYVY;
#endif
	else
		return VIDEO_FORMAT_RGBA;
}

static inline enum obs_scale_type GetScaleType(const char *scaleTypeStr)
{
	if (scaleTypeStr != NULL) {
		if (astrcmpi(scaleTypeStr, "bilinear") == 0)
			return OBS_SCALE_BILINEAR;
		else if (astrcmpi(scaleTypeStr, "lanczos") == 0)
			return OBS_SCALE_LANCZOS;
		else
			return OBS_SCALE_BICUBIC;
	} else {
		return OBS_SCALE_BICUBIC;
	}
}

static inline const char *GetRenderModule(config_t *config)
{
	const char *renderer = config_get_string(config, "Video", "Renderer");

	const char *DL_D3D11 = "libobs-d3d11.dll";
	const char *DL_OPENGL;

#ifdef _WIN32
	DL_OPENGL = "libobs-opengl.dll";
#else
	DL_OPENGL = "libobs-opengl.dylib";
#endif

	if (renderer != NULL) {
		return (astrcmpi(renderer, "Direct3D 11") == 0) ? DL_D3D11 : DL_OPENGL;
	} else {
		return DL_D3D11;
	}
}

void GetFPSInteger(bool defaultConf, uint32_t &num, uint32_t &den)
{
	num = (uint32_t)basicConfigGetUInt("Video", "FPSInt", defaultConf);

	if (num <= 0)
		num = 1;

	den = 1;
}

void GetFPSFraction(bool defaultConf, uint32_t &num, uint32_t &den)
{
	num = (uint32_t)basicConfigGetUInt("Video", "FPSNum", defaultConf);
	if (num <= 0)
		num = 1;

	den = (uint32_t)basicConfigGetUInt("Video", "FPSDen", defaultConf);
	if (den <= 0)
		den = 1;

	if ((num / den) <= 0) {
		num = 1;
		den = 1;
	}
}

void GetFPSNanoseconds(bool defaultConf, uint32_t &num, uint32_t &den)
{
	num = 1000000000;
	den = (uint32_t)basicConfigGetUInt("Video", "FPSNS", defaultConf);
}

void GetFPSCommon(bool defaultConf, uint32_t &num, uint32_t &den)
{
	const char *val = basicConfigGetString("Video", "FPSCommon", defaultConf);
	if (val != NULL) {
		if (strcmp(val, "10") == 0) {
			num = 10;
			den = 1;
		} else if (strcmp(val, "20") == 0) {
			num = 20;
			den = 1;
		} else if (strcmp(val, "24 NTSC") == 0) {
			num = 24000;
			den = 1001;
		} else if (strcmp(val, "25") == 0) {
			num = 25;
			den = 1;
		} else if (strcmp(val, "29.97") == 0) {
			num = 30000;
			den = 1001;
		} else if (strcmp(val, "48") == 0) {
			num = 48;
			den = 1;
		} else if (strcmp(val, "59.94") == 0) {
			num = 60000;
			den = 1001;
		} else if (strcmp(val, "60") == 0) {
			num = 60;
			den = 1;
		} else {
			num = 30;
			den = 1;
		}
	} else {
		num = 30;
		den = 1;
		if (!defaultConf) {
			config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "FPSType", 0);
			config_set_string(ConfigManager::getInstance().getBasic(), "Video", "FPSCommon", "30");
			config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);
		}
	}
}

void GetConfigFPS(bool defaultConf, uint32_t &num, uint32_t &den)
{
	uint64_t type = basicConfigGetUInt("Video", "FPSType", defaultConf);
	if (type == 1) //"Integer"
		GetFPSInteger(defaultConf, num, den);
	else if (type == 2) //"Fraction"
		GetFPSFraction(defaultConf, num, den);
	else if (false) //"Nanoseconds", currently not implemented
		GetFPSNanoseconds(defaultConf, num, den);
	else
		GetFPSCommon(defaultConf, num, den);
}

/* some nice default output resolution vals */
static const double vals[] = {1.0, 1.25, (1.0 / 0.75), 1.5, (1.0 / 0.6), 1.75, 2.0, 2.25, 2.5, 2.75, 3.0};

static const size_t numVals = sizeof(vals) / sizeof(double);

int OBS_service::resetVideoContext(bool reload, bool retryWithDefaultConf)
{
	obs_video_info ovi = prepareOBSVideoInfo(reload, false);
	int errorcode = OBS_VIDEO_NOT_SUPPORTED;

	config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);

	blog(LOG_INFO, "About to reset the video context with the user configuration");
	errorcode = doResetVideoContext(&ovi);

	// OBS_VIDEO_NOT_SUPPORTED: any of the following functions fails:
	//   gl_init_extensions,
	//   CreateDXGIFactory1,
	//   DXGIFactory1::EnumAdapters1,
	//   D3D11CreateDevice,
	//   etc.
	// OBS_VIDEO_INVALID_PARAM: A parameter is invalid.
	// OBS_VIDEO_CURRENTLY_ACTIVE: Video is currently active.
	// OBS_VIDEO_MODULE_NOT_FOUND: Could not load a dynamic library (ovi.graphics_module):
	//   libobs-d3d11.dll,
	//   libobs-opengl.
	// OBS_VIDEO_FAIL: Generic failure.
	if (retryWithDefaultConf && (errorcode == OBS_VIDEO_FAIL || errorcode == OBS_VIDEO_INVALID_PARAM)) {
		blog(LOG_ERROR, "The video context reset with the user configuration failed: %d", errorcode);

		ovi = prepareOBSVideoInfo(false, true);

		blog(LOG_INFO, "About to reset the video context with the default configuration");
		errorcode = doResetVideoContext(&ovi);
		if (errorcode == OBS_VIDEO_SUCCESS) {
			keepFallbackVideoConfig(ovi);
		} else {
			blog(LOG_ERROR, "The video context reset with the default configuration failed: %d", errorcode);
		}
	}

	if (errorcode == OBS_VIDEO_SUCCESS) {
		const float sdr_white_level = (float)config_get_uint(ConfigManager::getInstance().getBasic(), "Video", "SdrWhiteLevel");
		const float hdr_nominal_peak_level = (float)config_get_uint(ConfigManager::getInstance().getBasic(), "Video", "HdrNominalPeakLevel");
		obs_set_video_levels(sdr_white_level, hdr_nominal_peak_level);
	}

	return errorcode;
}

void OBS_service::stopConnectingOutputs()
{
	for (auto &itr : streamingOutput) {
		if (itr == nullptr)
			continue;
		if (obs_output_connecting(itr))
			obs_output_force_stop(itr);
	}
}

int OBS_service::doResetVideoContext(obs_video_info *ovi)
{
	// Cannot disrupt video ptr inside obs while outputs are connecting
	stopConnectingOutputs();

	try {
		if (!base_canvas)
			base_canvas = obs_create_video_info();

		int ret = obs_set_video_info(base_canvas, ovi);

		return ret;
	} catch (const char *error) {
		blog(LOG_ERROR, "Failed to reset video: %s", error);
		return OBS_VIDEO_FAIL;
	}
}

static inline enum video_colorspace GetVideoColorSpaceFromName(const char *name)
{
	enum video_colorspace colorspace = VIDEO_CS_SRGB;
	if (strcmp(name, "601") == 0)
		colorspace = VIDEO_CS_601;
	else if (strcmp(name, "709") == 0)
		colorspace = VIDEO_CS_709;
	else if (strcmp(name, "2100PQ") == 0)
		colorspace = VIDEO_CS_2100_PQ;
	else if (strcmp(name, "2100HLG") == 0)
		colorspace = VIDEO_CS_2100_HLG;

	return colorspace;
}

obs_video_info OBS_service::prepareOBSVideoInfo(bool reload, bool defaultConf)
{
	obs_video_info ovi = {0};
#ifdef _WIN32
	ovi.graphics_module = "libobs-d3d11.dll";
#else
	ovi.graphics_module = "libobs-opengl.dylib";
#endif

	if (reload)
		ConfigManager::getInstance().reloadConfig();

	ovi.base_width = (uint32_t)basicConfigGetUInt("Video", "BaseCX", defaultConf);
	ovi.base_height = (uint32_t)basicConfigGetUInt("Video", "BaseCY", defaultConf);

	// Do we really need it?
#if 0
	const char* outputMode = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "Mode");

	if (outputMode == NULL) {
		outputMode = "Simple";
	}
#endif

	ovi.output_width = (uint32_t)basicConfigGetUInt("Video", "OutputCX", defaultConf);
	ovi.output_height = (uint32_t)basicConfigGetUInt("Video", "OutputCY", defaultConf);

	std::vector<std::pair<uint32_t, uint32_t>> resolutions = OBS_API::availableResolutions();
	uint32_t limit_cx = 1920;
	uint32_t limit_cy = 1080;

	if (ovi.base_width == 0 || ovi.base_height == 0) {
		for (int i = 0; i < resolutions.size(); i++) {
			uint32_t nbPixels = resolutions.at(i).first * resolutions.at(i).second;
			if (int(ovi.base_width * ovi.base_height) < nbPixels && nbPixels <= limit_cx * limit_cy) {
				ovi.base_width = resolutions.at(i).first;
				ovi.base_height = resolutions.at(i).second;
			}
		}
		if (ovi.base_width == 0 || ovi.base_height == 0) {
			ovi.base_width = 1920;
			ovi.base_height = 1080;
		}
	}

	if (!defaultConf) {
		config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "BaseCX", ovi.base_width);
		config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "BaseCY", ovi.base_height);
	}

	if (ovi.output_width == 0 || ovi.output_height == 0) {
		if (ovi.base_width > 1280 && ovi.base_height > 720) {
			int idx = 0;
			do {
				double use_val = 1.0;
				if (idx < numVals) {
					use_val = vals[idx];
				} else {
					use_val = vals[numVals - 1] + double(numVals - idx + 1) / 2.0;
				}
				ovi.output_width = uint32_t(double(ovi.base_width) / use_val);
				ovi.output_height = uint32_t(double(ovi.base_height) / use_val);
				idx++;
			} while (ovi.output_width > 1280 && ovi.output_height > 720);
		} else {
			ovi.output_width = ovi.base_width;
			ovi.output_height = ovi.base_height;
		}

		if (ovi.output_width == 0 || ovi.output_height == 0) {
			ovi.output_width = 1280;
			ovi.output_height = 720;
		}

		if (!defaultConf) {
			config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "OutputCX", ovi.output_width);
			config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "OutputCY", ovi.output_height);
		}
	}

	GetConfigFPS(defaultConf, ovi.fps_num, ovi.fps_den);

	const char *colorFormat = basicConfigGetString("Video", "ColorFormat", defaultConf);
	const char *colorSpace = basicConfigGetString("Video", "ColorSpace", defaultConf);
	const char *colorRange = basicConfigGetString("Video", "ColorRange", defaultConf);

	ovi.output_format = GetVideoFormatFromName(colorFormat);

	ovi.adapter = 0;
	ovi.gpu_conversion = true;

	ovi.colorspace = GetVideoColorSpaceFromName(colorSpace);
	ovi.range = astrcmpi(colorRange, "Full") == 0 ? VIDEO_RANGE_FULL : VIDEO_RANGE_PARTIAL;

	const char *scaleTypeStr = basicConfigGetString("Video", "ScaleType", defaultConf);

	ovi.scale_type = GetScaleType(scaleTypeStr);

	blog(LOG_DEBUG, "Prepared obs_video_info:");
	blog(LOG_DEBUG, "  base_width: %u", ovi.base_width);
	blog(LOG_DEBUG, "  base_height: %u", ovi.base_height);
	blog(LOG_DEBUG, "  output_width: %u", ovi.output_width);
	blog(LOG_DEBUG, "  output_height: %u", ovi.output_height);
	blog(LOG_DEBUG, "  fps_num: %u", ovi.fps_num);
	blog(LOG_DEBUG, "  fps_den: %u", ovi.fps_den);
	blog(LOG_DEBUG, "  output_format: %u", static_cast<uint32_t>(ovi.output_format));
	blog(LOG_DEBUG, "  colorspace: %u", static_cast<uint32_t>(ovi.colorspace));
	blog(LOG_DEBUG, "  range: %u", static_cast<uint32_t>(ovi.range));
	blog(LOG_DEBUG, "  scale_type: %u", static_cast<uint32_t>(ovi.scale_type));

	return ovi;
}

static void copyDefaultUIntToUserBasicConfig(const char *section, const char *name)
{
	config_set_uint(ConfigManager::getInstance().getBasic(), section, name,
			config_get_default_uint(ConfigManager::getInstance().getBasic(), section, name));
}

static void copyDefaultStringToUserBasicConfig(const char *section, const char *name)
{
	config_set_string(ConfigManager::getInstance().getBasic(), section, name,
			  config_get_default_string(ConfigManager::getInstance().getBasic(), section, name));
}

void OBS_service::keepFallbackVideoConfig(const obs_video_info &ovi)
{
	blog(LOG_DEBUG, "Saving the fallback/default video configuration to basic.ini");

	// Overall, we only copy and save parameters
	// which were used for the successful obs_reset_video call.
	// Some values come from config_get_default_uint/config_get_default_string.
	// The other values come from |ovi| because the default configuration
	// does not have some of the actual values.
	config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "BaseCX", ovi.base_width);
	config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "BaseCY", ovi.base_height);
	config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "OutputCX", ovi.output_width);
	config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "OutputCY", ovi.output_height);

	// Currently, there is no "FPSNS" in the default configuration,
	// So we do not copy it here.
	copyDefaultUIntToUserBasicConfig("Video", "FPSType");
	copyDefaultUIntToUserBasicConfig("Video", "FPSCommon");
	copyDefaultUIntToUserBasicConfig("Video", "FPSInt");
	copyDefaultUIntToUserBasicConfig("Video", "FPSNum");
	copyDefaultUIntToUserBasicConfig("Video", "FPSDen");

	copyDefaultStringToUserBasicConfig("Video", "ColorFormat");
	copyDefaultStringToUserBasicConfig("Video", "ColorSpace");
	copyDefaultStringToUserBasicConfig("Video", "ColorRange");

	copyDefaultStringToUserBasicConfig("Video", "ScaleType");

	config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);
}

void OBS_service::setVideoInfo(obs_video_info *ovi, StreamServiceId serviceId)
{
	videoInfo[serviceId] = ovi;
}

const char *FindAudioEncoderFromCodec(const char *type)
{
	const char *alt_enc_id = nullptr;
	size_t i = 0;

	while (obs_enum_encoder_types(i++, &alt_enc_id)) {
		if (alt_enc_id == nullptr)
			continue;
		const char *codec = obs_get_encoder_codec(alt_enc_id);
		if (strcmp(type, codec) == 0) {
			return alt_enc_id;
		}
	}

	return nullptr;
}

bool OBS_service::createAudioEncoder(obs_encoder_t **audioEncoder, std::string &id, const std::string &requested_id, int bitrate, const char *name, size_t idx)
{
	const char *id_ = nullptr;
	if (!requested_id.empty()) {
		if (strstr(requested_id.c_str(), "aac") != nullptr) {
			id_ = GetAACEncoderForBitrate(bitrate);
		} else if (strstr(requested_id.c_str(), "opus") != nullptr) {
			id_ = GetOpusEncoderForBitrate(bitrate);
		}
	} else {
		id_ = GetAACEncoderForBitrate(bitrate);
	}

	if (!id_) {
		id.clear();
		*audioEncoder = nullptr;
		return false;
	}

	if (id == id_)
		return true;

	id = id_;
	*audioEncoder = obs_audio_encoder_create(id_, name, nullptr, idx, nullptr);

	if (*audioEncoder) {
		return true;
	}

	return false;
}

std::string OBS_service::GetVideoEncoderName(StreamServiceId serviceId, bool isSimpleMode, bool recording, const char *encoder)
{
	const char *codec = obs_get_encoder_codec(encoder);

	if (codec == nullptr) {
		return "";
	}

	std::string encoder_name;
	encoder_name += isSimpleMode ? "simple_" : "adv_";
	encoder_name += recording ? "recording_" : "streaming_";
	encoder_name += codec;
	encoder_name += serviceId == StreamServiceId::Main ? "_main" : "";
	encoder_name += serviceId == StreamServiceId::Second ? "_second" : "";

	return encoder_name;
}

bool OBS_service::createVideoStreamingEncoder(StreamServiceId serviceId)
{
	std::string currentOutputMode = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "Mode");
	bool isSimpleMode = currentOutputMode.compare("Simple") == 0;
	const char *encoderId = nullptr;

	if (isSimpleMode) {
		encoderId = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "StreamEncoder");
	} else {
		encoderId = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "Encoder");
	}

	if (encoderId == NULL || !osn::EncoderUtils::isEncoderRegistered(encoderId) || osn::EncoderUtils::isInvalidAppleEncoder(encoderId)) {
		encoderId = ADVANCED_ENCODER_X264;
	}

	std::string encoder_name = GetVideoEncoderName(serviceId, isSimpleMode, false, encoderId);

	struct stat buffer;
	std::string streamConfigFile = ConfigManager::getInstance().getStream();
	bool fileExist = (os_stat(streamConfigFile.c_str(), &buffer) == 0);
	obs_data_t *data = obs_data_create_from_json_file_safe(streamConfigFile.c_str(), "bak");
	obs_data_t *settings = obs_encoder_defaults(encoderId);
	obs_data_apply(settings, data);

	obs_encoder_t *new_encoder = obs_video_encoder_create(encoderId, encoder_name.c_str(), settings, nullptr);
	OBS_service::setStreamingEncoder(new_encoder, serviceId);

	if (new_encoder == nullptr) {
		return false;
	}

	updateVideoStreamingEncoder(isSimpleMode, serviceId);

	return true;
}

void OBS_service::createAudioStreamingEncoder(StreamServiceId serviceId, bool isSimpleMode, const std::string &encoder_id)
{
	std::string id;
	obs_encoder_t *audioStreamingEncoder = nullptr;
	uint64_t trackIndex = 0;
	std::string audio_encoder_name = isSimpleMode ? "simple_audio_streaming" : "adv_audio_streaming";
	audio_encoder_name = audio_encoder_name + (serviceId == StreamServiceId::Main ? "_main" : "_second");

	if (!isSimpleMode) {
		trackIndex = config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "TrackIndex") - 1;
	}

	if (!createAudioEncoder(&audioStreamingEncoder, id, encoder_id, GetSimpleAudioBitrate(), audio_encoder_name.c_str(), trackIndex)) {
		throw "Failed to create audio streaming encoder";
	}

	setAudioStreamingEncoder(audioStreamingEncoder, serviceId);
}

static inline bool valid_string(const char *str)
{
	while (str && *str) {
		if (*(str++) != ' ')
			return true;
	}

	return false;
}
static void replace_text(struct dstr *str, size_t pos, size_t len, const char *new_text)
{
	struct dstr front = {0};
	struct dstr back = {0};

	dstr_left(&front, str, pos);
	dstr_right(&back, str, pos + len);
	dstr_copy_dstr(str, &front);
	dstr_cat(str, new_text);
	dstr_cat_dstr(str, &back);
	dstr_free(&front);
	dstr_free(&back);
}

static void erase_ch(struct dstr *str, size_t pos)
{
	struct dstr new_str = {0};
	dstr_left(&new_str, str, pos);
	dstr_cat(&new_str, str->array + pos + 1);
	dstr_free(str);
	*str = new_str;
}

char *osn_generate_formatted_filename(const char *extension, bool space, const char *format, int width, int height)
{
	time_t now = time(0);
	struct tm *cur_time;
	cur_time = localtime(&now);

	const size_t spec_count = 23;
	static const char *spec[][2] = {
		{"%CCYY", "%Y"}, {"%YY", "%y"}, {"%MM", "%m"}, {"%DD", "%d"}, {"%hh", "%H"}, {"%mm", "%M"}, {"%ss", "%S"}, {"%%", "%%"},

		{"%a", ""},      {"%A", ""},    {"%b", ""},    {"%B", ""},    {"%d", ""},    {"%H", ""},    {"%I", ""},    {"%m", ""},
		{"%M", ""},      {"%p", ""},    {"%S", ""},    {"%y", ""},    {"%Y", ""},    {"%z", ""},    {"%Z", ""},
	};

	char convert[128] = {0};
	struct dstr sf;
	struct dstr c = {0};
	size_t pos = 0;

	dstr_init_copy(&sf, format);

	while (pos < sf.len) {
		for (size_t i = 0; i < spec_count && !convert[0]; i++) {
			size_t len = strlen(spec[i][0]);

			const char *cmp = sf.array + pos;

			if (astrcmp_n(cmp, spec[i][0], len) == 0) {
				if (strlen(spec[i][1]))
					strftime(convert, sizeof(convert), spec[i][1], cur_time);
				else
					strftime(convert, sizeof(convert), spec[i][0], cur_time);

				dstr_copy(&c, convert);
				if (c.len && valid_string(c.array))
					replace_text(&sf, pos, len, convert);
			}
		}

		if (convert[0]) {
			pos += strlen(convert);
			convert[0] = 0;
		} else if (!convert[0] && sf.array[pos] == '%') {
			erase_ch(&sf, pos);
		} else {
			pos++;
		}
	}

	if (!space)
		dstr_replace(&sf, " ", "_");

	if (width > 0 && height > 0) {
		std::string resolution = std::to_string(width) + std::string("x") + std::to_string(height) + std::string("-");
		dstr_cat(&sf, resolution.c_str());

		dstr_cat_ch(&sf, (char)(rand() % 9 + 0x30));
		dstr_cat_ch(&sf, (char)(rand() % 9 + 0x30));
	}

	dstr_cat_ch(&sf, '.');
	dstr_cat(&sf, extension);
	dstr_free(&c);

	if (sf.len > 255)
		dstr_mid(&sf, &sf, 0, 255);

	return sf.array;
}

std::string GenerateSpecifiedFilename(const char *extension, bool noSpace, const char *format, int width, int height)
{
	char *filename = osn_generate_formatted_filename(extension, !noSpace, format, width, height);
	if (filename == nullptr) {
		throw "Invalid filename";
	}

	std::string result(filename);
	result = result;
	bfree(filename);
	return result;
}

static void FindBestFilename(std::string &strPath, bool noSpace)
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

static void remove_reserved_file_characters(std::string &s)
{
	replace(s.begin(), s.end(), '/', '_');
	replace(s.begin(), s.end(), '\\', '_');
	replace(s.begin(), s.end(), '*', '_');
	replace(s.begin(), s.end(), '?', '_');
	replace(s.begin(), s.end(), '"', '_');
	replace(s.begin(), s.end(), '|', '_');
	replace(s.begin(), s.end(), ':', '_');
	replace(s.begin(), s.end(), '>', '_');
	replace(s.begin(), s.end(), '<', '_');
}

bool OBS_service::createDefaultSimpleVideoRecordingEncoder()
{
	std::string encoderName = GetVideoEncoderName(StreamServiceId::Main, true, true, ADVANCED_ENCODER_X264);
	obs_encoder_t *newRecordingEncoder = obs_video_encoder_create(ADVANCED_ENCODER_X264, encoderName.c_str(), nullptr, nullptr);
	OBS_service::setRecordingEncoder(newRecordingEncoder);

	if (newRecordingEncoder == nullptr) {
		return false;
	}

	return true;
}

bool OBS_service::createService(StreamServiceId serviceId)
{
	const char *type = nullptr;
	obs_data_t *data = nullptr;
	obs_data_t *settings = nullptr;
	obs_data_t *hotkey_data = nullptr;
	struct stat buffer;

	auto CreateNewService = [&]() {
		obs_service_t *service = obs_service_create("rtmp_common", "default_service", nullptr, nullptr);
		if (service == nullptr) {
			return (obs_service_t *)nullptr;
		}

		data = obs_data_create();
		settings = obs_service_get_settings(service);

		obs_data_set_string(settings, "streamType", "rtmp_common");
		obs_data_set_string(settings, "service", "Twitch");
		obs_data_set_bool(settings, "show_all", 0);
		obs_data_set_string(settings, "server", "auto");
		obs_data_set_string(settings, "key", "");

		obs_data_set_string(data, "type", obs_service_get_type(service));
		obs_data_set_obj(data, "settings", settings);
		return service;
	};

	bool fileExist = (os_stat(ConfigManager::getInstance().getService(serviceId).c_str(), &buffer) == 0);
	if (!fileExist) {
		services[serviceId] = CreateNewService();

	} else {
		// Verify if the service.json was corrupted
		data = obs_data_create_from_json_file_safe(ConfigManager::getInstance().getService(serviceId).c_str(), "bak");
		if (data == nullptr) {
			blog(LOG_WARNING, "Failed to create data from service json, using default properties!");
			services[serviceId] = CreateNewService();
		} else {
			obs_data_set_default_string(data, "type", "rtmp_common");
			type = obs_data_get_string(data, "type");

			settings = obs_data_get_obj(data, "settings");
			hotkey_data = obs_data_get_obj(data, "hotkeys");

			// If the type is invalid it could cause a crash since internally obs uses strcmp (nullptr = undef behavior)
			if (type == nullptr || strlen(type) == 0) {
				obs_data_release(data);
				obs_data_release(hotkey_data);
				obs_data_release(settings);

				blog(LOG_WARNING, "Failed to retrieve a valid service type from the data, using default properties!");
				services[serviceId] = CreateNewService();

				// Create the service normally since the service.json info looks valid
			} else {
				services[serviceId] = obs_service_create(type, "default_service", settings, hotkey_data);
				if (services[serviceId] == nullptr) {
					obs_data_release(data);
					obs_data_release(hotkey_data);
					obs_data_release(settings);
					blog(LOG_ERROR, "Failed to create service using service info from a file!");
					return false;
				}

				obs_data_release(hotkey_data);
			}
		}
	}

	if (!obs_data_save_json_safe(data, ConfigManager::getInstance().getService(serviceId).c_str(), "tmp", "bak")) {
		blog(LOG_WARNING, "Failed to save service %s", ConfigManager::getInstance().getService(serviceId).c_str());
	}

	obs_data_release(settings);
	obs_data_release(data);

	return true;
}

std::string OBS_service::createStreamingOutputName(StreamServiceId serviceId)
{
	switch (serviceId) {
	case StreamServiceId::Main:
		return "main_stream_first";
	case StreamServiceId::Second:
		return "main_stream_second";
	default:
		return "main_stream_unset";
	}
}

bool OBS_service::createStreamingOutput(StreamServiceId serviceId)
{
	const char *type = getStreamOutputType(services[serviceId]);
	if (!type)
		type = "rtmp_output";

	streamingOutput[serviceId] = obs_output_create(type, createStreamingOutputName(serviceId).c_str(), nullptr, nullptr);
	if (streamingOutput[serviceId] == nullptr) {
		return false;
	}

	connectOutputSignals(serviceId);

	return true;
}

bool OBS_service::createRecordingOutput(void)
{
	recordingOutput = obs_output_create("ffmpeg_muxer", "recording_file_output", nullptr, nullptr);
	if (recordingOutput == nullptr) {
		return false;
	}

	connectOutputSignals(StreamServiceId::Main);

	return true;
}

void OBS_service::createReplayBufferOutput(void)
{
	replayBufferOutput = obs_output_create("replay_buffer", "replay_buffer_output", nullptr, nullptr);
	connectOutputSignals(StreamServiceId::Main);
}

void OBS_service::setupRecordingAudioEncoder(void)
{
	const char *audio_encoder = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecAEncoder");
	std::string id = "ffmpeg_aac";
	if (audio_encoder) {
		id = audio_encoder;
	}

	const char *codec = obs_get_encoder_codec(id.c_str());

	for (int i = 0; i < MAX_AUDIO_MIXES; i++) {
		std::ostringstream nameStream;
		nameStream << "adv_audio_recording_" << codec << "_" << i;

		AdvancedRecordingAudioEncodersID[i] = "";
		if (!createAudioEncoder(&(AdvancedRecordingAudioTracks[i]), AdvancedRecordingAudioEncodersID[i], id, GetAdvancedAudioBitrate(i),
					nameStream.str().c_str(), i)) {
			std::ostringstream errorStream;
			errorStream << "audio encoder failed id: " << id << nameStream.str();
			util::CrashManager::AddWarning(errorStream.str());
			throw std::runtime_error("Failed to create audio encoder (advanced output)");
		}
		obs_encoder_set_audio(AdvancedRecordingAudioTracks[i], obs_get_audio());
	}
}

void OBS_service::clearRecordingAudioEncoder(void)
{
	for (int i = 0; i < MAX_AUDIO_MIXES; i++) {
		if (AdvancedRecordingAudioTracks[i])
			obs_encoder_release(AdvancedRecordingAudioTracks[i]);
	}
}

void OBS_service::updateStreamingEncoders(bool isSimpleMode, StreamServiceId serviceId)
{
	if (!isStreaming[serviceId]) {
		updateAudioStreamingEncoder(isSimpleMode, serviceId);
		updateVideoStreamingEncoder(isSimpleMode, serviceId);
	}
}

static bool can_use_output(const char *prot, const char *output, const char *prot_test1, const char *prot_test2 = nullptr)
{
	return (strcmp(prot, prot_test1) == 0 || (prot_test2 && strcmp(prot, prot_test2) == 0)) && (obs_get_output_flags(output) & OBS_OUTPUT_SERVICE) != 0;
}

static bool return_first_id(void *data, const char *id)
{
	const char **output = (const char **)data;

	*output = id;
	return false;
}

const char *OBS_service::getStreamOutputType(const obs_service_t *service)
{
	const char *protocol = obs_service_get_protocol(service);
	const char *output = nullptr;

	if (!protocol) {
		blog(LOG_WARNING, "The service '%s' has no protocol set", obs_service_get_id(service));
		return nullptr;
	}

	if (!obs_is_output_protocol_registered(protocol)) {
		blog(LOG_WARNING, "The protocol '%s' is not registered", protocol);
		return nullptr;
	}

	/* Check if the service has a preferred output type */
	output = obs_service_get_preferred_output_type(service);
	if (output) {
		if ((obs_get_output_flags(output) & OBS_OUTPUT_SERVICE) != 0)
			return output;

		blog(LOG_WARNING, "The output '%s' is not registered, fallback to another one", output);
	}

	/* Otherwise, prefer first-party output types */
	if (can_use_output(protocol, "rtmp_output", "RTMP", "RTMPS")) {
		return "rtmp_output";
	} else if (can_use_output(protocol, "ffmpeg_hls_muxer", "HLS")) {
		return "ffmpeg_hls_muxer";
	} else if (can_use_output(protocol, "ffmpeg_mpegts_muxer", "SRT", "RIST")) {
		return "ffmpeg_mpegts_muxer";
	}

	/* If third-party protocol, use the first enumerated type */
	obs_enum_output_types_with_protocol(protocol, &output, return_first_id);
	if (output)
		return output;

	blog(LOG_WARNING, "No output compatible with the service '%s' is registered", obs_service_get_id(service));

	return nullptr;
}

static inline bool ServiceSupportsVodTrack(const char *service)
{
	static const char *vodTrackServices[] = {"Twitch"};

	for (const char *vodTrackService : vodTrackServices) {
		if (astrcmpi(vodTrackService, service) == 0)
			return true;
	}

	return false;
}

static bool IsVodTrackEnabled(obs_service_t *service)
{
	const bool advanced = config_get_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "UseAdvanced");
	const bool vodTrackEnabledAdv = config_get_bool(ConfigManager::getInstance().getBasic(), "AdvOut", "VodTrackEnabled");
	const bool enable = config_get_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "VodTrackEnabled");
	const bool enableForCustomServer = config_get_bool(ConfigManager::getInstance().getGlobal(), "General", "EnableCustomServerVodTrack");

	OBSDataAutoRelease settings = obs_service_get_settings(service);
	const char *name = obs_data_get_string(settings, "service");

	const char *id = obs_service_get_id(service);
	if (strcmp(id, "rtmp_custom") == 0)
		return enableForCustomServer ? enable : false;
	else
		return vodTrackEnabledAdv && ServiceSupportsVodTrack(name);
}

bool OBS_service::startSingleTrackStreaming(StreamServiceId serviceId)
{
	const char *type = getStreamOutputType(services[serviceId]);
	blog(LOG_INFO, "startSingleTrackStreaming output for service %p, %s", services[serviceId], (type ? type : "null"));

	if (!type)
		type = "rtmp_output";

	streamingOutput[serviceId] = obs_output_create(type, createStreamingOutputName(serviceId).c_str(), nullptr, nullptr);
	if (!streamingOutput[serviceId])
		return false;

	connectOutputSignals(serviceId);

	std::string currentOutputMode = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "Mode");
	bool isSimpleMode = currentOutputMode.compare("Simple") == 0;

	updateStreamingEncoders(isSimpleMode, serviceId);
	updateService(serviceId);
	updateStreamingOutput(serviceId);

	obs_output_set_video_encoder(streamingOutput[serviceId], videoStreamingEncoder[serviceId]);
	obs_output_set_audio_encoder(streamingOutput[serviceId], audioStreamingEncoder[serviceId], 0);
	obs_encoder_set_video_mix(audioStreamingEncoder[serviceId], obs_video_mix_get(videoInfo[serviceId], OBS_MAIN_VIDEO_RENDERING));

	setupVodTrack(isSimpleMode);

	blog(LOG_INFO, "Start Streaming for service %s using %s encoder.", serviceId == StreamServiceId::Main ? "Main" : "Second",
	     obs_encoder_get_id(videoStreamingEncoder[serviceId]));

	outdated_driver_error::instance()->set_active(true);
	isStreaming[serviceId] = obs_output_start(streamingOutput[serviceId]);
	outdated_driver_error::instance()->set_active(false);
	if (!isStreaming[serviceId]) {
		SignalInfo signal = SignalInfo("streaming", "stop");
		std::string outdated_driver_error = outdated_driver_error::instance()->get_error();
		if (outdated_driver_error.size() != 0) {
			signal.setErrorMessage(outdated_driver_error);
			signal.setCode(OBS_OUTPUT_OUTDATED_DRIVER);
		} else {
			const char *error = obs_output_get_last_error(streamingOutput[serviceId]);
			if (error) {
				signal.setErrorMessage(error);
				blog(LOG_INFO, "Last streaming error: %s", error);
			}
			signal.setCode(OBS_OUTPUT_ERROR);
		}

		std::unique_lock<std::mutex> ulock(signalMutex);
		outputSignal.push(signal);
	}
	return isStreaming[serviceId];
}

bool OBS_service::startMultiTrackStreaming(StreamServiceId serviceId, bool dualStreamingMode)
{
	blog(LOG_INFO, "startMultiTrackStreaming - serviceId: %d, dualStreamingMode: %d", serviceId, (int)dualStreamingMode);

	const bool is_custom = strncmp("rtmp_custom", obs_service_get_type(services[serviceId]), 11) == 0;

	OBSDataAutoRelease settings = obs_service_get_settings(services[serviceId]);
	std::string key = obs_data_get_string(settings, "key");

	const char *service_name = "<unknown>";
	if (is_custom && obs_data_has_user_value(settings, "service_name")) {
		service_name = obs_data_get_string(settings, "service_name");
	} else if (!is_custom) {
		service_name = obs_data_get_string(settings, "service");
	}

	std::optional<std::string> custom_rtmp_url;
	auto server = obs_data_get_string(settings, "server");
	if (strcmp(server, "auto") != 0) {
		custom_rtmp_url = server;
	}

	auto service_custom_server = obs_data_get_bool(settings, "using_custom_server");
	if (custom_rtmp_url.has_value()) {
		blog(LOG_INFO, "Using %sserver '%s'", service_custom_server ? "custom " : "", custom_rtmp_url->c_str());
	}

	auto auto_config_url = osn::MultitrackVideoAutoConfigURL(services[serviceId]);
	blog(LOG_INFO, "Auto config URL: %s", auto_config_url.c_str());

	const auto vodTrackIndex = int(config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "VodTrackIndex")) - 1;
	blog(LOG_INFO, "vodTrackIndex: %d", vodTrackIndex);

	auto vod_track_mixer = IsVodTrackEnabled(services[serviceId]) ? std::optional{vodTrackIndex} : std::nullopt;

	std::vector<obs_video_info *> canvases;
	canvases.push_back(videoInfo[serviceId]);
	if (dualStreamingMode) {
		if (serviceId == StreamServiceId::Main) {
			canvases.push_back(videoInfo[StreamServiceId::Second]);
		} else if (serviceId == StreamServiceId::Second) {
			canvases.push_back(videoInfo[StreamServiceId::Main]);
		}
	}

	auto go_live_post = osn::constructGoLivePost(canvases, key, std::nullopt, std::nullopt, vod_track_mixer.has_value());
	std::optional<osn::Config> go_live_config = osn::DownloadGoLiveConfig(auto_config_url, go_live_post);
	if (!go_live_config.has_value()) {
		throw std::runtime_error("startStreaming - go live config is empty");
	}

	const auto audio_bitrate = osn::GetMultitrackAudioBitrate();
	const auto audio_encoder_id = osn::GetSimpleAACEncoderForBitrate(audio_bitrate);

	std::vector<OBSEncoderAutoRelease> audio_encoders;
	std::shared_ptr<obs_encoder_group_t> video_encoder_group;
	auto output = osn::SetupOBSOutput("Enhanced Broadcasting", go_live_config.value(), audio_encoders, video_encoder_group, audio_encoder_id, 0,
					  vod_track_mixer, canvases);
	if (!output) {
		throw std::runtime_error("startStreaming - failed to create multitrack output");
	}

	auto multitrack_video_service = osn::create_service(*go_live_config, std::nullopt, ""); // Stream key is defined by config from Twitch
	if (!multitrack_video_service) {
		throw std::runtime_error("startStreaming - failed to create multitrack video service");
	}

	obs_output_set_service(output, multitrack_video_service);
	streamingOutput[serviceId] = obs_output_get_ref(output);
	connectOutputSignals(serviceId);

	// Register the BPM (Broadcast Performance Metrics) callback
	obs_output_add_packet_callback(output, bpm_inject, NULL);

	isStreaming[serviceId] = obs_output_start(output);

	enhancedBroadcastContext.emplace(osn::EnhancedBroadcastOutputObjects{
		std::move(output),
		video_encoder_group,
		std::move(audio_encoders),
		std::move(multitrack_video_service),
	});

	return isStreaming[serviceId];
}

bool OBS_service::startStreaming(StreamServiceId serviceId, bool dualStreamingMode)
{
	if (streamingOutput[serviceId]) {
		obs_output_release(streamingOutput[serviceId]);
		streamingOutput[serviceId] = nullptr;
	}

	StreamServiceId checkServiceId = serviceId;
	if (dualStreamingMode)
		checkServiceId = StreamServiceId::Both;
	if (!isConfiguredStreamingEncoderValid(checkServiceId)) {
		blog(LOG_ERROR, "The selected streaming encoder is not compatible with the current service. Please update these settings.");
		return false;
	}

	try {
		if (isTwitchStream(serviceId) && (osn::IsMultitrackVideoEnabled() || dualStreamingMode)) {
			return startMultiTrackStreaming(serviceId, dualStreamingMode);
		}

		return startSingleTrackStreaming(serviceId);
	} catch (std::runtime_error &e) {
		blog(LOG_ERROR, "startStreaming - error: %s", e.what());
		return false;
	}
}

void OBS_service::updateAudioStreamingEncoder(bool isSimpleMode, StreamServiceId serviceId)
{
	const char *codec = nullptr;

	if (streamingOutput[serviceId]) {
		codec = obs_output_get_supported_audio_codecs(streamingOutput[serviceId]);
	} else {
		const char *type = getStreamOutputType(services[serviceId]);
		if (!type)
			type = "rtmp_output";

		obs_output_t *currentOutput = obs_output_create(type, "temp_stream", nullptr, nullptr);
		if (!currentOutput)
			return;

		codec = obs_output_get_supported_audio_codecs(currentOutput);
		obs_output_release(currentOutput);
	}

	if (!codec) {
		return;
	}

	obs_encoder_t *enc = getAudioStreamingEncoder(serviceId);

	if (enc && obs_encoder_active(enc))
		return;

	if (enc) {
		setAudioStreamingEncoder(nullptr, serviceId);
		enc = nullptr;
	}

	if (strstr(codec, "aac") != NULL) {
		createAudioStreamingEncoder(serviceId, isSimpleMode, ffmpeg_aac_id);
		enc = audioStreamingEncoder[serviceId];
	} else if (strstr(codec, "opus") != NULL) {
		createAudioStreamingEncoder(serviceId, isSimpleMode, ffmpeg_opus_id);
		enc = audioStreamingEncoder[serviceId];
	} else {
		uint64_t trackIndex = config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "TrackIndex");
		const char *id = FindAudioEncoderFromCodec(codec);
		int audioBitrate = GetAdvancedAudioBitrate(static_cast<int>(trackIndex - 1));
		obs_data_t *settings = obs_data_create();
		obs_data_set_int(settings, "bitrate", static_cast<long long>(audioBitrate));

		enc = obs_audio_encoder_create(id, "alt_audio_enc", nullptr, isSimpleMode ? 0 : trackIndex - 1, nullptr);
		if (!(enc))
			return;

		obs_encoder_update(enc, settings);
		obs_data_release(settings);
	}
	obs_encoder_set_audio(enc, obs_get_audio());
	obs_encoder_set_video_mix(enc, obs_video_mix_get(videoInfo[serviceId], OBS_STREAMING_VIDEO_RENDERING));
	return;
}

void OBS_service::updateAudioRecordingEncoder(bool isSimpleMode)
{
	if (isRecording && rpUsesRec)
		return;

	if (isSimpleMode) {
		const char *audioEncoder = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecAEncoder");
		if (audioEncoder)
			audioSimpleRecEncID = "";

		const char *codec = obs_get_encoder_codec(audioEncoder);
		std::string name = "simple_audio_recording_" + std::string(codec);

		if (!createAudioEncoder(&audioSimpleRecordingEncoder, audioSimpleRecEncID, std::string(audioEncoder), 192, name.c_str(), 0))
			throw std::runtime_error("Failed to create audio simple recording encoder");

		obs_encoder_set_audio(audioSimpleRecordingEncoder, obs_get_audio());
		obs_encoder_set_video_mix(audioSimpleRecordingEncoder, obs_video_mix_get(0, OBS_RECORDING_VIDEO_RENDERING));
	} else {
		updateRecordingAudioTracks();
	}
}

void OBS_service::LoadRecordingPreset_Lossy(const char *encoderId)
{
	std::string encoderName = GetVideoEncoderName(StreamServiceId::Main, true, true, encoderId);
	obs_encoder_t *newRecordingEncoder = obs_video_encoder_create(encoderId, encoderName.c_str(), nullptr, nullptr);
	OBS_service::setRecordingEncoder(newRecordingEncoder);

	if (!videoRecordingEncoder)
		throw "Failed to create video recording encoder (simple output)";
}

void OBS_service::updateVideoRecordingEncoder(bool isSimpleMode)
{
	if (isRecording && rpUsesRec)
		return;

	const char *section = isSimpleMode ? "SimpleOutput" : "AdvOut";

	const char *quality = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecQuality");
	const char *encoder = config_get_string(ConfigManager::getInstance().getBasic(), section, "RecEncoder");
	std::string internalID = encoder;
	if (isSimpleMode)
		internalID = osn::EncoderUtils::getInternalEncoderFromSimple(encoder);

	videoEncoder = encoder;
	videoQuality = quality;
	ffmpegOutput = false;

	if (isSimpleMode) {
		lowCPUx264 = false;
		if (strcmp(encoder, SIMPLE_ENCODER_X264_LOWCPU) == 0)
			lowCPUx264 = true;
		LoadRecordingPreset_Lossy(internalID.c_str());
		usingRecordingPreset = true;
		updateVideoRecordingEncoderSettings();
	} else {
		if (encoder && strcmp(encoder, ENCODER_NVENC_H264_TEX) != 0) {
			unsigned int cx = 0;
			unsigned int cy = 0;

			bool rescale = config_get_bool(ConfigManager::getInstance().getBasic(), "AdvOut", "RecRescale");
			const char *rescaleRes = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecRescaleRes");

			if (rescale && rescaleRes && *rescaleRes) {
				if (sscanf(rescaleRes, "%ux%u", &cx, &cy) != 2) {
					cx = 0;
					cy = 0;
				}
				obs_encoder_set_scaled_size(videoRecordingEncoder, cx, cy);
			}
		}
	}
	if (obs_get_multiple_rendering()) {
		obs_encoder_set_video_mix(videoRecordingEncoder, obs_video_mix_get(0, OBS_RECORDING_VIDEO_RENDERING));
	} else {
		obs_encoder_set_video_mix(videoRecordingEncoder, obs_video_mix_get(0, OBS_MAIN_VIDEO_RENDERING));
	}
}

bool isConfiguredStreamingEncoderValid(StreamServiceId serviceId)
{
	const char *mode = NULL;
	const char *curEncoder = NULL;
	bool validEncoder = false;
	bool simpleMode = false;

	//get mode and configured encoder
	mode = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "Mode");
	if (mode == nullptr) {
		mode = "Simple";
	}
	simpleMode = (strcmp(mode, "Simple") == 0);

	if (!simpleMode) {
		curEncoder = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "Encoder");
	} else {
		curEncoder = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "StreamEncoder");
	}

	if (serviceId == StreamServiceId::Both) {
		validEncoder = osn::EncoderUtils::isEncoderCompatibleStreaming(OBS_service::getService(StreamServiceId::Main), curEncoder, simpleMode) &&
			       osn::EncoderUtils::isEncoderCompatibleStreaming(OBS_service::getService(StreamServiceId::Second), curEncoder, simpleMode);
	} else {
		validEncoder = osn::EncoderUtils::isEncoderCompatibleStreaming(OBS_service::getService(serviceId), curEncoder, simpleMode);
	}

	return validEncoder;
}

bool isConfiguredRecordingEncoderValid(bool checkReplayBuffer)
{
	const char *encoder = nullptr;
	const std::string simpleQuality = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecQuality");
	const std::string advancedRecEncoder = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecEncoder");
	const std::string currentOutputMode = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "Mode");
	bool isSimpleMode = currentOutputMode.compare("Simple") == 0;
	const char *section = isSimpleMode ? "SimpleOutput" : "AdvOut";
	bool replayBufferUsesStream = config_get_bool(ConfigManager::getInstance().getBasic(), section, "replayBufferUseStreamOutput");
	bool simpleUsesStream = false;
	bool advancedUsesStream = false;

	if (isSimpleMode && simpleQuality.compare("Lossless") == 0) {
		//lossless recording doesn't use encoder/format settings
		return true;
	}

	if (!osn::IsMultitrackVideoEnabled()) {
		simpleUsesStream = isSimpleMode && simpleQuality.compare("Stream") == 0;
		advancedUsesStream = !isSimpleMode && (advancedRecEncoder.compare("") == 0 || advancedRecEncoder.compare("none") == 0);
		//if checking replay buffer, check use stream OR use recording and recording uses stream
		if (checkReplayBuffer) {
			if (isSimpleMode)
				simpleUsesStream |= replayBufferUsesStream;
			else
				advancedUsesStream |= replayBufferUsesStream;
		}
	}

	//check encoder for recording compatibility with the configured recording format
	char *field = "RecEncoder";
	if (advancedUsesStream)
		field = "Encoder";
	else if (simpleUsesStream)
		field = "StreamEncoder";
	encoder = config_get_string(ConfigManager::getInstance().getBasic(), section, field);
	std::string container = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), section, "RecFormat"));
	if (!osn::EncoderUtils::isEncoderCompatibleRecording(encoder, container, isSimpleMode)) {
		blog(LOG_ERROR, "The selected encoder '%s' is not compatible with the recording format '%s'.", encoder, container.c_str());
		return false;
	}
	return true;
}

bool OBS_service::updateRecordingEncoders(bool isSimpleMode, StreamServiceId serviceId)
{
	const std::string simpleQuality = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecQuality");
	const std::string advancedQuality = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecEncoder");
	bool useStreamEncoder = false;

	bool simpleUsesStream = false;
	bool advancedUsesStream = false;
	if (!osn::IsMultitrackVideoEnabled()) {
		simpleUsesStream = isSimpleMode && simpleQuality.compare("Stream") == 0;
		advancedUsesStream = !isSimpleMode && (advancedQuality.compare("") == 0 || advancedQuality.compare("none") == 0);
	}

	if (simpleUsesStream || advancedUsesStream) {
		usingRecordingPreset = false;
		ffmpegOutput = false;

		if (isSimpleMode) {
			if (!isStreaming[serviceId]) {
				updateAudioStreamingEncoder(isSimpleMode, serviceId);
			}

			if (!obs_get_multiple_rendering()) {
				useStreamEncoder = true;
			} else {
				duplicate_encoder(&audioSimpleRecordingEncoder, audioStreamingEncoder[serviceId], 0);
				obs_encoder_set_audio(audioSimpleRecordingEncoder, obs_get_audio());
				useStreamEncoder = false;
			}
		} else {
			updateAudioRecordingEncoder(isSimpleMode);
		}

		if (!isStreaming[serviceId]) {
			updateVideoStreamingEncoder(isSimpleMode, serviceId);
		}

		if (!obs_get_multiple_rendering()) {
			obs_encoder_set_video_mix(videoStreamingEncoder[serviceId], obs_video_mix_get(0, OBS_MAIN_VIDEO_RENDERING));
			useStreamEncoder = true;
		} else {
			duplicate_encoder(&videoRecordingEncoder, videoStreamingEncoder[serviceId]);
			obs_encoder_set_video_mix(videoRecordingEncoder, obs_video_mix_get(0, OBS_RECORDING_VIDEO_RENDERING));
			useStreamEncoder = false;
		}
	} else {
		updateAudioRecordingEncoder(isSimpleMode);
		updateVideoRecordingEncoder(isSimpleMode);

		useStreamEncoder = false;
	}
	return useStreamEncoder;
}

bool OBS_service::startRecording(void)
{
	//check encoder for recording compatibility with the configured recording format
	if (!isConfiguredRecordingEncoderValid(false)) {
		//update config recording format = mkv because it supports all encoder types
		const char *mode = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "Mode");
		if (mode == nullptr) {
			mode = "Simple";
		}
		if (strcmp(mode, "Simple") == 0) {
			config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecFormat", "mkv");
		} else {
			config_set_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecFormat", "mkv");
		}
		config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);
		blog(LOG_INFO, "Recording format was updated to 'mkv' to ensure compatibility with the selected encoder.");

		//set failure info
		SignalInfo signal = SignalInfo("recording", "stop");
		signal.setErrorMessage("The selected recording encoder is not compatible with the selected recording format. Please update these settings.");
		std::unique_lock<std::mutex> ulock(signalMutex);
		signal.setCode(OBS_OUTPUT_ERROR);
		outputSignal.push(signal);
		return false;
	}

	if (recordingOutput)
		obs_output_release(recordingOutput);

	createRecordingOutput();

	std::string currentOutputMode = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "Mode");
	bool isSimpleMode = currentOutputMode.compare("Simple") == 0;
	std::string simpleQuality = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecQuality");

	bool useStreamEncoder = false;

	usingRecordingPreset = true;
	if (isSimpleMode && simpleQuality.compare("Lossless") == 0) {
		LoadRecordingPreset_Lossless();
		ffmpegOutput = true;
	} else {
		if (!(obs_get_multiple_rendering() && obs_get_replay_buffer_rendering_mode() == OBS_RECORDING_REPLAY_BUFFER_RENDERING &&
		      isReplayBufferActive)) {
			useStreamEncoder = updateRecordingEncoders(isSimpleMode, StreamServiceId::Main);
		}
	}
	updateFfmpegOutput(isSimpleMode, recordingOutput);

	obs_output_set_video_encoder(recordingOutput, useStreamEncoder ? videoStreamingEncoder[0] : videoRecordingEncoder);
	if (isSimpleMode) {
		obs_output_set_audio_encoder(recordingOutput, useStreamEncoder ? audioStreamingEncoder[0] : audioSimpleRecordingEncoder, 0);
	} else {
		int tracks = int(config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "RecTracks"));
		int idx = 0;
		for (int i = 0; i < MAX_AUDIO_MIXES; i++) {
			if ((tracks & (1 << i)) != 0) {
				obs_output_set_audio_encoder(recordingOutput, AdvancedRecordingAudioTracks[i], idx);
				idx++;
			}
		}
	}

	blog(LOG_INFO, "Start Recording using %s encoder.",
	     useStreamEncoder ? obs_encoder_get_id(videoStreamingEncoder[0]) : obs_encoder_get_id(videoRecordingEncoder));

	outdated_driver_error::instance()->set_active(true);
	isRecording = obs_output_start(recordingOutput);
	{
		const char *error = obs_output_get_last_error(recordingOutput);
		if (error) {
			blog(LOG_INFO, "Last recording error: %s", error);
		} else {
			blog(LOG_INFO, "Last recording error: (null)");
		}
	}
	outdated_driver_error::instance()->set_active(false);
	if (!isRecording) {
		blog(LOG_INFO, "Recording start failed recording error");
		SignalInfo signal = SignalInfo("recording", "stop");
		std::string outdated_driver_error = outdated_driver_error::instance()->get_error();
		if (outdated_driver_error.size() != 0) {
			signal.setErrorMessage(outdated_driver_error);
			signal.setCode(OBS_OUTPUT_OUTDATED_DRIVER);
		} else {
			const char *error = obs_output_get_last_error(recordingOutput);
			if (error) {
				signal.setErrorMessage(error);
				blog(LOG_INFO, "Last recording error: %s", error);
			}
			signal.setCode(OBS_OUTPUT_ERROR);
		}
		std::unique_lock<std::mutex> ulock(signalMutex);
		outputSignal.push(signal);
	}
	return isRecording;
}

void OBS_service::stopStreaming(bool forceStop, StreamServiceId serviceId)
{
	blog(LOG_INFO, "stopStreaming - forceStop: %d, serviceId: %d", forceStop, serviceId);

	obs_output_t *output = streamingOutput[serviceId];
	if (!output) {
		blog(LOG_WARNING, "stopStreaming - output is null");
		isStreaming[serviceId] = false;
		return;
	}

	if (!obs_output_active(output) && !obs_output_connecting(output) && !obs_output_reconnecting(output)) {
		blog(LOG_INFO, "stopStreaming - stream is not active, skip stopping");
		isStreaming[serviceId] = false;
		return;
	}

	if (forceStop)
		obs_output_force_stop(output);
	else
		obs_output_stop(output);

	// Unregister the BPM (Broadcast Performance Metrics) callback and destroy the allocated metrics data.
	if (isTwitchStream(serviceId) && osn::IsMultitrackVideoEnabled()) {
		obs_output_remove_packet_callback(output, bpm_inject, NULL);
		bpm_destroy(output);
	}

	waitReleaseWorker();

	releaseWorker = std::thread(releaseStreamingOutput, serviceId);

	isStreaming[serviceId] = false;
}

void OBS_service::stopRecording(void)
{
	blog(LOG_WARNING, "stopRecording with %s", obs_output_active(recordingOutput) ? "recordingOutput active" : "recordingOutput not active");
	obs_output_stop(recordingOutput);
	isRecording = false;
}

void OBS_service::updateReplayBufferOutput(bool isSimpleMode, bool useStreamEncoder)
{
	blog(LOG_INFO, "updateReplayBufferOutput - isSimpleMode: %d, useStreamEncoder: %d", (int)isSimpleMode, (int)useStreamEncoder);

	const char *path;
	std::string format;
	const char *mux;
	bool noSpace;
	const char *fileNameFormat;
	bool overwriteIfExists;
	const char *rbPrefix;
	const char *rbSuffix;
	int64_t rbTime;
	int64_t rbSize;

	if (isSimpleMode) {
		path = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "FilePath");
		format = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecFormat");
		mux = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "MuxerCustom");
		noSpace = config_get_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "FileNameWithoutSpace");
		fileNameFormat = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "FilenameFormatting");
		overwriteIfExists = config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "OverwriteIfExists");
		rbPrefix = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecRBPrefix");
		rbSuffix = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecRBSuffix");
		rbTime = int(config_get_int(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecRBTime"));
		rbSize = int(config_get_int(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecRBSize"));
	} else {
		path = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecFilePath");
		format = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecFormat");
		fileNameFormat = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "FilenameFormatting");
		overwriteIfExists = config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "OverwriteIfExists");
		noSpace = config_get_bool(ConfigManager::getInstance().getBasic(), "AdvOut", "RecFileNameWithoutSpace");
		rbPrefix = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecRBPrefix");
		rbSuffix = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecRBSuffix");
		rbTime = config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "RecRBTime");
		rbSize = config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "RecRBSize");
	}
	format = GetFormatExt(format);
	std::string f;
	if (rbPrefix && *rbPrefix) {
		f += rbPrefix;
		if (f.back() != ' ')
			f += " ";
	}
	f += fileNameFormat;
	if (rbSuffix && *rbSuffix) {
		if (*rbSuffix != ' ')
			f += " ";
		f += rbSuffix;
	}
	remove_reserved_file_characters(f);

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "directory", path);
	obs_data_set_string(settings, "format", f.c_str());
	obs_data_set_string(settings, "extension", format.c_str());
	obs_data_set_bool(settings, "allow_spaces", !noSpace);
	obs_data_set_int(settings, "max_time_sec", rbTime);
	obs_data_set_int(settings, "max_size_mb", usingRecordingPreset ? rbSize : 0);

	if (!isSimpleMode) {
		bool usesBitrate = false;
		obs_data_t *streamEncSettings = obs_data_create_from_json_file_safe(ConfigManager::getInstance().getStream().c_str(), "bak");
		obs_data_t *recordEncSettings = obs_data_create_from_json_file_safe(ConfigManager::getInstance().getRecord().c_str(), "bak");

		const char *rate_control = obs_data_get_string(useStreamEncoder ? streamEncSettings : recordEncSettings, "rate_control");
		if (!rate_control)
			rate_control = "";
		usesBitrate = astrcmpi(rate_control, "CBR") == 0 || astrcmpi(rate_control, "VBR") == 0 || astrcmpi(rate_control, "ABR") == 0;
		obs_data_set_int(settings, "max_size_mb", usesBitrate ? 0 : rbSize);
	}

	obs_output_update(replayBufferOutput, settings);
	obs_data_release(settings);
}

bool OBS_service::startReplayBuffer(void)
{
	//check encoder for recording compatibility with the configured recording format before starting recording
	if (!isConfiguredRecordingEncoderValid(true)) {
		//update config recording format = mkv because it supports all encoder types
		const char *mode = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "Mode");
		if (mode == nullptr) {
			mode = "Simple";
		}
		if (strcmp(mode, "Simple") == 0) {
			config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecFormat", "mkv");
		} else {
			config_set_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecFormat", "mkv");
		}
		config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);
		blog(LOG_INFO, "Recording format was updated to 'mkv' to ensure compatibility with the selected encoder.");

		SignalInfo signal = SignalInfo("replay-buffer", "stop");
		isReplayBufferActive = false;
		rpUsesRec = false;
		rpUsesStream = false;
		signal.setErrorMessage("The selected Replay Buffer settings are not compatible with the recording format. Please update these settings.");
		std::unique_lock<std::mutex> ulock(signalMutex);
		signal.setCode(OBS_OUTPUT_ERROR);
		outputSignal.push(signal);
		return false;
	}

	if (replayBufferOutput)
		obs_output_release(replayBufferOutput);

	replayBufferOutput = obs_output_create("replay_buffer", "ReplayBuffer", nullptr, nullptr);
	if (!replayBufferOutput)
		return false;

	connectOutputSignals(StreamServiceId::Main);

	std::string currentOutputMode = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "Mode");
	bool isSimpleMode = currentOutputMode.compare("Simple") == 0;
	bool useStreamEncoder = false;

	if (obs_get_multiple_rendering() && obs_get_replay_buffer_rendering_mode() == OBS_STREAMING_REPLAY_BUFFER_RENDERING) {
		updateStreamingEncoders(isSimpleMode, StreamServiceId::Main);
		useStreamEncoder = true;
		rpUsesStream = true;
	} else if (obs_get_multiple_rendering() && obs_get_replay_buffer_rendering_mode() == OBS_RECORDING_REPLAY_BUFFER_RENDERING) {
		if (!isRecording)
			updateRecordingEncoders(isSimpleMode, StreamServiceId::Main);
		rpUsesRec = true;
	} else {
		useStreamEncoder = isRecording ? !usingRecordingPreset : updateRecordingEncoders(isSimpleMode, StreamServiceId::Main);

		rpUsesRec = true;
	}

	updateFfmpegOutput(isSimpleMode, replayBufferOutput);
	updateReplayBufferOutput(isSimpleMode, useStreamEncoder);

	obs_output_set_video_encoder(replayBufferOutput, useStreamEncoder ? videoStreamingEncoder[0] : videoRecordingEncoder);
	if (isSimpleMode) {
		obs_output_set_audio_encoder(replayBufferOutput, useStreamEncoder ? audioStreamingEncoder[0] : audioSimpleRecordingEncoder, 0);
	} else {
		int tracks = int(config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "RecTracks"));
		int idx = 0;
		for (int i = 0; i < MAX_AUDIO_MIXES; i++) {
			if ((tracks & (1 << i)) != 0) {
				obs_output_set_audio_encoder(replayBufferOutput, AdvancedRecordingAudioTracks[i], idx);
				idx++;
			}
		}
	}

	blog(LOG_INFO, "Start Replay Buffer using %s encoder.",
	     useStreamEncoder ? obs_encoder_get_id(videoStreamingEncoder[0]) : obs_encoder_get_id(videoRecordingEncoder));

	outdated_driver_error::instance()->set_active(true);
	bool result = obs_output_start(replayBufferOutput);
	outdated_driver_error::instance()->set_active(false);
	if (!result) {
		SignalInfo signal = SignalInfo("replay-buffer", "stop");
		isReplayBufferActive = false;
		rpUsesRec = false;
		rpUsesStream = false;
		std::string outdated_driver_error = outdated_driver_error::instance()->get_error();
		if (outdated_driver_error.size() != 0) {
			signal.setErrorMessage(outdated_driver_error);
			signal.setCode(OBS_OUTPUT_OUTDATED_DRIVER);
		} else {
			const char *error = obs_output_get_last_error(replayBufferOutput);
			if (error) {
				signal.setErrorMessage(error);
				blog(LOG_INFO, "Last replay buffer error: %s", error);
			}
			signal.setCode(OBS_OUTPUT_ERROR);
		}
		std::unique_lock<std::mutex> ulock(signalMutex);
		outputSignal.push(signal);
	} else {
		isReplayBufferActive = true;
	}

	return isReplayBufferActive;
}

void OBS_service::stopReplayBuffer(bool forceStop)
{
	if (forceStop)
		obs_output_force_stop(replayBufferOutput);
	else
		obs_output_stop(replayBufferOutput);
}

obs_service_t *OBS_service::getService(StreamServiceId serviceId)
{
	if (serviceId >= services.size())
		return nullptr;

	return services[serviceId];
}

void OBS_service::setService(obs_service_t *newService, StreamServiceId serviceId)
{
	if (serviceId >= services.size())
		return;

	obs_service_release(services[serviceId]);
	services[serviceId] = newService;
}

void OBS_service::saveService(void)
{
	saveService(services[StreamServiceId::Main], StreamServiceId::Main);
	saveService(services[StreamServiceId::Second], StreamServiceId::Second);
}

void OBS_service::saveService(obs_service_t *service, StreamServiceId serviceId)
{
	if (!service)
		return;

	obs_data_t *data = obs_data_create();
	obs_data_t *settings = obs_service_get_settings(service);

	const char *serviceType = obs_service_get_type(service);

	if (serviceType && strlen(serviceType) > 0) {
		obs_data_set_string(data, "type", serviceType);
		obs_data_set_obj(data, "settings", settings);

		if (!obs_data_save_json_safe(data, ConfigManager::getInstance().getService(serviceId).c_str(), "tmp", "bak"))
			blog(LOG_WARNING, "Failed to save service");

		obs_service_update(service, settings);
	}
	obs_data_release(settings);
	obs_data_release(data);
}

bool OBS_service::isStreamingOutputActive(StreamServiceId serviceId)
{
	return obs_output_active(streamingOutput[serviceId]);
}

bool OBS_service::isRecordingOutputActive(void)
{
	return obs_output_active(recordingOutput);
}

bool OBS_service::isReplayBufferOutputActive(void)
{
	return obs_output_active(replayBufferOutput);
}

int OBS_service::GetSimpleAudioBitrate(void)
{
	int bitrate = (int)config_get_uint(ConfigManager::getInstance().getBasic(), "SimpleOutput", "ABitrate");

	return FindClosestAvailableAACBitrate(bitrate);
}

int OBS_service::GetAdvancedAudioBitrate(int i)
{
	static const char *names[] = {
		"Track1Bitrate", "Track2Bitrate", "Track3Bitrate", "Track4Bitrate", "Track5Bitrate", "Track6Bitrate",
	};
	int bitrate = (int)config_get_uint(ConfigManager::getInstance().getBasic(), "AdvOut", names[i]);
	return FindClosestAvailableAACBitrate(bitrate);
}

void OBS_service::updateVideoStreamingEncoder(bool isSimpleMode, StreamServiceId serviceId)
{
	if (videoStreamingEncoder[serviceId] && obs_encoder_active(videoStreamingEncoder[serviceId]))
		return;

	if (isSimpleMode) {
		obs_data_t *h264Settings = obs_data_create();
		obs_data_t *aacSettings = obs_data_create();

		int videoBitrate = int(config_get_uint(ConfigManager::getInstance().getBasic(), "SimpleOutput", "VBitrate"));
		int audioBitrate = GetSimpleAudioBitrate();
		bool advanced = config_get_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "UseAdvanced");
		bool enforceBitrate = config_get_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "EnforceBitrate");
		const char *custom = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "x264Settings");
		const char *encoder = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "StreamEncoder");
		std::string encoderID = "";
		std::string presetType = "";
		const char *preset = nullptr;

		if (encoder != NULL) {
			std::string presetType = osn::EncoderUtils::getEncoderPreset(encoder);
			encoderID = osn::EncoderUtils::getInternalEncoderFromSimple(encoder);

			if (!presetType.empty()) {
				preset = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", presetType.c_str());
				//if this calls fails and preset type is NVENC, use legacy NVENC preset for backward compatibility
				if (preset == NULL && presetType == PRESET_NVENC) {
					presetType = PRESET_NVENC_DEP;
					preset = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", presetType.c_str());
					if (preset != NULL) {
						//convert the old preset to new
						const char *oldValue = preset;
						preset = osn::EncoderUtils::convertNvencSimplePreset(oldValue);
						blog(LOG_INFO, "NVENC preset converted from %s to %s", oldValue, preset);
					}
				}
			}

			// Here and in other places we repeat the same pattern.
			// Avoiding case when to an output there might not be any attached video encoder which can lead to crash.
			std::string encoder_name = GetVideoEncoderName(serviceId, true, false, encoderID.c_str());
			obs_encoder_t *streamingEncoder = obs_video_encoder_create(encoderID.c_str(), encoder_name.c_str(), nullptr, nullptr);
			setStreamingEncoder(streamingEncoder, serviceId);
		}

		if (videoBitrate == 0) {
			videoBitrate = 4500;
			config_set_uint(ConfigManager::getInstance().getBasic(), "SimpleOutput", "VBitrate", videoBitrate);
			config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);
		}

		obs_data_set_string(h264Settings, "rate_control", "CBR");
		obs_data_set_int(h264Settings, "bitrate", videoBitrate);

		if (advanced) {
			obs_data_set_string(h264Settings, "preset", preset);
			obs_data_set_string(h264Settings, "x264opts", custom);
		}

		obs_data_set_string(aacSettings, "rate_control", "CBR");
		obs_data_set_int(aacSettings, "bitrate", audioBitrate);

		obs_service_apply_encoder_settings(services[serviceId], h264Settings, aacSettings);

		if (advanced && !enforceBitrate) {
			obs_data_set_int(h264Settings, "bitrate", videoBitrate);
			obs_data_set_int(aacSettings, "bitrate", audioBitrate);
		}

		video_t *video = obs_get_video();
		enum video_format format = video_output_get_format(video);

		switch (format) {
		case VIDEO_FORMAT_I420:
		case VIDEO_FORMAT_NV12:
		case VIDEO_FORMAT_I010:
		case VIDEO_FORMAT_P010:
			break;
		default:
			obs_encoder_set_preferred_video_format(videoStreamingEncoder[serviceId], VIDEO_FORMAT_NV12);
		}

		if (osn::EncoderUtils::getEncoderFamily(encoderID.c_str()) == FAMILY_APPLE) {
			const char *profile = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "Profile");
			if (profile)
				obs_data_set_string(h264Settings, "profile", profile);
		}

		obs_encoder_update(videoStreamingEncoder[serviceId], h264Settings);
		obs_encoder_update(audioStreamingEncoder[serviceId], aacSettings);

		obs_data_release(h264Settings);
		obs_data_release(aacSettings);
	} else {
		const char *streamEncoder = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "Encoder");
		if (streamEncoder && strcmp(streamEncoder, ENCODER_NVENC_H264_TEX) != 0) {
			unsigned int cx = 0;
			unsigned int cy = 0;

			bool rescale = config_get_bool(ConfigManager::getInstance().getBasic(), "AdvOut", "Rescale");
			const char *rescaleRes = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RescaleRes");

			if (rescale && rescaleRes && *rescaleRes) {
				if (sscanf(rescaleRes, "%ux%u", &cx, &cy) != 2) {
					cx = 0;
					cy = 0;
				}
				obs_encoder_set_scaled_size(videoStreamingEncoder[serviceId], cx, cy);
			}
		}
	}
	if (obs_get_multiple_rendering()) {
		obs_encoder_set_video_mix(videoStreamingEncoder[serviceId], obs_video_mix_get(videoInfo[serviceId], OBS_STREAMING_VIDEO_RENDERING));
	} else {
		obs_encoder_set_video_mix(videoStreamingEncoder[serviceId], obs_video_mix_get(videoInfo[serviceId], OBS_MAIN_VIDEO_RENDERING));
	}
}

std::string OBS_service::GetDefaultVideoSavePath(void)
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

void OBS_service::updateService(StreamServiceId serviceId)
{
	OBSDataAutoRelease settings = obs_service_get_settings(services[serviceId]);
	const char *platform = obs_data_get_string(settings, "service");

	const char *server = obs_service_get_connect_info(services[serviceId], OBS_SERVICE_CONNECT_INFO_SERVER_URL);

	if (platform && strcmp(platform, "Twitch") == 0) {
		if (!server || strcmp(server, "") == 0) {
			server = "auto";
			obs_data_set_string(settings, "server", server);
			obs_service_update(services[serviceId], settings);
		}
	}

	obs_output_set_service(streamingOutput[serviceId], services[serviceId]);
}

bool OBS_service::isTwitchStream(StreamServiceId serviceId)
{
	OBSDataAutoRelease settings = obs_service_get_settings(services[serviceId]);
	const char *platform = obs_data_get_string(settings, "service");
	bool result = false;

	if (platform && strcmp(platform, "Twitch") == 0) {
		result = true;
	}

	return result;
}

void OBS_service::updateFfmpegOutput(bool isSimpleMode, obs_output_t *output)
{
	const char *path;
	std::string format;
	const char *mux;
	bool noSpace;
	const char *fileNameFormat;
	bool overwriteIfExists;

	if (isSimpleMode) {
		path = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "FilePath");
		format = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecFormat");
		mux = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "MuxerCustom");
		noSpace = config_get_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "FileNameWithoutSpace");
		fileNameFormat = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "FilenameFormatting");
		overwriteIfExists = config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "OverwriteIfExists");
	} else {
		path = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecFilePath");
		format = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecFormat");
		fileNameFormat = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "FilenameFormatting");
		overwriteIfExists = config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "OverwriteIfExists");
		noSpace = config_get_bool(ConfigManager::getInstance().getBasic(), "AdvOut", "RecFileNameWithoutSpace");
	}
	format = GetFormatExt(format);
	std::string initialPath;
	if (path != nullptr) {
		initialPath = path;
	}

	if (fileNameFormat == NULL) {
		fileNameFormat = "%CCYY-%MM-%DD %hh-%mm-%ss";
	}

	std::string strPath;
	strPath += initialPath;

	char lastChar = strPath.back();
	if (lastChar != '/' && lastChar != '\\')
		strPath += "/";

	if (fileNameFormat != NULL && format.size())
		strPath += GenerateSpecifiedFilename(ffmpegOutput ? "avi" : format.c_str(), noSpace, fileNameFormat, 0, 0);

	if (!overwriteIfExists)
		FindBestFilename(strPath, noSpace);

	if (strPath.size() > 0) {
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, ffmpegOutput ? "url" : "path", strPath.c_str());
		obs_data_set_string(settings, "extension", format.c_str());
		obs_data_set_string(settings, "directory", path);
		obs_data_set_string(settings, "format", fileNameFormat);
		obs_data_set_bool(settings, "allow_spaces", !noSpace);
		obs_data_set_bool(settings, "allow_overwrite", overwriteIfExists);

		if (!isSimpleMode) {
			if (config_get_bool(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFile")) {
				const char *splitFileType = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFileType");
				if (strcmp(splitFileType, "Time") == 0) {
					obs_data_set_int(settings, "max_time_sec",
							 config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFileTime") * 60);
				} else if (strcmp(splitFileType, "Size") == 0) {
					obs_data_set_int(settings, "max_size_mb",
							 config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFileSize"));
				}

				obs_data_set_bool(settings, "reset_timestamps",
						  config_get_bool(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFileResetTimestamps"));

				obs_data_set_bool(settings, "split_file", true);
			} else {
				obs_data_set_bool(settings, "split_file", false);
			}
		} else {
			obs_data_set_bool(settings, "split_file", false);
		}

		obs_output_update(output, settings);
		obs_data_release(settings);
	}
}

void OBS_service::updateRecordingAudioTracks()
{
	static const char *configTracksNames[] = {
		"Track1Name", "Track2Name", "Track3Name", "Track4Name", "Track5Name", "Track6Name",
	};

	obs_data_t *settings[MAX_AUDIO_MIXES];

	for (size_t i = 0; i < MAX_AUDIO_MIXES; i++) {
		if (AdvancedRecordingAudioTracks[i] && !obs_encoder_active(AdvancedRecordingAudioTracks[i])) {
			settings[i] = obs_data_create();
			obs_data_set_int(settings[i], "bitrate", GetAdvancedAudioBitrate(static_cast<int>(i)));

			const char *name = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", configTracksNames[i]);
			if (name)
				obs_encoder_set_name(AdvancedRecordingAudioTracks[i], name);

			obs_encoder_update(AdvancedRecordingAudioTracks[i], settings[i]);
			obs_data_release(settings[i]);
			obs_encoder_set_audio(AdvancedRecordingAudioTracks[i], obs_get_audio());
		}
	}
}

void OBS_service::LoadRecordingPreset_Lossless()
{
	if (recordingOutput != NULL) {
		obs_output_release(recordingOutput);
	}
	recordingOutput = obs_output_create("ffmpeg_output", "simple_ffmpeg_output", nullptr, nullptr);

	connectOutputSignals(StreamServiceId::Main);
	if (!recordingOutput)
		throw "Failed to create recording FFmpeg output "
		      "(simple output)";

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "format_name", "avi");
	obs_data_set_string(settings, "video_encoder", "utvideo");
	obs_data_set_string(settings, "audio_encoder", "pcm_s16le");

	obs_output_set_mixers(recordingOutput, 1);
	obs_output_update(recordingOutput, settings);

	blog(LOG_INFO, "Created FFmpeg output for simple lossless recording.");

	obs_data_release(settings);
}

void OBS_service::LoadRecordingPreset_h264(const char *encoderId)
{
	std::string encoderName = GetVideoEncoderName(StreamServiceId::Main, true, true, encoderId);
	obs_encoder_t *newRecordingEncoder = obs_video_encoder_create(encoderId, encoderName.c_str(), nullptr, nullptr);
	OBS_service::setRecordingEncoder(newRecordingEncoder);

	if (!videoRecordingEncoder)
		throw "Failed to create h264 recording encoder (simple output)";
}

static bool update_ffmpeg_output(config_t *config)
{
	if (config_has_user_value(config, "AdvOut", "FFOutputToFile"))
		return false;

	const char *url = config_get_string(config, "AdvOut", "FFURL");
	if (!url)
		return false;

	bool isActualURL = strstr(url, "://") != nullptr;
	if (isActualURL)
		return false;

	std::string urlStr = url;
	std::string extension;

	for (size_t i = urlStr.length(); i > 0; i--) {
		size_t idx = i - 1;

		if (urlStr[idx] == '.') {
			extension = &urlStr[i];
		}

		if (urlStr[idx] == '\\' || urlStr[idx] == '/') {
			urlStr[idx] = 0;
			break;
		}
	}

	if (urlStr.empty() || extension.empty())
		return false;

	config_remove_value(config, "AdvOut", "FFURL");
	config_set_string(config, "AdvOut", "FFFilePath", urlStr.c_str());
	config_set_string(config, "AdvOut", "FFExtension", extension.c_str());
	config_set_bool(config, "AdvOut", "FFOutputToFile", true);
	return true;
}

void OBS_service::UpdateFFmpegCustomOutput(void)
{
	update_ffmpeg_output(ConfigManager::getInstance().getBasic());

	if (recordingOutput != NULL) {
		obs_output_release(recordingOutput);
	}
	recordingOutput = obs_output_create("ffmpeg_output", "simple_ffmpeg_output", nullptr, nullptr);
	connectOutputSignals(StreamServiceId::Main);

	const char *url = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "FFURL");
	int vBitrate = int(config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "FFVBitrate"));
	int gopSize = int(config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "FFVGOPSize"));
	bool rescale = config_get_bool(ConfigManager::getInstance().getBasic(), "AdvOut", "FFRescale");
	const char *rescaleRes = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "FFRescaleRes");
	const char *formatName = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "FFFormat");
	const char *mimeType = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "FFFormatMimeType");
	const char *muxCustom = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "FFMCustom");
	const char *vEncoder = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "FFVEncoder");
	int vEncoderId = int(config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "FFVEncoderId"));
	const char *vEncCustom = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "FFVCustom");
	int aBitrate = int(config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "FFABitrate"));
	int aTrack = int(config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "FFAudioTrack"));
	const char *aEncoder = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "FFAEncoder");
	int aEncoderId = int(config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "FFAEncoderId"));
	const char *aEncCustom = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "FFACustom");

	obs_data_t *settings = obs_data_create();

	obs_data_set_string(settings, "url", url);
	obs_data_set_string(settings, "format_name", formatName);
	obs_data_set_string(settings, "format_mime_type", mimeType);
	obs_data_set_string(settings, "muxer_settings", muxCustom);
	obs_data_set_int(settings, "gop_size", gopSize);
	obs_data_set_int(settings, "video_bitrate", vBitrate);
	obs_data_set_string(settings, "video_encoder", vEncoder);
	obs_data_set_int(settings, "video_encoder_id", vEncoderId);
	obs_data_set_string(settings, "video_settings", vEncCustom);
	obs_data_set_int(settings, "audio_bitrate", aBitrate);
	obs_data_set_string(settings, "audio_encoder", aEncoder);
	obs_data_set_int(settings, "audio_encoder_id", aEncoderId);
	obs_data_set_string(settings, "audio_settings", aEncCustom);

	if (rescale && rescaleRes && *rescaleRes) {
		int width;
		int height;
		int val = sscanf(rescaleRes, "%dx%d", &width, &height);

		if (val == 2 && width && height) {
			obs_data_set_int(settings, "scale_width", width);
			obs_data_set_int(settings, "scale_height", height);
		}
	}

	obs_output_set_mixer(recordingOutput, aTrack - 1);
	obs_core_video_mix_t *video_mix = obs_video_mix_get(0, OBS_RECORDING_VIDEO_RENDERING);

	if (video_mix) {
		obs_output_set_media(recordingOutput, obs_video_mix_get_video(video_mix), obs_get_audio());
	} else {
		blog(LOG_ERROR, "UpdateFFmpegCustomOutput - no video mix for OBS_RECORDING_VIDEO_RENDERING");
	}
	obs_output_update(recordingOutput, settings);

	obs_data_release(settings);
}

static bool icq_available(obs_encoder_t *encoder)
{
	obs_properties_t *props = obs_encoder_properties(encoder);
	obs_property_t *p = obs_properties_get(props, "rate_control");
	bool icq_found = false;

	size_t num = obs_property_list_item_count(p);
	for (size_t i = 0; i < num; i++) {
		const char *val = obs_property_list_item_string(p, i);
		if (strcmp(val, "ICQ") == 0) {
			icq_found = true;
			break;
		}
	}

	obs_properties_destroy(props);
	return icq_found;
}

void OBS_service::UpdateRecordingSettings_qsv11(int crf)
{
	bool icq = icq_available(videoRecordingEncoder);

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "profile", "high");

	if (icq) {
		obs_data_set_string(settings, "rate_control", "ICQ");
		obs_data_set_int(settings, "icq_quality", crf);
	} else {
		obs_data_set_string(settings, "rate_control", "CQP");
		obs_data_set_int(settings, "qpi", crf);
		obs_data_set_int(settings, "qpp", crf);
		obs_data_set_int(settings, "qpb", crf);
	}

	obs_encoder_update(videoRecordingEncoder, settings);

	obs_data_release(settings);
}

void OBS_service::UpdateRecordingSettings_nvenc(int cqp)
{
	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "rate_control", "CQP");
	obs_data_set_string(settings, "profile", "high");
	obs_data_set_string(settings, "preset", "hq");
	obs_data_set_int(settings, "cqp", cqp);
	obs_data_set_int(settings, "bitrate", 0);

	obs_encoder_update(videoRecordingEncoder, settings);

	obs_data_release(settings);
}

void UpdateRecordingSettings_nvenc_hevc(int cqp)
{
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "rate_control", "CQP");
	obs_data_set_string(settings, "profile", "main");
	obs_data_set_string(settings, "preset", "hq");
	obs_data_set_int(settings, "cqp", cqp);

	obs_encoder_update(videoRecordingEncoder, settings);
}

void UpdateRecordingSettings_apple(int quality)
{
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "rate_control", "CRF");
	obs_data_set_string(settings, "profile", "high");
	obs_data_set_int(settings, "quality", quality);

	obs_encoder_update(videoRecordingEncoder, settings);
}

void OBS_service::UpdateStreamingSettings_amd(obs_data_t *settings, int bitrate)
{
	obs_data_set_string(settings, "profile", "high");
	obs_data_set_string(settings, "preset", "quality");
	obs_data_set_string(settings, "rate_control", "CBR");
	obs_data_set_int(settings, "bitrate", bitrate);
	obs_data_set_int(settings, "keyint_sec", 2);
	obs_data_set_int(settings, "cqp", 20);
	obs_data_set_int(settings, "bf", 3);
}

void OBS_service::UpdateRecordingSettings_amd_cqp(int cqp)
{
	obs_data_t *settings = obs_data_create();

	obs_data_set_string(settings, "rate_control", "CQP");
	obs_data_set_string(settings, "profile", "high");
	obs_data_set_string(settings, "preset", "quality");
	obs_data_set_int(settings, "cqp", cqp);

	// Update and release
	obs_encoder_update(videoRecordingEncoder, settings);
	obs_data_release(settings);
}

void OBS_service::UpdateRecordingSettings_x264_crf(int crf)
{
	obs_data_t *settings = obs_data_create();
	obs_data_set_int(settings, "crf", crf);
	obs_data_set_bool(settings, "use_bufsize", true);
	obs_data_set_string(settings, "rate_control", "CRF");
	obs_data_set_string(settings, "profile", "high");
	obs_data_set_string(settings, "preset", lowCPUx264 ? "ultrafast" : "veryfast");

	obs_encoder_update(videoRecordingEncoder, settings);

	obs_data_release(settings);
}

#define CROSS_DIST_CUTOFF 2000.0

int CalcCRF(int crf)
{
	uint64_t cx = config_get_uint(ConfigManager::getInstance().getBasic(), "Video", "OutputCX");
	uint64_t cy = config_get_uint(ConfigManager::getInstance().getBasic(), "Video", "OutputCY");
	double fCX = double(cx);
	double fCY = double(cy);

	if (lowCPUx264)
		crf -= 2;

	double crossDist = sqrt(fCX * fCX + fCY * fCY);
	double crfResReduction = fmin(CROSS_DIST_CUTOFF, crossDist) / CROSS_DIST_CUTOFF;
	crfResReduction = (1.0 - crfResReduction) * 10.0;

	return crf - int(crfResReduction);
}

void OBS_service::updateVideoRecordingEncoderSettings()
{
	bool ultra_hq = (videoQuality == "HQ");
	int crf = CalcCRF(ultra_hq ? 16 : 23);
	std::string encFamily = osn::EncoderUtils::getEncoderFamily(videoEncoder.c_str());

	if (encFamily == FAMILY_OBS)
		UpdateRecordingSettings_x264_crf(crf);
	else if (encFamily == FAMILY_NVENC)
		UpdateRecordingSettings_nvenc(crf);
	else if (encFamily == FAMILY_NVENC_HEVC)
		UpdateRecordingSettings_nvenc_hevc(crf);
	else if (encFamily == FAMILY_QSV)
		UpdateRecordingSettings_qsv11(crf);
	else if (encFamily == FAMILY_AMD)
		UpdateRecordingSettings_amd_cqp(crf);
	else if (encFamily == FAMILY_APPLE)
		UpdateRecordingSettings_apple(ultra_hq ? 70 : 50);
	else
		blog(LOG_WARNING, "Unable to update settings with unknown encoder family.");
}

obs_encoder_t *OBS_service::getStreamingEncoder(StreamServiceId serviceId)
{
	return videoStreamingEncoder[serviceId];
}

void OBS_service::setStreamingEncoder(obs_encoder_t *encoder, StreamServiceId serviceId)
{
	const char *encoderName = obs_encoder_get_name(encoder);
	blog(LOG_INFO, "Set streaming encoder. name: %s for service %s", encoderName, serviceId == StreamServiceId::Main ? "Main" : "Second");

	if (videoStreamingEncoder[serviceId])
		obs_encoder_release(videoStreamingEncoder[serviceId]);
	videoStreamingEncoder[serviceId] = encoder;
}

obs_encoder_t *OBS_service::getRecordingEncoder(void)
{
	return videoRecordingEncoder;
}

void OBS_service::setRecordingEncoder(obs_encoder_t *encoder)
{
	if (videoRecordingEncoder)
		obs_encoder_release(videoRecordingEncoder);
	videoRecordingEncoder = encoder;
}

obs_encoder_t *OBS_service::getAudioStreamingEncoder(StreamServiceId serviceId)
{
	return audioStreamingEncoder[serviceId];
}

void OBS_service::setAudioStreamingEncoder(obs_encoder_t *encoder, StreamServiceId serviceId)
{
	if (audioStreamingEncoder[serviceId]) {
		obs_encoder_release(audioStreamingEncoder[serviceId]);
	}
	audioStreamingEncoder[serviceId] = encoder;
}

obs_encoder_t *OBS_service::getAudioSimpleRecordingEncoder(void)
{
	return audioSimpleRecordingEncoder;
}

obs_encoder_t *OBS_service::getArchiveEncoder(void)
{
	return streamArchiveEncVod;
}

void OBS_service::setAudioSimpleRecordingEncoder(obs_encoder_t *encoder)
{
	obs_encoder_release(audioSimpleRecordingEncoder);
	audioSimpleRecordingEncoder = encoder;
}

obs_output_t *OBS_service::getStreamingOutput(StreamServiceId serviceId)
{
	return streamingOutput[serviceId];
}

void OBS_service::setStreamingOutput(obs_output_t *output, StreamServiceId serviceId)
{
	obs_output_release(streamingOutput[serviceId]);
	streamingOutput[serviceId] = output;
}

obs_output_t *OBS_service::getRecordingOutput(void)
{
	return recordingOutput;
}

void OBS_service::setRecordingOutput(obs_output_t *output)
{
	obs_output_release(recordingOutput);
	recordingOutput = output;
}

obs_output_t *OBS_service::getReplayBufferOutput(void)
{
	return replayBufferOutput;
}

void OBS_service::setReplayBufferOutput(obs_output_t *output)
{
	obs_output_release(replayBufferOutput);
	replayBufferOutput = output;
}

obs_output_t *OBS_service::getVirtualWebcamOutput(void)
{
	return virtualCam.Get();
}

void OBS_service::setVirtualWebcamOutput(obs_output_t *output)
{
	virtualCam = output;
}

void OBS_service::clearOutputObjectsForShutdown(void)
{
	// These holders survive until CRT/static teardown. Clear the real owners before
	// obs_shutdown() so later destructors do not release stale libobs pointers.
	setStreamingOutput(nullptr, StreamServiceId::Main);
	setStreamingOutput(nullptr, StreamServiceId::Second);
	setRecordingOutput(nullptr);
	setReplayBufferOutput(nullptr);
	setVirtualWebcamOutput(nullptr);

	enhancedBroadcastContext.reset();
}

void OBS_service::updateStreamingOutput(StreamServiceId serviceId)
{
	const char *currentOutputMode = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "Mode");

	if (strcmp(currentOutputMode, "Advanced") == 0) {
		bool applyServiceSettings = config_get_bool(ConfigManager::getInstance().getBasic(), "AdvOut", "ApplyServiceSettings");

		if (applyServiceSettings) {
			obs_data_t *encoderSettings = obs_encoder_get_settings(videoStreamingEncoder[serviceId]);
			obs_service_apply_encoder_settings(OBS_service::getService(serviceId), encoderSettings, nullptr);
		}
	}

	bool reconnect = config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "Reconnect");
	uint64_t retryDelay = config_get_uint(ConfigManager::getInstance().getBasic(), "Output", "RetryDelay");
	uint64_t maxRetries = config_get_uint(ConfigManager::getInstance().getBasic(), "Output", "MaxRetries");

	bool useDelay = config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "DelayEnable");
	int64_t delaySec = config_get_int(ConfigManager::getInstance().getBasic(), "Output", "DelaySec");
	bool preserveDelay = config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "DelayPreserve");

	if (useDelay && delaySec < 0)
		delaySec = 0;

	const char *bindIP = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "BindIP");
	bool enableDynBitrate = config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "DynamicBitrate");
	bool enableNewSocketLoop = config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "NewSocketLoopEnable");
	bool enableLowLatencyMode = config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "LowLatencyEnable");

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "bind_ip", bindIP);
	obs_data_set_bool(settings, "dyn_bitrate", enableDynBitrate);
	obs_data_set_bool(settings, "new_socket_loop_enabled", enableNewSocketLoop);
	obs_data_set_bool(settings, "low_latency_mode_enabled", enableLowLatencyMode);
	obs_output_update(streamingOutput[serviceId], settings);
	obs_data_release(settings);

	if (!reconnect)
		maxRetries = 0;

	obs_output_set_delay(streamingOutput[serviceId], useDelay ? uint32_t(delaySec) : 0, preserveDelay ? OBS_OUTPUT_DELAY_PRESERVE : 0);

	obs_output_set_reconnect_settings(streamingOutput[serviceId], static_cast<int>(maxRetries), static_cast<int>(retryDelay));
}

std::vector<SignalInfo> streamingSignals;
std::vector<SignalInfo> recordingSignals;
std::vector<SignalInfo> replayBufferSignals;
std::vector<SignalInfo> virtualCamSignals;

void OBS_service::OBS_service_connectOutputSignals(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	streamingSignals.push_back(SignalInfo("streaming", "start", StreamServiceId::Main));
	streamingSignals.push_back(SignalInfo("streaming", "stop", StreamServiceId::Main));
	streamingSignals.push_back(SignalInfo("streaming", "starting", StreamServiceId::Main));
	streamingSignals.push_back(SignalInfo("streaming", "stopping", StreamServiceId::Main));
	streamingSignals.push_back(SignalInfo("streaming", "activate", StreamServiceId::Main));
	streamingSignals.push_back(SignalInfo("streaming", "deactivate", StreamServiceId::Main));
	streamingSignals.push_back(SignalInfo("streaming", "reconnect", StreamServiceId::Main));
	streamingSignals.push_back(SignalInfo("streaming", "reconnect_success", StreamServiceId::Main));

	streamingSignals.push_back(SignalInfo("streaming", "start", StreamServiceId::Second));
	streamingSignals.push_back(SignalInfo("streaming", "stop", StreamServiceId::Second));
	streamingSignals.push_back(SignalInfo("streaming", "starting", StreamServiceId::Second));
	streamingSignals.push_back(SignalInfo("streaming", "stopping", StreamServiceId::Second));
	streamingSignals.push_back(SignalInfo("streaming", "activate", StreamServiceId::Second));
	streamingSignals.push_back(SignalInfo("streaming", "deactivate", StreamServiceId::Second));
	streamingSignals.push_back(SignalInfo("streaming", "reconnect", StreamServiceId::Second));
	streamingSignals.push_back(SignalInfo("streaming", "reconnect_success", StreamServiceId::Second));

	recordingSignals.push_back(SignalInfo("recording", "start"));
	recordingSignals.push_back(SignalInfo("recording", "stop"));
	recordingSignals.push_back(SignalInfo("recording", "stopping"));
	recordingSignals.push_back(SignalInfo("recording", "wrote"));

	replayBufferSignals.push_back(SignalInfo("replay-buffer", "start"));
	replayBufferSignals.push_back(SignalInfo("replay-buffer", "stop"));
	replayBufferSignals.push_back(SignalInfo("replay-buffer", "stopping"));

	replayBufferSignals.push_back(SignalInfo("replay-buffer", "writing"));
	replayBufferSignals.push_back(SignalInfo("replay-buffer", "wrote"));
	replayBufferSignals.push_back(SignalInfo("replay-buffer", "writing_error"));

	virtualCamSignals.push_back(SignalInfo("virtual-camera", "activate"));
	virtualCamSignals.push_back(SignalInfo("virtual-camera", "deactivate"));
	virtualCamSignals.push_back(SignalInfo("virtual-camera", "start"));
	virtualCamSignals.push_back(SignalInfo("virtual-camera", "stop"));

	connectOutputSignals(StreamServiceId::Main);
	connectOutputSignals(StreamServiceId::Second);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_service::Query(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	std::unique_lock<std::mutex> ulock(signalMutex);
	if (outputSignal.empty()) {
		ulock.unlock();
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		AUTO_DEBUG;
		return;
	}

	auto frontSignal = outputSignal.front();
	outputSignal.pop();
	ulock.unlock();

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(frontSignal.getOutputType()));
	rval.push_back(ipc::value(frontSignal.getSignal()));
	rval.push_back(ipc::value(frontSignal.getCode()));
	rval.push_back(ipc::value(frontSignal.getErrorMessage()));
	rval.push_back(ipc::value(static_cast<int32_t>(frontSignal.getIndex())));

	AUTO_DEBUG;
}

void OBS_service::JSCallbackOutputSignal(void *data, calldata_t *params)
{
	SignalInfo &signal = *reinterpret_cast<SignalInfo *>(data);

	std::string signalReceived = signal.getSignal();
	blog(LOG_DEBUG, "signal received: %s %s", signalReceived.c_str(), signal.getOutputType().c_str());

	if (signalReceived.compare("stop") == 0) {
		signal.setCode((int)calldata_int(params, "code"));

		obs_output_t *output;

		if (signal.getOutputType().compare("streaming") == 0) {
			output = streamingOutput[signal.getIndex()];
			isStreaming[signal.getIndex()] = false;
		} else if (signal.getOutputType().compare("recording") == 0) {
			output = recordingOutput;
			isRecording = false;
		} else {
			output = replayBufferOutput;
			isReplayBufferActive = false;
		}

		const char *error = obs_output_get_last_error(output);
		if (error) {
			if (signal.getOutputType().compare("recording") == 0 && signal.getCode() == 0)
				signal.setCode(OBS_OUTPUT_ERROR);
			signal.setErrorMessage(error);
		}
	}

	std::unique_lock<std::mutex> ulock(signalMutex);
	outputSignal.push(signal);
}

void OBS_service::connectOutputSignals(StreamServiceId serviceId)
{
	if (streamingOutput[serviceId]) {
		signal_handler *streamingOutputSignalHandler = obs_output_get_signal_handler(streamingOutput[serviceId]);

		// Connect streaming output
		for (int i = 0; i < streamingSignals.size(); i++) {
			if (streamingSignals.at(i).getIndex() != serviceId)
				continue;
			signal_handler_connect(streamingOutputSignalHandler, streamingSignals.at(i).getSignal().c_str(), JSCallbackOutputSignal,
					       &(streamingSignals.at(i)));
		}
	}

	if (recordingOutput) {
		signal_handler *recordingOutputSignalHandler = obs_output_get_signal_handler(recordingOutput);

		// Connect recording output
		for (int i = 0; i < recordingSignals.size(); i++) {
			signal_handler_connect(recordingOutputSignalHandler, recordingSignals.at(i).getSignal().c_str(), JSCallbackOutputSignal,
					       &(recordingSignals.at(i)));
		}
	}

	if (replayBufferOutput) {
		signal_handler *replayBufferOutputSignalHandler = obs_output_get_signal_handler(replayBufferOutput);

		// Connect replay buffer output
		for (int i = 0; i < replayBufferSignals.size(); i++) {
			signal_handler_connect(replayBufferOutputSignalHandler, replayBufferSignals.at(i).getSignal().c_str(), JSCallbackOutputSignal,
					       &(replayBufferSignals.at(i)));
		}
	}

	if (virtualCam) {
		signal_handler *virtualCamOutputSignalHandler = obs_output_get_signal_handler(virtualCam);

		// Connect virtualCam output
		for (int i = 0; i < virtualCamSignals.size(); i++) {
			signal_handler_connect(virtualCamOutputSignalHandler, virtualCamSignals.at(i).getSignal().c_str(), JSCallbackOutputSignal,
					       &(virtualCamSignals.at(i)));
		}
	}
}

struct HotkeyInfo {
	std::string objectName;
	obs_hotkey_registerer_type objectType;
	std::string hotkeyName;
	std::string hotkeyDesc;
	obs_hotkey_id hotkeyId;
};

void OBS_service::OBS_service_processReplayBufferHotkey(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id id, obs_hotkey_t *key) {
			if (obs_hotkey_get_registerer_type(key) == OBS_HOTKEY_REGISTERER_OUTPUT) {
				std::string key_name = obs_hotkey_get_name(key);
				if (key_name.compare("ReplayBuffer.Save") == 0) {
					obs_hotkey_enable_callback_rerouting(true);
					obs_hotkey_trigger_routed_callback(id, true);
				}
			}
			return true;
		},
		nullptr);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_service::OBS_service_getLastReplay(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	if (!replayBufferOutput) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Invalid replay-buffer ouput.");
	}

	calldata_t cd = {0};

	proc_handler_t *ph = obs_output_get_proc_handler(replayBufferOutput);

	proc_handler_call(ph, "get_last_file", &cd);
	const char *path = calldata_string(&cd, "path");

	if (path == NULL)
		path = "";

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(path));
	calldata_free(&cd);
	AUTO_DEBUG;
}

void OBS_service::OBS_service_getLastRecording(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	if (!recordingOutput) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Invalid recording ouput.");
	}

	calldata_t cd = {0};

	proc_handler_t *ph = obs_output_get_proc_handler(recordingOutput);

	proc_handler_call(ph, "get_last_file", &cd);
	const char *path = calldata_string(&cd, "path");

	if (path == NULL)
		path = "";

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(path));
	calldata_free(&cd);
	AUTO_DEBUG;
}

void OBS_service::OBS_service_splitFile(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	if (!recordingOutput) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Invalid recording ouput.");
	}

	calldata_t cd = {0};

	proc_handler_t *ph = obs_output_get_proc_handler(recordingOutput);
	proc_handler_call(ph, "split_file", &cd);
	calldata_free(&cd);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

bool OBS_service::useRecordingPreset()
{
	return usingRecordingPreset;
}

void OBS_service::duplicate_encoder(obs_encoder_t **dst, obs_encoder_t *src, uint64_t trackIndex)
{
	if (!src)
		return;

	obs_encoder_t *prev_encoder = nullptr;
	if (*dst != src && *dst)
		prev_encoder = *dst;

	std::string name = obs_encoder_get_name(src);
	name += "-duplicate";

	if (obs_encoder_get_type(src) == OBS_ENCODER_AUDIO) {
		*dst = obs_audio_encoder_create(obs_encoder_get_id(src), name.c_str(), obs_encoder_get_settings(src), trackIndex, nullptr);
	} else if (obs_encoder_get_type(src) == OBS_ENCODER_VIDEO) {
		*dst = obs_video_encoder_create(obs_encoder_get_id(src), name.c_str(), obs_encoder_get_settings(src), nullptr);
	}

	if (prev_encoder) {
		obs_encoder_release(prev_encoder);
	}
}

void OBS_service::releaseStreamingOutput(StreamServiceId serviceId)
{
	blog(LOG_INFO, "releaseStreamingOutput - serviceId: %d", serviceId);

	if (config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "DelayEnable")) {
		uint32_t delay = obs_output_get_active_delay(streamingOutput[serviceId]);
		while (delay != 0) {
			delay = obs_output_get_active_delay(streamingOutput[serviceId]);
		}
	}

	clearArchiveVodEncoder();

	obs_output_release(streamingOutput[serviceId]);
	streamingOutput[serviceId] = nullptr;

	enhancedBroadcastContext.reset();
}

void OBS_service::waitReleaseWorker()
{
	if (releaseWorker.joinable()) {
		releaseWorker.join();
	}
}

bool OBS_service::VirtualCamActive()
{
	if (vcamEnabled) {
		return obs_output_active(virtualCam);
	}

	return false;
}

void OBS_service::DestroyVirtualCamView()
{
	blog(LOG_INFO, "DestroyVirtualCamView");

	DestroyVirtualCameraScene();

	if (!virtualCamView) {
		virtualCamVideo = nullptr;
		return;
	}

	obs_view_remove(virtualCamView);
	obs_view_set_source(virtualCamView, 0, nullptr);
	virtualCamVideo = nullptr;

	obs_view_destroy(virtualCamView);
	virtualCamView = nullptr;
}

void OBS_service::DestroyVirtualCameraScene()
{
	if (vCamActiveScene) {
		obs_deactivate_scene_on_backstage(vCamActiveScene);
		obs_source_release(vCamActiveScene);
		vCamActiveScene = nullptr;
	}

	if (!vCamSourceScene)
		return;

	if (vCamSourceSceneItem) {
		obs_sceneitem_remove(vCamSourceSceneItem);
		vCamSourceSceneItem = nullptr;
	}

	obs_source_t *sceneSource = obs_scene_get_source(vCamSourceScene);
	obs_deactivate_scene_on_backstage(sceneSource);
	obs_scene_release(vCamSourceScene);
	vCamSourceScene = nullptr;
}

static obs_video_info *GetPrimaryVCamCanvas()
{
	obs_video_info *primary = nullptr;
	osn::Video::Manager::GetInstance().for_each([&primary](obs_video_info *canvas) {
		if (!primary)
			primary = canvas;
	});
	return primary;
}

// Resolve the scene to render for SceneOutput. Falls back to the current program
// scene when the configured name is missing (the client may send a display label
// for the default selection).
static OBSSourceAutoRelease ResolveVCamScene(const std::string &name)
{
	OBSSourceAutoRelease s = obs_get_source_by_name(name.c_str());
	if (s)
		return s;

	OBSSourceAutoRelease transition = obs_get_output_source(0);
	if (transition)
		s = obs_transition_get_active_source(transition);
	blog(LOG_INFO, "VCam SceneOutput: scene '%s' not found, falling back to program scene (found=%d)", name.c_str(), !!s);
	return s;
}

// Resolve the source to render for SourceOutput. Falls back to the first
// non-scene item in the program scene when the configured name is missing.
static OBSSourceAutoRelease ResolveVCamSource(const std::string &name)
{
	OBSSourceAutoRelease s = obs_get_source_by_name(name.c_str());
	if (s)
		return s;

	OBSSourceAutoRelease transition = obs_get_output_source(0);
	OBSSourceAutoRelease programSrc;
	if (transition)
		programSrc = obs_transition_get_active_source(transition);
	obs_scene_t *programScene = programSrc ? obs_scene_from_source(programSrc) : nullptr;
	if (!programScene)
		return s;

	obs_source_t *result = nullptr;
	obs_scene_enum_items(
		programScene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *data) -> bool {
			obs_source_t *itemSrc = obs_sceneitem_get_source(item);
			if (itemSrc && !obs_scene_from_source(itemSrc)) {
				*static_cast<obs_source_t **>(data) = obs_source_get_ref(itemSrc);
				return false;
			}
			return true;
		},
		&result);

	s = result;
	blog(LOG_INFO, "VCam SourceOutput: source '%s' not found, falling back to program scene source (found=%d)", name.c_str(), !!s);
	return s;
}

void OBS_service::UpdateVirtualCamOutputSource()
{
	if (!vcamEnabled || !virtualCamView)
		return;

	OBSSourceAutoRelease source;

	switch (vcamConfig.type) {
	case VCamOutputType::Invalid:
	case VCamOutputType::ProgramView:
		DestroyVirtualCameraScene();
		return;
	case VCamOutputType::PreviewOutput:
		// Studio Mode VCam — not supported yet
		break;
	case VCamOutputType::SceneOutput: {
		OBSSourceAutoRelease s = ResolveVCamScene(vcamConfig.scene);
		if (!s) {
			blog(LOG_WARNING, "VCam SceneOutput: no usable scene for '%s'", vcamConfig.scene.c_str());
			DestroyVirtualCameraScene();
			break;
		}
		if (vCamActiveScene != s) {
			if (vCamActiveScene) {
				obs_deactivate_scene_on_backstage(vCamActiveScene);
				obs_source_release(vCamActiveScene);
			}
			obs_activate_scene_on_backstage(s);
			vCamActiveScene = obs_source_get_ref(s);
		}
		source = obs_source_get_ref(s);
		break;
	}
	case VCamOutputType::SourceOutput: {
		OBSSourceAutoRelease s = ResolveVCamSource(vcamConfig.source);
		if (!s) {
			blog(LOG_WARNING, "VCam SourceOutput: source '%s' not found and no fallback available", vcamConfig.source.c_str());
			DestroyVirtualCameraScene();
			break;
		}

		if (!vCamSourceScene) {
			vCamSourceScene = obs_scene_create_private("vcam_source");
			obs_activate_scene_on_backstage(obs_scene_get_source(vCamSourceScene));
		}

		source = obs_source_get_ref(obs_scene_get_source(vCamSourceScene));

		if (vCamSourceSceneItem && obs_sceneitem_get_source(vCamSourceSceneItem) != s) {
			obs_sceneitem_remove(vCamSourceSceneItem);
			vCamSourceSceneItem = nullptr;
		}

		if (!vCamSourceSceneItem) {
			vCamSourceSceneItem = obs_scene_add(vCamSourceScene, s);
			obs_sceneitem_set_bounds_type(vCamSourceSceneItem, OBS_BOUNDS_SCALE_INNER);
			obs_sceneitem_set_bounds_alignment(vCamSourceSceneItem, OBS_ALIGN_CENTER);

			const struct vec2 size = {
				(float)obs_source_get_width(source),
				(float)obs_source_get_height(source),
			};
			obs_sceneitem_set_bounds(vCamSourceSceneItem, &size);
		}
		break;
	}
	}

	OBSSourceAutoRelease current = obs_view_get_source(virtualCamView, 0);
	if (source != current)
		obs_view_set_source(virtualCamView, 0, source);
}

void OBS_service::StartVirtualCam(std::vector<ipc::value> &rval)
{
	if (!virtualCam) {
		virtualCam = obs_output_create(VIRTUAL_CAM_ID, "Virtual Webcam", nullptr, nullptr);
		vcamEnabled = (obs_get_output_flags(VIRTUAL_CAM_ID) & OBS_OUTPUT_VIDEO) != 0;
	}

	if (VirtualCamActive())
		return;

	if (!vcamEnabled)
		return;

	const bool typeIsProgram = vcamConfig.type == VCamOutputType::ProgramView;

	if (!virtualCamView && !typeIsProgram)
		virtualCamView = obs_view_create();

	UpdateVirtualCamOutputSource();

	if (!virtualCamVideo) {
		if (typeIsProgram) {
			// In multi-canvas builds obs_get_video() is empty; pull the mix
			// from the primary canvas instead.
			if (obs_video_info *primaryCanvas = GetPrimaryVCamCanvas()) {
				if (obs_core_video_mix_t *mix = obs_video_mix_get(primaryCanvas, OBS_MAIN_VIDEO_RENDERING))
					virtualCamVideo = obs_video_mix_get_video(mix);
			}
			if (!virtualCamVideo)
				virtualCamVideo = obs_get_video();
		} else {
			virtualCamVideo = obs_view_add2(virtualCamView, GetPrimaryVCamCanvas());
		}

		if (!virtualCamVideo) {
			PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to create virtual camera video");
		}
	}

	obs_output_set_media(virtualCam, virtualCamVideo, obs_get_audio());

	bool success = obs_output_start(virtualCam);
	if (!success) {
		const char *error = obs_output_get_last_error(virtualCam);
		blog(LOG_ERROR, "StartVirtualCam: output start failed: %s", error ? error : "(null)");
		DestroyVirtualCamView();
		PRETTY_ERROR_RETURN(ErrorCode::Error, error);
	}

	virtualCamActive = true;
	logVCamChanged(vcamConfig, true);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
}

void OBS_service::OBS_service_createVirtualCam(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
#if defined(__APPLE__)
	virtualCam = obs_output_create(VIRTUAL_CAM_ID, "mac-virtualcam", nullptr, nullptr);
	vcamEnabled = (obs_get_output_flags(VIRTUAL_CAM_ID) & OBS_OUTPUT_VIDEO) != 0;
	connectOutputSignals(StreamServiceId::Main);
#endif
	AUTO_DEBUG;
}

void OBS_service::OBS_service_startVirtualCam(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	if (virtualCamActive) {
		AUTO_DEBUG;
		return;
	}

	StartVirtualCam(rval);
	AUTO_DEBUG;
}

void OBS_service::StopVirtualCam()
{
	virtualCamActive = false;

	if (vcamEnabled)
		obs_output_stop(virtualCam);
}

void OBS_service::OBS_service_stopVirtualCam(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	if (!virtualCamActive) {
		AUTO_DEBUG;
		return;
	}

	StopVirtualCam();
	// TODO: There might be better solution for this, but this is how it's done inside OBS.
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	DestroyVirtualCamView();

	DeactivateSources();
	AUTO_DEBUG;
}

void OBS_service::DeactivateSources()
{
	DestroyVirtualCameraScene();
}

void OBS_service::OBS_service_updateVirtualCam(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	const auto outputType = static_cast<VCamOutputType>(args[0].value_union.i32);
	const auto objectName = args[1].value_str;

	blog(LOG_INFO, "OBS_service_updateVirtualCam - %d, '%s'", outputType, objectName.c_str());

	if (virtualCamActive) {
		DeactivateSources();
	}

	const bool needRestart = vcamConfig.type != outputType;
	vcamConfig.type = outputType;

	if (outputType == VCamOutputType::SceneOutput) {
		vcamConfig.scene = objectName;
	} else if (outputType == VCamOutputType::SourceOutput) {
		vcamConfig.source = objectName;
	}

	if (!virtualCamActive) {
		AUTO_DEBUG;
		return;
	}

	if (needRestart) {
		// Synchronization via sleep is an approach taken from OBS, here we are just following their best practices
		StopVirtualCam();
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		DestroyVirtualCamView();
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		StartVirtualCam(rval);
	} else {
		UpdateVirtualCamOutputSource();
	}

	logVCamChanged(vcamConfig, false);
	AUTO_DEBUG;
}

namespace {

const char *GetOutputBusyState(obs_output_t *output, bool includeReconnect = false)
{
	if (!output)
		return nullptr;

	if (obs_output_active(output))
		return "active";

	if (obs_output_connecting(output))
		return "connecting";

	if (includeReconnect && obs_output_reconnecting(output))
		return "reconnecting";

	return nullptr;
}

bool OutputIsBusy(obs_output_t *output, bool includeReconnect = false)
{
	return GetOutputBusyState(output, includeReconnect) != nullptr;
}

void AppendBusyOutputDescription(std::string &description, const std::string &label, obs_output_t *output, bool includeReconnect = false)
{
	const char *outputState = GetOutputBusyState(output, includeReconnect);
	if (!outputState)
		return;

	if (!description.empty())
		description += ", ";

	description += label;

	const char *outputName = obs_output_get_name(output);
	if (outputName && *outputName) {
		description += "='";
		description += outputName;
		description += "'";
	}

	description += " (";
	description += outputState;
	description += ")";
}

std::string DescribeBusyOutputs()
{
	std::string description;

	AppendBusyOutputDescription(description, "stream-main", streamingOutput[StreamServiceId::Main], true);
	AppendBusyOutputDescription(description, "stream-second", streamingOutput[StreamServiceId::Second], true);
	AppendBusyOutputDescription(description, "recording", recordingOutput);
	AppendBusyOutputDescription(description, "replay-buffer", replayBufferOutput);
	AppendBusyOutputDescription(description, "virtual-cam", virtualCam.Get());

	if (description.empty())
		description = "unknown";

	return description;
}

constexpr auto SHUTDOWN_OUTPUTS_STOPT_IMEOUT = std::chrono::seconds(10);
constexpr auto SHUTDOWN_OUTPUTS_STOP_POLL_INTERVAL = std::chrono::milliseconds(25);

void WaitForAllOutputsToStop()
{
	const auto deadline = std::chrono::steady_clock::now() + SHUTDOWN_OUTPUTS_STOPT_IMEOUT;

	while (OutputIsBusy(streamingOutput[StreamServiceId::Main], true) || OutputIsBusy(streamingOutput[StreamServiceId::Second], true) ||
	       OutputIsBusy(recordingOutput) || OutputIsBusy(replayBufferOutput) || OutputIsBusy(virtualCam.Get())) {
		if (std::chrono::steady_clock::now() >= deadline) {
			const std::string busyOutputs = DescribeBusyOutputs();
			const std::string crashMessage = "Timed out waiting for outputs to stop during shutdown: " + busyOutputs;

			blog(LOG_ERROR, "%s", crashMessage.c_str());
			util::CrashManager::AddWarning(crashMessage);
#ifdef WIN32
			util::CrashManager::GetMetricsProvider()->BlameServer();
#endif

			std::terminate();
		}

		std::this_thread::sleep_for(SHUTDOWN_OUTPUTS_STOP_POLL_INTERVAL);
	}
}

} // namespace

void OBS_service::stopAllOutputs()
{
	waitReleaseWorker();

	auto stopStreamForShutdown = [](StreamServiceId serviceId) {
		obs_output_t *output = streamingOutput[serviceId];
		if (!OutputIsBusy(output, true))
			return;

		obs_output_stop(output);

		if (OBS_service::isTwitchStream(serviceId) && osn::IsMultitrackVideoEnabled()) {
			obs_output_remove_packet_callback(output, bpm_inject, NULL);
			bpm_destroy(output);
		}

		isStreaming[serviceId] = false;
	};

	stopStreamForShutdown(StreamServiceId::Main);
	stopStreamForShutdown(StreamServiceId::Second);

	if (OutputIsBusy(replayBufferOutput))
		obs_output_stop(replayBufferOutput);

	if (OutputIsBusy(recordingOutput))
		obs_output_stop(recordingOutput);

	obs_output_t *virtualCamOutput = virtualCam.Get();
	if (OutputIsBusy(virtualCamOutput))
		obs_output_stop(virtualCamOutput);

	WaitForAllOutputsToStop();

	DestroyVirtualCamView();
	virtualCamActive = false;

	isStreaming[StreamServiceId::Main] = false;
	isStreaming[StreamServiceId::Second] = false;
	isRecording = false;
	isReplayBufferActive = false;
	rpUsesRec = false;
	rpUsesStream = false;
}

static inline uint32_t setMixer(obs_source_t *source, const int mixerIdx, const bool checked)
{
	uint32_t mixers = obs_source_get_audio_mixers(source);
	uint32_t new_mixers = mixers;
	if (checked) {
		new_mixers |= (1 << mixerIdx);
	} else {
		new_mixers &= ~(1 << mixerIdx);
	}
	obs_source_set_audio_mixers(source, new_mixers);
	return mixers;
}

void OBS_service::clearArchiveVodEncoder()
{
	if (streamArchiveEncVod) {
		obs_encoder_release(streamArchiveEncVod);
		streamArchiveEncVod = nullptr;
	}
}

void OBS_service::setupVodTrack(bool isSimpleMode)
{
	if (!services[0])
		return;

	obs_data_t *settings = obs_service_get_settings(services[0]);
	const char *serviceName = obs_data_get_string(settings, "service");
	obs_data_release(settings);

	if (serviceName && strcmp(serviceName, "Twitch") != 0)
		return;

	if (streamArchiveEncVod && obs_encoder_active(streamArchiveEncVod))
		return;

	clearArchiveVodEncoder();

	int streamTrack = 0;
	bool vodTrackEnabled = false;
	int vodTrackIndex = 1;

	if (isSimpleMode) {
		bool advanced = config_get_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "UseAdvanced");
		vodTrackEnabled = advanced ? config_get_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "VodTrackEnabled") : false;
		blog(LOG_INFO, "vodTrackEnabled: %d", vodTrackEnabled);
	} else {
		streamTrack = int(config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "TrackIndex")) - 1;
		vodTrackEnabled = config_get_bool(ConfigManager::getInstance().getBasic(), "AdvOut", "VodTrackEnabled");
		vodTrackIndex = int(config_get_int(ConfigManager::getInstance().getBasic(), "AdvOut", "VodTrackIndex")) - 1;
	}

	if (vodTrackEnabled && streamTrack != vodTrackIndex) {
		std::string id;
		if (createAudioEncoder(&streamArchiveEncVod, id, ffmpeg_aac_id, isSimpleMode ? GetSimpleAudioBitrate() : GetAdvancedAudioBitrate(vodTrackIndex),
				       ARCHIVE_NAME, vodTrackIndex)) {
			obs_encoder_set_audio(streamArchiveEncVod, obs_get_audio());
			obs_output_set_audio_encoder(streamingOutput[0], streamArchiveEncVod, 1);
			obs_encoder_set_video_mix(streamArchiveEncVod, obs_video_mix_get(videoInfo[0], OBS_STREAMING_VIDEO_RENDERING));
		}
	}
}
std::string GetFormatExt(const std::string container)
{
	std::string ext = container;
	if (ext == "fragmented_mp4")
		ext = "mp4";
	else if (ext == "fragmented_mov")
		ext = "mov";
	else if (ext == "hls")
		ext = "m3u8";
	else if (ext == "mpegts")
		ext = "ts";

	return ext;
}
