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

#include "osn-streaming.hpp"
#include "osn-service.hpp"
#include "osn-error.hpp"
#include "shared.hpp"
#include <osn-video.hpp>
#include "osn-encoders.hpp"
//os_gettime_ns
#include <util/platform.h>
#include <chrono>
#include <thread>

osn::Streaming::~Streaming()
{
	DeleteOutput();
	if (streamArchive && !obs_encoder_active(streamArchive)) {
		obs_encoder_release(streamArchive);
		streamArchive = nullptr;
	}
	if (originalServiceSettings) {
		obs_data_release(originalServiceSettings);
		originalServiceSettings = nullptr;
	}
}

void osn::Streaming::testBandwidth(bool &gotError, int testBitrate)
{
	if (!service || !GetOutput()) {
		gotError = true;
		return;
	}

	// Override the user's encoder bitrate with the ceiling-search target so the
	// bandwidth measurement isn't capped by whatever the user happens to have
	// set (often 2500). Restored in CleanTestMode().
	if (testBitrate > 0 && videoEncoder) {
		obs_data_t *encSettings = obs_encoder_get_settings(videoEncoder);
		originalEncoderBitrate = (int)obs_data_get_int(encSettings, "bitrate");
		obs_data_release(encSettings);

		obs_data_t *override = obs_data_create();
		obs_data_set_int(override, "bitrate", (long long)testBitrate);
		obs_encoder_update(videoEncoder, override);
		obs_data_release(override);
	}

	if (originalServiceSettings) {
		obs_data_release(originalServiceSettings);
	}
	originalServiceSettings = obs_service_get_settings(service);
	obs_data_addref(originalServiceSettings);

	obs_data_t *serviceSettings = obs_data_create();
	obs_data_apply(serviceSettings, originalServiceSettings);

	const char *serviceName = obs_data_get_string(serviceSettings, "service");
	if (serviceName && strcmp(serviceName, "Twitch") == 0) {
		std::string key = obs_service_get_connect_info(service, OBS_SERVICE_CONNECT_INFO_STREAM_KEY);

		while (!key.empty()) {
			char ch = key.back();
			if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
				key.pop_back();
			else
				break;
		}

		key += "?bandwidthtest";
		obs_data_set_string(serviceSettings, "key", key.c_str());
	}

	obs_service_update(service, serviceSettings);
	obs_data_release(serviceSettings);

	testMode = true;
	start();
}

void osn::Streaming::CleanTestMode()
{
	if (GetOutput()) {
		if (obs_output_active(GetOutput())) {
			obs_output_stop(GetOutput());
		}
		// obs_output_stop is asynchronous: the output stays active until its
		// internal stop thread finishes flushing. Block here so callers
		// (autoconfig SaveSettings -> applyResults -> obs_set_video_info)
		// don't race with OBS_VIDEO_CURRENTLY_ACTIVE (-4).
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
		while (obs_output_active(GetOutput()) && std::chrono::steady_clock::now() < deadline) {
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
	}

	if (service && originalServiceSettings) {
		obs_service_update(service, originalServiceSettings);
		obs_data_release(originalServiceSettings);
		originalServiceSettings = nullptr;
	}

	if (videoEncoder && originalEncoderBitrate > 0) {
		obs_data_t *restore = obs_data_create();
		obs_data_set_int(restore, "bitrate", (long long)originalEncoderBitrate);
		obs_encoder_update(videoEncoder, restore);
		obs_data_release(restore);
		originalEncoderBitrate = 0;
	}

	testMode = false;
}

void osn::Streaming::DeleteOutput()
{
	Output::DeleteOutput();
}

bool osn::Streaming::ApplyOutputSettings(obs_output_t *output, std::string &errorMessage)
{
	if (!output) {
		errorMessage = "Invalid streaming output.";
		return false;
	}

	if (!delay) {
		errorMessage = "Invalid delay.";
		return false;
	}

	obs_output_set_delay(output, delay->enabled ? uint32_t(delay->delaySec) : 0, delay->preserveDelay ? OBS_OUTPUT_DELAY_PRESERVE : 0);

	if (!reconnect) {
		errorMessage = "Invalid reconnect.";
		return false;
	}

	uint32_t maxRetries = reconnect->enabled ? reconnect->maxRetries : 0;
	obs_output_set_reconnect_settings(output, maxRetries, reconnect->retryDelay);

	if (!network) {
		errorMessage = "Invalid network.";
		return false;
	}

	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "bind_ip", network->bindIP.c_str());
	obs_data_set_bool(settings, "dyn_bitrate", network->enableDynamicBitrate);
	obs_data_set_bool(settings, "new_socket_loop_enabled", network->enableOptimizations);
	obs_data_set_bool(settings, "low_latency_mode_enabled", network->enableLowLatency);
	obs_output_update(output, settings);

	return true;
}

