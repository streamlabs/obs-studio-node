/******************************************************************************
    Copyright (C) 2026 by Streamlabs

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "nodeobs_autoconfig.h"

#include "osn-encoders.hpp"
#include "osn-error.hpp"
#include "shared.hpp"

#include <obs.h>
#include <media-io/audio-io.h>
#include <media-io/video-frame.h>
#include <media-io/video-io.h>
#include <util/platform.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace autoConfig {
namespace {

constexpr int kSchemaVersion = 1;
constexpr int kProbeConnectTimeoutMs = 8000;
constexpr int kProbeWarmupMs = 750;
constexpr int kProbeSampleMs = 5000;
constexpr int kProbeStopTimeoutMs = 3000;
constexpr int kCancelTimeoutMs = 8000;
constexpr uint64_t kProbeMaxBytes = 25ULL * 1024ULL * 1024ULL;
constexpr int kProbeMaximumBitrateKbps = 10000;
constexpr int kDefaultEstimatedBitrateKbps = 2500;
constexpr int kHardwareWarmupMs = 500;
constexpr int kHardwareSampleMs = 1500;
constexpr int kHardwareStopTimeoutMs = 1000;
constexpr int kHardwarePhaseTimeoutMs = 12000;
constexpr int kHardwareMaximumLongEdge = 1920;
constexpr int kHardwareMaximumShortEdge = 1080;

enum class SessionState { Created, Running, Complete, Cancelled, Failed, Closed };

struct Limits {
	int maxBitrateKbps = 0;
	int maxWidth = 0;
	int maxHeight = 0;
	int maxFpsNum = 0;
	int maxFpsDen = 0;

	bool any() const { return maxBitrateKbps > 0 || maxWidth > 0 || maxHeight > 0 || maxFpsNum > 0; }
};

struct CurrentSettings {
	int width = 0;
	int height = 0;
	int fpsNum = 0;
	int fpsDen = 1;
	int bitrateKbps = 0;
	std::string encoderId;
	std::string codec;
	std::string preset;
};

struct EncoderSelection {
	std::string id;
	bool replaced = false;
};

struct HardwareAssessment {
	bool attempted = false;
	bool passed = false;
	bool cancelled = false;
	bool constrained = false;
	std::string reason;
	CurrentSettings value;
};

struct Destination {
	std::string platform;
};

struct LegRequest {
	std::string legId;
	std::string display;
	std::vector<Destination> destinations;
	CurrentSettings current;
	Limits limits;
	std::string estimateReason;
};

struct ProbeRequest {
	bool present = false;
	std::string kind;
	std::string legId;
	std::string serviceName;
	std::string server;
	std::string streamKey;
};

struct Recommendation {
	std::string legId;
	std::string display;
	std::vector<Destination> destinations;
	Limits limits;
	std::string measurementMode = "estimated";
	std::string confidence = "medium";
	std::string reason;
	CurrentSettings value;
};

struct SessionEvent {
	uint64_t sequence = 0;
	std::string type;
	std::string phase;
	double progress = 0;
	std::string code;
	std::string legId;
	std::string measurementMode;
};

struct Session : std::enable_shared_from_this<Session> {
	std::string id;
	std::string topology;
	std::vector<LegRequest> legs;
	ProbeRequest probe;
	bool activeProbeEligible = false;
	std::string activeProbeDenialReason;

	std::atomic<SessionState> state{SessionState::Created};
	std::atomic<bool> cancelRequested{false};
	// Serializes creation and inspection of worker. IPC calls may arrive from
	// different client connections, so the atomic state alone is not sufficient
	// to protect std::future from concurrent assignment/wait operations.
	std::mutex lifecycleMutex;
	std::future<void> worker;

	std::mutex mutex;
	uint64_t nextSequence = 1;
	std::queue<SessionEvent> events;
	std::string resultJson;

	// The worker owns this output. Cancel only borrows it while holding this
	// mutex, so it can request a force-stop without racing release.
	std::mutex probeMutex;
	obs_output_t *activeProbeOutput = nullptr;
};

std::mutex sessionsMutex;
std::shared_ptr<Session> activeSession;
std::atomic<uint64_t> nextSessionId{1};
std::atomic<bool> shuttingDown{false};

static void returnError(std::vector<ipc::value> &rval, const char *message)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Error));
	rval.push_back(ipc::value(message));
}

static std::string lowerCopy(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
	return value;
}

static bool hasSuffix(const std::string &value, const std::string &suffix)
{
	return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static bool isOfficialTwitchServer(const std::string &server)
{
	std::string value = lowerCopy(server);
	if (value == "auto")
		return true;

	const size_t scheme = value.find("://");
	if (scheme == std::string::npos || (value.compare(0, 7, "rtmp://") != 0 && value.compare(0, 8, "rtmps://") != 0))
		return false;

	const size_t hostStart = scheme + 3;
	const size_t hostEnd = value.find_first_of("/:?#", hostStart);
	const std::string host = value.substr(hostStart, hostEnd == std::string::npos ? std::string::npos : hostEnd - hostStart);
	if (host.empty() || host.find('@') != std::string::npos)
		return false;

	return host == "live.twitch.tv" || hasSuffix(host, ".twitch.tv") || host == "live-video.net" || hasSuffix(host, ".live-video.net");
}

static void trim(std::string &value)
{
	while (!value.empty() && std::isspace((unsigned char)value.back()))
		value.pop_back();
	size_t first = 0;
	while (first < value.size() && std::isspace((unsigned char)value[first]))
		first++;
	if (first)
		value.erase(0, first);
}

static std::string normalizeTwitchBandwidthKey(std::string key)
{
	trim(key);
	const size_t queryPos = key.find('?');
	const std::string base = key.substr(0, queryPos);
	std::vector<std::string> retained;

	if (queryPos != std::string::npos) {
		std::string query = key.substr(queryPos + 1);
		size_t offset = 0;
		while (offset <= query.size()) {
			const size_t next = query.find('&', offset);
			std::string item = query.substr(offset, next == std::string::npos ? std::string::npos : next - offset);
			const size_t equals = item.find('=');
			const std::string name = lowerCopy(item.substr(0, equals));
			if (!item.empty() && name != "bandwidthtest")
				retained.push_back(item);
			if (next == std::string::npos)
				break;
			offset = next + 1;
		}
	}

	std::string result = base + "?";
	for (const auto &item : retained) {
		result += item;
		result += "&";
	}
	result += "bandwidthtest=true";
	return result;
}

static std::string defaultEstimateReason(const std::string &topology, const LegRequest &leg)
{
	if (!leg.estimateReason.empty())
		return leg.estimateReason;
	if (topology == "custom-rtmp")
		return "custom_rtmp";
	if (topology == "cloud-multistream")
		return "cloud_multistream";
	if (topology == "dual-output")
		return "dual_output";
	if (topology == "enhanced-broadcasting")
		return "enhanced_broadcasting";
	if (topology == "stream-shift")
		return "stream_shift";
	if (topology == "mixed")
		return "mixed_topology";
	return "non_twitch";
}

static bool isKnownDisplay(const std::string &display)
{
	return display == "horizontal" || display == "vertical" || display == "both";
}

static bool isKnownPlatform(const std::string &platform)
{
	static const std::set<std::string> known = {"twitch", "youtube", "facebook", "kick", "tiktok", "custom", "other"};
	return known.count(platform) != 0;
}

static bool isKnownTopology(const std::string &topology)
{
	static const std::set<std::string> known = {"direct-single",         "cloud-multistream", "custom-rtmp", "dual-output",
						    "enhanced-broadcasting", "stream-shift",      "mixed"};
	return known.count(topology) != 0;
}

static bool parseRequest(const std::string &json, Session &session, std::string &error)
{
	obs_data_t *root = obs_data_create_from_json(json.c_str());
	if (!root) {
		error = "invalid_autoconfig_request_json";
		return false;
	}

	bool valid = true;
	if ((int)obs_data_get_int(root, "schemaVersion") != kSchemaVersion) {
		error = "unsupported_autoconfig_schema";
		valid = false;
	}

	session.topology = obs_data_get_string(root, "topology");
	if (valid && !isKnownTopology(session.topology)) {
		error = "invalid_autoconfig_topology";
		valid = false;
	}

	obs_data_array_t *legs = obs_data_get_array(root, "legs");
	const size_t legCount = legs ? obs_data_array_count(legs) : 0;
	if (valid && (legCount == 0 || legCount > 8)) {
		error = "invalid_autoconfig_legs";
		valid = false;
	}

	std::set<std::string> legIds;
	for (size_t i = 0; valid && i < legCount; i++) {
		obs_data_t *item = obs_data_array_item(legs, i);
		LegRequest leg;
		leg.legId = obs_data_get_string(item, "legId");
		leg.display = obs_data_get_string(item, "display");
		leg.estimateReason = obs_data_get_string(item, "estimateReason");

		if (leg.legId.empty() || leg.legId.size() > 128 || !legIds.insert(leg.legId).second || !isKnownDisplay(leg.display)) {
			error = "invalid_autoconfig_leg_identity";
			valid = false;
		}

		obs_data_t *current = obs_data_get_obj(item, "current");
		if (!current) {
			error = "missing_autoconfig_current_settings";
			valid = false;
		} else {
			leg.current.width = (int)obs_data_get_int(current, "width");
			leg.current.height = (int)obs_data_get_int(current, "height");
			leg.current.fpsNum = (int)obs_data_get_int(current, "fpsNum");
			leg.current.fpsDen = (int)obs_data_get_int(current, "fpsDen");
			leg.current.bitrateKbps = (int)obs_data_get_int(current, "bitrateKbps");
			leg.current.encoderId = obs_data_get_string(current, "encoderId");
			leg.current.codec = obs_data_get_string(current, "codec");
			leg.current.preset = obs_data_get_string(current, "preset");
			if (leg.current.width < 64 || leg.current.width > 8192 || leg.current.height < 64 || leg.current.height > 8192 ||
			    leg.current.fpsNum <= 0 || leg.current.fpsNum > 240000 || leg.current.fpsDen <= 0 || leg.current.fpsDen > 10000 ||
			    leg.current.bitrateKbps < 0 || leg.current.bitrateKbps > 100000) {
				error = "invalid_autoconfig_current_settings";
				valid = false;
			}
			obs_data_release(current);
		}

		obs_data_t *limits = obs_data_get_obj(item, "limits");
		if (limits) {
			leg.limits.maxBitrateKbps = (int)obs_data_get_int(limits, "maxBitrateKbps");
			leg.limits.maxWidth = (int)obs_data_get_int(limits, "maxWidth");
			leg.limits.maxHeight = (int)obs_data_get_int(limits, "maxHeight");
			leg.limits.maxFpsNum = (int)obs_data_get_int(limits, "maxFpsNum");
			leg.limits.maxFpsDen = (int)obs_data_get_int(limits, "maxFpsDen");
			if (leg.limits.maxFpsNum > 0 && leg.limits.maxFpsDen <= 0)
				leg.limits.maxFpsDen = 1;
			obs_data_release(limits);
		}

		obs_data_array_t *destinations = obs_data_get_array(item, "destinations");
		const size_t destinationCount = destinations ? obs_data_array_count(destinations) : 0;
		if (destinationCount == 0 || destinationCount > 16) {
			error = "invalid_autoconfig_destinations";
			valid = false;
		} else {
			for (size_t di = 0; di < destinationCount; di++) {
				obs_data_t *destination = obs_data_array_item(destinations, di);
				Destination parsed{lowerCopy(obs_data_get_string(destination, "platform"))};
				obs_data_release(destination);
				if (!isKnownPlatform(parsed.platform)) {
					error = "invalid_autoconfig_platform";
					valid = false;
					break;
				}
				leg.destinations.push_back(std::move(parsed));
			}
		}
		if (destinations)
			obs_data_array_release(destinations);

		obs_data_release(item);
		if (valid)
			session.legs.push_back(std::move(leg));
	}
	if (legs)
		obs_data_array_release(legs);

	obs_data_t *probe = obs_data_get_obj(root, "activeProbe");
	if (valid && probe) {
		session.probe.present = true;
		session.probe.kind = obs_data_get_string(probe, "kind");
		session.probe.legId = obs_data_get_string(probe, "legId");
		session.probe.serviceName = obs_data_get_string(probe, "serviceName");
		session.probe.server = obs_data_get_string(probe, "server");
		session.probe.streamKey = obs_data_get_string(probe, "streamKey");
	}
	if (probe)
		obs_data_release(probe);
	obs_data_release(root);

	if (!valid)
		return false;

	// Active probing is deliberately default-deny. A request that does not meet
	// every invariant remains usable, but all legs are estimated and the secret
	// is discarded before any network object can be created.
	session.activeProbeEligible =
		session.probe.present && session.topology == "direct-single" && session.legs.size() == 1 && session.legs[0].destinations.size() == 1 &&
		session.legs[0].destinations[0].platform == "twitch" && session.probe.kind == "twitch-standard-v1" && session.probe.serviceName == "Twitch" &&
		session.probe.legId == session.legs[0].legId && !session.probe.streamKey.empty() && isOfficialTwitchServer(session.probe.server);
	if (session.probe.present && !session.activeProbeEligible) {
		session.activeProbeDenialReason = "active_probe_not_eligible";
		session.probe.streamKey.clear();
		session.probe.server.clear();
	}

	return true;
}

static void pushEvent(const std::shared_ptr<Session> &session, const char *type, const char *phase, double progress, const std::string &code = {},
		      const std::string &legId = {}, const std::string &measurementMode = {})
{
	std::lock_guard<std::mutex> lock(session->mutex);
	SessionEvent event;
	event.sequence = session->nextSequence++;
	event.type = type;
	event.phase = phase;
	event.progress = progress;
	event.code = code;
	event.legId = legId;
	event.measurementMode = measurementMode;
	session->events.push(std::move(event));
}

static std::shared_ptr<Session> findSession(const std::string &id)
{
	std::lock_guard<std::mutex> lock(sessionsMutex);
	if (activeSession && activeSession->id == id)
		return activeSession;
	return nullptr;
}

static std::string resolveEncoderId(const std::string &id)
{
	if (id.empty())
		return {};
	// The software VideoToolbox implementation is not a hardware-capacity
	// signal and Desktop cannot currently apply Apple encoder-family metadata.
	// Registered hardware VideoToolbox IDs are still preserved when already
	// selected, but this software implementation must never be recommended.
	if (id == "com.apple.videotoolbox.videoencoder.h264")
		return {};
	if (obs_get_encoder_codec(id.c_str()))
		return id;
	for (const auto &option : osn::EncoderUtils::videoEncoderOptions) {
		if (option.simple_name == id) {
			const std::string internal = osn::EncoderUtils::getInternalEncoderFromSimple(id.c_str());
			return osn::EncoderUtils::isEncoderRegistered(internal) ? internal : std::string{};
		}
	}
	return {};
}

static EncoderSelection chooseEncoder(const CurrentSettings &current)
{
	if (!resolveEncoderId(current.encoderId).empty())
		return {current.encoderId, false};

	// Apple/VideoToolbox is intentionally not an automatic fallback. Desktop's
	// current encoder metadata has no Apple family mapping, so returning one here
	// would produce a recommendation it cannot apply. A currently selected and
	// registered Apple encoder is still preserved by the branch above.
	const char *preferred[] = {ENCODER_NVENC_H264_TEX, ADVANCED_ENCODER_QSV_V2, ADVANCED_ENCODER_QSV, ADVANCED_ENCODER_AMD, ADVANCED_ENCODER_X264};
	for (const char *candidate : preferred) {
		if (candidate && osn::EncoderUtils::isEncoderRegistered(candidate))
			return {candidate, candidate != current.encoderId};
	}
	return {{}, !current.encoderId.empty()};
}

static std::string scratchEncoderId(const std::string &recommendationId)
{
	const std::string encoder = resolveEncoderId(recommendationId);
	if (encoder == ENCODER_NVENC_H264_TEX)
		return "obs_nvenc_h264_soft";
	if (encoder == ENCODER_NVENC_HEVC_TEX)
		return "obs_nvenc_hevc_soft";
	if (encoder == ENCODER_NVENC_AV1_TEX)
		return "obs_nvenc_av1_soft";
	if (encoder == ADVANCED_ENCODER_QSV)
		return "obs_qsv11_soft";
	if (encoder == ADVANCED_ENCODER_QSV_V2)
		return "obs_qsv11_soft_v2";
	if (encoder == ADVANCED_ENCODER_QSV_AV1)
		return "obs_qsv11_av1_soft";
	if (encoder == ADVANCED_ENCODER_QSV_HEVC)
		return "obs_qsv11_hevc_soft";
	if (encoder == ADVANCED_ENCODER_AMD)
		return "h264_fallback_amf";
	if (encoder == ADVANCED_ENCODER_AMD_HEVC)
		return "h265_fallback_amf";
	if (encoder == ADVANCED_ENCODER_AMD_AV1)
		return "av1_fallback_amf";
	return encoder;
}

static bool isX264Preset(const std::string &preset)
{
	static const std::set<std::string> supported = {"ultrafast", "superfast", "veryfast", "faster",   "fast",
							"medium",    "slow",      "slower",   "veryslow", "placebo"};
	return supported.count(lowerCopy(preset)) != 0;
}

static int offlinePlatformCapKbps(const std::string &platform)
{
	const char *serviceName = nullptr;
	if (platform == "twitch")
		serviceName = "Twitch";
	else if (platform == "youtube")
		serviceName = "YouTube - RTMPS";
	else if (platform == "facebook")
		serviceName = "Facebook Live";
	else
		return 0;

	// This only loads the bundled rtmp-services metadata and invokes its encoder
	// constraints. No output is created and no DNS/network operation is possible.
	obs_data_t *serviceSettings = obs_data_create();
	obs_data_set_string(serviceSettings, "service", serviceName);
	obs_service_t *service = obs_service_create_private("rtmp_common", "auto_optimizer_offline_cap", serviceSettings);
	obs_data_release(serviceSettings);
	if (!service)
		return 0;

	constexpr int probeValue = 100000;
	obs_data_t *encoderSettings = obs_data_create();
	obs_data_set_int(encoderSettings, "bitrate", probeValue);
	obs_service_apply_encoder_settings(service, encoderSettings, nullptr);
	const int value = (int)obs_data_get_int(encoderSettings, "bitrate");
	obs_data_release(encoderSettings);
	obs_service_release(service);
	return value > 0 && value < probeValue ? value : 0;
}

static LegRequest withOfflinePlatformCaps(const LegRequest &input)
{
	LegRequest leg = input;
	int strictest = 0;
	for (const auto &destination : leg.destinations) {
		const int cap = offlinePlatformCapKbps(destination.platform);
		if (cap > 0 && (strictest == 0 || cap < strictest))
			strictest = cap;
	}
	if (strictest > 0 && (leg.limits.maxBitrateKbps == 0 || strictest < leg.limits.maxBitrateKbps))
		leg.limits.maxBitrateKbps = strictest;
	return leg;
}

static bool fitWithin(CurrentSettings &value, int maxWidth, int maxHeight)
{
	maxWidth = std::max(64, maxWidth);
	maxHeight = std::max(64, maxHeight);
	const double scale = std::min({1.0, (double)maxWidth / (double)value.width, (double)maxHeight / (double)value.height});
	const int width = std::max(64, ((int)std::floor((double)value.width * scale)) & ~1);
	const int height = std::max(64, ((int)std::floor((double)value.height * scale)) & ~1);
	const bool changed = width != value.width || height != value.height;
	value.width = width;
	value.height = height;
	return changed;
}

static bool capFps(CurrentSettings &value, int maxNum, int maxDen)
{
	maxDen = maxDen > 0 ? maxDen : 1;
	if ((int64_t)value.fpsNum * maxDen <= (int64_t)maxNum * value.fpsDen)
		return false;
	value.fpsNum = maxNum;
	value.fpsDen = maxDen;
	return true;
}

static void applyEncoderSelection(CurrentSettings &value, const EncoderSelection &selection)
{
	if (selection.id != value.encoderId) {
		value.encoderId = selection.id;
		// Presets are encoder-family-specific. Never carry a preset from an
		// unavailable/failed encoder into its replacement; encoder defaults are
		// safer than a syntactically valid preset for the wrong family.
		value.preset.clear();
		const std::string internal = resolveEncoderId(value.encoderId);
		const char *codec = internal.empty() ? nullptr : obs_get_encoder_codec(internal.c_str());
		value.codec = codec ? codec : "h264";
	}
	if (value.codec.empty()) {
		const std::string internal = resolveEncoderId(value.encoderId);
		const char *codec = internal.empty() ? nullptr : obs_get_encoder_codec(internal.c_str());
		value.codec = codec ? codec : "h264";
	}
}

static CurrentSettings baseRecommendation(const LegRequest &leg)
{
	CurrentSettings value = leg.current;
	if (value.bitrateKbps <= 0)
		value.bitrateKbps = kDefaultEstimatedBitrateKbps;
	if (leg.limits.maxBitrateKbps > 0)
		value.bitrateKbps = std::min(value.bitrateKbps, leg.limits.maxBitrateKbps);
	fitWithin(value, leg.limits.maxWidth > 0 ? leg.limits.maxWidth : value.width, leg.limits.maxHeight > 0 ? leg.limits.maxHeight : value.height);
	if (leg.limits.maxFpsNum > 0)
		capFps(value, leg.limits.maxFpsNum, leg.limits.maxFpsDen);
	applyEncoderSelection(value, chooseEncoder(value));
	return value;
}

static CurrentSettings estimateRecommendation(const LegRequest &leg, const HardwareAssessment &hardware)
{
	CurrentSettings value = baseRecommendation(leg);
	if (hardware.attempted) {
		value.width = hardware.value.width;
		value.height = hardware.value.height;
		value.fpsNum = hardware.value.fpsNum;
		value.fpsDen = hardware.value.fpsDen;
		applyEncoderSelection(value, {hardware.value.encoderId, hardware.value.encoderId != value.encoderId});
		value.preset = hardware.value.preset;
	}
	return value;
}

static void putLimits(obs_data_t *parent, const Limits &limits)
{
	if (!limits.any())
		return;
	obs_data_t *obj = obs_data_create();
	if (limits.maxBitrateKbps > 0)
		obs_data_set_int(obj, "maxBitrateKbps", limits.maxBitrateKbps);
	if (limits.maxWidth > 0)
		obs_data_set_int(obj, "maxWidth", limits.maxWidth);
	if (limits.maxHeight > 0)
		obs_data_set_int(obj, "maxHeight", limits.maxHeight);
	if (limits.maxFpsNum > 0) {
		obs_data_set_int(obj, "maxFpsNum", limits.maxFpsNum);
		obs_data_set_int(obj, "maxFpsDen", limits.maxFpsDen > 0 ? limits.maxFpsDen : 1);
	}
	obs_data_set_obj(parent, "limits", obj);
	obs_data_release(obj);
}

static std::string serializeResult(const Session &session, const char *status, const std::vector<Recommendation> &recommendations,
				   const std::string &errorCode = {})
{
	obs_data_t *root = obs_data_create();
	obs_data_set_int(root, "schemaVersion", kSchemaVersion);
	obs_data_set_string(root, "sessionId", session.id.c_str());
	obs_data_set_string(root, "status", status);
	if (!errorCode.empty()) {
		obs_data_t *error = obs_data_create();
		obs_data_set_string(error, "code", errorCode.c_str());
		obs_data_set_obj(root, "error", error);
		obs_data_release(error);
	}

	obs_data_array_t *legs = obs_data_array_create();
	for (const auto &recommendation : recommendations) {
		obs_data_t *leg = obs_data_create();
		obs_data_set_string(leg, "legId", recommendation.legId.c_str());
		obs_data_set_string(leg, "display", recommendation.display.c_str());

		obs_data_array_t *destinations = obs_data_array_create();
		for (const auto &destination : recommendation.destinations) {
			obs_data_t *item = obs_data_create();
			obs_data_set_string(item, "platform", destination.platform.c_str());
			obs_data_array_push_back(destinations, item);
			obs_data_release(item);
		}
		obs_data_set_array(leg, "destinations", destinations);
		obs_data_array_release(destinations);

		obs_data_t *measurement = obs_data_create();
		obs_data_set_string(measurement, "mode", recommendation.measurementMode.c_str());
		obs_data_set_string(measurement, "confidence", recommendation.confidence.c_str());
		if (!recommendation.reason.empty())
			obs_data_set_string(measurement, "reason", recommendation.reason.c_str());
		obs_data_set_obj(leg, "measurement", measurement);
		obs_data_release(measurement);

		obs_data_t *value = obs_data_create();
		obs_data_set_int(value, "width", recommendation.value.width);
		obs_data_set_int(value, "height", recommendation.value.height);
		obs_data_set_int(value, "fpsNum", recommendation.value.fpsNum);
		obs_data_set_int(value, "fpsDen", recommendation.value.fpsDen);
		obs_data_set_int(value, "bitrateKbps", recommendation.value.bitrateKbps);
		obs_data_set_string(value, "encoderId", recommendation.value.encoderId.c_str());
		obs_data_set_string(value, "codec", recommendation.value.codec.c_str());
		if (!recommendation.value.preset.empty())
			obs_data_set_string(value, "preset", recommendation.value.preset.c_str());
		obs_data_set_obj(leg, "recommendation", value);
		obs_data_release(value);

		putLimits(leg, recommendation.limits);
		obs_data_array_push_back(legs, leg);
		obs_data_release(leg);
	}
	obs_data_set_array(root, "legs", legs);
	obs_data_array_release(legs);

	std::string json = obs_data_get_json(root);
	obs_data_release(root);
	return json;
}

struct ProbeResult {
	bool success = false;
	bool cancelled = false;
	uint64_t measuredKbps = 0;
	int platformCapKbps = 0;
	std::string errorCode;
};

static bool silentAudioCallback(void *, uint64_t startTimestamp, uint64_t, uint64_t *outputTimestamp, uint32_t, struct audio_data_mixes_outputs *)
{
	*outputTimestamp = startTimestamp;
	return true;
}

class ScratchResources {
public:
	explicit ScratchResources(Session &session_, int stopTimeoutMs_ = kProbeStopTimeoutMs) : session(session_), stopTimeoutMs(stopTimeoutMs_) {}
	~ScratchResources() { cleanup(); }

	Session &session;
	int stopTimeoutMs;
	uint32_t videoWidth = 0;
	uint32_t videoHeight = 0;
	uint32_t videoFpsNum = 0;
	uint32_t videoFpsDen = 1;
	video_t *syntheticVideo = nullptr;
	obs_view_t *scratchView = nullptr;
	obs_core_video_mix_t *scratchMix = nullptr;
	std::unique_ptr<obs_video_info> scratchViewInfo;
	bool coreVideoMix = false;
	audio_t *syntheticAudio = nullptr;
	obs_encoder_t *videoEncoder = nullptr;
	obs_encoder_t *audioEncoder = nullptr;
	obs_service_t *service = nullptr;
	obs_output_t *output = nullptr;
	std::atomic<bool> stopFeeder{false};
	std::atomic<uint32_t> scheduledFrames{0};
	std::atomic<uint32_t> submittedFrames{0};
	std::atomic<uint32_t> lockFailedFrames{0};
	std::atomic<uint32_t> lateFrames{0};
	std::thread feeder;
	std::vector<uint8_t> framePatternA;
	std::vector<uint8_t> framePatternB;

	bool createSyntheticVideo(uint32_t width, uint32_t height, uint32_t fpsNum, uint32_t fpsDen, bool useCoreVideoMix = false)
	{
		videoWidth = width;
		videoHeight = height;
		videoFpsNum = fpsNum;
		videoFpsDen = fpsDen;
		coreVideoMix = useCoreVideoMix;
		if (coreVideoMix) {
			// Hardware texture encoders require a real OBS video mix. Create an
			// isolated, source-free view instead of attaching to any user canvas.
			// obs_view_add2 creates only a private mix and does not reset the
			// application's existing video contexts.
			scratchViewInfo = std::make_unique<obs_video_info>();
			scratchViewInfo->base_width = width;
			scratchViewInfo->base_height = height;
			scratchViewInfo->output_width = width;
			scratchViewInfo->output_height = height;
			scratchViewInfo->fps_num = fpsNum;
			scratchViewInfo->fps_den = fpsDen;
			scratchViewInfo->fps_type = 1;
			scratchViewInfo->output_format = VIDEO_FORMAT_NV12;
			scratchViewInfo->colorspace = VIDEO_CS_709;
			scratchViewInfo->range = VIDEO_RANGE_PARTIAL;
			scratchViewInfo->scale_type = OBS_SCALE_BILINEAR;
			scratchViewInfo->adapter = 0;
			scratchViewInfo->gpu_conversion = true;
			scratchView = obs_view_create();
			syntheticVideo = scratchView ? obs_view_add2(scratchView, scratchViewInfo.get()) : nullptr;
			scratchMix = syntheticVideo ? obs_video_mix_get(scratchViewInfo.get(), OBS_MAIN_VIDEO_RENDERING) : nullptr;
			if (!syntheticVideo || !scratchMix) {
				if (scratchView)
					obs_view_remove(scratchView);
				if (scratchView)
					obs_view_destroy(scratchView);
				scratchView = nullptr;
				scratchMix = nullptr;
				scratchViewInfo.reset();
				return false;
			}
			return true;
		}

		video_output_info info{};
		info.name = "auto_optimizer_synthetic_video";
		info.format = VIDEO_FORMAT_NV12;
		info.fps_num = fpsNum;
		info.fps_den = fpsDen;
		info.width = width;
		info.height = height;
		info.cache_size = 3;
		info.colorspace = VIDEO_CS_709;
		info.range = VIDEO_RANGE_PARTIAL;
		if (video_output_open(&syntheticVideo, &info) != VIDEO_OUTPUT_SUCCESS)
			return false;

		const size_t frameBytes = (size_t)width * (size_t)height * 3U / 2U;
		framePatternA.resize(frameBytes);
		framePatternB.resize(frameBytes);
		uint64_t random = 0x9e3779b97f4a7c15ULL ^ ((uint64_t)width << 32) ^ ((uint64_t)height << 16) ^ fpsNum;
		for (size_t offset = 0; offset < frameBytes; offset++) {
			random ^= random << 7;
			random ^= random >> 9;
			random ^= random << 8;
			framePatternA[offset] = (uint8_t)random;
			framePatternB[offset] = (uint8_t)(random >> 8) ^ (uint8_t)(offset * 31U);
		}
		return true;
	}

	bool createSyntheticAudio()
	{
		audio_output_info info{};
		info.name = "auto_optimizer_synthetic_audio";
		info.samples_per_sec = 48000;
		info.format = AUDIO_FORMAT_FLOAT_PLANAR;
		info.speakers = SPEAKERS_STEREO;
		info.input_callback = silentAudioCallback;
		return audio_output_open(&syntheticAudio, &info) == AUDIO_OUTPUT_SUCCESS;
	}

	void startFeeder()
	{
		if (coreVideoMix)
			return;
		feeder = std::thread([this]() {
			const auto frameDuration = std::chrono::nanoseconds((1000000000ULL * videoFpsDen) / videoFpsNum);
			auto nextFrame = std::chrono::steady_clock::now();
			uint64_t timestamp = os_gettime_ns();
			bool alternate = false;
			while (!stopFeeder.load()) {
				scheduledFrames.fetch_add(1, std::memory_order_relaxed);
				video_frame frame{};
				if (video_output_lock_frame(syntheticVideo, &frame, 1, timestamp)) {
					const std::vector<uint8_t> &pattern = alternate ? framePatternB : framePatternA;
					const uint8_t *luma = pattern.data();
					const uint8_t *chroma = pattern.data() + (size_t)videoWidth * videoHeight;
					for (uint32_t y = 0; y < videoHeight; y++)
						std::memcpy(frame.data[0] + y * frame.linesize[0], luma + (size_t)y * videoWidth, videoWidth);
					for (uint32_t y = 0; y < videoHeight / 2; y++)
						std::memcpy(frame.data[1] + y * frame.linesize[1], chroma + (size_t)y * videoWidth, videoWidth);
					video_output_unlock_frame(syntheticVideo);
					submittedFrames.fetch_add(1, std::memory_order_relaxed);
					alternate = !alternate;
				} else {
					lockFailedFrames.fetch_add(1, std::memory_order_relaxed);
				}
				timestamp += (uint64_t)frameDuration.count();
				nextFrame += frameDuration;
				const auto now = std::chrono::steady_clock::now();
				if (nextFrame < now) {
					// Skip missed schedule slots instead of submitting a burst of
					// catch-up frames that would distort encoder throughput.
					lateFrames.fetch_add(1, std::memory_order_relaxed);
					nextFrame = now + frameDuration;
					timestamp = os_gettime_ns() + (uint64_t)frameDuration.count();
				}
				std::this_thread::sleep_until(nextFrame);
			}
		});
	}

	void publishOutput()
	{
		std::lock_guard<std::mutex> lock(session.probeMutex);
		session.activeProbeOutput = output;
	}

	void cleanup()
	{
		if (output && obs_output_active(output)) {
			obs_output_force_stop(output);
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(stopTimeoutMs);
			while (obs_output_active(output) && std::chrono::steady_clock::now() < deadline)
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			if (obs_output_active(output)) {
				// Never release an active output or media objects it may still
				// reference. The public cancel call has its own bounded wait and
				// reports cleanup_timeout; this worker remains alive solely to
				// finish safe teardown if OBS takes longer than expected.
				while (obs_output_active(output)) {
					obs_output_force_stop(output);
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
				}
			}
		}

		{
			std::lock_guard<std::mutex> lock(session.probeMutex);
			if (session.activeProbeOutput == output)
				session.activeProbeOutput = nullptr;
			if (output) {
				obs_output_release(output);
				output = nullptr;
			}
		}
		stopFeeder.store(true);
		if (feeder.joinable())
			feeder.join();

		if (videoEncoder) {
			obs_encoder_release(videoEncoder);
			videoEncoder = nullptr;
		}
		if (audioEncoder) {
			obs_encoder_release(audioEncoder);
			audioEncoder = nullptr;
		}
		if (syntheticVideo && !coreVideoMix) {
			video_output_stop(syntheticVideo);
			video_output_close(syntheticVideo);
			syntheticVideo = nullptr;
		}
		if (scratchView) {
			obs_view_remove(scratchView);
			obs_view_destroy(scratchView);
			scratchView = nullptr;
			scratchMix = nullptr;
			syntheticVideo = nullptr;
			// The render thread removes orphaned mixes on its next tick. Keep
			// the video-info snapshot alive until that tick has elapsed.
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			scratchViewInfo.reset();
		}
		if (syntheticAudio) {
			audio_output_close(syntheticAudio);
			syntheticAudio = nullptr;
		}
		if (service) {
			obs_service_release(service);
			service = nullptr;
		}
	}
};

static bool waitForOutputInactive(obs_output_t *output, int timeoutMs)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	while (output && obs_output_active(output) && std::chrono::steady_clock::now() < deadline)
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	return !output || !obs_output_active(output);
}

struct HardwareAttempt {
	bool success = false;
	bool cancelled = false;
	bool timedOut = false;
	uint32_t totalFrames = 0;
	uint32_t skippedFrames = 0;
	uint32_t encodedFrames = 0;
	uint32_t scheduledFrames = 0;
	uint32_t submittedFrames = 0;
	uint32_t lockFailedFrames = 0;
	uint32_t lateFrames = 0;
	std::string errorCode;
};

static bool waitForScratchInterval(const std::shared_ptr<Session> &session, obs_output_t *output, std::chrono::steady_clock::time_point intervalDeadline,
				   std::chrono::steady_clock::time_point phaseDeadline, HardwareAttempt &result)
{
	while (std::chrono::steady_clock::now() < intervalDeadline) {
		if (session->cancelRequested.load()) {
			result.cancelled = true;
			if (output)
				obs_output_force_stop(output);
			return false;
		}
		if (std::chrono::steady_clock::now() >= phaseDeadline) {
			result.timedOut = true;
			if (output)
				obs_output_force_stop(output);
			return false;
		}
		if (output && !obs_output_active(output)) {
			result.errorCode = "hardware_benchmark_output_stopped";
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	return true;
}

static HardwareAttempt runEncoderWorkload(const std::shared_ptr<Session> &session, const CurrentSettings &candidate,
					  std::chrono::steady_clock::time_point phaseDeadline)
{
	HardwareAttempt result;
	if (session->cancelRequested.load()) {
		result.cancelled = true;
		return result;
	}
	if (std::chrono::steady_clock::now() >= phaseDeadline) {
		result.timedOut = true;
		return result;
	}

	const std::string resolvedEncoderId = resolveEncoderId(candidate.encoderId);
	const bool useCoreVideoMix = resolvedEncoderId != ADVANCED_ENCODER_X264;
	const std::string encoderId = useCoreVideoMix ? resolvedEncoderId : scratchEncoderId(candidate.encoderId);
	if (encoderId.empty() || !obs_get_encoder_codec(encoderId.c_str())) {
		result.errorCode = "hardware_benchmark_encoder_unavailable";
		return result;
	}

	ScratchResources resources(*session, kHardwareStopTimeoutMs);
	if (!resources.createSyntheticVideo((uint32_t)candidate.width, (uint32_t)candidate.height, (uint32_t)candidate.fpsNum, (uint32_t)candidate.fpsDen,
					    useCoreVideoMix)) {
		result.errorCode = "hardware_benchmark_video_create_failed";
		return result;
	}
	if (!resources.createSyntheticAudio()) {
		result.errorCode = "hardware_benchmark_audio_create_failed";
		return result;
	}

	obs_data_t *encoderSettings = obs_data_create();
	obs_data_set_int(encoderSettings, "bitrate", std::clamp(candidate.bitrateKbps, 500, kProbeMaximumBitrateKbps));
	obs_data_set_string(encoderSettings, "rate_control", "CBR");
	obs_data_set_int(encoderSettings, "keyint_sec", 2);
	if (encoderId == ADVANCED_ENCODER_X264 && isX264Preset(candidate.preset))
		obs_data_set_string(encoderSettings, "preset", candidate.preset.c_str());
	resources.videoEncoder = obs_video_encoder_create(encoderId.c_str(), "auto_optimizer_hardware_benchmark_encoder", encoderSettings, nullptr);
	obs_data_release(encoderSettings);
	if (!resources.videoEncoder) {
		result.errorCode = "hardware_benchmark_encoder_create_failed";
		return result;
	}
	if (useCoreVideoMix)
		obs_encoder_set_video_mix(resources.videoEncoder, resources.scratchMix);
	else
		obs_encoder_set_video(resources.videoEncoder, resources.syntheticVideo);

	obs_data_t *audioSettings = obs_data_create();
	obs_data_set_int(audioSettings, "bitrate", 32);
	resources.audioEncoder = obs_audio_encoder_create("ffmpeg_aac", "auto_optimizer_hardware_benchmark_audio", audioSettings, 0, nullptr);
	obs_data_release(audioSettings);
	if (!resources.audioEncoder) {
		result.errorCode = "hardware_benchmark_audio_encoder_create_failed";
		return result;
	}
	obs_encoder_set_audio(resources.audioEncoder, resources.syntheticAudio);

	resources.output = obs_output_create("null_output", "auto_optimizer_hardware_benchmark_output", nullptr, nullptr);
	if (!resources.output) {
		result.errorCode = "hardware_benchmark_output_create_failed";
		return result;
	}
	obs_output_set_video_encoder(resources.output, resources.videoEncoder);
	obs_output_set_audio_encoder(resources.output, resources.audioEncoder, 0);
	resources.publishOutput();
	resources.startFeeder();

	if (session->cancelRequested.load()) {
		result.cancelled = true;
		return result;
	}
	if (!obs_output_start(resources.output)) {
		result.errorCode = "hardware_benchmark_start_failed";
		return result;
	}

	const auto warmupDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kHardwareWarmupMs);
	if (!waitForScratchInterval(session, resources.output, warmupDeadline, phaseDeadline, result))
		return result;

	const uint32_t startTotal = video_output_get_total_frames(resources.syntheticVideo);
	const uint32_t startSkipped = video_output_get_skipped_frames(resources.syntheticVideo);
	const uint32_t startEncoded = obs_encoder_get_encoded_frames(resources.videoEncoder);
	const uint32_t startScheduled = resources.scheduledFrames.load(std::memory_order_relaxed);
	const uint32_t startSubmitted = resources.submittedFrames.load(std::memory_order_relaxed);
	const uint32_t startLockFailed = resources.lockFailedFrames.load(std::memory_order_relaxed);
	const uint32_t startLate = resources.lateFrames.load(std::memory_order_relaxed);
	const auto sampleStart = std::chrono::steady_clock::now();
	const auto sampleDeadline = sampleStart + std::chrono::milliseconds(kHardwareSampleMs);
	if (!waitForScratchInterval(session, resources.output, sampleDeadline, phaseDeadline, result))
		return result;

	const auto sampleEnd = std::chrono::steady_clock::now();
	result.totalFrames = video_output_get_total_frames(resources.syntheticVideo) - startTotal;
	result.skippedFrames = video_output_get_skipped_frames(resources.syntheticVideo) - startSkipped;
	result.encodedFrames = obs_encoder_get_encoded_frames(resources.videoEncoder) - startEncoded;
	result.scheduledFrames = resources.scheduledFrames.load(std::memory_order_relaxed) - startScheduled;
	result.submittedFrames = resources.submittedFrames.load(std::memory_order_relaxed) - startSubmitted;
	result.lockFailedFrames = resources.lockFailedFrames.load(std::memory_order_relaxed) - startLockFailed;
	result.lateFrames = resources.lateFrames.load(std::memory_order_relaxed) - startLate;

	obs_output_stop(resources.output);
	if (!waitForOutputInactive(resources.output, kHardwareStopTimeoutMs)) {
		obs_output_force_stop(resources.output);
		if (!waitForOutputInactive(resources.output, kHardwareStopTimeoutMs)) {
			result.errorCode = "hardware_benchmark_cleanup_timeout";
			return result;
		}
	}

	const double elapsedSeconds = std::chrono::duration<double>(sampleEnd - sampleStart).count();
	const double requestedFps = (double)candidate.fpsNum / (double)candidate.fpsDen;
	const uint32_t expectedFrames = (uint32_t)std::max(1.0, std::floor(requestedFps * elapsedSeconds));
	const uint32_t pipelineAllowance = std::min(4U, expectedFrames);
	const uint32_t minimumEncoded = std::max(3U, (expectedFrames - pipelineAllowance) * 85U / 100U);
	const uint32_t allowedSkipped = std::max(1U, result.totalFrames * 5U / 100U);
	const uint32_t minimumSubmitted = std::max(3U, expectedFrames * 85U / 100U);
	const uint32_t allowedLockFailed = std::max(1U, result.scheduledFrames * 5U / 100U);
	const uint32_t allowedLate = std::max(1U, result.scheduledFrames * 5U / 100U);
	const bool feederHealthy = useCoreVideoMix || (result.submittedFrames >= minimumSubmitted && result.lockFailedFrames <= allowedLockFailed &&
						       result.lateFrames <= allowedLate);
	result.success = feederHealthy && result.encodedFrames >= minimumEncoded && result.skippedFrames <= allowedSkipped;
	if (!result.success)
		result.errorCode = "hardware_benchmark_overloaded";
	return result;
}

static bool sameHardwareWorkload(const CurrentSettings &left, const CurrentSettings &right)
{
	return left.width == right.width && left.height == right.height && left.fpsNum == right.fpsNum && left.fpsDen == right.fpsDen &&
	       resolveEncoderId(left.encoderId) == resolveEncoderId(right.encoderId);
}

static CurrentSettings lowerHardwareCandidate(CurrentSettings value, int longEdge, int shortEdge)
{
	const bool landscape = value.width >= value.height;
	fitWithin(value, landscape ? longEdge : shortEdge, landscape ? shortEdge : longEdge);
	capFps(value, 30, 1);
	return value;
}

static bool isInfrastructureFailure(const HardwareAttempt &attempt)
{
	return !attempt.success && !attempt.cancelled && attempt.errorCode != "hardware_benchmark_overloaded";
}

static HardwareAssessment assessHardware(const std::shared_ptr<Session> &session, const LegRequest &leg, std::chrono::steady_clock::time_point phaseDeadline)
{
	HardwareAssessment assessment;
	assessment.attempted = true;
	const EncoderSelection initialSelection = chooseEncoder(leg.current);
	CurrentSettings target = baseRecommendation(leg);

	const bool landscape = target.width >= target.height;
	bool ceilingConstrained = fitWithin(target, landscape ? kHardwareMaximumLongEdge : kHardwareMaximumShortEdge,
					    landscape ? kHardwareMaximumShortEdge : kHardwareMaximumLongEdge);
	ceilingConstrained = capFps(target, 60, 1) || ceilingConstrained;
	if (target.encoderId.empty()) {
		assessment.constrained = true;
		assessment.reason = "hardware_no_usable_encoder";
		assessment.value = target;
		return assessment;
	}

	auto unavailable = [&](const HardwareAttempt &failedAttempt) {
		assessment.passed = false;
		assessment.constrained = true;
		assessment.reason = failedAttempt.timedOut || std::chrono::steady_clock::now() >= phaseDeadline ? "hardware_benchmark_timeout"
														: "hardware_benchmark_unavailable";
		// An infrastructure failure provides no evidence for a downgrade. Keep
		// the capped current recommendation instead of returning an untested
		// resolution or encoder.
		assessment.value = target;
	};

	HardwareAttempt attempt = runEncoderWorkload(session, target, phaseDeadline);
	if (attempt.cancelled) {
		assessment.cancelled = true;
		return assessment;
	}
	if (attempt.success) {
		assessment.passed = true;
		assessment.value = target;
		assessment.constrained = initialSelection.replaced || ceilingConstrained;
		if (initialSelection.replaced)
			assessment.reason = "hardware_encoder_unavailable_fallback";
		else if (ceilingConstrained)
			assessment.reason = "hardware_benchmark_ceiling";
		return assessment;
	}
	if (isInfrastructureFailure(attempt)) {
		unavailable(attempt);
		return assessment;
	}

	// A genuine overload is the only reason to test lower settings. Preserve the
	// selected hardware encoder through the first downgrade before considering
	// a software fallback.
	CurrentSettings lowerSelected = lowerHardwareCandidate(target, 1280, 720);
	if (!sameHardwareWorkload(target, lowerSelected) && std::chrono::steady_clock::now() < phaseDeadline) {
		attempt = runEncoderWorkload(session, lowerSelected, phaseDeadline);
		if (attempt.cancelled) {
			assessment.cancelled = true;
			return assessment;
		}
		if (attempt.success) {
			assessment.passed = true;
			assessment.constrained = true;
			assessment.reason = "hardware_benchmark_resolution_fallback";
			assessment.value = lowerSelected;
			return assessment;
		}
		if (isInfrastructureFailure(attempt)) {
			unavailable(attempt);
			return assessment;
		}
	}

	CurrentSettings softwareCandidate = lowerSelected;
	if (osn::EncoderUtils::isEncoderRegistered(ADVANCED_ENCODER_X264) && resolveEncoderId(softwareCandidate.encoderId) != ADVANCED_ENCODER_X264) {
		applyEncoderSelection(softwareCandidate, {ADVANCED_ENCODER_X264, true});
		attempt = runEncoderWorkload(session, softwareCandidate, phaseDeadline);
		if (attempt.cancelled) {
			assessment.cancelled = true;
			return assessment;
		}
		if (attempt.success) {
			assessment.passed = true;
			assessment.constrained = true;
			assessment.reason = "hardware_benchmark_encoder_fallback";
			assessment.value = softwareCandidate;
			return assessment;
		}
		if (isInfrastructureFailure(attempt)) {
			unavailable(attempt);
			return assessment;
		}
	}

	CurrentSettings conservative = lowerHardwareCandidate(softwareCandidate, 640, 360);
	if (!sameHardwareWorkload(softwareCandidate, conservative) && std::chrono::steady_clock::now() < phaseDeadline) {
		attempt = runEncoderWorkload(session, conservative, phaseDeadline);
		if (attempt.cancelled) {
			assessment.cancelled = true;
			return assessment;
		}
		if (attempt.success) {
			assessment.passed = true;
			assessment.constrained = true;
			assessment.reason = "hardware_benchmark_resolution_fallback";
			assessment.value = conservative;
			return assessment;
		}
		if (isInfrastructureFailure(attempt)) {
			unavailable(attempt);
			return assessment;
		}
	}

	// No candidate passed. Do not present the last failed candidate as a safe
	// recommendation; retain capped current settings and make the low-confidence
	// outcome explicit to Desktop.
	assessment.passed = false;
	assessment.constrained = true;
	assessment.reason = std::chrono::steady_clock::now() >= phaseDeadline || attempt.timedOut ? "hardware_benchmark_timeout"
												  : "hardware_benchmark_overloaded";
	assessment.value = target;
	return assessment;
}

static ProbeResult runTwitchProbe(const std::shared_ptr<Session> &session, const LegRequest &leg)
{
	ProbeResult result;
	ScratchResources resources(*session);

	obs_data_t *serviceSettings = obs_data_create();
	obs_data_set_string(serviceSettings, "service", "Twitch");
	obs_data_set_string(serviceSettings, "server", session->probe.server.c_str());
	const std::string bandwidthKey = normalizeTwitchBandwidthKey(session->probe.streamKey);
	obs_data_set_string(serviceSettings, "key", bandwidthKey.c_str());
	resources.service = obs_service_create_private("rtmp_common", "auto_optimizer_twitch_probe_service", serviceSettings);
	obs_data_release(serviceSettings);

	// Drop the only application-owned copy as soon as the disposable service has
	// consumed it. It is never included in events, result JSON, or logs.
	session->probe.streamKey.clear();
	if (!resources.service) {
		result.errorCode = "twitch_probe_service_create_failed";
		return result;
	}

	obs_data_t *encoderSettings = obs_data_create();
	const int requested = std::clamp(std::max(leg.current.bitrateKbps, 6000), 500, kProbeMaximumBitrateKbps);
	obs_data_set_int(encoderSettings, "bitrate", requested);
	obs_data_set_string(encoderSettings, "rate_control", "CBR");
	obs_data_set_string(encoderSettings, "preset", "veryfast");
	obs_data_set_int(encoderSettings, "keyint_sec", 2);

	obs_data_t *platformProbe = obs_data_create();
	obs_data_set_int(platformProbe, "bitrate", kProbeMaximumBitrateKbps);
	obs_service_apply_encoder_settings(resources.service, platformProbe, nullptr);
	const int platformReturned = (int)obs_data_get_int(platformProbe, "bitrate");
	if (platformReturned > 0 && platformReturned < kProbeMaximumBitrateKbps)
		result.platformCapKbps = platformReturned;
	obs_data_release(platformProbe);
	if (result.platformCapKbps > 0)
		obs_data_set_int(encoderSettings, "bitrate", std::min(requested, result.platformCapKbps));
	obs_service_apply_encoder_settings(resources.service, encoderSettings, nullptr);

	if (!resources.createSyntheticVideo(128, 128, 30, 1)) {
		obs_data_release(encoderSettings);
		result.errorCode = "twitch_probe_video_create_failed";
		return result;
	}
	if (!resources.createSyntheticAudio()) {
		obs_data_release(encoderSettings);
		result.errorCode = "twitch_probe_audio_create_failed";
		return result;
	}

	resources.videoEncoder = obs_video_encoder_create(ADVANCED_ENCODER_X264, "auto_optimizer_twitch_probe_encoder", encoderSettings, nullptr);
	obs_data_release(encoderSettings);
	if (!resources.videoEncoder) {
		result.errorCode = "twitch_probe_encoder_create_failed";
		return result;
	}

	obs_encoder_set_video(resources.videoEncoder, resources.syntheticVideo);
	obs_data_t *audioEncoderSettings = obs_data_create();
	obs_data_set_int(audioEncoderSettings, "bitrate", 32);
	resources.audioEncoder = obs_audio_encoder_create("ffmpeg_aac", "auto_optimizer_twitch_probe_audio_encoder", audioEncoderSettings, 0, nullptr);
	obs_data_release(audioEncoderSettings);
	if (!resources.audioEncoder) {
		result.errorCode = "twitch_probe_audio_encoder_create_failed";
		return result;
	}
	obs_encoder_set_audio(resources.audioEncoder, resources.syntheticAudio);
	resources.startFeeder();

	resources.output = obs_output_create("rtmp_output", "auto_optimizer_twitch_probe_output", nullptr, nullptr);
	if (!resources.output) {
		result.errorCode = "twitch_probe_output_create_failed";
		return result;
	}
	obs_output_set_reconnect_settings(resources.output, 0, 0);
	obs_output_set_video_encoder(resources.output, resources.videoEncoder);
	obs_output_set_audio_encoder(resources.output, resources.audioEncoder, 0);
	obs_output_set_service(resources.output, resources.service);
	resources.publishOutput();

	if (session->cancelRequested.load()) {
		result.cancelled = true;
		return result;
	}
	if (!obs_output_start(resources.output)) {
		result.errorCode = "twitch_probe_start_failed";
		return result;
	}

	const auto connectDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kProbeConnectTimeoutMs);
	while (!obs_output_active(resources.output) && std::chrono::steady_clock::now() < connectDeadline) {
		if (session->cancelRequested.load()) {
			result.cancelled = true;
			obs_output_force_stop(resources.output);
			return result;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	if (!obs_output_active(resources.output)) {
		result.errorCode = "twitch_probe_connect_failed";
		return result;
	}

	const auto warmupDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kProbeWarmupMs);
	while (std::chrono::steady_clock::now() < warmupDeadline) {
		if (session->cancelRequested.load()) {
			result.cancelled = true;
			obs_output_force_stop(resources.output);
			return result;
		}
		if (!obs_output_active(resources.output)) {
			result.errorCode = "twitch_probe_disconnected";
			return result;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	const uint64_t startBytes = obs_output_get_total_bytes(resources.output);
	const auto sampleStart = std::chrono::steady_clock::now();
	const auto sampleDeadline = sampleStart + std::chrono::milliseconds(kProbeSampleMs);
	while (std::chrono::steady_clock::now() < sampleDeadline) {
		if (session->cancelRequested.load()) {
			result.cancelled = true;
			obs_output_force_stop(resources.output);
			return result;
		}
		if (!obs_output_active(resources.output)) {
			result.errorCode = "twitch_probe_disconnected";
			return result;
		}
		if (obs_output_get_total_bytes(resources.output) - startBytes >= kProbeMaxBytes)
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	const auto sampleEnd = std::chrono::steady_clock::now();
	const uint64_t endBytes = obs_output_get_total_bytes(resources.output);
	const uint64_t elapsedNs = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(sampleEnd - sampleStart).count();
	if (endBytes <= startBytes || elapsedNs == 0) {
		result.errorCode = "twitch_probe_no_data";
		return result;
	}

	// Measure only the sample window. Connection/handshake latency is a separate
	// diagnostic and must never be used as the throughput denominator.
	result.measuredKbps = ((endBytes - startBytes) * 8ULL * 1000000000ULL) / elapsedNs / 1000ULL;
	obs_output_stop(resources.output);
	if (!waitForOutputInactive(resources.output, kProbeStopTimeoutMs)) {
		obs_output_force_stop(resources.output);
		if (!waitForOutputInactive(resources.output, kProbeStopTimeoutMs)) {
			result.errorCode = "twitch_probe_cleanup_timeout";
			return result;
		}
	}

	result.success = result.measuredKbps > 0;
	return result;
}

static void completeCancelled(const std::shared_ptr<Session> &session)
{
	session->probe.streamKey.clear();
	{
		std::lock_guard<std::mutex> lock(session->mutex);
		session->resultJson = serializeResult(*session, "cancelled", {}, "cancelled");
	}
	session->state.store(SessionState::Cancelled);
	pushEvent(session, "cancelled", "cleanup", 100, "cancelled");
}

static void completeFailed(const std::shared_ptr<Session> &session, const char *code)
{
	session->probe.streamKey.clear();
	{
		std::lock_guard<std::mutex> lock(session->mutex);
		session->resultJson = serializeResult(*session, "failed", {}, code);
	}
	session->state.store(SessionState::Failed);
	pushEvent(session, "error", "cleanup", 100, code);
	pushEvent(session, "complete", "cleanup", 100, code);
}

static void runSession(const std::shared_ptr<Session> &session)
{
	pushEvent(session, "phase", "preflight", 0);
	if (session->cancelRequested.load()) {
		completeCancelled(session);
		return;
	}

	pushEvent(session, "phase", "hardware", 15, "scratch_encoder_benchmark");
	const auto hardwareDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kHardwarePhaseTimeoutMs);
	std::vector<LegRequest> preparedLegs;
	std::vector<HardwareAssessment> hardwareAssessments;
	preparedLegs.reserve(session->legs.size());
	hardwareAssessments.reserve(session->legs.size());
	for (size_t index = 0; index < session->legs.size(); index++) {
		preparedLegs.push_back(withOfflinePlatformCaps(session->legs[index]));
		hardwareAssessments.push_back(assessHardware(session, preparedLegs.back(), hardwareDeadline));
		const HardwareAssessment &assessment = hardwareAssessments.back();
		if (assessment.cancelled || session->cancelRequested.load()) {
			completeCancelled(session);
			return;
		}
		const std::string code = assessment.reason.empty() ? "hardware_benchmark_passed" : assessment.reason;
		const double progress = 15.0 + (15.0 * (double)(index + 1) / (double)session->legs.size());
		pushEvent(session, "progress", "hardware", progress, code, preparedLegs.back().legId);
	}

	ProbeResult probeResult;
	bool usedActiveProbe = false;
	if (session->activeProbeEligible) {
		pushEvent(session, "phase", "bandwidth", 30, {}, session->legs[0].legId, "active");
		probeResult = runTwitchProbe(session, session->legs[0]);
		if (probeResult.cancelled || session->cancelRequested.load()) {
			completeCancelled(session);
			return;
		}
		usedActiveProbe = probeResult.success;
		if (!probeResult.success)
			pushEvent(session, "progress", "bandwidth", 65, "twitch_probe_failed_estimate_used", session->legs[0].legId, "estimated");
	} else {
		session->probe.streamKey.clear();
		pushEvent(session, "progress", "bandwidth", 65, session->activeProbeDenialReason.empty() ? "estimate_only" : session->activeProbeDenialReason,
			  {}, "estimated");
	}

	if (session->cancelRequested.load()) {
		completeCancelled(session);
		return;
	}

	pushEvent(session, "phase", "recommendation", 75);
	std::vector<Recommendation> recommendations;
	for (size_t index = 0; index < preparedLegs.size(); index++) {
		const LegRequest &leg = preparedLegs[index];
		const HardwareAssessment &hardware = hardwareAssessments[index];
		Recommendation recommendation;
		recommendation.legId = leg.legId;
		recommendation.display = leg.display;
		recommendation.destinations = leg.destinations;
		recommendation.limits = leg.limits;
		recommendation.value = estimateRecommendation(leg, hardware);
		recommendation.reason = defaultEstimateReason(session->topology, leg);
		if (!hardware.passed || hardware.constrained) {
			recommendation.confidence = hardware.passed ? "medium" : "low";
			recommendation.reason = hardware.reason;
		}

		if (usedActiveProbe && leg.legId == session->probe.legId) {
			recommendation.measurementMode = "active";
			if (hardware.passed && !hardware.constrained) {
				recommendation.confidence = "high";
				recommendation.reason.clear();
			}
			uint64_t safeKbps = probeResult.measuredKbps * 70ULL / 100ULL;
			if (probeResult.platformCapKbps > 0)
				safeKbps = std::min<uint64_t>(safeKbps, (uint64_t)probeResult.platformCapKbps);
			if (leg.limits.maxBitrateKbps > 0)
				safeKbps = std::min<uint64_t>(safeKbps, (uint64_t)leg.limits.maxBitrateKbps);
			// Never turn a low measurement into a higher recommendation merely to
			// satisfy a nominal bitrate floor. Surface the low-confidence result and
			// let Desktop decide how to explain an insufficient connection.
			if (safeKbps < 500) {
				recommendation.confidence = "low";
				recommendation.reason = "insufficient_bandwidth";
			}
			recommendation.value.bitrateKbps = (int)std::clamp<uint64_t>(safeKbps, 1, kProbeMaximumBitrateKbps);
		} else if (session->activeProbeEligible && leg.legId == session->probe.legId && !probeResult.success) {
			recommendation.confidence = "low";
			recommendation.reason = "probe_failed";
		}

		recommendations.push_back(std::move(recommendation));
	}

	{
		std::lock_guard<std::mutex> lock(session->mutex);
		session->resultJson = serializeResult(*session, "complete", recommendations);
	}
	session->state.store(SessionState::Complete);
	pushEvent(session, "result", "recommendation", 95);
	pushEvent(session, "complete", "cleanup", 100);
}

static bool requestCancellation(const std::shared_ptr<Session> &session)
{
	std::unique_lock<std::mutex> lifecycleLock(session->lifecycleMutex);
	const SessionState state = session->state.load();
	if (state == SessionState::Created) {
		session->cancelRequested.store(true);
		completeCancelled(session);
		return true;
	}
	if (state != SessionState::Running)
		return true;

	session->cancelRequested.store(true);
	{
		std::lock_guard<std::mutex> lock(session->probeMutex);
		if (session->activeProbeOutput)
			obs_output_force_stop(session->activeProbeOutput);
	}

	if (session->worker.valid() && session->worker.wait_for(std::chrono::milliseconds(kCancelTimeoutMs)) != std::future_status::ready) {
		pushEvent(session, "error", "cleanup", 100, "cleanup_timeout");
		return false;
	}
	return true;
}

} // namespace

void Register(ipc::server &srv)
{
	auto collection = std::make_shared<ipc::collection>("AutoConfig");

	collection->register_function(std::make_shared<ipc::function>("GetAutoConfigCapabilities", std::vector<ipc::type>{}, GetCapabilities));
	collection->register_function(std::make_shared<ipc::function>("CreateAutoConfigSession", std::vector<ipc::type>{ipc::type::String}, CreateSession));
	collection->register_function(std::make_shared<ipc::function>("StartAutoConfigSession", std::vector<ipc::type>{ipc::type::String}, StartSession));
	collection->register_function(std::make_shared<ipc::function>("QueryAutoConfigSession", std::vector<ipc::type>{ipc::type::String}, QuerySession));
	collection->register_function(std::make_shared<ipc::function>("GetAutoConfigResult", std::vector<ipc::type>{ipc::type::String}, GetResult));
	collection->register_function(std::make_shared<ipc::function>("CancelAutoConfigSession", std::vector<ipc::type>{ipc::type::String}, CancelSession));
	collection->register_function(std::make_shared<ipc::function>("CloseAutoConfigSession", std::vector<ipc::type>{ipc::type::String}, CloseSession));

	srv.register_collection(collection);
}

void GetCapabilities(void *, const int64_t, const std::vector<ipc::value> &, std::vector<ipc::value> &rval)
{
	static const char *capabilities =
		R"({"apiVersion":2,"resultSchemaVersion":1,"previewApplySplit":true,"awaitableCancel":true,"perUploadLegResults":true,"desktopOwnedApply":true,"bandwidthModes":["twitch-standard-active","estimate"]})";
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(capabilities));
}

void CreateSession(void *, const int64_t, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	if (args.size() != 1) {
		returnError(rval, "CreateAutoConfigSession expects request JSON");
		return;
	}
	if (shuttingDown.load()) {
		returnError(rval, "autoconfig_shutting_down");
		return;
	}

	auto session = std::make_shared<Session>();
	session->id = "autoconfig-v2-" + std::to_string(os_gettime_ns()) + "-" + std::to_string(nextSessionId.fetch_add(1));
	std::string error;
	if (!parseRequest(args[0].value_str, *session, error)) {
		returnError(rval, error.c_str());
		return;
	}

	{
		std::lock_guard<std::mutex> lock(sessionsMutex);
		if (shuttingDown.load()) {
			returnError(rval, "autoconfig_shutting_down");
			return;
		}
		if (activeSession) {
			returnError(rval, "autoconfig_session_busy");
			return;
		}
		activeSession = session;
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(session->id));
}

void StartSession(void *, const int64_t, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	if (args.size() != 1) {
		returnError(rval, "StartAutoConfigSession expects sessionId");
		return;
	}
	auto session = findSession(args[0].value_str);
	if (!session) {
		returnError(rval, "autoconfig_session_not_found");
		return;
	}
	std::lock_guard<std::mutex> lifecycleLock(session->lifecycleMutex);
	SessionState expected = SessionState::Created;
	if (!session->state.compare_exchange_strong(expected, SessionState::Running)) {
		returnError(rval, "autoconfig_session_already_started");
		return;
	}
	try {
		session->worker = std::async(std::launch::async, [session]() {
			try {
				runSession(session);
			} catch (...) {
				completeFailed(session, "autoconfig_worker_failed");
			}
		});
	} catch (...) {
		completeFailed(session, "autoconfig_worker_launch_failed");
		returnError(rval, "autoconfig_worker_launch_failed");
		return;
	}
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
}

void QuerySession(void *, const int64_t, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	if (args.size() != 1) {
		returnError(rval, "QueryAutoConfigSession expects sessionId");
		return;
	}
	auto session = findSession(args[0].value_str);
	if (!session) {
		returnError(rval, "autoconfig_session_not_found");
		return;
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	std::lock_guard<std::mutex> lock(session->mutex);
	if (session->events.empty())
		return;

	const SessionEvent &event = session->events.front();
	rval.push_back(ipc::value((uint32_t)kSchemaVersion));
	rval.push_back(ipc::value(session->id));
	rval.push_back(ipc::value(event.sequence));
	rval.push_back(ipc::value(event.type));
	rval.push_back(ipc::value(event.phase));
	rval.push_back(ipc::value(event.progress));
	rval.push_back(ipc::value(event.code));
	rval.push_back(ipc::value(event.legId));
	rval.push_back(ipc::value(event.measurementMode));
	session->events.pop();
}

void GetResult(void *, const int64_t, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	if (args.size() != 1) {
		returnError(rval, "GetAutoConfigResult expects sessionId");
		return;
	}
	auto session = findSession(args[0].value_str);
	if (!session) {
		returnError(rval, "autoconfig_session_not_found");
		return;
	}
	std::lock_guard<std::mutex> lock(session->mutex);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(session->resultJson));
}

void CancelSession(void *, const int64_t, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	if (args.size() != 1) {
		returnError(rval, "CancelAutoConfigSession expects sessionId");
		return;
	}
	auto session = findSession(args[0].value_str);
	if (!session) {
		returnError(rval, "autoconfig_session_not_found");
		return;
	}
	if (!requestCancellation(session)) {
		returnError(rval, "autoconfig_cleanup_timeout");
		return;
	}
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
}

void CloseSession(void *, const int64_t, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	if (args.size() != 1) {
		returnError(rval, "CloseAutoConfigSession expects sessionId");
		return;
	}
	auto session = findSession(args[0].value_str);
	if (!session) {
		// Idempotent close: an already-closed/missing session is success.
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		return;
	}

	if (!requestCancellation(session)) {
		returnError(rval, "autoconfig_cleanup_timeout");
		return;
	}

	{
		std::lock_guard<std::mutex> lifecycleLock(session->lifecycleMutex);
		if (session->worker.valid() && session->worker.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
			returnError(rval, "autoconfig_session_still_running");
			return;
		}
		session->state.store(SessionState::Closed);
	}
	{
		std::lock_guard<std::mutex> lock(sessionsMutex);
		if (activeSession == session)
			activeSession.reset();
	}
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
}

void Shutdown()
{
	shuttingDown.store(true);
	std::shared_ptr<Session> session;
	{
		std::lock_guard<std::mutex> lock(sessionsMutex);
		session = activeSession;
	}
	if (!session)
		return;

	{
		std::unique_lock<std::mutex> lifecycleLock(session->lifecycleMutex);
		const SessionState state = session->state.load();
		if (state == SessionState::Created) {
			session->cancelRequested.store(true);
			completeCancelled(session);
		} else if (state == SessionState::Running) {
			session->cancelRequested.store(true);
			{
				std::lock_guard<std::mutex> lock(session->probeMutex);
				if (session->activeProbeOutput)
					obs_output_force_stop(session->activeProbeOutput);
			}
		}

		// Shutdown is the final safety barrier before libobs teardown. Unlike the
		// public cancellation API, it must not continue while scratch resources
		// are still owned by the worker, even if cleanup takes longer than the
		// normal cancellation deadline.
		if (session->worker.valid())
			session->worker.wait();
		session->probe.streamKey.clear();
		session->state.store(SessionState::Closed);
	}

	{
		std::lock_guard<std::mutex> lock(sessionsMutex);
		if (activeSession == session)
			activeSession.reset();
	}
}

} // namespace autoConfig
