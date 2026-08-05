/******************************************************************************
    Copyright (C) 2026 by Streamlabs

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "nodeobs_autoconfig.h"

#include "autoconfig-probe-policy.hpp"
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
#include <condition_variable>
#include <cstring>
#include <future>
#include <map>
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
constexpr int kYoutubeIngestConfirmationTimeoutMs = 15000;
constexpr int kCancelTimeoutMs = 8000;
constexpr uint64_t kProbeMaxBytes = 25ULL * 1024ULL * 1024ULL;
constexpr uint64_t kYoutubeProbeMaxBytes = 64ULL * 1024ULL * 1024ULL;
constexpr int kProbeMaximumBitrateKbps = 10000;
constexpr int kYoutubeProbeMaximumBitrateKbps = 12000;
constexpr int kYoutubeProbeInitialBitrateKbps = 1000;
constexpr int kYoutubeProbeSettleMs = 500;
constexpr int kYoutubeProbeSampleMs = 5000;
constexpr int kYoutubeProbeSubwindowMs = 1000;
constexpr int kYoutubeProbeTotalTimeoutMs = 100000;
constexpr int kYoutubeProbeBudgetSlackMs = 250;
constexpr int kYoutubeProbeMaximumConfirmationEpisodes = 2;
constexpr int kYoutubeProbeBudgetEstimatePercent = 115;
constexpr float kYoutubeProbeCongestionHigh = 0.20f;
constexpr float kYoutubeProbeCongestionSevere = 0.50f;
constexpr int kTwitchProbeSafeMultiplierPercent = 70;
constexpr int kYoutubeProbeSafeMultiplierPercent = 80;
constexpr int kTwitchProbeAudioBitrateKbps = 32;
constexpr int kYoutubeProbeAudioBitrateKbps = 128;
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
	std::string probeId;
	std::string kind;
	std::string legId;
	std::string serviceName;
	std::string server;
	std::string streamKey;
	std::string provider;
	bool eligible = false;
	std::string denialReason;
};

struct MeasurementProvenance {
	std::string provider;
	std::string method;
	uint64_t measuredKbps = 0;
	uint64_t safeKbps = 0;
	int headroomPercent = 0;
	bool success = false;
	bool ceilingReached = false;
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
	std::vector<MeasurementProvenance> probes;
};

struct SessionEvent {
	uint64_t sequence = 0;
	std::string type;
	std::string phase;
	double progress = 0;
	std::string code;
	std::string legId;
	std::string measurementMode;
	std::string probeId;
	std::string provider;
	uint32_t targetBitrateKbps = 0;
};

struct Session : std::enable_shared_from_this<Session> {
	std::string id;
	std::string topology;
	std::vector<LegRequest> legs;
	std::vector<ProbeRequest> probes;

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
	std::mutex probeConfirmationMutex;
	std::condition_variable probeConfirmationCondition;
	// 0 = pending, 1 = accepted, -1 = rejected. Only YouTube probes use
	// this gate; Twitch retains its bandwidth-test-key behavior.
	std::map<std::string, int> probeConfirmations;
	std::string activeConfirmationProbeId;
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

static bool containsWhitespaceOrControl(const std::string &value)
{
	return std::any_of(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch) || std::iscntrl(ch); });
}

static bool isBoundedTwitchKey(const std::string &key)
{
	return !key.empty() && key.size() <= 4096 && !containsWhitespaceOrControl(key);
}

static bool isBoundedYoutubeKey(const std::string &key)
{
	if (key.empty() || key.size() > 1024 || containsWhitespaceOrControl(key))
		return false;
	return key.find_first_of("/\\?#@:") == std::string::npos;
}

static bool isOfficialYoutubeRtmpsServer(const std::string &server)
{
	if (server.empty() || server.size() > 2048 || containsWhitespaceOrControl(server))
		return false;

	const std::string value = lowerCopy(server);
	constexpr const char *scheme = "rtmps://";
	if (value.compare(0, std::strlen(scheme), scheme) != 0 || value.find_first_of("?#") != std::string::npos)
		return false;

	const size_t authorityStart = std::strlen(scheme);
	const size_t pathStart = value.find('/', authorityStart);
	if (pathStart == std::string::npos || value.substr(pathStart) != "/live2")
		return false;

	const std::string authority = value.substr(authorityStart, pathStart - authorityStart);
	if (authority.empty() || authority.find('@') != std::string::npos)
		return false;

	std::string host = authority;
	const size_t portSeparator = authority.find(':');
	if (portSeparator != std::string::npos) {
		if (authority.find(':', portSeparator + 1) != std::string::npos || authority.substr(portSeparator + 1) != "443")
			return false;
		host = authority.substr(0, portSeparator);
	}
	return host == "a.rtmps.youtube.com";
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

	obs_data_array_t *probes = obs_data_get_array(root, "activeProbes");
	const size_t probeCount = probes ? obs_data_array_count(probes) : 0;
	if (valid && probeCount > 16) {
		error = "invalid_autoconfig_active_probes";
		valid = false;
	}
	std::set<std::string> probeIds;
	for (size_t i = 0; valid && i < probeCount; i++) {
		obs_data_t *item = obs_data_array_item(probes, i);
		ProbeRequest probe;
		probe.probeId = obs_data_get_string(item, "probeId");
		probe.kind = obs_data_get_string(item, "kind");
		probe.legId = obs_data_get_string(item, "legId");
		probe.serviceName = obs_data_get_string(item, "serviceName");
		probe.server = obs_data_get_string(item, "server");
		probe.streamKey = obs_data_get_string(item, "streamKey");
		obs_data_release(item);

		if (probe.probeId.empty() || probe.probeId.size() > 128 || probe.legId.empty() || probe.legId.size() > 128 ||
		    !probeIds.insert(probe.probeId).second) {
			error = "invalid_autoconfig_probe_identity";
			valid = false;
			break;
		}
		if (probe.kind == "twitch-standard")
			probe.provider = "twitch";
		else if (probe.kind == "youtube-unbound")
			probe.provider = "youtube";
		session.probes.push_back(std::move(probe));
	}
	if (probes)
		obs_data_array_release(probes);
	obs_data_release(root);

	if (!valid)
		return false;

	// Active probing is deliberately default-deny. Credentials survive parsing
	// only when the probe, upload leg, topology, and destination all agree.
	const bool multipleDualOutputLegs = session.topology == "dual-output" && session.legs.size() > 1;
	std::map<std::string, size_t> probePairCounts;
	for (const auto &probe : session.probes)
		probePairCounts[probe.legId + "\n" + probe.provider]++;

	for (auto &probe : session.probes) {
		const auto legIt = std::find_if(session.legs.begin(), session.legs.end(), [&](const LegRequest &leg) { return leg.legId == probe.legId; });
		const bool legFound = legIt != session.legs.end();
		const bool destinationFound = legFound && std::any_of(legIt->destinations.begin(), legIt->destinations.end(),
								      [&](const Destination &destination) { return destination.platform == probe.provider; });
		const bool directEligible = session.topology == "direct-single" && session.legs.size() == 1 && legFound && legIt->destinations.size() == 1;
		const bool dualEligible = session.topology == "dual-output" && session.legs.size() == 1 && legFound && legIt->destinations.size() == 1;
		const bool cloudEligible = session.topology == "cloud-multistream" && session.legs.size() == 1 && legFound;
		const bool providerValid = (probe.provider == "twitch" && probe.serviceName == "Twitch" && isOfficialTwitchServer(probe.server) &&
					    isBoundedTwitchKey(probe.streamKey)) ||
					   (probe.provider == "youtube" && probe.serviceName == "YouTube - RTMPS" &&
					    isOfficialYoutubeRtmpsServer(probe.server) && isBoundedYoutubeKey(probe.streamKey));

		probe.eligible = !probe.provider.empty() && destinationFound && (directEligible || dualEligible || cloudEligible) && providerValid &&
				 probePairCounts[probe.legId + "\n" + probe.provider] == 1;
		if (!probe.eligible) {
			probe.denialReason = multipleDualOutputLegs ? "dual_output_multiple_active_legs" : "active_probe_not_eligible";
			probe.streamKey.clear();
			probe.server.clear();
		}
		if (probe.eligible && probe.provider == "youtube")
			session.probeConfirmations.emplace(probe.probeId, 0);
	}

	// A shared cloud upload is active-measured only when every Twitch/YouTube
	// destination has exactly one eligible probe. Otherwise a partial provider
	// sample could recommend a bitrate that is unsafe for the unmeasured peer.
	if (session.topology == "cloud-multistream" && session.legs.size() == 1) {
		const LegRequest &leg = session.legs.front();
		bool completeProviderSet = true;
		for (const auto &destination : leg.destinations) {
			if (destination.platform != "twitch" && destination.platform != "youtube")
				continue;
			const size_t eligibleCount = std::count_if(session.probes.begin(), session.probes.end(), [&](const ProbeRequest &probe) {
				return probe.eligible && probe.legId == leg.legId && probe.provider == destination.platform;
			});
			completeProviderSet = completeProviderSet && eligibleCount == 1;
		}
		if (!completeProviderSet) {
			for (auto &probe : session.probes) {
				if (probe.legId == leg.legId) {
					probe.eligible = false;
					probe.denialReason = "active_probe_set_incomplete";
					probe.streamKey.clear();
					probe.server.clear();
				}
			}
		}
	}

	return true;
}

static void pushEvent(const std::shared_ptr<Session> &session, const char *type, const char *phase, double progress, const std::string &code = {},
		      const std::string &legId = {}, const std::string &measurementMode = {}, const std::string &probeId = {}, const std::string &provider = {},
		      uint32_t targetBitrateKbps = 0)
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
	event.probeId = probeId;
	event.provider = provider;
	event.targetBitrateKbps = targetBitrateKbps;
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
		if (!recommendation.probes.empty()) {
			obs_data_array_t *probes = obs_data_array_create();
			for (const auto &provenance : recommendation.probes) {
				obs_data_t *probe = obs_data_create();
				obs_data_set_string(probe, "provider", provenance.provider.c_str());
				obs_data_set_string(probe, "method", provenance.method.c_str());
				obs_data_set_bool(probe, "success", provenance.success);
				if (probePolicy::hasProbeThroughputMetrics(provenance.success, provenance.measuredKbps)) {
					obs_data_set_int(probe, "measuredKbps", (long long)provenance.measuredKbps);
					obs_data_set_int(probe, "safeKbps", (long long)provenance.safeKbps);
				}
				if (provenance.success || provenance.headroomPercent > 0)
					obs_data_set_int(probe, "headroomPercent", provenance.headroomPercent);
				obs_data_set_bool(probe, "ceilingReached", provenance.ceilingReached);
				obs_data_array_push_back(probes, probe);
				obs_data_release(probe);
			}
			obs_data_set_array(measurement, "probes", probes);
			obs_data_array_release(probes);
		}
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

enum class ProbeStability { Stable, Degraded, Variable, Unstable };

static const char *probeStabilityName(ProbeStability stability)
{
	switch (stability) {
	case ProbeStability::Stable:
		return "stable";
	case ProbeStability::Degraded:
		return "degraded";
	case ProbeStability::Variable:
		return "variable";
	case ProbeStability::Unstable:
		return "unstable";
	}
	return "unknown";
}

struct ProbeResult {
	bool success = false;
	bool cancelled = false;
	uint64_t measuredKbps = 0;
	uint64_t safeKbps = 0;
	int platformCapKbps = 0;
	int headroomPercent = 0;
	bool ceilingReached = false;
	std::string provider;
	std::string method;
	std::string legId;
	std::string errorCode;
	ProbeStability stability = ProbeStability::Stable;
	bool observedThroughputReliable = true;
};

static bool silentAudioCallback(void *, uint64_t startTimestamp, uint64_t, uint64_t *outputTimestamp, uint32_t, struct audio_data_mixes_outputs *)
{
	*outputTimestamp = startTimestamp;
	return true;
}

static float nextAudioNoiseSample(uint32_t &state)
{
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return ((float)(state & 0xffffU) / 32767.5f - 1.0f) * 0.25f;
}

static bool noiseAudioCallback(void *param, uint64_t startTimestamp, uint64_t, uint64_t *outputTimestamp, uint32_t activeMixers,
			       struct audio_data_mixes_outputs *mixes)
{
	*outputTimestamp = startTimestamp;
	if (!param || !mixes)
		return false;

	uint32_t &state = *static_cast<uint32_t *>(param);
	for (size_t canvas = 0; canvas < mixes->outputs.num; canvas++) {
		for (size_t mix = 0; mix < MAX_AUDIO_MIXES; mix++) {
			if (!(activeMixers & (1U << mix)))
				continue;

			for (size_t channel = 0; channel < 2; channel++) {
				float *plane = mixes->outputs.array[canvas].output[mix].data[channel];
				if (!plane)
					continue;
				for (size_t frame = 0; frame < AUDIO_OUTPUT_FRAMES; frame++)
					plane[frame] = nextAudioNoiseSample(state);
			}
		}
	}
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
	uint32_t audioNoiseState = 0xa341316cU;

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

	bool createSyntheticAudio(bool useNoise = false)
	{
		audio_output_info info{};
		info.name = "auto_optimizer_synthetic_audio";
		info.samples_per_sec = 48000;
		info.format = AUDIO_FORMAT_FLOAT_PLANAR;
		info.speakers = SPEAKERS_STEREO;
		info.input_callback = useNoise ? noiseAudioCallback : silentAudioCallback;
		info.input_param = useNoise ? &audioNoiseState : nullptr;
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

static bool waitForProbeInterval(const std::shared_ptr<Session> &session, obs_output_t *output, std::chrono::steady_clock::time_point deadline,
				 ProbeResult &result)
{
	while (std::chrono::steady_clock::now() < deadline) {
		if (session->cancelRequested.load()) {
			result.cancelled = true;
			obs_output_force_stop(output);
			return false;
		}
		if (!obs_output_active(output)) {
			result.errorCode = result.provider + "_probe_disconnected";
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	return true;
}

static bool waitForYoutubeIngestConfirmation(const std::shared_ptr<Session> &session, const ProbeRequest &probe, obs_output_t *output,
					     std::chrono::steady_clock::time_point deadline, double progress, ProbeResult &result)
{
	{
		std::lock_guard<std::mutex> lock(session->probeConfirmationMutex);
		session->activeConfirmationProbeId = probe.probeId;
	}
	pushEvent(session, "progress", "bandwidth", progress, "youtube_probe_waiting_for_ingest", probe.legId, "active", probe.probeId, probe.provider);
	std::unique_lock<std::mutex> lock(session->probeConfirmationMutex);
	const bool signalled = session->probeConfirmationCondition.wait_until(lock, deadline, [&]() {
		const auto found = session->probeConfirmations.find(probe.probeId);
		return session->cancelRequested.load() || found == session->probeConfirmations.end() || found->second != 0;
	});
	if (session->cancelRequested.load()) {
		result.cancelled = true;
		lock.unlock();
		obs_output_force_stop(output);
		return false;
	}
	const auto found = session->probeConfirmations.find(probe.probeId);
	if (!signalled || found == session->probeConfirmations.end() || found->second == 0) {
		result.errorCode = "youtube_probe_ingest_confirmation_timeout";
		return false;
	}
	if (found->second < 0) {
		result.errorCode = "youtube_probe_ingest_not_received";
		return false;
	}
	return true;
}

struct YoutubeProbeSample {
	int targetVideoKbps = 0;
	uint64_t expectedAggregateKbps = 0;
	uint64_t measuredAggregateKbps = 0;
	uint64_t medianSubwindowAggregateKbps = 0;
	uint64_t wholeWindowAggregateKbps = 0;
	uint64_t sampleBytes = 0;
	uint32_t frames = 0;
	uint32_t droppedFrames = 0;
	uint32_t congestionSamples = 0;
	uint32_t congestionHighSamples = 0;
	uint32_t congestionSevereSamples = 0;
	float maximumCongestion = 0.0f;
	float p95Congestion = 0.0f;
	long long elapsedMs = 0;
	probePolicy::YoutubeProbeSampleMetrics metrics;
};

static const char *youtubeSampleClassName(probePolicy::YoutubeProbeSampleClass sampleClass)
{
	switch (sampleClass) {
	case probePolicy::YoutubeProbeSampleClass::Clean:
		return "clean";
	case probePolicy::YoutubeProbeSampleClass::Marginal:
		return "marginal";
	case probePolicy::YoutubeProbeSampleClass::Hard:
		return "hard";
	}
	return "unknown";
}

static const char *youtubeBaselineDecisionName(probePolicy::YoutubeBaselineDecision decision)
{
	switch (decision) {
	case probePolicy::YoutubeBaselineDecision::Clean:
		return "clean";
	case probePolicy::YoutubeBaselineDecision::Impaired:
		return "impaired";
	case probePolicy::YoutubeBaselineDecision::NeedsThird:
		return "needs_third";
	case probePolicy::YoutubeBaselineDecision::Unstable:
		return "unstable";
	}
	return "unknown";
}

static const char *youtubeConfirmationDecisionName(probePolicy::YoutubeConfirmationDecision decision)
{
	switch (decision) {
	case probePolicy::YoutubeConfirmationDecision::CapacityKnee:
		return "capacity_knee";
	case probePolicy::YoutubeConfirmationDecision::TransientRecovered:
		return "transient_recovered";
	case probePolicy::YoutubeConfirmationDecision::PathUnstable:
		return "path_unstable";
	case probePolicy::YoutubeConfirmationDecision::Inconsistent:
		return "inconsistent";
	}
	return "unknown";
}

static uint64_t medianValue(std::vector<uint64_t> values)
{
	if (values.empty())
		return 0;
	std::sort(values.begin(), values.end());
	return values[(values.size() - 1) / 2];
}

static double youtubeProbeStepProgress(double slotStart, double slotEnd, size_t targetIndex, size_t targetCount, double fraction)
{
	const double measurementStart = std::min(slotStart + 2.0, slotEnd);
	if (targetCount == 0 || slotEnd <= measurementStart)
		return slotEnd;
	const double position = ((double)std::min(targetIndex, targetCount - 1) + std::clamp(fraction, 0.0, 0.99)) / (double)targetCount;
	return measurementStart + (slotEnd - measurementStart) * position;
}

static uint64_t estimatedYoutubeSampleBytes(int targetVideoKbps, int durationMs)
{
	if (targetVideoKbps <= 0 || durationMs <= 0)
		return 0;
	const uint64_t aggregateKbps = (uint64_t)targetVideoKbps + kYoutubeProbeAudioBitrateKbps;
	const uint64_t payloadBytes = aggregateKbps * (uint64_t)durationMs / 8ULL;
	return payloadBytes * kYoutubeProbeBudgetEstimatePercent / 100ULL;
}

static uint64_t youtubeProbeBytesUsed(obs_output_t *output, uint64_t budgetStartBytes)
{
	const uint64_t currentBytes = output ? obs_output_get_total_bytes(output) : 0;
	return currentBytes >= budgetStartBytes ? currentBytes - budgetStartBytes : 0;
}

static bool youtubeSampleGroupFits(std::chrono::steady_clock::time_point deadline, uint64_t totalProbeBytes, int activeTarget, const std::vector<int> &targets)
{
	long long requiredMs = kYoutubeProbeBudgetSlackMs;
	uint64_t requiredBytes = 0;
	int targetBeforeSample = activeTarget;
	for (int target : targets) {
		if (target <= 0)
			return false;
		int durationMs = kYoutubeProbeSampleMs;
		if (target != targetBeforeSample) {
			requiredMs += kYoutubeProbeSettleMs;
			durationMs += kYoutubeProbeSettleMs;
		}
		requiredMs += kYoutubeProbeSampleMs;
		requiredBytes += estimatedYoutubeSampleBytes(target, durationMs);
		targetBeforeSample = target;
	}
	return std::chrono::steady_clock::now() + std::chrono::milliseconds(requiredMs) < deadline && totalProbeBytes + requiredBytes <= kYoutubeProbeMaxBytes;
}

static bool runYoutubeProbeSample(const std::shared_ptr<Session> &session, ScratchResources &resources, const ProbeRequest &probe, int requestedTarget,
				  int &activeTarget, std::chrono::steady_clock::time_point youtubeDeadline, uint64_t budgetStartBytes,
				  uint64_t &totalProbeBytes, double progress, const char *eventCode, YoutubeProbeSample &sample, ProbeResult &result)
{
	totalProbeBytes = youtubeProbeBytesUsed(resources.output, budgetStartBytes);
	if (totalProbeBytes >= kYoutubeProbeMaxBytes) {
		result.errorCode = "youtube_probe_byte_budget_exhausted";
		return false;
	}

	int target = requestedTarget;
	const bool targetChanged = target != activeTarget;
	if (targetChanged) {
		obs_data_t *updatedSettings = obs_data_create();
		obs_data_set_int(updatedSettings, "bitrate", target);
		obs_data_set_string(updatedSettings, "rate_control", "CBR");
		obs_data_set_string(updatedSettings, "preset", "veryfast");
		obs_data_set_int(updatedSettings, "keyint_sec", 2);
		obs_service_apply_encoder_settings(resources.service, updatedSettings, nullptr);
		target = (int)obs_data_get_int(updatedSettings, "bitrate");
		obs_encoder_update(resources.videoEncoder, updatedSettings);
		obs_data_release(updatedSettings);
		activeTarget = target;
	}

	if (target <= 0) {
		result.errorCode = "youtube_probe_invalid_applied_target";
		return false;
	}

	pushEvent(session, "progress", "bandwidth", progress, eventCode, probe.legId, "active", probe.probeId, probe.provider, (uint32_t)target);
	blog(LOG_INFO,
	     "[Auto Optimizer][YouTube Probe] Starting sample: purpose=%s, requested_video_target=%d Kbps, "
	     "applied_video_target=%d Kbps",
	     eventCode, requestedTarget, target);

	if (targetChanged) {
		const auto settleDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kYoutubeProbeSettleMs);
		if (settleDeadline > youtubeDeadline) {
			result.errorCode = "youtube_probe_sample_deadline_exhausted";
			return false;
		}
		if (!waitForProbeInterval(session, resources.output, settleDeadline, result)) {
			blog(LOG_WARNING, "[Auto Optimizer][YouTube Probe] Sample %d Kbps stopped during settle: %s", target,
			     result.errorCode.empty() ? (result.cancelled ? "cancelled" : "unknown") : result.errorCode.c_str());
			return false;
		}
	}
	totalProbeBytes = youtubeProbeBytesUsed(resources.output, budgetStartBytes);
	if (totalProbeBytes >= kYoutubeProbeMaxBytes) {
		result.errorCode = "youtube_probe_byte_budget_exhausted";
		return false;
	}

	const uint64_t startBytes = obs_output_get_total_bytes(resources.output);
	const uint32_t startDropped = obs_output_get_frames_dropped(resources.output);
	const uint32_t startFrames = obs_output_get_total_frames(resources.output);
	const auto sampleStart = std::chrono::steady_clock::now();
	const auto sampleDeadline = sampleStart + std::chrono::milliseconds(kYoutubeProbeSampleMs);
	if (sampleDeadline > youtubeDeadline) {
		result.errorCode = "youtube_probe_sample_deadline_exhausted";
		return false;
	}
	auto subwindowStart = sampleStart;
	auto nextSubwindow = sampleStart + std::chrono::milliseconds(kYoutubeProbeSubwindowMs);
	uint64_t subwindowStartBytes = startBytes;
	std::vector<uint64_t> subwindowThroughputs;
	std::vector<float> congestionValues;

	while (std::chrono::steady_clock::now() < sampleDeadline) {
		const float congestion = obs_output_get_congestion(resources.output);
		congestionValues.push_back(congestion);
		const auto tickDeadline = std::min(std::chrono::steady_clock::now() + std::chrono::milliseconds(50), sampleDeadline);
		if (!waitForProbeInterval(session, resources.output, tickDeadline, result)) {
			blog(LOG_WARNING, "[Auto Optimizer][YouTube Probe] Sample %d Kbps stopped during measurement: %s", target,
			     result.errorCode.empty() ? (result.cancelled ? "cancelled" : "unknown") : result.errorCode.c_str());
			return false;
		}
		totalProbeBytes = youtubeProbeBytesUsed(resources.output, budgetStartBytes);
		if (totalProbeBytes > kYoutubeProbeMaxBytes) {
			result.errorCode = "youtube_probe_byte_budget_exhausted";
			blog(LOG_WARNING, "[Auto Optimizer][YouTube Probe] Sample %d Kbps exceeded byte budget: used=%llu, budget=%llu", target,
			     (unsigned long long)totalProbeBytes, (unsigned long long)kYoutubeProbeMaxBytes);
			return false;
		}

		const auto now = std::chrono::steady_clock::now();
		if (now >= nextSubwindow || now >= sampleDeadline) {
			const uint64_t subwindowEndBytes = obs_output_get_total_bytes(resources.output);
			const uint64_t elapsedNs = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(now - subwindowStart).count();
			if (elapsedNs > 0) {
				const uint64_t bytes = subwindowEndBytes >= subwindowStartBytes ? subwindowEndBytes - subwindowStartBytes : 0;
				subwindowThroughputs.push_back(bytes * 8ULL * 1000000000ULL / elapsedNs / 1000ULL);
			}
			subwindowStart = now;
			subwindowStartBytes = subwindowEndBytes;
			nextSubwindow += std::chrono::milliseconds(kYoutubeProbeSubwindowMs);
		}
	}

	const auto sampleEnd = std::chrono::steady_clock::now();
	const uint64_t endBytes = obs_output_get_total_bytes(resources.output);
	const uint32_t endDropped = obs_output_get_frames_dropped(resources.output);
	const uint32_t endFrames = obs_output_get_total_frames(resources.output);
	const uint64_t elapsedNs = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(sampleEnd - sampleStart).count();
	if (endBytes <= startBytes || elapsedNs == 0 || subwindowThroughputs.empty()) {
		result.errorCode = "youtube_probe_no_data";
		blog(LOG_WARNING,
		     "[Auto Optimizer][YouTube Probe] Sample %d Kbps produced no measurable data: start_bytes=%llu, end_bytes=%llu, "
		     "elapsed_ns=%llu, subwindows=%llu",
		     target, (unsigned long long)startBytes, (unsigned long long)endBytes, (unsigned long long)elapsedNs,
		     (unsigned long long)subwindowThroughputs.size());
		return false;
	}

	sample.targetVideoKbps = target;
	sample.expectedAggregateKbps = (uint64_t)target + kYoutubeProbeAudioBitrateKbps;
	sample.medianSubwindowAggregateKbps = medianValue(subwindowThroughputs);
	sample.wholeWindowAggregateKbps = (endBytes - startBytes) * 8ULL * 1000000000ULL / elapsedNs / 1000ULL;
	// The median filters isolated scheduler/network spikes, while the complete
	// window catches multi-second stalls. Use the conservative value for both
	// classification and recommendation.
	sample.measuredAggregateKbps = std::min(sample.medianSubwindowAggregateKbps, sample.wholeWindowAggregateKbps);
	sample.sampleBytes = endBytes - startBytes;
	sample.frames = endFrames >= startFrames ? endFrames - startFrames : 0;
	sample.droppedFrames = endDropped >= startDropped ? endDropped - startDropped : 0;
	sample.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(sampleEnd - sampleStart).count();
	sample.congestionSamples = (uint32_t)congestionValues.size();
	for (float congestion : congestionValues) {
		sample.maximumCongestion = std::max(sample.maximumCongestion, congestion);
		if (congestion >= kYoutubeProbeCongestionHigh)
			sample.congestionHighSamples++;
		if (congestion >= kYoutubeProbeCongestionSevere)
			sample.congestionSevereSamples++;
	}
	std::sort(congestionValues.begin(), congestionValues.end());
	if (!congestionValues.empty()) {
		const size_t percentileIndex = std::min(congestionValues.size() - 1, (congestionValues.size() * 95 + 99) / 100 - 1);
		sample.p95Congestion = congestionValues[percentileIndex];
	}
	sample.metrics = probePolicy::makeYoutubeProbeSampleMetrics((uint32_t)std::min<uint64_t>(sample.measuredAggregateKbps, UINT32_MAX),
								    (uint32_t)std::min<uint64_t>(sample.expectedAggregateKbps, UINT32_MAX),
								    sample.droppedFrames, sample.frames, sample.congestionHighSamples,
								    sample.congestionSevereSamples, sample.congestionSamples);
	totalProbeBytes = youtubeProbeBytesUsed(resources.output, budgetStartBytes);

	const probePolicy::YoutubeProbeSampleClass sampleClass = probePolicy::classifyYoutubeProbeSample(sample.metrics);
	blog(LOG_INFO,
	     "[Auto Optimizer][YouTube Probe] Sample result: purpose=%s, video_target=%d Kbps, expected_aggregate=%llu Kbps, "
	     "representative_aggregate=%llu Kbps, median_subwindow_aggregate=%llu Kbps, whole_window_aggregate=%llu Kbps, "
	     "elapsed=%lld ms, sample_bytes=%llu, frames=%u, "
	     "dropped=%u, max_congestion=%.3f, p95_congestion=%.3f, congestion_high=%u/%u, congestion_severe=%u/%u, "
	     "throughput_ratio=%.2f%%, drop_ratio=%.2f%%, congestion_high_ratio=%.2f%%, congestion_severe_ratio=%.2f%%, class=%s",
	     eventCode, target, (unsigned long long)sample.expectedAggregateKbps, (unsigned long long)sample.measuredAggregateKbps,
	     (unsigned long long)sample.medianSubwindowAggregateKbps, (unsigned long long)sample.wholeWindowAggregateKbps, sample.elapsedMs,
	     (unsigned long long)sample.sampleBytes, (unsigned int)sample.frames, (unsigned int)sample.droppedFrames, (double)sample.maximumCongestion,
	     (double)sample.p95Congestion, (unsigned int)sample.congestionHighSamples, (unsigned int)sample.congestionSamples,
	     (unsigned int)sample.congestionSevereSamples, (unsigned int)sample.congestionSamples, (double)sample.metrics.throughputBasisPoints / 100.0,
	     (double)sample.metrics.dropBasisPoints / 100.0, (double)sample.metrics.congestionHighBasisPoints / 100.0,
	     (double)sample.metrics.congestionSevereBasisPoints / 100.0, youtubeSampleClassName(sampleClass));
	return true;
}

static ProbeResult runRtmpProbe(const std::shared_ptr<Session> &session, ProbeRequest &probe, const LegRequest &leg, double slotStartProgress,
				double slotEndProgress)
{
	ProbeResult result;
	result.provider = probe.provider;
	result.legId = probe.legId;
	result.method = probe.provider == "youtube" ? "youtube-unbound-ramp" : "twitch-bandwidth-test";
	result.headroomPercent = 100 - (probe.provider == "youtube" ? kYoutubeProbeSafeMultiplierPercent : kTwitchProbeSafeMultiplierPercent);
	ScratchResources resources(*session);

	obs_data_t *serviceSettings = obs_data_create();
	obs_data_set_string(serviceSettings, "service", probe.provider == "youtube" ? "YouTube - RTMPS" : "Twitch");
	obs_data_set_string(serviceSettings, "server", probe.server.c_str());
	std::string serviceKey = probe.provider == "youtube" ? probe.streamKey : normalizeTwitchBandwidthKey(probe.streamKey);
	obs_data_set_string(serviceSettings, "key", serviceKey.c_str());
	resources.service = obs_service_create_private("rtmp_common", "auto_optimizer_probe_service", serviceSettings);
	obs_data_release(serviceSettings);

	// Drop the only application-owned copies as soon as the disposable service
	// has consumed them. Neither value is emitted, serialized, or logged.
	probe.streamKey.clear();
	probe.server.clear();
	serviceKey.clear();
	if (!resources.service) {
		result.errorCode = result.provider + "_probe_service_create_failed";
		return result;
	}

	const int maximumBitrate = probe.provider == "youtube" ? kYoutubeProbeMaximumBitrateKbps : kProbeMaximumBitrateKbps;
	const int requested = probe.provider == "youtube" ? kYoutubeProbeInitialBitrateKbps
							  : std::clamp(std::max(leg.current.bitrateKbps, 6000), 500, maximumBitrate);
	obs_data_t *platformProbe = obs_data_create();
	obs_data_set_int(platformProbe, "bitrate", maximumBitrate);
	obs_service_apply_encoder_settings(resources.service, platformProbe, nullptr);
	const int platformReturned = (int)obs_data_get_int(platformProbe, "bitrate");
	if (platformReturned > 0 && platformReturned < maximumBitrate)
		result.platformCapKbps = platformReturned;
	obs_data_release(platformProbe);

	int initialBitrate = requested;
	if (result.platformCapKbps > 0)
		initialBitrate = std::min(initialBitrate, result.platformCapKbps);
	if (leg.limits.maxBitrateKbps > 0)
		initialBitrate = std::min(initialBitrate, leg.limits.maxBitrateKbps);
	obs_data_t *encoderSettings = obs_data_create();
	obs_data_set_int(encoderSettings, "bitrate", initialBitrate);
	obs_data_set_string(encoderSettings, "rate_control", "CBR");
	obs_data_set_string(encoderSettings, "preset", "veryfast");
	obs_data_set_int(encoderSettings, "keyint_sec", 2);
	obs_service_apply_encoder_settings(resources.service, encoderSettings, nullptr);
	initialBitrate = (int)obs_data_get_int(encoderSettings, "bitrate");

	const uint32_t width = probe.provider == "youtube" ? 640 : 128;
	const uint32_t height = probe.provider == "youtube" ? 360 : 128;
	if (!resources.createSyntheticVideo(width, height, 30, 1)) {
		obs_data_release(encoderSettings);
		result.errorCode = result.provider + "_probe_video_create_failed";
		return result;
	}
	if (!resources.createSyntheticAudio(probe.provider == "youtube")) {
		obs_data_release(encoderSettings);
		result.errorCode = result.provider + "_probe_audio_create_failed";
		return result;
	}

	resources.videoEncoder = obs_video_encoder_create(ADVANCED_ENCODER_X264, "auto_optimizer_probe_encoder", encoderSettings, nullptr);
	obs_data_release(encoderSettings);
	if (!resources.videoEncoder) {
		result.errorCode = result.provider + "_probe_encoder_create_failed";
		return result;
	}
	obs_encoder_set_video(resources.videoEncoder, resources.syntheticVideo);

	obs_data_t *audioEncoderSettings = obs_data_create();
	obs_data_set_int(audioEncoderSettings, "bitrate", probe.provider == "youtube" ? kYoutubeProbeAudioBitrateKbps : kTwitchProbeAudioBitrateKbps);
	resources.audioEncoder = obs_audio_encoder_create("ffmpeg_aac", "auto_optimizer_probe_audio_encoder", audioEncoderSettings, 0, nullptr);
	obs_data_release(audioEncoderSettings);
	if (!resources.audioEncoder) {
		result.errorCode = result.provider + "_probe_audio_encoder_create_failed";
		return result;
	}
	obs_encoder_set_audio(resources.audioEncoder, resources.syntheticAudio);
	resources.startFeeder();

	resources.output = obs_output_create("rtmp_output", "auto_optimizer_probe_output", nullptr, nullptr);
	if (!resources.output) {
		result.errorCode = result.provider + "_probe_output_create_failed";
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
	const auto probeStarted = std::chrono::steady_clock::now();
	const auto youtubeDeadline = probeStarted + std::chrono::milliseconds(kYoutubeProbeTotalTimeoutMs);
	if (!obs_output_start(resources.output)) {
		result.errorCode = result.provider + "_probe_start_failed";
		return result;
	}

	const auto connectDeadline = std::min(probeStarted + std::chrono::milliseconds(kProbeConnectTimeoutMs), youtubeDeadline);
	while (!obs_output_active(resources.output) && std::chrono::steady_clock::now() < connectDeadline) {
		if (session->cancelRequested.load()) {
			result.cancelled = true;
			obs_output_force_stop(resources.output);
			return result;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	if (!obs_output_active(resources.output)) {
		result.errorCode = result.provider + "_probe_connect_failed";
		return result;
	}
	const uint64_t probeBudgetStartBytes = obs_output_get_total_bytes(resources.output);

	if (probe.provider == "youtube") {
		blog(LOG_INFO, "[Auto Optimizer][YouTube Probe] RTMP output is active; waiting for ingest confirmation");
		const auto confirmationDeadline =
			std::min(std::chrono::steady_clock::now() + std::chrono::milliseconds(kYoutubeIngestConfirmationTimeoutMs), youtubeDeadline);
		const double confirmationProgress = std::min(slotStartProgress + 2.0, slotEndProgress);
		if (!waitForYoutubeIngestConfirmation(session, probe, resources.output, confirmationDeadline, confirmationProgress, result)) {
			blog(LOG_WARNING, "[Auto Optimizer][YouTube Probe] Ingest confirmation failed: reason=%s, output_active=%s",
			     result.errorCode.empty() ? (result.cancelled ? "cancelled" : "unknown") : result.errorCode.c_str(),
			     obs_output_active(resources.output) ? "true" : "false");
			return result;
		}
		blog(LOG_INFO, "[Auto Optimizer][YouTube Probe] Ingest confirmed; starting bandwidth ladder");
	}

	if (probe.provider == "twitch") {
		pushEvent(session, "progress", "bandwidth", probePolicy::probeSubstepProgress(slotStartProgress, slotEndProgress, 0, 1),
			  "twitch_probe_measuring", probe.legId, "active", probe.probeId, probe.provider, (uint32_t)std::max(0, initialBitrate));
		if (!waitForProbeInterval(session, resources.output, std::chrono::steady_clock::now() + std::chrono::milliseconds(kProbeWarmupMs), result))
			return result;
		const uint64_t startBytes = obs_output_get_total_bytes(resources.output);
		const auto sampleStart = std::chrono::steady_clock::now();
		const auto sampleDeadline = sampleStart + std::chrono::milliseconds(kProbeSampleMs);
		while (std::chrono::steady_clock::now() < sampleDeadline && obs_output_get_total_bytes(resources.output) - startBytes < kProbeMaxBytes) {
			if (!waitForProbeInterval(session, resources.output, std::chrono::steady_clock::now() + std::chrono::milliseconds(50), result))
				return result;
		}
		const auto sampleEnd = std::chrono::steady_clock::now();
		const uint64_t endBytes = obs_output_get_total_bytes(resources.output);
		const uint64_t elapsedNs = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(sampleEnd - sampleStart).count();
		if (endBytes <= startBytes || elapsedNs == 0) {
			result.errorCode = "twitch_probe_no_data";
			return result;
		}
		result.measuredKbps = ((endBytes - startBytes) * 8ULL * 1000000000ULL) / elapsedNs / 1000ULL;
		result.safeKbps = probePolicy::safeVideoKbps(result.measuredKbps, kTwitchProbeSafeMultiplierPercent, kTwitchProbeAudioBitrateKbps);
		if (result.platformCapKbps > 0)
			result.safeKbps = std::min<uint64_t>(result.safeKbps, (uint64_t)result.platformCapKbps);
		result.success = result.measuredKbps > 0;
	} else {
		const int ladder[] = {1000, 2000, 4000, 6000, 8000, 10000, 12000};
		uint64_t totalProbeBytes = youtubeProbeBytesUsed(resources.output, probeBudgetStartBytes);
		probePolicy::YoutubeRampEvidence rampEvidence;
		const int effectiveCeilingKbps =
			probePolicy::effectiveProbeCeilingKbps(kYoutubeProbeMaximumBitrateKbps, result.platformCapKbps, leg.limits.maxBitrateKbps);
		std::vector<std::pair<int, int>> plannedTargets;
		int lastPlannedTarget = 0;
		for (int ladderTarget : ladder) {
			const int plannedTarget = std::min(ladderTarget, effectiveCeilingKbps);
			if (plannedTarget > lastPlannedTarget) {
				plannedTargets.emplace_back(ladderTarget, plannedTarget);
				lastPlannedTarget = plannedTarget;
			}
		}
		std::string terminationReason = "ladder_exhausted";
		int activeTarget = initialBitrate;
		size_t confirmationEpisodes = 0;
		bool measurementConclusive = false;
		const auto rampStarted = std::chrono::steady_clock::now();
		const auto probeElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(rampStarted - probeStarted).count();
		const auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(youtubeDeadline - rampStarted).count();
		blog(LOG_INFO,
		     "[Auto Optimizer][YouTube Probe] Adaptive ladder configuration: effective_video_ceiling=%d Kbps, settle=%d ms, "
		     "sample=%d ms, subwindow=%d ms, total_timeout=%d ms, probe_elapsed=%lld ms, remaining=%lld ms, byte_budget=%llu, "
		     "maximum_confirmation_episodes=%d",
		     effectiveCeilingKbps, kYoutubeProbeSettleMs, kYoutubeProbeSampleMs, kYoutubeProbeSubwindowMs, kYoutubeProbeTotalTimeoutMs,
		     (long long)probeElapsedMs, (long long)remainingMs, (unsigned long long)kYoutubeProbeMaxBytes, kYoutubeProbeMaximumConfirmationEpisodes);

		if (plannedTargets.empty()) {
			terminationReason = "no_planned_targets";
			result.errorCode = "youtube_probe_no_passing_step";
			result.observedThroughputReliable = false;
		} else {
			const int baselineTarget = plannedTargets.front().second;
			if (!youtubeSampleGroupFits(youtubeDeadline, totalProbeBytes, activeTarget, {baselineTarget, baselineTarget})) {
				terminationReason = "budget_before_baseline";
				result.errorCode = "youtube_probe_baseline_budget_exhausted";
				result.observedThroughputReliable = false;
			} else {
				YoutubeProbeSample baselineFirst;
				YoutubeProbeSample baselineSecond;
				if (!runYoutubeProbeSample(session, resources, probe, baselineTarget, activeTarget, youtubeDeadline, probeBudgetStartBytes,
							   totalProbeBytes,
							   youtubeProbeStepProgress(slotStartProgress, slotEndProgress, 0, plannedTargets.size(), 0.15),
							   "youtube_probe_baseline", baselineFirst, result))
					return result;
				if (!runYoutubeProbeSample(session, resources, probe, baselineTarget, activeTarget, youtubeDeadline, probeBudgetStartBytes,
							   totalProbeBytes,
							   youtubeProbeStepProgress(slotStartProgress, slotEndProgress, 0, plannedTargets.size(), 0.40),
							   "youtube_probe_baseline", baselineSecond, result))
					return result;

				probePolicy::YoutubeBaselineAssessment baseline =
					probePolicy::assessYoutubeBaseline(baselineFirst.metrics, baselineSecond.metrics);
				std::vector<uint64_t> baselineThroughputs{baselineFirst.measuredAggregateKbps, baselineSecond.measuredAggregateKbps};
				bool usedThirdBaselineSample = false;
				if (baseline.decision == probePolicy::YoutubeBaselineDecision::NeedsThird) {
					totalProbeBytes = youtubeProbeBytesUsed(resources.output, probeBudgetStartBytes);
					if (!youtubeSampleGroupFits(youtubeDeadline, totalProbeBytes, activeTarget, {baselineTarget})) {
						terminationReason = "budget_before_third_baseline_sample";
						result.errorCode = "youtube_probe_baseline_confirmation_budget_exhausted";
						result.observedThroughputReliable = false;
					} else {
						YoutubeProbeSample baselineThird;
						if (!runYoutubeProbeSample(session, resources, probe, baselineTarget, activeTarget, youtubeDeadline,
									   probeBudgetStartBytes, totalProbeBytes,
									   youtubeProbeStepProgress(slotStartProgress, slotEndProgress, 0,
												    plannedTargets.size(), 0.65),
									   "youtube_probe_baseline", baselineThird, result))
							return result;
						baselineThroughputs.push_back(baselineThird.measuredAggregateKbps);
						baseline = probePolicy::resolveYoutubeBaseline(baselineFirst.metrics, baselineSecond.metrics,
											       baselineThird.metrics);
						usedThirdBaselineSample = true;
					}
				}

				if (result.errorCode.empty()) {
					const uint64_t baselineBasis = baselineThroughputs.size() == 2
									       ? std::min(baselineThroughputs[0], baselineThroughputs[1])
									       : medianValue(baselineThroughputs);
					result.measuredKbps = baselineBasis;
					blog(baseline.decision == probePolicy::YoutubeBaselineDecision::Unstable ? LOG_WARNING : LOG_INFO,
					     "[Auto Optimizer][YouTube Probe] Baseline decision: decision=%s, samples=%llu, "
					     "recommendation_basis=%llu Kbps, throughput_reference=%.2f%%, drop_reference=%.2f%%, "
					     "congestion_high_reference=%.2f%%, congestion_severe_reference=%.2f%%",
					     youtubeBaselineDecisionName(baseline.decision), (unsigned long long)baselineThroughputs.size(),
					     (unsigned long long)baselineBasis, (double)baseline.reference.throughputBasisPoints / 100.0,
					     (double)baseline.reference.dropBasisPoints / 100.0, (double)baseline.reference.congestionHighBasisPoints / 100.0,
					     (double)baseline.reference.congestionSevereBasisPoints / 100.0);

					if (baseline.decision == probePolicy::YoutubeBaselineDecision::Unstable ||
					    baseline.decision == probePolicy::YoutubeBaselineDecision::NeedsThird) {
						terminationReason = "unstable_baseline";
						result.stability = ProbeStability::Unstable;
						result.observedThroughputReliable = false;
						result.errorCode = "youtube_probe_unstable_connection";
					} else {
						if (baseline.decision == probePolicy::YoutubeBaselineDecision::Impaired)
							result.stability = ProbeStability::Degraded;
						else if (usedThirdBaselineSample)
							result.stability = ProbeStability::Variable;

						rampEvidence.observe(baselineBasis, true);
						YoutubeProbeSample lastAccepted = baselineFirst;
						lastAccepted.targetVideoKbps = baselineTarget;
						lastAccepted.expectedAggregateKbps = (uint64_t)baselineTarget + kYoutubeProbeAudioBitrateKbps;
						lastAccepted.measuredAggregateKbps = baselineBasis;
						lastAccepted.metrics = baseline.reference;
						result.ceilingReached = probePolicy::reachedEffectiveProbeCeiling(baselineTarget, effectiveCeilingKbps);
						if (result.ceilingReached) {
							terminationReason = "effective_ceiling_reached_at_baseline";
							measurementConclusive = true;
						}

						for (size_t targetIndex = 1; targetIndex < plannedTargets.size() && !result.ceilingReached; targetIndex++) {
							const auto &[ladderTarget, plannedTarget] = plannedTargets[targetIndex];
							totalProbeBytes = youtubeProbeBytesUsed(resources.output, probeBudgetStartBytes);
							if (!youtubeSampleGroupFits(youtubeDeadline, totalProbeBytes, activeTarget, {plannedTarget})) {
								terminationReason = "budget_before_next_rung";
								break;
							}

							YoutubeProbeSample highFirst;
							if (!runYoutubeProbeSample(session, resources, probe, plannedTarget, activeTarget, youtubeDeadline,
										   probeBudgetStartBytes, totalProbeBytes,
										   youtubeProbeStepProgress(slotStartProgress, slotEndProgress, targetIndex,
													    plannedTargets.size(), 0.20),
										   "youtube_probe_measuring", highFirst, result))
								return result;

							if (probePolicy::youtubeSampleAccepted(highFirst.metrics, baseline)) {
								rampEvidence.observe(highFirst.measuredAggregateKbps, true);
								result.measuredKbps = rampEvidence.recommendationBasisKbps;
								lastAccepted = highFirst;
								result.ceilingReached = probePolicy::reachedEffectiveProbeCeiling(highFirst.targetVideoKbps,
																  effectiveCeilingKbps);
								if (result.ceilingReached) {
									terminationReason = "effective_ceiling_reached";
									measurementConclusive = true;
								}
								if (highFirst.targetVideoKbps < ladderTarget) {
									terminationReason = "provider_or_request_cap_reached";
									measurementConclusive = true;
									break;
								}
								continue;
							}

							confirmationEpisodes++;
							if (confirmationEpisodes > kYoutubeProbeMaximumConfirmationEpisodes) {
								terminationReason = "confirmation_episode_limit_exceeded";
								result.stability = ProbeStability::Unstable;
								result.observedThroughputReliable = false;
								result.errorCode = "youtube_probe_unstable_connection";
								break;
							}
							totalProbeBytes = youtubeProbeBytesUsed(resources.output, probeBudgetStartBytes);
							if (!youtubeSampleGroupFits(youtubeDeadline, totalProbeBytes, activeTarget,
										    {lastAccepted.targetVideoKbps, plannedTarget})) {
								terminationReason = "confirmation_budget_exhausted_after_unconfirmed_failure";
								if (result.stability == ProbeStability::Stable)
									result.stability = ProbeStability::Variable;
								break;
							}

							YoutubeProbeSample lowControl;
							YoutubeProbeSample highRetry;
							if (!runYoutubeProbeSample(session, resources, probe, lastAccepted.targetVideoKbps, activeTarget,
										   youtubeDeadline, probeBudgetStartBytes, totalProbeBytes,
										   youtubeProbeStepProgress(slotStartProgress, slotEndProgress, targetIndex,
													    plannedTargets.size(), 0.45),
										   "youtube_probe_confirming_stability", lowControl, result))
								return result;
							if (!runYoutubeProbeSample(session, resources, probe, plannedTarget, activeTarget, youtubeDeadline,
										   probeBudgetStartBytes, totalProbeBytes,
										   youtubeProbeStepProgress(slotStartProgress, slotEndProgress, targetIndex,
													    plannedTargets.size(), 0.70),
										   "youtube_probe_retrying", highRetry, result))
								return result;

							const bool lowRecovered =
								probePolicy::youtubeLowControlRecovered(lowControl.metrics, lastAccepted.metrics, baseline);
							const bool highAccepted = probePolicy::youtubeSampleAccepted(highRetry.metrics, baseline);
							const probePolicy::YoutubeConfirmationDecision confirmation =
								probePolicy::decideYoutubeConfirmation(lowRecovered, highAccepted);
							blog(LOG_INFO,
							     "[Auto Optimizer][YouTube Probe] Confirmation decision: episode=%llu, high_target=%d Kbps, "
							     "low_recovered=%s, high_retry_accepted=%s, decision=%s",
							     (unsigned long long)confirmationEpisodes, plannedTarget, lowRecovered ? "true" : "false",
							     highAccepted ? "true" : "false", youtubeConfirmationDecisionName(confirmation));

							if (confirmation == probePolicy::YoutubeConfirmationDecision::CapacityKnee) {
								const uint64_t confirmedBasis =
									std::min(lastAccepted.measuredAggregateKbps, lowControl.measuredAggregateKbps);
								rampEvidence.observe(confirmedBasis, true);
								result.measuredKbps = rampEvidence.recommendationBasisKbps;
								terminationReason = "confirmed_capacity_knee";
								measurementConclusive = true;
								break;
							}
							if (confirmation == probePolicy::YoutubeConfirmationDecision::TransientRecovered) {
								if (result.stability == ProbeStability::Stable)
									result.stability = ProbeStability::Variable;
								rampEvidence.observe(highRetry.measuredAggregateKbps, true);
								result.measuredKbps = rampEvidence.recommendationBasisKbps;
								lastAccepted = highRetry;
								result.ceilingReached = probePolicy::reachedEffectiveProbeCeiling(highRetry.targetVideoKbps,
																  effectiveCeilingKbps);
								if (result.ceilingReached) {
									terminationReason = "effective_ceiling_reached_after_retry";
									measurementConclusive = true;
								}
								if (highRetry.targetVideoKbps < ladderTarget) {
									terminationReason = "provider_or_request_cap_reached_after_retry";
									measurementConclusive = true;
									break;
								}
								continue;
							}

							terminationReason = confirmation == probePolicy::YoutubeConfirmationDecision::PathUnstable
										    ? "path_unstable"
										    : "inconsistent_confirmation";
							result.stability = ProbeStability::Unstable;
							result.observedThroughputReliable = false;
							result.errorCode = "youtube_probe_unstable_connection";
							break;
						}
					}
				}
			}
		}

		if (result.stability != ProbeStability::Unstable && rampEvidence.passedStep && !measurementConclusive) {
			result.observedThroughputReliable = false;
			if (result.errorCode.empty())
				result.errorCode = "youtube_probe_inconclusive";
		}

		uint64_t uncappedSafeKbps = 0;
		if (result.stability != ProbeStability::Unstable && rampEvidence.passedStep && measurementConclusive) {
			uncappedSafeKbps = rampEvidence.safeVideoKbps(kYoutubeProbeSafeMultiplierPercent, kYoutubeProbeAudioBitrateKbps);
			result.safeKbps = uncappedSafeKbps;
			if (result.platformCapKbps > 0)
				result.safeKbps = std::min<uint64_t>(result.safeKbps, (uint64_t)result.platformCapKbps);
			if (leg.limits.maxBitrateKbps > 0)
				result.safeKbps = std::min<uint64_t>(result.safeKbps, (uint64_t)leg.limits.maxBitrateKbps);
			result.success = result.safeKbps > 0;
		} else {
			result.safeKbps = 0;
			result.success = false;
			if (result.errorCode.empty())
				result.errorCode = "youtube_probe_no_passing_step";
		}
		blog(result.success ? LOG_INFO : LOG_WARNING,
		     "[Auto Optimizer][YouTube Probe] Adaptive ladder summary: passed_step=%s, stability=%s, "
		     "observed_throughput_reliable=%s, recommendation_basis=%llu Kbps, safe_multiplier=%d%%, audio_reserve=%d Kbps, "
		     "uncapped_safe_video=%llu Kbps, final_safe_video=%llu Kbps, platform_cap=%d Kbps, request_cap=%d Kbps, "
		     "total_output_bytes=%llu, ceiling_reached=%s, conclusive=%s, confirmation_episodes=%llu, termination=%s, error=%s",
		     rampEvidence.passedStep ? "true" : "false", probeStabilityName(result.stability), result.observedThroughputReliable ? "true" : "false",
		     (unsigned long long)rampEvidence.recommendationBasisKbps, kYoutubeProbeSafeMultiplierPercent, kYoutubeProbeAudioBitrateKbps,
		     (unsigned long long)uncappedSafeKbps, (unsigned long long)result.safeKbps, result.platformCapKbps, leg.limits.maxBitrateKbps,
		     (unsigned long long)totalProbeBytes, result.ceilingReached ? "true" : "false", measurementConclusive ? "true" : "false",
		     (unsigned long long)confirmationEpisodes, terminationReason.c_str(), result.errorCode.empty() ? "none" : result.errorCode.c_str());
	}

	obs_output_stop(resources.output);
	if (!waitForOutputInactive(resources.output, kProbeStopTimeoutMs)) {
		obs_output_force_stop(resources.output);
		if (!waitForOutputInactive(resources.output, kProbeStopTimeoutMs)) {
			result.success = false;
			result.errorCode = result.provider + "_probe_cleanup_timeout";
			return result;
		}
	}
	return result;
}

static void clearProbeSecrets(Session &session)
{
	for (auto &probe : session.probes) {
		probe.streamKey.clear();
		probe.server.clear();
	}
}

static void completeCancelled(const std::shared_ptr<Session> &session)
{
	clearProbeSecrets(*session);
	{
		std::lock_guard<std::mutex> lock(session->mutex);
		session->resultJson = serializeResult(*session, "cancelled", {}, "cancelled");
	}
	session->state.store(SessionState::Cancelled);
	pushEvent(session, "cancelled", "cleanup", 100, "cancelled");
}

static void completeFailed(const std::shared_ptr<Session> &session, const char *code)
{
	clearProbeSecrets(*session);
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

	std::vector<ProbeResult> probeResults;
	const size_t eligibleProbeCount =
		std::count_if(session->probes.begin(), session->probes.end(), [](const ProbeRequest &probe) { return probe.eligible; });
	size_t completedProbeCount = 0;
	for (auto &probe : session->probes) {
		if (!probe.eligible) {
			pushEvent(session, "progress", "bandwidth", 30, probe.denialReason, probe.legId, "estimated", probe.probeId, probe.provider);
			continue;
		}
		const auto legIt = std::find_if(preparedLegs.begin(), preparedLegs.end(), [&](const LegRequest &leg) { return leg.legId == probe.legId; });
		if (legIt == preparedLegs.end())
			continue;
		const double startProgress = 30.0 + (35.0 * (double)completedProbeCount / (double)std::max<size_t>(1, eligibleProbeCount));
		const double endProgress = 30.0 + (35.0 * (double)(completedProbeCount + 1) / (double)std::max<size_t>(1, eligibleProbeCount));
		pushEvent(session, "phase", "bandwidth", startProgress, probe.provider + "_probe_started", probe.legId, "active", probe.probeId,
			  probe.provider);
		const auto probeRunStarted = std::chrono::steady_clock::now();
		ProbeResult result = runRtmpProbe(session, probe, *legIt, startProgress, endProgress);
		const auto probeRunElapsedMs =
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - probeRunStarted).count();
		if (result.provider == "youtube") {
			blog(result.success ? LOG_INFO : LOG_WARNING,
			     "[Auto Optimizer][YouTube Probe] Probe summary: success=%s, cancelled=%s, measured_aggregate=%llu Kbps, "
			     "safe_video=%llu Kbps, ceiling_reached=%s, stability=%s, observed_throughput_reliable=%s, elapsed=%lld ms, error=%s",
			     result.success ? "true" : "false", result.cancelled ? "true" : "false", (unsigned long long)result.measuredKbps,
			     (unsigned long long)result.safeKbps, result.ceilingReached ? "true" : "false", probeStabilityName(result.stability),
			     result.observedThroughputReliable ? "true" : "false", (long long)probeRunElapsedMs,
			     result.errorCode.empty() ? "none" : result.errorCode.c_str());
		}
		if (probe.provider == "youtube") {
			std::lock_guard<std::mutex> lock(session->probeConfirmationMutex);
			session->probeConfirmations.erase(probe.probeId);
			if (session->activeConfirmationProbeId == probe.probeId)
				session->activeConfirmationProbeId.clear();
		}
		completedProbeCount++;
		if (result.cancelled || session->cancelRequested.load()) {
			completeCancelled(session);
			return;
		}
		const std::string completionCode = result.success                                 ? result.provider + "_probe_completed"
						   : result.stability == ProbeStability::Unstable ? result.provider + "_probe_unstable_estimate_used"
												  : result.provider + "_probe_failed_estimate_used";
		pushEvent(session, "progress", "bandwidth", endProgress, completionCode, result.legId, result.success ? "active" : "estimated", probe.probeId,
			  probe.provider);
		probeResults.push_back(std::move(result));
	}
	clearProbeSecrets(*session);
	if (eligibleProbeCount == 0)
		pushEvent(session, "progress", "bandwidth", 65, "estimate_only", {}, "estimated");

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

		const size_t requiredProbeCount = std::count_if(session->probes.begin(), session->probes.end(),
								[&](const ProbeRequest &probe) { return probe.eligible && probe.legId == leg.legId; });
		std::vector<const ProbeResult *> legProbeResults;
		for (const auto &probeResult : probeResults) {
			if (probeResult.legId == leg.legId)
				legProbeResults.push_back(&probeResult);
		}
		const bool allRequiredProbesPassed =
			requiredProbeCount > 0 && legProbeResults.size() == requiredProbeCount &&
			std::all_of(legProbeResults.begin(), legProbeResults.end(), [](const ProbeResult *result) { return result->success; });
		for (const ProbeResult *result : legProbeResults) {
			recommendation.probes.push_back({result->provider, result->method, result->measuredKbps, result->safeKbps, result->headroomPercent,
							 result->success, result->ceilingReached});
		}

		if (allRequiredProbesPassed) {
			recommendation.measurementMode = "active";
			if (hardware.passed && session->topology == "cloud-multistream") {
				recommendation.confidence = "medium";
				recommendation.reason = "indirect_provider_probes";
			} else if (hardware.passed && !hardware.constrained) {
				recommendation.confidence = "high";
				recommendation.reason.clear();
			}
			uint64_t safeKbps = UINT64_MAX;
			for (const ProbeResult *result : legProbeResults)
				safeKbps = std::min(safeKbps, result->safeKbps);
			if (leg.limits.maxBitrateKbps > 0)
				safeKbps = std::min<uint64_t>(safeKbps, (uint64_t)leg.limits.maxBitrateKbps);
			// Never turn a low measurement into a higher recommendation merely to
			// satisfy a nominal bitrate floor. Surface the low-confidence result and
			// let Desktop decide how to explain an insufficient connection.
			const bool hasDegradedProbe = std::any_of(legProbeResults.begin(), legProbeResults.end(),
								  [](const ProbeResult *result) { return result->stability == ProbeStability::Degraded; });
			const bool hasVariableProbe = std::any_of(legProbeResults.begin(), legProbeResults.end(),
								  [](const ProbeResult *result) { return result->stability == ProbeStability::Variable; });
			if (hasDegradedProbe && recommendation.confidence != "low") {
				recommendation.confidence = "low";
				recommendation.reason = "unstable_connection";
			} else if (hasVariableProbe && recommendation.confidence == "high") {
				recommendation.confidence = "medium";
				recommendation.reason = "connection_variability_detected";
			}
			if (safeKbps < 500) {
				recommendation.confidence = "low";
				recommendation.reason = "insufficient_bandwidth";
			}
			recommendation.value.bitrateKbps = (int)std::clamp<uint64_t>(safeKbps, 1, kYoutubeProbeMaximumBitrateKbps);
		} else if (requiredProbeCount > 0) {
			recommendation.confidence = "low";
			const bool hasUnstableProbe = std::any_of(legProbeResults.begin(), legProbeResults.end(),
								  [](const ProbeResult *result) { return result->stability == ProbeStability::Unstable; });
			recommendation.reason = hasUnstableProbe                           ? "unstable_connection"
						: session->topology == "cloud-multistream" ? "indirect_provider_probe_failed"
											   : "probe_failed";
			// A failed probe can still have trustworthy throughput evidence.
			// Unstable observations are deliberately excluded: they describe a
			// variable path, not a defensible bandwidth ceiling.
			uint64_t observedSafeKbps = UINT64_MAX;
			bool hasObservedThroughput = false;
			for (const ProbeResult *result : legProbeResults) {
				if (result->observedThroughputReliable && result->measuredKbps > 0 && result->safeKbps > 0) {
					hasObservedThroughput = true;
					observedSafeKbps = std::min(observedSafeKbps, result->safeKbps);
				}
			}
			if (hasObservedThroughput) {
				const uint64_t representableSafeKbps = std::max<uint64_t>(1, observedSafeKbps);
				recommendation.value.bitrateKbps = probePolicy::clampEstimateToObservedSafe(
					recommendation.value.bitrateKbps, representableSafeKbps, kYoutubeProbeMaximumBitrateKbps);
				if (observedSafeKbps < 500)
					recommendation.reason = "insufficient_bandwidth";
			}
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
	session->probeConfirmationCondition.notify_all();
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
	collection->register_function(std::make_shared<ipc::function>(
		"ConfirmAutoConfigProbeIngest", std::vector<ipc::type>{ipc::type::String, ipc::type::String, ipc::type::UInt32}, ConfirmProbeIngest));
	collection->register_function(std::make_shared<ipc::function>("QueryAutoConfigSession", std::vector<ipc::type>{ipc::type::String}, QuerySession));
	collection->register_function(std::make_shared<ipc::function>("GetAutoConfigResult", std::vector<ipc::type>{ipc::type::String}, GetResult));
	collection->register_function(std::make_shared<ipc::function>("CancelAutoConfigSession", std::vector<ipc::type>{ipc::type::String}, CancelSession));
	collection->register_function(std::make_shared<ipc::function>("CloseAutoConfigSession", std::vector<ipc::type>{ipc::type::String}, CloseSession));

	srv.register_collection(collection);
}

void GetCapabilities(void *, const int64_t, const std::vector<ipc::value> &, std::vector<ipc::value> &rval)
{
	static const char *capabilities =
		R"({"apiVersion":2,"resultSchemaVersion":1,"previewApplySplit":true,"awaitableCancel":true,"perUploadLegResults":true,"desktopOwnedApply":true,"multipleActiveProbes":true,"bandwidthModes":["twitch-standard-active","youtube-unbound-active","estimate"]})";
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
	session->id = "autoconfig-" + std::to_string(os_gettime_ns()) + "-" + std::to_string(nextSessionId.fetch_add(1));
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

void ConfirmProbeIngest(void *, const int64_t, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	if (args.size() != 3) {
		returnError(rval, "ConfirmAutoConfigProbeIngest expects sessionId, probeId, and received");
		return;
	}
	auto session = findSession(args[0].value_str);
	if (!session) {
		returnError(rval, "autoconfig_session_not_found");
		return;
	}
	if (session->state.load() != SessionState::Running) {
		returnError(rval, "autoconfig_session_not_running");
		return;
	}
	const std::string &probeId = args[1].value_str;
	const auto probe = std::find_if(session->probes.begin(), session->probes.end(), [&](const ProbeRequest &candidate) {
		return candidate.eligible && candidate.provider == "youtube" && candidate.probeId == probeId;
	});
	if (probe == session->probes.end()) {
		returnError(rval, "autoconfig_probe_not_found");
		return;
	}
	{
		std::lock_guard<std::mutex> lock(session->probeConfirmationMutex);
		auto confirmation = session->probeConfirmations.find(probeId);
		if (confirmation == session->probeConfirmations.end() || session->activeConfirmationProbeId != probeId) {
			returnError(rval, "autoconfig_probe_not_confirmable");
			return;
		}
		confirmation->second = args[2].value_union.ui32 ? 1 : -1;
	}
	session->probeConfirmationCondition.notify_all();
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
	rval.push_back(ipc::value(event.probeId));
	rval.push_back(ipc::value(event.provider));
	rval.push_back(ipc::value(event.targetBitrateKbps));
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
			session->probeConfirmationCondition.notify_all();
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
		clearProbeSecrets(*session);
		session->state.store(SessionState::Closed);
	}

	{
		std::lock_guard<std::mutex> lock(sessionsMutex);
		if (activeSession == session)
			activeSession.reset();
	}
}

} // namespace autoConfig