void osn::IStreaming::GetService(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	blog(LOG_WARNING, "Function %s is deprecated", __func__);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IStreaming::SetService(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	if (args[1].value_union.ui64 == UINT64_MAX) {
		streaming->service = nullptr;
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		AUTO_DEBUG;
		return;
	}

	obs_service_t *service = osn::Service::Manager::GetInstance().find(args[1].value_union.ui64);
	if (!service) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Service reference is not valid.");
	}

	streaming->service = service;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IStreaming::GetVideoCanvas(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	uint64_t uid = osn::Video::Manager::GetInstance().find(streaming->GetCanvas());

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::IStreaming::SetVideoCanvas(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	obs_video_info *canvas = osn::Video::Manager::GetInstance().find(args[1].value_union.ui64);
	if (!canvas) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Canvas reference is not valid.");
	}

	blog(LOG_INFO, "IStreaming::SetVideoCanvas - canvas: 0x%" PRIXPTR ", uid: %d", (uintptr_t)canvas, (int)args[1].value_union.ui64);

	streaming->SetCanvas(canvas);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IStreaming::GetVideoEncoder(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	blog(LOG_WARNING, "Function %s is deprecated", __func__);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IStreaming::SetVideoEncoder(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	if (args[1].value_union.ui64 == UINT64_MAX) {
		streaming->videoEncoder = nullptr;
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		AUTO_DEBUG;
		return;
	}

	obs_encoder_t *encoder = osn::VideoEncoder::Manager::GetInstance().find(args[1].value_union.ui64);
	if (!encoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Encoder reference is not valid.");
	}

	streaming->videoEncoder = encoder;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IStreaming::GetEnforceServiceBirate(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(streaming->enforceServiceBitrate));
	AUTO_DEBUG;
}

void osn::IStreaming::SetEnforceServiceBirate(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	streaming->enforceServiceBitrate = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IStreaming::GetEnableTwitchVOD(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(streaming->enableTwitchVOD));
	AUTO_DEBUG;
}

void osn::IStreaming::SetEnableTwitchVOD(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	streaming->enableTwitchVOD = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IStreaming::GetDelay(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	blog(LOG_WARNING, "Function %s is deprecated", __func__);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IStreaming::SetDelay(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	if (args[1].value_union.ui64 == UINT64_MAX) {
		streaming->delay = nullptr;
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		AUTO_DEBUG;
		return;
	}

	Delay *delay = osn::IDelay::Manager::GetInstance().find(args[1].value_union.ui64);
	if (!delay) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Encoder reference is not valid.");
	}

	streaming->delay = delay;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IStreaming::GetReconnect(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	blog(LOG_WARNING, "Function %s is deprecated", __func__);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IStreaming::SetReconnect(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	if (args[1].value_union.ui64 == UINT64_MAX) {
		streaming->reconnect = nullptr;
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		AUTO_DEBUG;
		return;
	}

	Reconnect *reconnect = osn::IReconnect::Manager::GetInstance().find(args[1].value_union.ui64);
	if (!reconnect) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Reconnect reference is not valid.");
	}

	streaming->reconnect = reconnect;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IStreaming::GetNetwork(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	blog(LOG_WARNING, "Function %s is deprecated", __func__);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IStreaming::SetNetwork(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	if (args[1].value_union.ui64 == UINT64_MAX) {
		streaming->network = nullptr;
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		AUTO_DEBUG;
		return;
	}

	Network *network = osn::INetwork::Manager::GetInstance().find(args[1].value_union.ui64);
	if (!network) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Network reference is not valid.");
	}

	streaming->network = network;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

bool osn::Streaming::isTwitchVODSupported()
{
	if (!service)
		return false;

	obs_data_t *settings = obs_service_get_settings(service);
	const char *serviceName = obs_data_get_string(settings, "service");
	obs_data_release(settings);

	if (serviceName && strcmp(serviceName, "Twitch") != 0)
		return false;

	return true;
}

void osn::IStreaming::Query(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	if (streaming->testMode) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		rval.push_back(ipc::value(true));
		AUTO_DEBUG;
		return;
	}

	auto signalOpt = streaming->PopReceivedSignal();
	if (!signalOpt.has_value()) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		AUTO_DEBUG;
		return;
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value("streaming"));
	rval.push_back(ipc::value(signalOpt.value().signal));
	rval.push_back(ipc::value(signalOpt.value().code));
	rval.push_back(ipc::value(signalOpt.value().errorMessage));

	AUTO_DEBUG;
}

osn::IStreaming::Manager &osn::IStreaming::Manager::GetInstance()
{
	static osn::IStreaming::Manager _inst;
	return _inst;
}

void osn::Streaming::getDelayLegacySettings()
{
	delay = new Delay();
	delay->enabled = config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "DelayEnable");
	delay->delaySec = static_cast<uint32_t>(config_get_int(ConfigManager::getInstance().getBasic(), "Output", "DelaySec"));
	delay->preserveDelay = config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "DelayPreserve");
	osn::IDelay::Manager::GetInstance().allocate(delay);
}

void osn::Streaming::getReconnectLegacySettings()
{
	reconnect = new Reconnect();
	reconnect->enabled = config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "Reconnect");
	reconnect->retryDelay = static_cast<uint32_t>(config_get_uint(ConfigManager::getInstance().getBasic(), "Output", "RetryDelay"));
	reconnect->maxRetries = static_cast<uint32_t>(config_get_uint(ConfigManager::getInstance().getBasic(), "Output", "MaxRetries"));
	osn::IReconnect::Manager::GetInstance().allocate(reconnect);
}

void osn::Streaming::getNetworkLegacySettings()
{
	network = new Network();
	network->bindIP = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "BindIP");
	network->enableDynamicBitrate = config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "DynamicBitrate");
	network->enableOptimizations = config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "NewSocketLoopEnable");
	network->enableLowLatency = config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "LowLatencyEnable");
	osn::INetwork::Manager::GetInstance().allocate(network);
}

