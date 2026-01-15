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
//os_gettime_ns
#include <util/platform.h>

osn::Streaming::~Streaming()
{
	DeleteOutput();
	if (streamArchive && !obs_encoder_active(streamArchive)) {
		obs_encoder_release(streamArchive);
		streamArchive = nullptr;
	}
}

void osn::Streaming::DeleteOutput() {
	blog(LOG_INFO, "osn::Streaming::DeleteOutput");
	Output::DeleteOutput();
	if (enhancedBroadcasting) {
		enhancedBroadcastingContext.reset();
	}
}

void osn::IStreaming::GetService(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Service reference is not valid.");
	}

	uint64_t uid = osn::Service::Manager::GetInstance().find(streaming->service);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::IStreaming::SetService(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
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
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	uint64_t uid = osn::VideoEncoder::Manager::GetInstance().find(streaming->videoEncoder);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::IStreaming::SetVideoEncoder(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
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

void osn::IStreaming::GetEnhancedBroadcasting(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(streaming->enhancedBroadcasting));
	AUTO_DEBUG;
}

void osn::IStreaming::SetEnhancedBroadcasting(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	streaming->enhancedBroadcasting = args[1].value_union.ui32;

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
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	uint64_t uid = osn::IDelay::Manager::GetInstance().find(streaming->delay);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::IStreaming::SetDelay(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
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
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	uint64_t uid = osn::IReconnect::Manager::GetInstance().find(streaming->reconnect);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::IStreaming::SetReconnect(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
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
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	uint64_t uid = osn::INetwork::Manager::GetInstance().find(streaming->network);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::IStreaming::SetNetwork(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
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

void osn::Streaming::StartEnhancedBroadcastingStream(std::optional<size_t> vod_track_mixer) {
	blog(LOG_INFO, "Streaming::StartEnhancedBroadcastingStream - service id: %s", obs_service_get_id(this->service));

	if (vod_track_mixer.has_value()) {
		blog(LOG_INFO, "vod_track_mixer: %d", vod_track_mixer.value());
	}

	const bool is_custom = strncmp("rtmp_custom", obs_service_get_type(this->service), 11) == 0;

	OBSDataAutoRelease settings = obs_service_get_settings(this->service);
	const std::string key = obs_data_get_string(settings, "key");

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
		blog(LOG_INFO, "Using %s server '%s'", service_custom_server ? "custom " : "", custom_rtmp_url->c_str());
	}

	auto auto_config_url = osn::MultitrackVideoAutoConfigURL(this->service);
	blog(LOG_INFO, "Auto config URL: %s", auto_config_url.c_str());

	auto go_live_post = osn::constructGoLivePost({this->GetCanvas()}, key, std::nullopt, std::nullopt, vod_track_mixer.has_value());
	std::optional<osn::Config> go_live_config = osn::DownloadGoLiveConfig(auto_config_url, go_live_post);
	if (!go_live_config.has_value()) {
		throw std::runtime_error("startStreaming - go live config is empty");
	}

	const auto audio_bitrate = osn::GetMultitrackAudioBitrate();
	const auto audio_encoder_id = osn::GetSimpleAACEncoderForBitrate(audio_bitrate);

	std::vector<OBSEncoderAutoRelease> audio_encoders;
	std::shared_ptr<obs_encoder_group_t> video_encoder_group;
	auto output =
		osn::SetupOBSOutput("Enhanced Broadcasting", go_live_config.value(), audio_encoders, video_encoder_group, audio_encoder_id, 0, vod_track_mixer);
	if (!output) {
		throw std::runtime_error("startStreaming - failed to create multitrack output");
	}

	// Stream key is defined by config from Twitch
	auto multitrack_video_service = osn::create_service(*go_live_config, std::nullopt, "");
	if (!multitrack_video_service) {
		throw std::runtime_error("startStreaming - failed to create multitrack video service");
	}

	this->SetOutput(output.Get());
	obs_output_set_service(output, multitrack_video_service);

	// Register the BPM (Broadcast Performance Metrics) callback
	obs_output_add_packet_callback(output, bpm_inject, NULL);

	this->StartOutput();

	enhancedBroadcastingContext.emplace(EnhancedBroadcastOutputObjects{
		std::move(output),
		video_encoder_group,
		std::move(audio_encoders),
		std::move(multitrack_video_service),
	});
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