void osn::Streaming::setDelayLegacySettings()
{
	if (!delay)
		return;

	config_set_bool(ConfigManager::getInstance().getBasic(), "Output", "DelayEnable", delay->enabled);
	config_set_int(ConfigManager::getInstance().getBasic(), "Output", "DelaySec", delay->delaySec);
	config_set_bool(ConfigManager::getInstance().getBasic(), "Output", "DelayPreserve", delay->preserveDelay);
}

void osn::Streaming::setReconnectLegacySettings()
{
	if (!reconnect)
		return;

	config_set_bool(ConfigManager::getInstance().getBasic(), "Output", "Reconnect", reconnect->enabled);
	config_set_uint(ConfigManager::getInstance().getBasic(), "Output", "RetryDelay", reconnect->retryDelay);
	config_set_uint(ConfigManager::getInstance().getBasic(), "Output", "MaxRetries", reconnect->maxRetries);
}

void osn::Streaming::setNetworkLegacySettings()
{
	if (!network)
		return;

	config_set_string(ConfigManager::getInstance().getBasic(), "Output", "BindIP", network->bindIP.c_str());
	config_set_bool(ConfigManager::getInstance().getBasic(), "Output", "DynamicBitrate", network->enableDynamicBitrate);
	config_set_bool(ConfigManager::getInstance().getBasic(), "Output", "NewSocketLoopEnable", network->enableDynamicBitrate);
	config_set_bool(ConfigManager::getInstance().getBasic(), "Output", "LowLatencyEnable", network->enableDynamicBitrate);
}

std::string osn::Streaming::testQuery()
{
	auto signalOpt = PopReceivedSignal();
	if (!signalOpt.has_value()) {
		return "";
	}
	return signalOpt.value().signal;
}

void osn::IStreaming::GetDroppedFrames(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	int totalDropped = 0;

	if (streaming->GetOutput() && obs_output_active(streaming->GetOutput())) {
		totalDropped = obs_output_get_frames_dropped(streaming->GetOutput());
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(totalDropped));
	AUTO_DEBUG;
}

void osn::IStreaming::GetTotalFrames(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	int totalFrames = 0;

	if (streaming->GetOutput() && obs_output_active(streaming->GetOutput())) {
		totalFrames = obs_output_get_total_frames(streaming->GetOutput());
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(totalFrames));
	AUTO_DEBUG;
}

void osn::IStreaming::GetKBitsPerSec(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	double kbitsPerSec = 0;

	if (streaming->GetOutput() && obs_output_active(streaming->GetOutput())) {

		uint64_t bytesSent = obs_output_get_total_bytes(streaming->GetOutput());
		uint64_t bytesSentTime = os_gettime_ns();

		if (bytesSent < streaming->lastBytesSent)
			bytesSent = 0;
		if (bytesSent == 0)
			streaming->lastBytesSent = 0;

		uint64_t bitsBetween = (bytesSent - streaming->lastBytesSent) * 8;

		double timePassed = double(bytesSentTime - streaming->lastBytesSentTime) / 1000000000.0;
		if (timePassed < std::numeric_limits<double>::epsilon() && timePassed > -std::numeric_limits<double>::epsilon()) {
			kbitsPerSec = 0.0;
		} else {
			kbitsPerSec = double(bitsBetween) / timePassed / 1000.0;
		}

		streaming->lastBytesSent = bytesSent;
		streaming->lastBytesSentTime = bytesSentTime;
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(kbitsPerSec));
	AUTO_DEBUG;
}

void osn::IStreaming::GetDataOutput(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	double dataOutput = 0;

	if (streaming->GetOutput() && obs_output_active(streaming->GetOutput())) {

		uint64_t bytesSent = obs_output_get_total_bytes(streaming->GetOutput());
		uint64_t bytesSentTime = os_gettime_ns();

		if (bytesSent < streaming->lastBytesSent)
			bytesSent = 0;
		if (bytesSent == 0)
			streaming->lastBytesSent = 0;

		uint64_t bitsBetween = (bytesSent - streaming->lastBytesSent) * 8;

		streaming->lastBytesSent = bytesSent;
		streaming->lastBytesSentTime = bytesSentTime;
		dataOutput = bytesSent / (1024.0 * 1024.0);
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(dataOutput));
	AUTO_DEBUG;
}
