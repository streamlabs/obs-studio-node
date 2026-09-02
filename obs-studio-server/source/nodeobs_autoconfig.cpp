/******************************************************************************
    Copyright (C) 2026 by Streamlabs

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#include "nodeobs_autoconfig.h"

#include "autoconfig-video-mix.hpp"
#include "autoconfig-probe-policy.hpp"
#include "autoconfig-enhanced-broadcasting-policy.hpp"
#include "autoconfig-quality-policy.hpp"
#include "osn-audio-bitrate.hpp"
#include "osn-common.hpp"
#include "osn-encoders.hpp"
#include "osn-error.hpp"
#include "osn-multitrack-video-configuration.hpp"
#include "osn-multitrack-video-output.hpp"
#include "osn-multitrack-video.hpp"
#include "osn-video.hpp"
#include "shared.hpp"

#include <obs.h>
#include <obs-output.h>
#include <media-io/audio-io.h>
#include <media-io/video-frame.h>
#include <media-io/video-io.h>
#include <util/platform.h>
#include <util/threading.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace autoConfig {
namespace {

constexpr int kSchemaVersion = 1;
constexpr int kProbeConnectTimeoutMs = 8000;
constexpr int kProbeWarmupMs = 750;
constexpr int kProbeSampleMs = 5000;
constexpr int kProbeExtendedSampleMs = 10000;
constexpr int kProbeSubwindowMs = 1000;
constexpr int kProbeStopTimeoutMs = 3000;
constexpr int kEnhancedBroadcastingWarmupMs = 1000;
constexpr int kEnhancedBroadcastingSampleMs = 5000;
constexpr uint64_t kEnhancedBroadcastingMaximumTrackBitrateKbps = 100000;
constexpr int kYoutubeIngestConfirmationTimeoutMs = 15000;
constexpr int kCancelTimeoutMs = 8000;
constexpr uint64_t kProbeMaxBytes = 25ULL * 1024ULL * 1024ULL;
constexpr uint64_t kYoutubeProbeMaxBytes = 64ULL * 1024ULL * 1024ULL;
constexpr int kProbeMaximumBitrateKbps = qualityPolicy::kMaximumRecommendedBitrateKbps;
constexpr int kYoutubeProbeMaximumBitrateKbps = 10000;
constexpr int kYoutubeProbeInitialBitrateKbps = 1000;
constexpr int kYoutubeProbeSettleMs = 500;
constexpr int kYoutubeProbeSampleMs = 5000;
constexpr int kYoutubeProbeSustainedSampleMs = 10000;
constexpr int kYoutubeProbeSubwindowMs = 1000;
constexpr int kYoutubeProbeRecoveryMaximumMs = 10000;
constexpr int kYoutubeProbeRecoveryPollMs = 50;
constexpr uint32_t kYoutubeProbeRecoveryHealthySamples = 20;
constexpr int kYoutubeProbeTotalTimeoutMs = 100000;
constexpr int kYoutubeProbeBudgetSlackMs = 250;
constexpr int kYoutubeProbeMaximumConfirmationEpisodes = 2;
constexpr int kYoutubeProbeBudgetEstimatePercent = 115;
constexpr float kProbeCongestionHigh = 0.20f;
constexpr float kProbeCongestionSevere = 0.50f;
constexpr int kTwitchProbeAudioBitrateKbps = 32;
constexpr int kYoutubeProbeAudioBitrateKbps = 128;
constexpr int kHardwareWarmupMs = 500;
constexpr int kHardwareSampleMs = 1500;
constexpr int kHardwareStopTimeoutMs = 1000;
constexpr char kHardwareBenchmarkOutputId[] = "auto_optimizer_video_only_output";

struct HardwareBenchmarkOutput {
	obs_output_t *output = nullptr;
};

static const char *hardwareBenchmarkOutputName(void *)
{
	return "Auto Optimizer Video-only Output";
}

static void *hardwareBenchmarkOutputCreate(obs_data_t *, obs_output_t *output)
{
	auto *context = new HardwareBenchmarkOutput();
	context->output = output;
	return context;
}

static void hardwareBenchmarkOutputDestroy(void *data)
{
	auto *context = static_cast<HardwareBenchmarkOutput *>(data);
	if (!context)
		return;
	delete context;
}

static bool hardwareBenchmarkOutputStart(void *data)
{
	auto *context = static_cast<HardwareBenchmarkOutput *>(data);
	if (!context)
		return false;
	if (!obs_output_can_begin_data_capture(context->output, 0) || !obs_output_initialize_encoders(context->output, 0))
		return false;
	return obs_output_begin_data_capture(context->output, 0);
}

static void hardwareBenchmarkOutputStop(void *data, uint64_t)
{
	auto *context = static_cast<HardwareBenchmarkOutput *>(data);
	if (!context)
		return;
	// libobs performs end-of-capture teardown on its own worker thread.
	obs_output_end_data_capture(context->output);
}

static void hardwareBenchmarkOutputPacket(void *, encoder_packet *) {}

static void registerHardwareBenchmarkOutput()
{
	for (size_t index = 0;; index++) {
		const char *id = nullptr;
		if (!obs_enum_output_types(index, &id))
			break;
		if (id && std::strcmp(id, kHardwareBenchmarkOutputId) == 0)
			return;
	}

	obs_output_info info{};
	info.id = kHardwareBenchmarkOutputId;
	// OBS's null_output waits for both audio and video. A private benchmark canvas
	// cannot provide audio through multi-canvas routing, so null_output would
	// never emit its video packets. Use a video-only output to test encoder packet
	// production independently.
	info.flags = OBS_OUTPUT_VIDEO | OBS_OUTPUT_ENCODED;
	info.get_name = hardwareBenchmarkOutputName;
	info.create = hardwareBenchmarkOutputCreate;
	info.destroy = hardwareBenchmarkOutputDestroy;
	info.start = hardwareBenchmarkOutputStart;
	info.stop = hardwareBenchmarkOutputStop;
	info.encoded_packet = hardwareBenchmarkOutputPacket;
	obs_register_output(&info);
}

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
	uint64_t canvasId = osn::common::INVALID_ID;
	int width = 0;
	int height = 0;
	int fpsNum = 0;
	int fpsDen = 1;
	int bitrateKbps = 0;
	std::string encoderId;
	std::string encoderFamily;
	std::string encoderTitle;
	std::string codec;
	std::string presetKey;
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
	bool fatal = false;
	bool constrained = false;
	std::string reason;
	CurrentSettings value;
};

struct Destination {
	std::string platform;
};

struct AdditionalVideoRequest {
	std::string display;
	CurrentSettings current;
	Limits limits;
};

struct LegRequest {
	std::string legId;
	std::string display;
	std::string outputKind;
	std::vector<Destination> destinations;
	CurrentSettings current;
	Limits limits;
	std::optional<AdditionalVideoRequest> additionalVideo;
	std::string estimateReason;
};

struct ProbeRequest {
	std::string probeId;
	std::string kind;
	std::string legId;
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
	uint32_t testedWidth = 0;
	uint32_t testedHeight = 0;
	uint32_t testedFpsNum = 0;
	uint32_t testedFpsDen = 0;
	std::optional<CurrentSettings> testedAdditionalVideo;
	uint32_t videoTrackCount = 0;
	uint64_t configuredAggregateBitrateKbps = 0;
};

struct Recommendation {
	std::string legId;
	std::string display;
	std::string outputKind;
	std::vector<Destination> destinations;
	Limits limits;
	std::string measurementMode = "estimated";
	std::string confidence = "medium";
	std::string reason;
	CurrentSettings value;
	std::optional<CurrentSettings> additionalVideo;
	std::vector<MeasurementProvenance> probes;
};

struct AggregateUploadResult {
	uint64_t safeVideoKbps = 0;
	uint64_t allocatedVideoKbps = 0;
};

struct CompanionWorkload {
	std::string legId;
	std::string display;
	CurrentSettings value;
};

struct CombinedWorkloadResult {
	std::string enhancedBroadcastingLegId;
	std::vector<CompanionWorkload> companionLegs;
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
	std::string encoderId;
	std::string encoderFamily;
	std::string encoderTitle;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t fpsNum = 0;
	uint32_t fpsDen = 0;
	std::optional<CurrentSettings> additionalVideo;
	uint32_t selectedBitrateKbps = 0;
	uint32_t availableBitrateKbps = 0;
};

struct Session : std::enable_shared_from_this<Session> {
	std::string id;
	std::string topology;
	std::vector<LegRequest> legs;
	std::vector<ProbeRequest> probes;
	bool dualOutputActiveProbePair = false;
	bool concurrentHardwareValidated = false;
	bool enhancedBroadcastingDualOutputWorkload = false;

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

	// The worker owns these outputs. Cancel only borrows them while holding
	// this mutex, so it can request force-stop without racing release. Provider
	// probes remain sequential, while the exact two-leg Dual Output hardware
	// assessment deliberately publishes two outputs at once.
	std::mutex probeMutex;
	std::vector<obs_output_t *> activeProbeOutputs;
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

static std::string asciiLowerCopy(std::string value)
{
	for (char &character : value)
		character = character >= 'A' && character <= 'Z' ? static_cast<char>(character + ('a' - 'A')) : character;
	return value;
}

static bool probeProviderContractIsValid(const ProbeRequest &probe)
{
	return (probe.kind == "twitch-standard" && probe.provider == "twitch" && probePolicy::isOfficialTwitchServer(probe.server) &&
		probePolicy::isBoundedTwitchKey(probe.streamKey)) ||
	       (probe.kind == "twitch-enhanced-broadcasting" && probe.provider == "twitch" && probe.server == "auto" &&
		probePolicy::isBoundedTwitchKey(probe.streamKey) &&
		osn::HasExactlyOneTwitchBandwidthTestParameter(osn::NormalizeTwitchBandwidthTestKey(probe.streamKey))) ||
	       (probe.kind == "youtube-unbound" && probe.provider == "youtube" && probePolicy::isOfficialYoutubeRtmpsServer(probe.server) &&
		probePolicy::isBoundedYoutubeKey(probe.streamKey));
}

static bool isSupportedStandardDualOutputActiveProbePair(const Session &session)
{
	if (session.topology != "dual-output" || session.legs.size() != 2 || session.probes.size() != 2)
		return false;
	const auto reject = [](const char *reason) {
		blog(LOG_INFO, "[Auto Optimizer][Dual Output] joint active probing is ineligible: %s", reason);
		return false;
	};

	const LegRequest &firstLeg = session.legs[0];
	const LegRequest &secondLeg = session.legs[1];
	const bool canvasPairValid = probePolicy::standardDualOutputCanvasPairIsValid(
		firstLeg.display, firstLeg.current.canvasId,
		firstLeg.current.canvasId != osn::common::INVALID_ID && osn::Video::Manager::GetInstance().find(firstLeg.current.canvasId) != nullptr,
		secondLeg.display, secondLeg.current.canvasId,
		secondLeg.current.canvasId != osn::common::INVALID_ID && osn::Video::Manager::GetInstance().find(secondLeg.current.canvasId) != nullptr);
	if (!canvasPairValid)
		return reject("legs must use distinct horizontal and vertical canvases");

	std::map<std::string, std::set<std::string>> destinationProvidersByLeg;
	for (const auto &leg : session.legs) {
		auto &providers = destinationProvidersByLeg[leg.legId];
		for (const auto &destination : leg.destinations)
			providers.insert(destination.platform);
	}

	std::set<std::string> probeProviders;
	std::set<std::string> probedLegs;
	for (const auto &probe : session.probes) {
		const auto destinations = destinationProvidersByLeg.find(probe.legId);
		if (destinations == destinationProvidersByLeg.end() || !destinations->second.contains(probe.provider))
			return reject("each probe must bind to a leg carrying the same provider");
		if (probe.kind != "twitch-standard" && probe.kind != "youtube-unbound")
			return reject("only standard Twitch and unbound YouTube probes are supported");
		if (!probeProviderContractIsValid(probe))
			return reject("a provider probe failed its credential or endpoint contract");
		if (!probeProviders.insert(probe.provider).second || !probedLegs.insert(probe.legId).second)
			return reject("each provider and leg must have exactly one probe");
	}
	return probeProviders == std::set<std::string>{"twitch", "youtube"} && probedLegs.size() == 2;
}

static bool sameVideoTuple(const CurrentSettings &left, const CurrentSettings &right)
{
	return left.canvasId == right.canvasId && left.width == right.width && left.height == right.height && left.fpsNum == right.fpsNum &&
	       left.fpsDen == right.fpsDen;
}

static bool isSupportedEnhancedBroadcastingDualOutputWorkload(const Session &session)
{
	if (session.topology != "enhanced-broadcasting-dual-output" || session.legs.size() < 2 || session.legs.size() > 3)
		return false;

	const LegRequest *enhancedLeg = nullptr;
	std::set<std::string> companionDisplays;
	for (const auto &leg : session.legs) {
		if (leg.outputKind == "twitch-enhanced-broadcasting") {
			if (enhancedLeg || leg.display != "both" || !leg.additionalVideo || leg.additionalVideo->display != "vertical" ||
			    leg.destinations.size() != 1 || leg.destinations.front().platform != "twitch")
				return false;
			enhancedLeg = &leg;
			continue;
		}
		if (leg.outputKind != "standard" || (leg.display != "horizontal" && leg.display != "vertical") || leg.additionalVideo ||
		    !companionDisplays.insert(leg.display).second || leg.destinations.empty() ||
		    std::any_of(leg.destinations.begin(), leg.destinations.end(),
				[](const Destination &destination) { return destination.platform == "twitch" || destination.platform == "custom"; }))
			return false;
	}
	if (!enhancedLeg || companionDisplays.empty())
		return false;

	const std::optional<uint64_t> additionalCanvasId = enhancedLeg->additionalVideo->current.canvasId;
	if (!enhancedBroadcastingPolicy::canvasReferencesAreValid(enhancedLeg->current.canvasId, additionalCanvasId, [](uint64_t canvasId) {
		    return osn::Video::Manager::GetInstance().find(canvasId) != nullptr;
	    }))
		return false;
	for (const auto &leg : session.legs) {
		if (leg.outputKind != "standard")
			continue;
		const CurrentSettings &identity = leg.display == "horizontal" ? enhancedLeg->current : enhancedLeg->additionalVideo->current;
		if (!sameVideoTuple(leg.current, identity))
			return false;
	}

	size_t enhancedProbeCount = 0;
	std::set<std::string> probedCompanionLegs;
	for (const auto &probe : session.probes) {
		const auto leg =
			std::find_if(session.legs.begin(), session.legs.end(), [&](const LegRequest &candidate) { return candidate.legId == probe.legId; });
		if (leg == session.legs.end() || !probeProviderContractIsValid(probe))
			return false;
		if (leg->outputKind == "twitch-enhanced-broadcasting") {
			if (probe.kind != "twitch-enhanced-broadcasting" || ++enhancedProbeCount != 1)
				return false;
		} else {
			const bool youtubeDestination = std::any_of(leg->destinations.begin(), leg->destinations.end(),
								    [](const Destination &destination) { return destination.platform == "youtube"; });
			if (probe.kind != "youtube-unbound" || !youtubeDestination || !probedCompanionLegs.insert(leg->legId).second)
				return false;
		}
	}
	return enhancedProbeCount == 1;
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
	if (topology == "enhanced-broadcasting-dual-output")
		return "enhanced_broadcasting_dual_output";
	if (topology == "stream-shift")
		return "stream_shift";
	if (topology == "mixed")
		return "mixed_topology";
	return "non_twitch";
}

static size_t probeableProviderCount(const LegRequest &leg)
{
	std::set<std::string> expectedProviders;
	for (const auto &destination : leg.destinations) {
		if (destination.platform == "twitch" || destination.platform == "youtube")
			expectedProviders.insert(destination.platform);
	}
	return expectedProviders.size();
}

static bool isKnownDisplay(const std::string &display)
{
	return display == "horizontal" || display == "vertical" || display == "both";
}

static bool isKnownPlatform(const std::string &platform)
{
	static const std::set<std::string> known = {"twitch", "youtube", "facebook", "kick", "tiktok", "custom", "other"};
	return known.contains(platform);
}

static bool isKnownTopology(const std::string &topology)
{
	static const std::set<std::string> known = {"direct-single",         "cloud-multistream",
						    "custom-rtmp",           "dual-output",
						    "enhanced-broadcasting", "enhanced-broadcasting-dual-output",
						    "stream-shift",          "mixed"};
	return known.contains(topology);
}

static bool isKnownOutputKind(const std::string &outputKind)
{
	return outputKind == "standard" || outputKind == "twitch-enhanced-broadcasting";
}

static bool providerOwnsEncoding(const std::string &topology, const LegRequest &leg)
{
	UNUSED_PARAMETER(topology);
	return leg.outputKind == "twitch-enhanced-broadcasting";
}

static bool parseCurrentSettings(obs_data_t *object, CurrentSettings &current)
{
	if (!object)
		return false;
	if (obs_data_has_user_value(object, "canvasId")) {
		obs_data_item_t *canvasIdItem = obs_data_item_byname(object, "canvasId");
		const bool integerCanvasId = canvasIdItem && obs_data_item_gettype(canvasIdItem) == OBS_DATA_NUMBER &&
					     obs_data_item_numtype(canvasIdItem) == OBS_DATA_NUM_INT;
		const int64_t canvasId = integerCanvasId ? obs_data_item_get_int(canvasIdItem) : -1;
		if (canvasIdItem)
			obs_data_item_release(&canvasIdItem);
		constexpr int64_t kMaximumJavascriptSafeInteger = 9007199254740991LL;
		if (!integerCanvasId || canvasId < 0 || canvasId > kMaximumJavascriptSafeInteger)
			return false;
		current.canvasId = static_cast<uint64_t>(canvasId);
	}
	current.width = (int)obs_data_get_int(object, "width");
	current.height = (int)obs_data_get_int(object, "height");
	current.fpsNum = (int)obs_data_get_int(object, "fpsNum");
	current.fpsDen = (int)obs_data_get_int(object, "fpsDen");
	current.bitrateKbps = (int)obs_data_get_int(object, "bitrateKbps");
	current.encoderId = obs_data_get_string(object, "encoderId");
	current.preset = obs_data_get_string(object, "preset");
	return current.width >= 64 && current.width <= 8192 && current.height >= 64 && current.height <= 8192 && current.fpsNum > 0 &&
	       current.fpsNum <= 240000 && current.fpsDen > 0 && current.fpsDen <= 10000 && current.bitrateKbps >= 0 && current.bitrateKbps <= 100000;
}

static bool parseLimits(obs_data_t *object, Limits &limits)
{
	if (!object)
		return true;
	const int64_t maxBitrateKbps = obs_data_get_int(object, "maxBitrateKbps");
	const int64_t maxWidth = obs_data_get_int(object, "maxWidth");
	const int64_t maxHeight = obs_data_get_int(object, "maxHeight");
	const int64_t maxFpsNum = obs_data_get_int(object, "maxFpsNum");
	const int64_t maxFpsDen = obs_data_get_int(object, "maxFpsDen");
	const bool hasPartialResolutionLimit = (maxWidth > 0) != (maxHeight > 0);
	const bool hasOrphanFpsDenominator = maxFpsNum == 0 && maxFpsDen > 0;
	if (maxBitrateKbps < 0 || maxBitrateKbps > 100000 || maxWidth < 0 || maxWidth > 8192 || maxHeight < 0 || maxHeight > 8192 ||
	    (maxWidth > 0 && maxWidth < 64) || (maxHeight > 0 && maxHeight < 64) || hasPartialResolutionLimit || maxFpsNum < 0 || maxFpsNum > 240000 ||
	    maxFpsDen < 0 || maxFpsDen > 10000 || hasOrphanFpsDenominator)
		return false;
	limits.maxBitrateKbps = (int)maxBitrateKbps;
	limits.maxWidth = (int)maxWidth;
	limits.maxHeight = (int)maxHeight;
	limits.maxFpsNum = (int)maxFpsNum;
	limits.maxFpsDen = maxFpsNum > 0 && maxFpsDen == 0 ? 1 : (int)maxFpsDen;
	return true;
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
	const size_t maximumLegCount = session.topology == "enhanced-broadcasting-dual-output" ? qualityPolicy::kMaximumEnhancedBroadcastingDualOutputLegs
											       : qualityPolicy::kMaximumUploadLegs;
	if (valid && (legCount == 0 || legCount > maximumLegCount)) {
		error = "invalid_autoconfig_legs";
		valid = false;
	}

	std::set<std::string> legIds;
	for (size_t i = 0; valid && i < legCount; i++) {
		obs_data_t *item = obs_data_array_item(legs, i);
		LegRequest leg;
		leg.legId = obs_data_get_string(item, "legId");
		leg.display = obs_data_get_string(item, "display");
		const bool outputKindExplicit = obs_data_has_user_value(item, "outputKind");
		leg.outputKind = obs_data_get_string(item, "outputKind");
		if (leg.outputKind.empty())
			leg.outputKind = session.topology == "enhanced-broadcasting" ? "twitch-enhanced-broadcasting" : "standard";
		leg.estimateReason = obs_data_get_string(item, "estimateReason");

		if (session.topology == "enhanced-broadcasting-dual-output" && !outputKindExplicit) {
			error = "invalid_autoconfig_output_kind";
			valid = false;
		} else if (leg.legId.empty() || leg.legId.size() > 128 || !legIds.insert(leg.legId).second || !isKnownDisplay(leg.display) ||
			   !isKnownOutputKind(leg.outputKind)) {
			error = "invalid_autoconfig_leg_identity";
			valid = false;
		}

		obs_data_t *current = obs_data_get_obj(item, "current");
		if (!current) {
			error = "missing_autoconfig_current_settings";
			valid = false;
		} else {
			if (!parseCurrentSettings(current, leg.current)) {
				error = "invalid_autoconfig_current_settings";
				valid = false;
			}
			obs_data_release(current);
		}

		obs_data_t *limits = obs_data_get_obj(item, "limits");
		if (limits) {
			if (!parseLimits(limits, leg.limits)) {
				error = "invalid_autoconfig_limits";
				valid = false;
			}
			obs_data_release(limits);
		}

		obs_data_t *additionalVideo = obs_data_get_obj(item, "additionalVideo");
		if (additionalVideo) {
			AdditionalVideoRequest parsed;
			parsed.display = obs_data_get_string(additionalVideo, "display");
			obs_data_t *additionalCurrent = obs_data_get_obj(additionalVideo, "current");
			obs_data_t *additionalLimits = obs_data_get_obj(additionalVideo, "limits");
			const bool settingsValid = parseCurrentSettings(additionalCurrent, parsed.current);
			const bool limitsValid = parseLimits(additionalLimits, parsed.limits);
			if (additionalCurrent)
				obs_data_release(additionalCurrent);
			if (additionalLimits)
				obs_data_release(additionalLimits);
			obs_data_release(additionalVideo);
			if ((session.topology != "enhanced-broadcasting" && session.topology != "enhanced-broadcasting-dual-output") ||
			    leg.outputKind != "twitch-enhanced-broadcasting" || leg.display != "both" || parsed.display != "vertical" || !settingsValid ||
			    !limitsValid) {
				error = "invalid_autoconfig_additional_video";
				valid = false;
			} else {
				leg.additionalVideo = std::move(parsed);
			}
		}

		obs_data_array_t *destinations = obs_data_get_array(item, "destinations");
		const size_t destinationCount = destinations ? obs_data_array_count(destinations) : 0;
		if (destinationCount == 0 || destinationCount > 16) {
			error = "invalid_autoconfig_destinations";
			valid = false;
		} else {
			for (size_t di = 0; di < destinationCount; di++) {
				obs_data_t *destination = obs_data_array_item(destinations, di);
				Destination parsed{asciiLowerCopy(obs_data_get_string(destination, "platform"))};
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
	if (valid && session.topology != "enhanced-broadcasting-dual-output") {
		const bool singleOutputEnhanced = session.topology == "enhanced-broadcasting";
		const bool outputKindsValid = (!singleOutputEnhanced || session.legs.size() == 1) &&
					      std::all_of(session.legs.begin(), session.legs.end(), [&](const LegRequest &leg) {
						      return leg.outputKind == (singleOutputEnhanced ? "twitch-enhanced-broadcasting" : "standard");
					      });
		if (!outputKindsValid) {
			error = "invalid_autoconfig_output_kind";
			valid = false;
		}
	}

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
		probe.server = obs_data_get_string(item, "server");
		if (probe.kind == "twitch-enhanced-broadcasting")
			probe.server = "auto";
		probe.streamKey = obs_data_get_string(item, "streamKey");
		obs_data_release(item);

		if (probe.probeId.empty() || probe.probeId.size() > 128 || probe.legId.empty() || probe.legId.size() > 128 ||
		    !probeIds.insert(probe.probeId).second) {
			error = "invalid_autoconfig_probe_identity";
			valid = false;
			break;
		}
		if (probe.kind == "twitch-standard" || probe.kind == "twitch-enhanced-broadcasting")
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

	if (session.topology == "enhanced-broadcasting-dual-output") {
		session.enhancedBroadcastingDualOutputWorkload = isSupportedEnhancedBroadcastingDualOutputWorkload(session);
		if (!session.enhancedBroadcastingDualOutputWorkload) {
			error = "invalid_autoconfig_enhanced_broadcasting_dual_output";
			return false;
		}
	}

	// Keep probe credentials only when the probe kind, output, stream setup, and
	// destination match an allowed active-probe configuration. Otherwise clear
	// them before any network work.
	const bool multipleDualOutputLegs = session.topology == "dual-output" && session.legs.size() > 1;
	session.dualOutputActiveProbePair = isSupportedStandardDualOutputActiveProbePair(session);
	std::map<std::string, size_t> probePairCounts;
	for (const auto &probe : session.probes)
		probePairCounts[probe.legId + "\n" + probe.provider]++;

	for (auto &probe : session.probes) {
		const auto legIt = std::find_if(session.legs.begin(), session.legs.end(), [&](const LegRequest &leg) { return leg.legId == probe.legId; });
		const bool legFound = legIt != session.legs.end();
		const bool destinationFound = legFound && std::any_of(legIt->destinations.begin(), legIt->destinations.end(),
								      [&](const Destination &destination) { return destination.platform == probe.provider; });
		const bool directEligible = session.topology == "direct-single" && session.legs.size() == 1 && legFound && legIt->destinations.size() == 1;
		const bool dualEligible = (session.topology == "dual-output" && session.legs.size() == 1 && legFound && legIt->destinations.size() == 1) ||
					  session.dualOutputActiveProbePair;
		const bool cloudEligible = session.topology == "cloud-multistream" && session.legs.size() == 1 && legFound;
		std::optional<uint64_t> additionalCanvasId;
		if (legFound && legIt->additionalVideo)
			additionalCanvasId = legIt->additionalVideo->current.canvasId;
		const bool canvasReferencesValid =
			legFound && enhancedBroadcastingPolicy::canvasReferencesAreValid(legIt->current.canvasId, additionalCanvasId, [](uint64_t canvasId) {
				return osn::Video::Manager::GetInstance().find(canvasId) != nullptr;
			});
		if (probe.kind == "twitch-enhanced-broadcasting" && session.topology == "enhanced-broadcasting" && legFound && !canvasReferencesValid) {
			error = "invalid_autoconfig_enhanced_broadcasting_canvas";
			return false;
		}
		const bool enhancedBroadcastingCanvasEligible =
			legFound && canvasReferencesValid &&
			((legIt->display == "horizontal" && !legIt->additionalVideo.has_value()) ||
			 (legIt->display == "both" && legIt->additionalVideo.has_value() && legIt->additionalVideo->display == "vertical"));
		const bool singleOutputEnhancedBroadcastingEligible = probe.kind == "twitch-enhanced-broadcasting" &&
								      session.topology == "enhanced-broadcasting" && session.legs.size() == 1 &&
								      probeCount == 1 && enhancedBroadcastingCanvasEligible &&
								      legIt->destinations.size() == 1 && legIt->destinations.front().platform == "twitch";
		const bool compositeEnhancedBroadcastingEligible =
			session.enhancedBroadcastingDualOutputWorkload && legFound &&
			((probe.kind == "twitch-enhanced-broadcasting" && legIt->outputKind == "twitch-enhanced-broadcasting") ||
			 (probe.kind == "youtube-unbound" && legIt->outputKind == "standard"));
		const bool providerValid = probeProviderContractIsValid(probe);

		const bool topologyEligible = singleOutputEnhancedBroadcastingEligible || compositeEnhancedBroadcastingEligible ||
					      (probe.kind != "twitch-enhanced-broadcasting" && (directEligible || dualEligible || cloudEligible));
		const auto eligibility = probePolicy::decideActiveProbeEligibility(!probe.provider.empty(), destinationFound, topologyEligible, providerValid,
										   probePairCounts[probe.legId + "\n" + probe.provider] == 1,
										   multipleDualOutputLegs, session.dualOutputActiveProbePair);
		probe.eligible = eligibility.eligible;
		if (!probe.eligible) {
			probe.denialReason = eligibility.denialReason;
			probe.streamKey.clear();
			probe.server.clear();
		}
		if (probe.eligible && probe.provider == "youtube")
			session.probeConfirmations.emplace(probe.probeId, 0);
	}

	return true;
}

static void pushEvent(const std::shared_ptr<Session> &session, const char *type, const char *phase, double progress, const std::string &code = {},
		      const std::string &legId = {}, const std::string &measurementMode = {}, const std::string &probeId = {}, const std::string &provider = {},
		      uint32_t targetBitrateKbps = 0, const CurrentSettings *video = nullptr, uint32_t selectedBitrateKbps = 0,
		      uint32_t availableBitrateKbps = 0, const CurrentSettings *additionalVideo = nullptr)
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
	if (video) {
		event.encoderId = video->encoderId;
		event.encoderFamily = video->encoderFamily;
		event.encoderTitle = video->encoderTitle;
		event.width = (uint32_t)std::max(0, video->width);
		event.height = (uint32_t)std::max(0, video->height);
		event.fpsNum = (uint32_t)std::max(0, video->fpsNum);
		event.fpsDen = (uint32_t)std::max(0, video->fpsDen);
		event.selectedBitrateKbps = selectedBitrateKbps;
	}
	if (additionalVideo)
		event.additionalVideo = *additionalVideo;
	event.availableBitrateKbps = availableBitrateKbps;
	session->events.push(std::move(event));
}

static std::shared_ptr<Session> findSession(const std::string &id)
{
	std::lock_guard<std::mutex> lock(sessionsMutex);
	if (activeSession && activeSession->id == id)
		return activeSession;
	return nullptr;
}

static void putAdditionalVideoTuple(obs_data_t *parent, const char *key, const CurrentSettings &video);

static std::string serializeEvent(const Session &session, const SessionEvent &event)
{
	obs_data_t *root = obs_data_create();
	obs_data_set_int(root, "schemaVersion", kSchemaVersion);
	obs_data_set_string(root, "sessionId", session.id.c_str());
	obs_data_set_int(root, "sequence", (long long)event.sequence);
	obs_data_set_string(root, "type", event.type.c_str());
	obs_data_set_string(root, "phase", event.phase.c_str());
	obs_data_set_double(root, "progress", event.progress);
	if (!event.code.empty())
		obs_data_set_string(root, "code", event.code.c_str());
	if (!event.legId.empty())
		obs_data_set_string(root, "legId", event.legId.c_str());
	if (!event.measurementMode.empty())
		obs_data_set_string(root, "measurementMode", event.measurementMode.c_str());
	if (!event.probeId.empty())
		obs_data_set_string(root, "probeId", event.probeId.c_str());
	if (!event.provider.empty())
		obs_data_set_string(root, "provider", event.provider.c_str());
	if (event.targetBitrateKbps > 0)
		obs_data_set_int(root, "targetBitrateKbps", event.targetBitrateKbps);
	if (!event.encoderId.empty())
		obs_data_set_string(root, "encoderId", event.encoderId.c_str());
	if (!event.encoderFamily.empty())
		obs_data_set_string(root, "encoderFamily", event.encoderFamily.c_str());
	if (!event.encoderTitle.empty())
		obs_data_set_string(root, "encoderTitle", event.encoderTitle.c_str());
	if (event.width > 0)
		obs_data_set_int(root, "width", event.width);
	if (event.height > 0)
		obs_data_set_int(root, "height", event.height);
	if (event.fpsNum > 0)
		obs_data_set_int(root, "fpsNum", event.fpsNum);
	if (event.fpsDen > 0)
		obs_data_set_int(root, "fpsDen", event.fpsDen);
	if (event.selectedBitrateKbps > 0)
		obs_data_set_int(root, "selectedBitrateKbps", event.selectedBitrateKbps);
	if (event.availableBitrateKbps > 0)
		obs_data_set_int(root, "availableBitrateKbps", event.availableBitrateKbps);
	if (event.additionalVideo && event.additionalVideo->width > 0)
		putAdditionalVideoTuple(root, "additionalVideo", *event.additionalVideo);

	std::string json = obs_data_get_json(root);
	obs_data_release(root);
	return json;
}

static std::string resolveEncoderId(const std::string &id)
{
	if (id.empty())
		return {};
	// The software VideoToolbox implementation is not a hardware-capacity
	// signal and must never be recommended.
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

struct EncoderDescriptor {
	std::string id;
	std::string family;
	std::string title;
	std::string presetKey;
	std::string preset;
	bool hardware = false;
};

struct EncoderPreset {
	const char *key;
	const char *value;
};

static bool isH264Encoder(const std::string &id)
{
	const char *codec = id.empty() ? nullptr : obs_get_encoder_codec(id.c_str());
	return codec && asciiLowerCopy(codec) == "h264";
}

static bool isModernHardwareH264(const std::string &id)
{
	return id == ENCODER_NVENC_H264_TEX || id == ADVANCED_ENCODER_QSV_V2 || id == ADVANCED_ENCODER_AMD || id == APPLE_HARDWARE_VIDEO_ENCODER ||
	       id == APPLE_HARDWARE_VIDEO_ENCODER_M1;
}

static EncoderPreset defaultEncoderPreset(const std::string &id)
{
	// These are the actual encoder properties and defaults exposed by the
	// supported OBS H.264 encoders. Return the exact property value used by the
	// temporary benchmark, not a similarly named OBS configuration field.
	if (id == ENCODER_NVENC_H264_TEX)
		return {"preset", "p5"};
	if (id == ADVANCED_ENCODER_QSV_V2)
		return {"target_usage", "TU4"};
	if (id == ADVANCED_ENCODER_AMD)
		return {"preset", "quality"};
	if (id == APPLE_HARDWARE_VIDEO_ENCODER || id == APPLE_HARDWARE_VIDEO_ENCODER_M1)
		return {"profile", "high"};
	if (id == ADVANCED_ENCODER_X264)
		return {"preset", "veryfast"};
	return {nullptr, nullptr};
}

static EncoderDescriptor describeEncoder(const std::string &id, bool hardware)
{
	const EncoderPreset preset = defaultEncoderPreset(id);
	return {id,
		osn::EncoderUtils::getPublicEncoderFamily(id.c_str()),
		osn::EncoderUtils::getPublicEncoderTitle(id.c_str()),
		preset.key ? preset.key : "",
		preset.value ? preset.value : "",
		hardware};
}

static std::vector<EncoderDescriptor> availableHardwareEncoders(const CurrentSettings &current)
{
	std::vector<EncoderDescriptor> result;
	auto add = [&](const std::string &id) {
		const std::string resolved = resolveEncoderId(id);
		if (!isModernHardwareH264(resolved) || !osn::EncoderUtils::isEncoderRegistered(resolved) || !isH264Encoder(resolved))
			return;
		if (std::none_of(result.begin(), result.end(), [&](const EncoderDescriptor &item) { return item.id == resolved; }))
			result.push_back(describeEncoder(resolved, true));
	};

	// Prefer the currently selected supported hardware encoder. An x264,
	// deprecated QSV H.264, HEVC, or AV1 selection does not prevent discovery of
	// supported H.264 hardware encoders.
	add(current.encoderId);
	add(ENCODER_NVENC_H264_TEX);
	add(ADVANCED_ENCODER_QSV_V2);
	add(ADVANCED_ENCODER_AMD);
	add(APPLE_HARDWARE_VIDEO_ENCODER);
	add(APPLE_HARDWARE_VIDEO_ENCODER_M1);
	return result;
}

static std::string scratchEncoderId(const std::string &recommendationId)
{
	const std::string encoder = resolveEncoderId(recommendationId);
	if (encoder == ENCODER_NVENC_H264_TEX)
		return "obs_nvenc_h264_soft";
	if (encoder == ADVANCED_ENCODER_QSV_V2)
		return "obs_qsv11_soft_v2";
	if (encoder == ADVANCED_ENCODER_AMD)
		return "h264_fallback_amf";
	return encoder;
}

static bool isX264Preset(const std::string &preset)
{
	static const std::set<std::string> supported = {"ultrafast", "superfast", "veryfast", "faster",   "fast",
							"medium",    "slow",      "slower",   "veryslow", "placebo"};
	return supported.contains(asciiLowerCopy(preset));
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

static bool capFps(CurrentSettings &value, int maxNum, int maxDen)
{
	maxDen = maxDen > 0 ? maxDen : 1;
	if ((int64_t)value.fpsNum * maxDen <= (int64_t)maxNum * value.fpsDen)
		return false;
	value.fpsNum = maxNum;
	value.fpsDen = maxDen;
	return true;
}

static void applyEncoderMetadata(CurrentSettings &value)
{
	const std::string internal = resolveEncoderId(value.encoderId);
	if (internal.empty())
		return;
	value.encoderId = internal;
	const char *codec = obs_get_encoder_codec(internal.c_str());
	value.codec = codec ? codec : value.codec;
	value.encoderFamily = osn::EncoderUtils::getPublicEncoderFamily(internal.c_str());
	value.encoderTitle = osn::EncoderUtils::getPublicEncoderTitle(internal.c_str());
}

static void applyEncoderSelection(CurrentSettings &value, const EncoderSelection &selection)
{
	if (selection.id != value.encoderId) {
		value.encoderId = selection.id;
		// Presets are encoder-family-specific. Never carry a preset from an
		// unavailable/failed encoder into its replacement; encoder defaults are
		// safer than a syntactically valid preset for the wrong family.
		value.preset.clear();
		value.codec.clear();
	}
	applyEncoderMetadata(value);
	if (value.codec.empty())
		value.codec = "h264";
}

static CurrentSettings baseRecommendation(const LegRequest &leg)
{
	CurrentSettings value = leg.current;
	if (leg.outputKind == "standard") {
		value.bitrateKbps = qualityPolicy::composeEstimatedBitrateKbps(value.bitrateKbps, leg.limits.maxBitrateKbps);
	} else {
		if (value.bitrateKbps <= 0)
			value.bitrateKbps = qualityPolicy::kDefaultEstimatedBitrateKbps;
		if (leg.limits.maxBitrateKbps > 0)
			value.bitrateKbps = std::min(value.bitrateKbps, leg.limits.maxBitrateKbps);
	}
	const auto bounded =
		qualityPolicy::boundCurrentToSupportedTier({value.width, value.height, value.fpsNum, value.fpsDen}, leg.limits.maxWidth, leg.limits.maxHeight);
	value.width = bounded.width;
	value.height = bounded.height;
	if (leg.limits.maxFpsNum > 0)
		capFps(value, leg.limits.maxFpsNum, leg.limits.maxFpsDen);
	// Encoder discovery and workload testing happen later in the hardware phase.
	// Until then, preserve the selected encoder for Twitch-managed ladders and as
	// the fallback if OSN cannot create temporary benchmark resources.
	applyEncoderMetadata(value);
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
		value.presetKey = hardware.value.presetKey;
		value.preset = hardware.value.preset;
	}
	return value;
}

static CurrentSettings benchmarkCeiling(const LegRequest &leg)
{
	CurrentSettings value = baseRecommendation(leg);
	const auto ceiling = qualityPolicy::benchmarkCeiling({value.width, value.height, value.fpsNum, value.fpsDen}, leg.limits.maxWidth, leg.limits.maxHeight,
							     leg.limits.maxFpsNum, leg.limits.maxFpsDen);
	value.width = ceiling.width;
	value.height = ceiling.height;
	value.fpsNum = ceiling.fpsNum;
	value.fpsDen = ceiling.fpsDen;
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

static void putAdditionalVideoTuple(obs_data_t *parent, const char *key, const CurrentSettings &video)
{
	obs_data_t *value = obs_data_create();
	obs_data_set_string(value, "display", "vertical");
	obs_data_set_int(value, "width", video.width);
	obs_data_set_int(value, "height", video.height);
	obs_data_set_int(value, "fpsNum", video.fpsNum);
	obs_data_set_int(value, "fpsDen", video.fpsDen);
	obs_data_set_obj(parent, key, value);
	obs_data_release(value);
}

static std::string serializeResult(const Session &session, const char *status, const std::vector<Recommendation> &recommendations,
				   const std::string &errorCode = {}, const std::optional<AggregateUploadResult> &aggregateUpload = {},
				   const std::optional<CombinedWorkloadResult> &combinedWorkload = {})
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
	if (aggregateUpload) {
		obs_data_t *aggregate = obs_data_create();
		obs_data_set_string(aggregate, "method", "dual-output-isolated-lower-bound");
		obs_data_set_int(aggregate, "safeVideoKbps", (long long)aggregateUpload->safeVideoKbps);
		obs_data_set_int(aggregate, "allocatedVideoKbps", (long long)aggregateUpload->allocatedVideoKbps);
		obs_data_set_bool(aggregate, "concurrentHardwareValidated", true);
		obs_data_set_obj(root, "aggregateUpload", aggregate);
		obs_data_release(aggregate);
	}
	if (combinedWorkload) {
		obs_data_t *combined = obs_data_create();
		obs_data_set_string(combined, "method", "enhanced-broadcasting-dual-output-concurrent");
		obs_data_set_string(combined, "enhancedBroadcastingLegId", combinedWorkload->enhancedBroadcastingLegId.c_str());
		obs_data_set_bool(combined, "validated", true);
		obs_data_array_t *companions = obs_data_array_create();
		for (const auto &companion : combinedWorkload->companionLegs) {
			obs_data_t *value = obs_data_create();
			obs_data_set_string(value, "legId", companion.legId.c_str());
			obs_data_set_string(value, "display", companion.display.c_str());
			obs_data_set_int(value, "width", companion.value.width);
			obs_data_set_int(value, "height", companion.value.height);
			obs_data_set_int(value, "fpsNum", companion.value.fpsNum);
			obs_data_set_int(value, "fpsDen", companion.value.fpsDen);
			obs_data_set_int(value, "bitrateKbps", companion.value.bitrateKbps);
			obs_data_set_string(value, "encoderId", companion.value.encoderId.c_str());
			if (!companion.value.preset.empty())
				obs_data_set_string(value, "preset", companion.value.preset.c_str());
			obs_data_array_push_back(companions, value);
			obs_data_release(value);
		}
		obs_data_set_array(combined, "companionLegs", companions);
		obs_data_array_release(companions);
		obs_data_set_obj(root, "combinedWorkload", combined);
		obs_data_release(combined);
	}

	obs_data_array_t *legs = obs_data_array_create();
	for (const auto &recommendation : recommendations) {
		obs_data_t *leg = obs_data_create();
		obs_data_set_string(leg, "legId", recommendation.legId.c_str());
		obs_data_set_string(leg, "display", recommendation.display.c_str());
		obs_data_set_string(leg, "outputKind", recommendation.outputKind.c_str());

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
				if (provenance.testedWidth > 0)
					obs_data_set_int(probe, "testedWidth", provenance.testedWidth);
				if (provenance.testedHeight > 0)
					obs_data_set_int(probe, "testedHeight", provenance.testedHeight);
				if (provenance.testedFpsNum > 0)
					obs_data_set_int(probe, "testedFpsNum", provenance.testedFpsNum);
				if (provenance.testedFpsDen > 0)
					obs_data_set_int(probe, "testedFpsDen", provenance.testedFpsDen);
				if (provenance.testedAdditionalVideo)
					putAdditionalVideoTuple(probe, "testedAdditionalVideo", *provenance.testedAdditionalVideo);
				if (provenance.videoTrackCount > 0)
					obs_data_set_int(probe, "videoTrackCount", provenance.videoTrackCount);
				if (provenance.configuredAggregateBitrateKbps > 0)
					obs_data_set_int(probe, "configuredAggregateBitrateKbps", (long long)provenance.configuredAggregateBitrateKbps);
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
		obs_data_set_string(value, "encoderFamily", recommendation.value.encoderFamily.c_str());
		obs_data_set_string(value, "encoderTitle", recommendation.value.encoderTitle.c_str());
		obs_data_set_string(value, "codec", recommendation.value.codec.c_str());
		if (!recommendation.value.preset.empty())
			obs_data_set_string(value, "preset", recommendation.value.preset.c_str());
		if (recommendation.additionalVideo)
			putAdditionalVideoTuple(value, "additionalVideo", *recommendation.additionalVideo);
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

enum class ProbeStability { Stable, SourceUnderfill, Degraded, Variable, Unstable };

static const char *probeStabilityName(ProbeStability stability)
{
	switch (stability) {
	case ProbeStability::Stable:
		return "stable";
	case ProbeStability::SourceUnderfill:
		return "source_underfill";
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
	uint32_t testedWidth = 0;
	uint32_t testedHeight = 0;
	uint32_t testedFpsNum = 0;
	uint32_t testedFpsDen = 0;
	std::optional<CurrentSettings> testedAdditionalVideo;
	uint32_t videoTrackCount = 0;
	uint64_t configuredAggregateBitrateKbps = 0;
	bool pairedCadenceEvidence = false;
	std::vector<CompanionWorkload> companionWorkloads;
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
	std::unique_ptr<obs_video_info> scratchViewInfo;
	obs_core_video_mix_t *scratchMix = nullptr;
	bool coreVideoMix = false;
	bool auxiliaryVideoMix = false;
	bool borrowedVideo = false;
	audio_t *syntheticAudio = nullptr;
	obs_encoder_t *videoEncoder = nullptr;
	obs_encoder_t *audioEncoder = nullptr;
	obs_service_t *service = nullptr;
	obs_output_t *output = nullptr;
	std::vector<OBSEncoderAutoRelease> multitrackAudioEncoders;
	std::shared_ptr<obs_encoder_group_t> multitrackVideoEncoderGroup;
	std::atomic<bool> stopFeeder{false};
	std::atomic<uint32_t> scheduledFrames{0};
	std::atomic<uint32_t> submittedFrames{0};
	std::atomic<uint32_t> lockFailedFrames{0};
	std::atomic<uint32_t> lateFrames{0};
	std::thread feeder;
	std::vector<uint8_t> framePatternA;
	std::vector<uint8_t> framePatternB;
	uint32_t audioNoiseState = 0xa341316cU;

	bool createSyntheticVideo(uint32_t width, uint32_t height, uint32_t fpsNum, uint32_t fpsDen, bool useCoreVideoMix = false, bool useCurrentScene = false,
				  obs_core_video_mix_t *identitySourceMix = nullptr)
	{
		if (identitySourceMix) {
			video_t *identityVideo = obs_video_mix_get_video(identitySourceMix);
			const video_output_info *identityInfo = identityVideo ? video_output_get_info(identityVideo) : nullptr;
			if (!identityInfo || !identityInfo->fps_num || !identityInfo->fps_den)
				return false;
			fpsNum = identityInfo->fps_num;
			fpsDen = identityInfo->fps_den;
		}
		videoWidth = width;
		videoHeight = height;
		videoFpsNum = fpsNum;
		videoFpsDen = fpsDen;
		coreVideoMix = useCoreVideoMix;
		if (coreVideoMix) {
			// Hardware texture encoders require a real OBS video mix. Create an
			// isolated view for the requested candidate. Hardware-only smoke tests
			// leave it source-free; Enhanced Broadcasting renders the current scene
			// through the auxiliary identity path below.
			// The private view does not reset the application's existing video
			// contexts.
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
			if (scratchView && useCurrentScene) {
				OBSSourceAutoRelease currentScene = obs_get_output_source(0);
				if (!currentScene)
					return false;
				obs_view_set_source(scratchView, 0, currentScene);
			}
			if (identitySourceMix) {
				// The probe renders the current scene at candidate settings without
				// registering another application canvas. It retains the horizontal
				// canvas ID because scene-item filtering and synthetic-audio routing
				// use that ID. The auxiliary-mix API changes render settings while
				// preserving the registered canvas identity.
				scratchMix = scratchView ? obs_view_add_auxiliary_mix(scratchView, scratchViewInfo.get(), identitySourceMix) : nullptr;
				auxiliaryVideoMix = scratchMix != nullptr;
				syntheticVideo = scratchMix ? obs_video_mix_get_video(scratchMix) : nullptr;
			} else {
				syntheticVideo = scratchView ? obs_view_add2(scratchView, scratchViewInfo.get()) : nullptr;
				// obs_encoder_set_video() is not sufficient for texture encoders
				// in the packaged libobs build: GPU startup resolves its mix again
				// and can observe no mix. Acquire the just-published private mix by
				// this unique video-info identity and keep that identity alive until
				// the render thread has removed the mix.
				scratchMix = syntheticVideo ? obs_video_mix_get(scratchViewInfo.get(), OBS_MAIN_VIDEO_RENDERING) : nullptr;
			}
			if (!syntheticVideo || !scratchMix)
				return false;
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

	bool useKnownActiveVideoMix(uint64_t canvasId)
	{
		obs_video_info *canvas = nullptr;
		if (canvasId != osn::common::INVALID_ID) {
			canvas = osn::Video::Manager::GetInstance().find(canvasId);
			if (!canvas)
				return false;
		}

		// Streaming mixes remain registered when multiple rendering is disabled,
		// but libobs renders only the main mix in that mode.
		scratchMix = obs_video_mix_get(canvas, videoMix::activeRenderingMode(obs_get_multiple_rendering()));
		if (!scratchMix)
			return false;
		borrowedVideo = true;
		syntheticVideo = obs_video_mix_get_video(scratchMix);
		return syntheticVideo != nullptr;
	}

	void startFeeder()
	{
		if (coreVideoMix || borrowedVideo)
			return;
		feeder = std::thread([this]() {
			os_set_thread_name("auto optimizer feeder");
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
		if (output && std::find(session.activeProbeOutputs.begin(), session.activeProbeOutputs.end(), output) == session.activeProbeOutputs.end())
			session.activeProbeOutputs.push_back(output);
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
			std::erase(session.activeProbeOutputs, output);
			if (output) {
				obs_output_release(output);
				output = nullptr;
			}
		}
		stopFeeder.store(true);
		if (feeder.joinable())
			feeder.join();
		multitrackAudioEncoders.clear();
		multitrackVideoEncoderGroup.reset();

		if (videoEncoder) {
			obs_encoder_release(videoEncoder);
			videoEncoder = nullptr;
		}
		if (audioEncoder) {
			obs_encoder_release(audioEncoder);
			audioEncoder = nullptr;
		}
		if (syntheticVideo && !coreVideoMix && !borrowedVideo) {
			video_output_stop(syntheticVideo);
			video_output_close(syntheticVideo);
			syntheticVideo = nullptr;
		}
		if (scratchView) {
			obs_video_info *removedViewInfo = scratchViewInfo.get();
			const bool removedAuxiliaryMix = auxiliaryVideoMix;
			obs_view_remove(scratchView);
			obs_view_destroy(scratchView);
			scratchView = nullptr;
			syntheticVideo = nullptr;
			scratchMix = nullptr;
			auxiliaryVideoMix = false;
			if (removedAuxiliaryMix) {
				// Auxiliary mix creation copies the render settings and retains its
				// registered identity independently of this request object.
				scratchViewInfo.reset();
			} else {
				// The render thread removes ordinary private mixes on a later tick.
				// Keep their caller-owned identity alive until the exact mix is no
				// longer published.
				const auto mixRemovalDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(1000, stopTimeoutMs));
				while (removedViewInfo && obs_video_mix_get(removedViewInfo, OBS_MAIN_VIDEO_RENDERING) &&
				       std::chrono::steady_clock::now() < mixRemovalDeadline)
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				if (removedViewInfo && obs_video_mix_get(removedViewInfo, OBS_MAIN_VIDEO_RENDERING)) {
					// Preserve the identity rather than leaving an asynchronous mix
					// with a dangling canvas_ovi pointer during abnormal shutdown.
					blog(LOG_WARNING, "[Auto Optimizer][Hardware] timed out waiting for the private video mix to be removed");
					(void)scratchViewInfo.release();
				} else {
					scratchViewInfo.reset();
				}
			}
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
	bool feederHealthy = false;
	uint32_t totalFrames = 0;
	uint32_t skippedFrames = 0;
	uint32_t encodedFrames = 0;
	uint32_t outputFrames = 0;
	uint32_t outputDroppedFrames = 0;
	uint32_t scheduledFrames = 0;
	uint32_t submittedFrames = 0;
	uint32_t lockFailedFrames = 0;
	uint32_t lateFrames = 0;
	uint32_t expectedFrames = 0;
	uint32_t minimumEncodedFrames = 0;
	uint32_t allowedSkippedFrames = 0;
	uint32_t sourceFpsNum = 0;
	uint32_t sourceFpsDen = 1;
	uint32_t frameRateDivisor = 1;
	uint32_t validatedFpsNum = 0;
	uint32_t validatedFpsDen = 1;
	bool sourceCadenceBelowTarget = false;
	std::string feedMode;
	std::string errorCode;
	std::string encoderLastError;
	std::string outputLastError;
};

enum class HardwareWorkloadFeed {
	Automatic,
	MainControl,
	SyntheticRawExact,
};

class HardwareStartGate {
public:
	explicit HardwareStartGate(size_t participants_) : participants(participants_) {}

	bool arriveAndWait(const std::shared_ptr<Session> &session, std::chrono::steady_clock::time_point deadline)
	{
		std::unique_lock<std::mutex> lock(mutex);
		arrived++;
		if (arrived == participants)
			condition.notify_all();
		const bool signalled =
			condition.wait_until(lock, deadline, [&]() { return aborted || arrived == participants || session->cancelRequested.load(); });
		if (!signalled) {
			aborted = true;
			condition.notify_all();
		}
		return signalled && !aborted && !session->cancelRequested.load();
	}

	void abort()
	{
		std::lock_guard<std::mutex> lock(mutex);
		aborted = true;
		condition.notify_all();
	}

private:
	size_t participants = 0;
	size_t arrived = 0;
	bool aborted = false;
	std::mutex mutex;
	std::condition_variable condition;
};

class HardwareStartGateParticipant {
public:
	explicit HardwareStartGateParticipant(std::shared_ptr<HardwareStartGate> gate_) : gate(std::move(gate_)) {}
	~HardwareStartGateParticipant()
	{
		if (gate && !arrived)
			gate->abort();
	}

	bool arriveAndWait(const std::shared_ptr<Session> &session, std::chrono::steady_clock::time_point deadline)
	{
		if (!gate)
			return true;
		arrived = true;
		return gate->arriveAndWait(session, deadline);
	}

private:
	std::shared_ptr<HardwareStartGate> gate;
	bool arrived = false;
};

static std::string boundedLogValue(const std::string &value)
{
	std::string result = value.substr(0, 512);
	std::replace(result.begin(), result.end(), '\r', ' ');
	std::replace(result.begin(), result.end(), '\n', ' ');
	return result;
}

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
					  std::chrono::steady_clock::time_point phaseDeadline, HardwareWorkloadFeed feed = HardwareWorkloadFeed::Automatic,
					  std::shared_ptr<HardwareStartGate> concurrentStartGate = {})
{
	HardwareAttempt result;
	HardwareStartGateParticipant startParticipant(std::move(concurrentStartGate));
	if (session->cancelRequested.load()) {
		result.cancelled = true;
		return result;
	}
	if (std::chrono::steady_clock::now() >= phaseDeadline) {
		result.timedOut = true;
		return result;
	}

	const std::string resolvedEncoderId = resolveEncoderId(candidate.encoderId);
	const bool useMainVideoControl = feed == HardwareWorkloadFeed::MainControl;
	const bool useSyntheticRawExact = feed == HardwareWorkloadFeed::SyntheticRawExact;
	const bool useCoreVideoMix = !useSyntheticRawExact && resolvedEncoderId != ADVANCED_ENCODER_X264;
	const std::string encoderId = useCoreVideoMix ? resolvedEncoderId : scratchEncoderId(candidate.encoderId);
	if (encoderId.empty() || !obs_get_encoder_codec(encoderId.c_str())) {
		result.errorCode = "hardware_benchmark_encoder_unavailable";
		return result;
	}

	ScratchResources resources(*session, kHardwareStopTimeoutMs);
	result.feedMode = useMainVideoControl    ? "main-control"
			  : useSyntheticRawExact ? "synthetic-raw-exact"
			  : useCoreVideoMix      ? "private-mix"
						 : "synthetic-raw";
	const bool videoCreated = useMainVideoControl ? useCoreVideoMix && resources.useKnownActiveVideoMix(candidate.canvasId)
						      : resources.createSyntheticVideo((uint32_t)candidate.width, (uint32_t)candidate.height,
										       (uint32_t)candidate.fpsNum, (uint32_t)candidate.fpsDen, useCoreVideoMix);
	if (!videoCreated) {
		result.errorCode = useCoreVideoMix ? "hardware_benchmark_video_mix_create_failed" : "hardware_benchmark_video_create_failed";
		return result;
	}
	obs_data_t *encoderSettings = obs_data_create();
	obs_data_set_int(encoderSettings, "bitrate", std::clamp(candidate.bitrateKbps, 500, kProbeMaximumBitrateKbps));
	obs_data_set_string(encoderSettings, "rate_control", "CBR");
	obs_data_set_int(encoderSettings, "keyint_sec", 2);
	if (!candidate.presetKey.empty() && !candidate.preset.empty() && (resolvedEncoderId != ADVANCED_ENCODER_X264 || isX264Preset(candidate.preset)))
		obs_data_set_string(encoderSettings, candidate.presetKey.c_str(), candidate.preset.c_str());
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
	if (useMainVideoControl)
		obs_encoder_set_scaled_size(resources.videoEncoder, (uint32_t)candidate.width, (uint32_t)candidate.height);
	if (const video_output_info *sourceInfo = video_output_get_info(resources.syntheticVideo)) {
		result.sourceFpsNum = sourceInfo->fps_num;
		result.sourceFpsDen = std::max(1U, sourceInfo->fps_den);
	}
	if (result.sourceFpsNum > 0) {
		const auto divisor = qualityPolicy::frameRateDivisor(result.sourceFpsNum, result.sourceFpsDen, (uint32_t)std::max(1, candidate.fpsNum),
								     (uint32_t)std::max(1, candidate.fpsDen));
		if (!divisor.supported) {
			if (!qualityPolicy::requiresExactHardwareCadenceValidation(useCoreVideoMix, result.sourceFpsNum, result.sourceFpsDen,
										   (uint32_t)std::max(1, candidate.fpsNum),
										   (uint32_t)std::max(1, candidate.fpsDen))) {
				result.errorCode = "hardware_benchmark_frame_rate_unsupported";
				return result;
			}
			// Libobs renders private texture mixes at the main canvas frame rate.
			// Test the selected texture encoder at the candidate resolution and
			// available frame rate. If the candidate frame rate is higher, also
			// require a raw-input test at the exact candidate settings before
			// recommending it.
			result.sourceCadenceBelowTarget = true;
			result.validatedFpsNum = result.sourceFpsNum;
			result.validatedFpsDen = result.sourceFpsDen;
		} else {
			result.frameRateDivisor = divisor.value;
			result.validatedFpsNum = (uint32_t)std::max(1, candidate.fpsNum);
			result.validatedFpsDen = (uint32_t)std::max(1, candidate.fpsDen);
			if (result.frameRateDivisor > 1 && !obs_encoder_set_frame_rate_divisor(resources.videoEncoder, result.frameRateDivisor)) {
				result.errorCode = "hardware_benchmark_frame_rate_divisor_failed";
				return result;
			}
		}
	}
	if (result.validatedFpsNum == 0) {
		result.validatedFpsNum = (uint32_t)std::max(1, candidate.fpsNum);
		result.validatedFpsDen = (uint32_t)std::max(1, candidate.fpsDen);
	}
	auto captureErrors = [&]() {
		if (const char *lastError = obs_encoder_get_last_error(resources.videoEncoder); lastError && *lastError)
			result.encoderLastError = lastError;
		if (resources.output) {
			if (const char *lastError = obs_output_get_last_error(resources.output); lastError && *lastError)
				result.outputLastError = lastError;
		}
	};

	resources.output = obs_output_create(kHardwareBenchmarkOutputId, "auto_optimizer_hardware_benchmark_output", nullptr, nullptr);
	if (!resources.output) {
		result.errorCode = "hardware_benchmark_output_create_failed";
		captureErrors();
		return result;
	}
	obs_output_set_video_encoder(resources.output, resources.videoEncoder);
	resources.publishOutput();
	resources.startFeeder();

	if (session->cancelRequested.load() || !startParticipant.arriveAndWait(session, phaseDeadline)) {
		if (session->cancelRequested.load())
			result.cancelled = true;
		else if (std::chrono::steady_clock::now() >= phaseDeadline)
			result.timedOut = true;
		else
			result.errorCode = "hardware_benchmark_peer_setup_failed";
		return result;
	}
	if (!obs_output_start(resources.output)) {
		result.errorCode = "hardware_benchmark_start_failed";
		captureErrors();
		return result;
	}

	const auto warmupDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kHardwareWarmupMs);
	if (!waitForScratchInterval(session, resources.output, warmupDeadline, phaseDeadline, result)) {
		captureErrors();
		return result;
	}

	const uint32_t startTotal = video_output_get_total_frames(resources.syntheticVideo);
	const uint32_t startSkipped = video_output_get_skipped_frames(resources.syntheticVideo);
	const uint32_t startEncoded = obs_encoder_get_encoded_frames(resources.videoEncoder);
	const uint32_t startOutputFrames = obs_output_get_total_frames(resources.output);
	const uint32_t startOutputDroppedFrames = obs_output_get_frames_dropped(resources.output);
	const uint32_t startScheduled = resources.scheduledFrames.load(std::memory_order_relaxed);
	const uint32_t startSubmitted = resources.submittedFrames.load(std::memory_order_relaxed);
	const uint32_t startLockFailed = resources.lockFailedFrames.load(std::memory_order_relaxed);
	const uint32_t startLate = resources.lateFrames.load(std::memory_order_relaxed);
	const auto sampleStart = std::chrono::steady_clock::now();
	const auto sampleDeadline = sampleStart + std::chrono::milliseconds(kHardwareSampleMs);
	if (!waitForScratchInterval(session, resources.output, sampleDeadline, phaseDeadline, result)) {
		captureErrors();
		return result;
	}

	const auto sampleEnd = std::chrono::steady_clock::now();
	result.totalFrames = video_output_get_total_frames(resources.syntheticVideo) - startTotal;
	result.skippedFrames = video_output_get_skipped_frames(resources.syntheticVideo) - startSkipped;
	result.encodedFrames = obs_encoder_get_encoded_frames(resources.videoEncoder) - startEncoded;
	result.outputFrames = obs_output_get_total_frames(resources.output) - startOutputFrames;
	result.outputDroppedFrames = obs_output_get_frames_dropped(resources.output) - startOutputDroppedFrames;
	result.scheduledFrames = resources.scheduledFrames.load(std::memory_order_relaxed) - startScheduled;
	result.submittedFrames = resources.submittedFrames.load(std::memory_order_relaxed) - startSubmitted;
	result.lockFailedFrames = resources.lockFailedFrames.load(std::memory_order_relaxed) - startLockFailed;
	result.lateFrames = resources.lateFrames.load(std::memory_order_relaxed) - startLate;
	captureErrors();

	obs_output_stop(resources.output);
	if (!waitForOutputInactive(resources.output, kHardwareStopTimeoutMs)) {
		obs_output_force_stop(resources.output);
		if (!waitForOutputInactive(resources.output, kHardwareStopTimeoutMs)) {
			result.errorCode = "hardware_benchmark_cleanup_timeout";
			captureErrors();
			return result;
		}
	}

	const double elapsedSeconds = std::chrono::duration<double>(sampleEnd - sampleStart).count();
	const double requestedFps = (double)result.validatedFpsNum / (double)result.validatedFpsDen;
	result.expectedFrames = (uint32_t)std::max(1.0, std::floor(requestedFps * elapsedSeconds));
	const uint32_t pipelineAllowance = std::min(4U, result.expectedFrames);
	result.minimumEncodedFrames = std::max(3U, (result.expectedFrames - pipelineAllowance) * 85U / 100U);
	result.allowedSkippedFrames = std::max(1U, result.totalFrames * 5U / 100U);
	const uint32_t minimumSubmitted = std::max(3U, result.expectedFrames * 85U / 100U);
	const uint32_t allowedLockFailed = std::max(1U, result.scheduledFrames * 5U / 100U);
	const uint32_t allowedLate = std::max(1U, result.scheduledFrames * 5U / 100U);
	result.feederHealthy = useMainVideoControl || useCoreVideoMix ||
			       (result.submittedFrames >= minimumSubmitted && result.lockFailedFrames <= allowedLockFailed && result.lateFrames <= allowedLate);
	// The control test uses the application's working video pipeline to verify
	// that this encoder can produce packets. Main-video skipped-frame counters
	// describe the running application, not this test encoder, so keep them only
	// for diagnostics and do not fail the control test because of them.
	const uint32_t classificationTotalFrames = useMainVideoControl ? std::max({result.totalFrames, result.encodedFrames, result.outputFrames})
								       : result.totalFrames;
	const uint32_t classificationSkippedFrames = useMainVideoControl ? 0 : result.skippedFrames;
	const auto classification = qualityPolicy::classifyHardwareSample(result.feederHealthy, classificationTotalFrames, classificationSkippedFrames,
									  result.encodedFrames, result.outputFrames, result.minimumEncodedFrames,
									  result.allowedSkippedFrames);
	result.success = classification.success;
	result.errorCode = classification.errorCode ? classification.errorCode : "";
	return result;
}

static std::vector<HardwareAttempt> runEncoderWorkloadsConcurrently(const std::shared_ptr<Session> &session, const std::vector<CurrentSettings> &candidates,
								    std::chrono::steady_clock::time_point phaseDeadline,
								    HardwareWorkloadFeed feed = HardwareWorkloadFeed::Automatic)
{
	if (candidates.size() <= 1)
		return candidates.empty() ? std::vector<HardwareAttempt>{}
					  : std::vector<HardwareAttempt>{runEncoderWorkload(session, candidates.front(), phaseDeadline, feed)};

	auto startGate = std::make_shared<HardwareStartGate>(candidates.size());
	std::vector<std::future<HardwareAttempt>> workers;
	workers.reserve(candidates.size());
	bool launchFailed = false;
	for (const auto &candidate : candidates) {
		try {
			workers.push_back(std::async(std::launch::async, [session, candidate, phaseDeadline, feed, startGate]() {
				return runEncoderWorkload(session, candidate, phaseDeadline, feed, startGate);
			}));
		} catch (...) {
			launchFailed = true;
			startGate->abort();
			break;
		}
	}

	std::vector<HardwareAttempt> results;
	results.reserve(candidates.size());
	for (auto &worker : workers) {
		try {
			results.push_back(worker.get());
		} catch (...) {
			HardwareAttempt failed;
			failed.errorCode = "hardware_benchmark_concurrent_worker_failed";
			results.push_back(std::move(failed));
		}
	}
	while (results.size() < candidates.size()) {
		HardwareAttempt failed;
		failed.errorCode = launchFailed ? "hardware_benchmark_concurrent_worker_launch_failed" : "hardware_benchmark_peer_setup_failed";
		results.push_back(std::move(failed));
	}
	return results;
}

static void logHardwareAttempt(const EncoderDescriptor &encoder, const CurrentSettings &candidate, const HardwareAttempt &value)
{
	const std::string encoderError = boundedLogValue(value.encoderLastError);
	const std::string outputError = boundedLogValue(value.outputLastError);
	blog(value.success ? LOG_INFO : LOG_WARNING,
	     "[Auto Optimizer][Hardware] encoder=%s family=%s workload=%dx%d@%d/%d feed=%s success=%s cancelled=%s timed_out=%s error=%s "
	     "source_fps=%u/%u validated_fps=%u/%u frame_rate_divisor=%u input_frames=%u skipped_frames=%u encoded_frames=%u output_frames=%u "
	     "output_dropped=%u expected_frames=%u minimum_encoded=%u allowed_skipped=%u feeder_healthy=%s "
	     "scheduled=%u submitted=%u lock_failed=%u late=%u encoder_error=\"%s\" output_error=\"%s\"",
	     encoder.id.c_str(), encoder.family.c_str(), candidate.width, candidate.height, candidate.fpsNum, candidate.fpsDen,
	     value.feedMode.empty() ? "unavailable" : value.feedMode.c_str(), value.success ? "true" : "false", value.cancelled ? "true" : "false",
	     value.timedOut ? "true" : "false", value.errorCode.empty() ? "none" : value.errorCode.c_str(), value.sourceFpsNum, value.sourceFpsDen,
	     value.validatedFpsNum, value.validatedFpsDen, value.frameRateDivisor, value.totalFrames, value.skippedFrames, value.encodedFrames,
	     value.outputFrames, value.outputDroppedFrames, value.expectedFrames, value.minimumEncodedFrames, value.allowedSkippedFrames,
	     value.feederHealthy ? "true" : "false", value.scheduledFrames, value.submittedFrames, value.lockFailedFrames, value.lateFrames,
	     encoderError.empty() ? "none" : encoderError.c_str(), outputError.empty() ? "none" : outputError.c_str());
}

static bool isSharedHardwareInfrastructureFailure(const HardwareAttempt &attempt)
{
	if (attempt.cancelled)
		return false;
	return qualityPolicy::hardwareFailureScope(attempt.errorCode, attempt.timedOut) == qualityPolicy::HardwareFailureScope::Phase;
}

static bool isEncoderCandidateFailure(const HardwareAttempt &attempt)
{
	return !attempt.success && !attempt.cancelled &&
	       qualityPolicy::hardwareFailureScope(attempt.errorCode, attempt.timedOut) == qualityPolicy::HardwareFailureScope::Encoder;
}

static CurrentSettings hardwareCandidate(const LegRequest &leg, const EncoderDescriptor &encoder, const qualityPolicy::HardwareTier &tier)
{
	CurrentSettings target = benchmarkCeiling(leg);
	const auto fitted = qualityPolicy::fitTier({target.width, target.height, target.fpsNum, target.fpsDen}, tier.longEdge, tier.shortEdge, tier.lowerFps);
	target.width = fitted.width;
	target.height = fitted.height;
	target.fpsNum = fitted.fpsNum;
	target.fpsDen = fitted.fpsDen;
	target.encoderId = encoder.id;
	target.encoderFamily = encoder.family;
	target.encoderTitle = encoder.title;
	target.codec = "h264";
	target.presetKey = encoder.presetKey;
	target.preset = encoder.preset;
	return target;
}

static void applySharedDualOutputCadence(std::vector<CurrentSettings> &candidates)
{
	if (candidates.size() != 2)
		return;
	qualityPolicy::VideoTuple first{candidates[0].width, candidates[0].height, candidates[0].fpsNum, candidates[0].fpsDen};
	qualityPolicy::VideoTuple second{candidates[1].width, candidates[1].height, candidates[1].fpsNum, candidates[1].fpsDen};
	qualityPolicy::applySharedMinimumCadence(first, second);
	candidates[0].fpsNum = first.fpsNum;
	candidates[0].fpsDen = first.fpsDen;
	candidates[1].fpsNum = second.fpsNum;
	candidates[1].fpsDen = second.fpsDen;
}

static bool hardwareFellBelowQualityCeiling(const LegRequest &leg, const CurrentSettings &selected)
{
	const CurrentSettings ceiling = benchmarkCeiling(leg);
	return ceiling.width != selected.width || ceiling.height != selected.height || ceiling.fpsNum != selected.fpsNum || ceiling.fpsDen != selected.fpsDen;
}

static std::vector<HardwareAssessment> assessSessionHardware(const std::shared_ptr<Session> &session, const std::vector<LegRequest> &legs)
{
	std::vector<HardwareAssessment> assessments(legs.size());
	for (size_t index = 0; index < legs.size(); index++) {
		assessments[index].attempted = true;
		assessments[index].value = baseRecommendation(legs[index]);
	}
	if (legs.empty())
		return assessments;

	const auto hardware = availableHardwareEncoders(legs.front().current);
	const bool x264Available = osn::EncoderUtils::isEncoderRegistered(ADVANCED_ENCODER_X264) && isH264Encoder(ADVANCED_ENCODER_X264);
	const EncoderDescriptor x264 = describeEncoder(ADVANCED_ENCODER_X264, false);
	const auto &tiers = qualityPolicy::hardwareTiers();
	std::vector<EncoderDescriptor> plannedEncoders = hardware;
	if (x264Available)
		plannedEncoders.push_back(x264);
	size_t plannedPrimaryAttempts = 0;
	size_t plannedControlAttempts = 0;
	size_t plannedCadenceValidationAttempts = 0;
	obs_video_info mainVideoInfo{};
	const bool hasMainVideoInfo = obs_get_video_info(&mainVideoInfo);
	for (const auto &encoder : plannedEncoders) {
		std::set<std::string> workloads;
		for (const auto &tier : tiers) {
			std::string workloadKey;
			std::vector<CurrentSettings> workloadCandidates;
			for (const auto &leg : legs) {
				const CurrentSettings candidate = hardwareCandidate(leg, encoder, tier);
				workloadCandidates.push_back(candidate);
			}
			if (session->dualOutputActiveProbePair)
				applySharedDualOutputCadence(workloadCandidates);
			for (const auto &candidate : workloadCandidates)
				workloadKey += std::to_string(candidate.width) + "x" + std::to_string(candidate.height) + "@" +
					       std::to_string(candidate.fpsNum) + "/" + std::to_string(candidate.fpsDen) + ";";
			if (workloads.insert(workloadKey).second) {
				plannedPrimaryAttempts += legs.size();
				if (encoder.hardware) {
					for (const auto &candidate : workloadCandidates) {
						const int sourceFpsNum = hasMainVideoInfo ? (int)mainVideoInfo.fps_num : candidate.fpsNum;
						const int sourceFpsDen = hasMainVideoInfo ? (int)std::max(1U, mainVideoInfo.fps_den) : candidate.fpsDen;
						if (qualityPolicy::requiresExactHardwareCadenceValidation(
							    encoder.hardware, (uint32_t)std::max(0, sourceFpsNum), (uint32_t)std::max(1, sourceFpsDen),
							    (uint32_t)std::max(0, candidate.fpsNum), (uint32_t)std::max(1, candidate.fpsDen)))
							plannedCadenceValidationAttempts++;
					}
				}
			}
		}
		if (encoder.hardware) {
			// Run at most one application-mix control test for each encoder on the
			// horizontal output. Repeating an unavailable control at every quality
			// candidate could exhaust the phase deadline before testing x264.
			plannedControlAttempts += std::count_if(legs.begin(), legs.end(), [](const LegRequest &leg) { return leg.display == "horizontal"; });
		}
	}
	const size_t plannedAttempts = std::max<size_t>(1, plannedPrimaryAttempts + plannedControlAttempts + plannedCadenceValidationAttempts);
	// A failed output may require both graceful-stop and force-stop waits, and
	// each private mix can take one additional stop interval to leave the render
	// thread. Budget all three so pathological cleanup cannot consume the time
	// reserved for the x264 fallback.
	const int phaseTimeoutMs = qualityPolicy::hardwarePhaseTimeoutMs(plannedAttempts, kHardwareWarmupMs, kHardwareSampleMs, 3 * kHardwareStopTimeoutMs);
	const auto phaseDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(phaseTimeoutMs);
	blog(LOG_INFO,
	     "[Auto Optimizer][Hardware] planned_attempts=%zu primary_attempts=%zu possible_main_controls=%zu possible_cadence_validations=%zu "
	     "timeout_ms=%d",
	     plannedAttempts, plannedPrimaryAttempts, plannedControlAttempts, plannedCadenceValidationAttempts, phaseTimeoutMs);
	size_t attemptOrdinal = 0;
	auto attemptProgress = [&]() { return 15.0 + 14.0 * (double)attemptOrdinal++ / (double)plannedAttempts; };

	std::set<std::string> disabledEncoders;
	std::set<std::string> attemptedMainControls;
	std::map<std::string, std::set<std::string>> attemptedWorkloads;
	std::map<std::string, std::pair<std::string, CurrentSettings>> lastAttemptedWorkload;
	bool overloadObserved = false;

	auto tryEncoder = [&](const EncoderDescriptor &encoder, const qualityPolicy::HardwareTier &tier, std::vector<CurrentSettings> &selected,
			      bool &sharedFailure) -> bool {
		selected.clear();
		std::string workloadKey;
		for (const auto &leg : legs) {
			CurrentSettings candidate = hardwareCandidate(leg, encoder, tier);
			selected.push_back(std::move(candidate));
		}
		if (session->dualOutputActiveProbePair)
			applySharedDualOutputCadence(selected);
		for (const auto &candidate : selected)
			workloadKey += std::to_string(candidate.width) + "x" + std::to_string(candidate.height) + "@" + std::to_string(candidate.fpsNum) + "/" +
				       std::to_string(candidate.fpsDen) + ";";
		if (!attemptedWorkloads[encoder.id].insert(workloadKey).second)
			return false;

		if (session->dualOutputActiveProbePair) {
			const double jointWorkloadProgress = 15.0 + 14.0 * (double)attemptOrdinal / (double)plannedAttempts;
			pushEvent(session, "progress", "hardware", jointWorkloadProgress, "dual_output_testing_workload", {}, {}, {}, {}, 0,
				  selected.empty() ? nullptr : &selected.front());
			bool exactCadenceValidationRequired = false;
			for (size_t legIndex = 0; legIndex < legs.size(); legIndex++) {
				const CurrentSettings &candidate = selected[legIndex];
				lastAttemptedWorkload[encoder.id] = {legs[legIndex].legId, candidate};
				const bool plannedCadencePair = qualityPolicy::requiresExactHardwareCadenceValidation(
					encoder.hardware, hasMainVideoInfo ? mainVideoInfo.fps_num : 0,
					hasMainVideoInfo ? std::max(1U, mainVideoInfo.fps_den) : 1, (uint32_t)std::max(0, candidate.fpsNum),
					(uint32_t)std::max(1, candidate.fpsDen));
				exactCadenceValidationRequired = exactCadenceValidationRequired || plannedCadencePair;
				CurrentSettings surfaceCandidate = candidate;
				if (plannedCadencePair) {
					surfaceCandidate.fpsNum = (int)mainVideoInfo.fps_num;
					surfaceCandidate.fpsDen = (int)std::max(1U, mainVideoInfo.fps_den);
				}
				const char *attemptCode = !encoder.hardware    ? "hardware_testing_x264"
							  : plannedCadencePair ? "hardware_testing_encoder_surfaces"
									       : "hardware_testing_encoder";
				pushEvent(session, "progress", "hardware", attemptProgress(), attemptCode, legs[legIndex].legId, {}, {}, {}, 0,
					  plannedCadencePair ? &surfaceCandidate : &candidate);
			}

			std::vector<HardwareAttempt> attempts = runEncoderWorkloadsConcurrently(session, selected, phaseDeadline);
			for (size_t legIndex = 0; legIndex < attempts.size(); legIndex++) {
				const HardwareAttempt &attempt = attempts[legIndex];
				logHardwareAttempt(encoder, selected[legIndex], attempt);
				if (qualityPolicy::hardwareFailureIndicatesOverload(attempt.errorCode))
					overloadObserved = true;
				if (attempt.cancelled) {
					for (auto &assessment : assessments)
						assessment.cancelled = true;
					return false;
				}
				const bool concurrentInfrastructureFailure = attempt.errorCode == "hardware_benchmark_concurrent_worker_failed" ||
									     attempt.errorCode == "hardware_benchmark_concurrent_worker_launch_failed";
				if (concurrentInfrastructureFailure || isSharedHardwareInfrastructureFailure(attempt) ||
				    std::chrono::steady_clock::now() >= phaseDeadline) {
					sharedFailure = true;
					return false;
				}
				if (isEncoderCandidateFailure(attempt)) {
					disabledEncoders.insert(encoder.id);
					pushEvent(session, "progress", "hardware", 29, "hardware_encoder_rejected", legs[legIndex].legId, {}, {}, {}, 0,
						  &selected[legIndex]);
					return false;
				}
				if (!attempt.success)
					return false;
				exactCadenceValidationRequired = exactCadenceValidationRequired || attempt.sourceCadenceBelowTarget;
			}

			if (encoder.hardware && exactCadenceValidationRequired) {
				for (size_t legIndex = 0; legIndex < legs.size(); legIndex++)
					pushEvent(session, "progress", "hardware", attemptProgress(), "hardware_validating_target_cadence",
						  legs[legIndex].legId, {}, {}, {}, 0, &selected[legIndex]);
				std::vector<HardwareAttempt> cadenceAttempts =
					runEncoderWorkloadsConcurrently(session, selected, phaseDeadline, HardwareWorkloadFeed::SyntheticRawExact);
				for (size_t legIndex = 0; legIndex < cadenceAttempts.size(); legIndex++) {
					const HardwareAttempt &attempt = cadenceAttempts[legIndex];
					logHardwareAttempt(encoder, selected[legIndex], attempt);
					if (qualityPolicy::hardwareFailureIndicatesOverload(attempt.errorCode))
						overloadObserved = true;
					if (attempt.cancelled) {
						for (auto &assessment : assessments)
							assessment.cancelled = true;
						return false;
					}
					const bool cadencePhaseFailure =
						attempt.errorCode == "hardware_benchmark_concurrent_worker_failed" ||
						attempt.errorCode == "hardware_benchmark_concurrent_worker_launch_failed" ||
						qualityPolicy::exactCadenceValidationFailureScope(attempt.errorCode, attempt.timedOut) ==
							qualityPolicy::HardwareFailureScope::Phase;
					if (cadencePhaseFailure || std::chrono::steady_clock::now() >= phaseDeadline) {
						sharedFailure = true;
						return false;
					}
					if (!attempt.success) {
						pushEvent(session, "progress", "hardware", 29, "hardware_target_cadence_rejected", legs[legIndex].legId, {}, {},
							  {}, 0, &selected[legIndex]);
						return false;
					}
				}
			}
			return true;
		}

		for (size_t legIndex = 0; legIndex < legs.size(); legIndex++) {
			const CurrentSettings &candidate = selected[legIndex];
			lastAttemptedWorkload[encoder.id] = {legs[legIndex].legId, candidate};
			const double progress = attemptProgress();
			const bool plannedCadencePair = qualityPolicy::requiresExactHardwareCadenceValidation(
				encoder.hardware, hasMainVideoInfo ? mainVideoInfo.fps_num : 0, hasMainVideoInfo ? std::max(1U, mainVideoInfo.fps_den) : 1,
				(uint32_t)std::max(0, candidate.fpsNum), (uint32_t)std::max(1, candidate.fpsDen));
			CurrentSettings surfaceCandidate = candidate;
			if (plannedCadencePair) {
				surfaceCandidate.fpsNum = (int)mainVideoInfo.fps_num;
				surfaceCandidate.fpsDen = (int)std::max(1U, mainVideoInfo.fps_den);
			}
			const char *attemptCode = !encoder.hardware    ? "hardware_testing_x264"
						  : plannedCadencePair ? "hardware_testing_encoder_surfaces"
								       : "hardware_testing_encoder";
			pushEvent(session, "progress", "hardware", progress, attemptCode, legs[legIndex].legId, {}, {}, {}, 0,
				  plannedCadencePair ? &surfaceCandidate : &candidate);
			HardwareAttempt attempt = runEncoderWorkload(session, candidate, phaseDeadline);
			HardwareAttempt mainControl;
			const std::string mainControlKey = encoder.id + ":" + legs[legIndex].legId;
			const bool privatePacketFailure = attempt.errorCode == "hardware_benchmark_no_input_frames" ||
							  attempt.errorCode == "hardware_benchmark_no_encoded_packets";
			const bool mainControlAttempted = encoder.hardware && legs[legIndex].display == "horizontal" && attempt.feedMode == "private-mix" &&
							  privatePacketFailure && attemptedMainControls.insert(mainControlKey).second;
			// This control retry is diagnostic. If it succeeds, the encoder is
			// healthy and only the isolated benchmark path failed; do not report
			// overload or switch silently to x264.
			double resolvedProgress = progress;
			if (mainControlAttempted) {
				resolvedProgress = attemptProgress();
				pushEvent(session, "progress", "hardware", resolvedProgress, "hardware_validating_encoder", legs[legIndex].legId, {}, {}, {}, 0,
					  plannedCadencePair ? &surfaceCandidate : &candidate);
				mainControl = runEncoderWorkload(session, candidate, phaseDeadline, HardwareWorkloadFeed::MainControl);
			}
			logHardwareAttempt(encoder, candidate, attempt);
			if (mainControlAttempted) {
				logHardwareAttempt(encoder, candidate, mainControl);
				if (mainControl.success)
					blog(LOG_WARNING,
					     "[Auto Optimizer][Hardware] private mix produced no packets, but the known-streaming-mix control passed for encoder=%s workload=%dx%d@%d/%d; accepting the validated hardware result",
					     encoder.id.c_str(), candidate.width, candidate.height, candidate.fpsNum, candidate.fpsDen);
				// Replace the isolated benchmark result only with a conclusive
				// success, cancellation, or phase-wide infrastructure failure. If
				// the optional control is unavailable or produces no packets, keep
				// the original result so lower qualities and x264 can still be tested.
				if (qualityPolicy::shouldAdoptHardwareControl(mainControl.success, mainControl.cancelled, mainControl.errorCode,
									      mainControl.timedOut))
					attempt = std::move(mainControl);
			}
			if (encoder.hardware && attempt.success && attempt.sourceCadenceBelowTarget) {
				resolvedProgress = attemptProgress();
				pushEvent(session, "progress", "hardware", resolvedProgress, "hardware_validating_target_cadence", legs[legIndex].legId, {}, {},
					  {}, 0, &candidate);
				HardwareAttempt cadenceValidation =
					runEncoderWorkload(session, candidate, phaseDeadline, HardwareWorkloadFeed::SyntheticRawExact);
				logHardwareAttempt(encoder, candidate, cadenceValidation);
				if (qualityPolicy::hardwareFailureIndicatesOverload(cadenceValidation.errorCode))
					overloadObserved = true;
				if (cadenceValidation.cancelled) {
					for (auto &assessment : assessments)
						assessment.cancelled = true;
					return false;
				}
				// The texture-input test already proved the selected encoder works. A
				// raw-input failure rejects only this resolution and frame rate;
				// continue with lower candidates instead of rejecting the encoder
				// family or switching immediately to x264.
				const bool cadencePhaseFailure =
					qualityPolicy::exactCadenceValidationFailureScope(cadenceValidation.errorCode, cadenceValidation.timedOut) ==
					qualityPolicy::HardwareFailureScope::Phase;
				if (cadencePhaseFailure || std::chrono::steady_clock::now() >= phaseDeadline) {
					sharedFailure = true;
					return false;
				}
				if (!cadenceValidation.success) {
					pushEvent(session, "progress", "hardware", resolvedProgress, "hardware_target_cadence_rejected", legs[legIndex].legId,
						  {}, {}, {}, 0, &candidate);
					return false;
				}
			}
			if (qualityPolicy::hardwareFailureIndicatesOverload(attempt.errorCode))
				overloadObserved = true;
			if (attempt.cancelled) {
				for (auto &assessment : assessments)
					assessment.cancelled = true;
				return false;
			}
			if (isSharedHardwareInfrastructureFailure(attempt) || std::chrono::steady_clock::now() >= phaseDeadline) {
				sharedFailure = true;
				return false;
			}
			if (isEncoderCandidateFailure(attempt)) {
				disabledEncoders.insert(encoder.id);
				pushEvent(session, "progress", "hardware", resolvedProgress, "hardware_encoder_rejected", legs[legIndex].legId, {}, {}, {}, 0,
					  &candidate);
				return false;
			}
			if (!attempt.success)
				return false;
		}
		return true;
	};

	auto selectEncoder = [&](const std::vector<EncoderDescriptor> &encoders) -> bool {
		for (const auto &tier : tiers) {
			for (const auto &encoder : encoders) {
				if (disabledEncoders.count(encoder.id))
					continue;
				std::vector<CurrentSettings> selected;
				bool sharedFailure = false;
				if (!tryEncoder(encoder, tier, selected, sharedFailure)) {
					if (sharedFailure) {
						for (auto &assessment : assessments) {
							assessment.passed = false;
							assessment.fatal = true;
							assessment.constrained = true;
							assessment.reason = std::chrono::steady_clock::now() >= phaseDeadline
										    ? "hardware_benchmark_timeout"
										    : "hardware_benchmark_unavailable";
						}
						return true;
					}
					if (std::any_of(assessments.begin(), assessments.end(), [](const HardwareAssessment &item) { return item.cancelled; }))
						return true;
					continue;
				}

				if (session->dualOutputActiveProbePair)
					session->concurrentHardwareValidated = true;
				for (size_t index = 0; index < assessments.size(); index++) {
					assessments[index].passed = true;
					assessments[index].value = selected[index];
					const bool softwareFallback = !encoder.hardware &&
								      resolveEncoderId(legs[index].current.encoderId) != ADVANCED_ENCODER_X264;
					assessments[index].constrained = hardwareFellBelowQualityCeiling(legs[index], selected[index]) || softwareFallback;
					if (hardwareFellBelowQualityCeiling(legs[index], selected[index]))
						assessments[index].reason = "hardware_benchmark_quality_fallback";
					else if (softwareFallback)
						assessments[index].reason = "hardware_benchmark_encoder_fallback";
					pushEvent(session, "progress", "hardware", 30, "hardware_encoder_selected", legs[index].legId, {}, {}, {}, 0,
						  &selected[index]);
				}
				return true;
			}
		}
		return false;
	};

	if (selectEncoder(hardware))
		return assessments;
	const double rejectionProgress = std::min(29.0, 15.0 + 14.0 * (double)attemptOrdinal / (double)plannedAttempts);
	for (const auto &encoder : hardware) {
		const auto attempted = lastAttemptedWorkload.find(encoder.id);
		if (disabledEncoders.count(encoder.id) || attempted == lastAttemptedWorkload.end())
			continue;
		pushEvent(session, "progress", "hardware", rejectionProgress, "hardware_encoder_rejected", attempted->second.first, {}, {}, {}, 0,
			  &attempted->second.second);
	}
	if (x264Available && selectEncoder({x264}))
		return assessments;

	for (auto &assessment : assessments) {
		assessment.passed = false;
		assessment.fatal = true;
		assessment.constrained = true;
		assessment.reason = qualityPolicy::hardwareFailureCode(std::chrono::steady_clock::now() >= phaseDeadline, overloadObserved);
	}
	pushEvent(session, "progress", "hardware", 30, assessments.front().reason, legs.front().legId);
	return assessments;
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

struct TwitchProbeSample {
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
	bool sustainedCongestion = false;
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

static const char *youtubeLoadResultName(probePolicy::YoutubeProbeLoadResult loadResult)
{
	switch (loadResult) {
	case probePolicy::YoutubeProbeLoadResult::Accepted:
		return "accepted";
	case probePolicy::YoutubeProbeLoadResult::SourceUnderfill:
		return "source_underfill";
	case probePolicy::YoutubeProbeLoadResult::TransportPressure:
		return "transport_pressure";
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

static const char *youtubeExtendedValidationDecisionName(probePolicy::YoutubeExtendedValidationDecision decision)
{
	switch (decision) {
	case probePolicy::YoutubeExtendedValidationDecision::TargetAccepted:
		return "target_accepted";
	case probePolicy::YoutubeExtendedValidationDecision::SourceUnderfill:
		return "source_underfill";
	case probePolicy::YoutubeExtendedValidationDecision::CapacityKnee:
		return "capacity_knee";
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

static bool runTwitchProbeSample(const std::shared_ptr<Session> &session, ScratchResources &resources, const ProbeRequest &probe, int targetVideoKbps,
				 int durationMs, double progress, const char *eventCode, TwitchProbeSample &sample, ProbeResult &result)
{
	pushEvent(session, "progress", "bandwidth", progress, eventCode, probe.legId, "active", probe.probeId, probe.provider,
		  (uint32_t)std::max(0, targetVideoKbps));
	blog(LOG_INFO, "[Auto Optimizer][Twitch Probe] Starting sample: purpose=%s, video_target=%d Kbps, duration=%d ms", eventCode, targetVideoKbps,
	     durationMs);

	const uint64_t startBytes = obs_output_get_total_bytes(resources.output);
	const uint32_t startDropped = obs_output_get_frames_dropped(resources.output);
	const uint32_t startFrames = obs_output_get_total_frames(resources.output);
	const auto sampleStart = std::chrono::steady_clock::now();
	const auto sampleDeadline = sampleStart + std::chrono::milliseconds(durationMs);
	auto subwindowStart = sampleStart;
	auto nextSubwindow = sampleStart + std::chrono::milliseconds(kProbeSubwindowMs);
	uint64_t subwindowStartBytes = startBytes;
	std::vector<uint64_t> subwindowThroughputs;
	std::vector<float> congestionValues;

	while (std::chrono::steady_clock::now() < sampleDeadline && obs_output_get_total_bytes(resources.output) - startBytes < kProbeMaxBytes) {
		const float congestion = obs_output_get_congestion(resources.output);
		congestionValues.push_back(congestion);
		const auto tickDeadline = std::min(std::chrono::steady_clock::now() + std::chrono::milliseconds(50), sampleDeadline);
		if (!waitForProbeInterval(session, resources.output, tickDeadline, result)) {
			blog(LOG_WARNING, "[Auto Optimizer][Twitch Probe] Sample stopped during measurement: purpose=%s, reason=%s", eventCode,
			     result.errorCode.empty() ? (result.cancelled ? "cancelled" : "unknown") : result.errorCode.c_str());
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
			nextSubwindow += std::chrono::milliseconds(kProbeSubwindowMs);
		}
	}

	const auto sampleEnd = std::chrono::steady_clock::now();
	const uint64_t endBytes = obs_output_get_total_bytes(resources.output);
	const uint32_t endDropped = obs_output_get_frames_dropped(resources.output);
	const uint32_t endFrames = obs_output_get_total_frames(resources.output);
	const uint64_t elapsedNs = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(sampleEnd - sampleStart).count();
	if (endBytes <= startBytes || elapsedNs == 0 || subwindowThroughputs.empty()) {
		result.errorCode = "twitch_probe_no_data";
		return false;
	}

	sample.medianSubwindowAggregateKbps = medianValue(subwindowThroughputs);
	sample.wholeWindowAggregateKbps = (endBytes - startBytes) * 8ULL * 1000000000ULL / elapsedNs / 1000ULL;
	sample.measuredAggregateKbps = std::min(sample.medianSubwindowAggregateKbps, sample.wholeWindowAggregateKbps);
	sample.sampleBytes = endBytes - startBytes;
	sample.frames = endFrames >= startFrames ? endFrames - startFrames : 0;
	sample.droppedFrames = endDropped >= startDropped ? endDropped - startDropped : 0;
	sample.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(sampleEnd - sampleStart).count();
	sample.congestionSamples = (uint32_t)congestionValues.size();
	for (float congestion : congestionValues) {
		sample.maximumCongestion = std::max(sample.maximumCongestion, congestion);
		if (congestion >= kProbeCongestionHigh)
			sample.congestionHighSamples++;
		if (congestion >= kProbeCongestionSevere)
			sample.congestionSevereSamples++;
	}
	std::sort(congestionValues.begin(), congestionValues.end());
	if (!congestionValues.empty()) {
		const size_t percentileIndex = std::min(congestionValues.size() - 1, (congestionValues.size() * 95 + 99) / 100 - 1);
		sample.p95Congestion = congestionValues[percentileIndex];
	}
	sample.sustainedCongestion =
		probePolicy::twitchCongestionIsSustained(sample.congestionHighSamples, sample.congestionSevereSamples, sample.congestionSamples);

	blog(LOG_INFO,
	     "[Auto Optimizer][Twitch Probe] Sample result: purpose=%s, video_target=%d Kbps, representative_aggregate=%llu Kbps, "
	     "median_subwindow_aggregate=%llu Kbps, whole_window_aggregate=%llu Kbps, elapsed=%lld ms, sample_bytes=%llu, "
	     "frames=%u, dropped=%u, max_congestion=%.3f, p95_congestion=%.3f, congestion_high=%u/%u, congestion_severe=%u/%u, "
	     "sustained_congestion=%s",
	     eventCode, targetVideoKbps, (unsigned long long)sample.measuredAggregateKbps, (unsigned long long)sample.medianSubwindowAggregateKbps,
	     (unsigned long long)sample.wholeWindowAggregateKbps, sample.elapsedMs, (unsigned long long)sample.sampleBytes, (unsigned int)sample.frames,
	     (unsigned int)sample.droppedFrames, (double)sample.maximumCongestion, (double)sample.p95Congestion, (unsigned int)sample.congestionHighSamples,
	     (unsigned int)sample.congestionSamples, (unsigned int)sample.congestionSevereSamples, (unsigned int)sample.congestionSamples,
	     sample.sustainedCongestion ? "true" : "false");
	return true;
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

static int applyYoutubeProbeTarget(ScratchResources &resources, int requestedTarget, int &activeTarget)
{
	int target = requestedTarget;
	if (target == activeTarget)
		return target;

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
	return target;
}

static bool waitForYoutubeRecoveryDrain(const std::shared_ptr<Session> &session, ScratchResources &resources, const ProbeRequest &probe, int recoveryTarget,
					int &activeTarget, std::chrono::steady_clock::time_point youtubeDeadline, uint64_t budgetStartBytes,
					uint64_t &totalProbeBytes, double progress, ProbeResult &result)
{
	const int target = applyYoutubeProbeTarget(resources, recoveryTarget, activeTarget);
	if (target <= 0) {
		result.errorCode = "youtube_probe_invalid_applied_target";
		return false;
	}

	pushEvent(session, "progress", "bandwidth", progress, "youtube_probe_confirming_stability", probe.legId, "active", probe.probeId, probe.provider,
		  (uint32_t)target);
	const auto recoveryStarted = std::chrono::steady_clock::now();
	const auto recoveryDeadline = std::min(recoveryStarted + std::chrono::milliseconds(kYoutubeProbeRecoveryMaximumMs), youtubeDeadline);
	if (recoveryStarted + std::chrono::milliseconds(kYoutubeProbeRecoveryPollMs * kYoutubeProbeRecoveryHealthySamples) > recoveryDeadline) {
		result.errorCode = "youtube_probe_recovery_deadline_exhausted";
		return false;
	}

	blog(LOG_INFO, "[Auto Optimizer][YouTube Probe] Starting recovery drain: video_target=%d Kbps, maximum=%d ms, healthy_window=%d ms", target,
	     kYoutubeProbeRecoveryMaximumMs, kYoutubeProbeRecoveryPollMs * kYoutubeProbeRecoveryHealthySamples);
	probePolicy::YoutubeRecoveryGate recoveryGate(kYoutubeProbeRecoveryHealthySamples);
	const uint32_t startDropped = obs_output_get_frames_dropped(resources.output);
	uint32_t previousDropped = startDropped;
	float lastCongestion = obs_output_get_congestion(resources.output);
	float maximumCongestion = lastCongestion;
	bool recovered = false;

	while (std::chrono::steady_clock::now() < recoveryDeadline) {
		const auto tickDeadline = std::min(std::chrono::steady_clock::now() + std::chrono::milliseconds(kYoutubeProbeRecoveryPollMs), recoveryDeadline);
		if (!waitForProbeInterval(session, resources.output, tickDeadline, result))
			return false;

		lastCongestion = obs_output_get_congestion(resources.output);
		maximumCongestion = std::max(maximumCongestion, lastCongestion);
		const uint32_t currentDropped = obs_output_get_frames_dropped(resources.output);
		const bool droppedFramesUnchanged = currentDropped == previousDropped;
		previousDropped = currentDropped;
		totalProbeBytes = youtubeProbeBytesUsed(resources.output, budgetStartBytes);
		if (totalProbeBytes > kYoutubeProbeMaxBytes) {
			result.errorCode = "youtube_probe_byte_budget_exhausted";
			return false;
		}

		if (recoveryGate.observe(lastCongestion < kProbeCongestionHigh, droppedFramesUnchanged)) {
			recovered = true;
			break;
		}
	}

	const auto recoveryEnded = std::chrono::steady_clock::now();
	const uint32_t endDropped = obs_output_get_frames_dropped(resources.output);
	const uint32_t droppedDelta = endDropped >= startDropped ? endDropped - startDropped : 0;
	const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(recoveryEnded - recoveryStarted).count();
	blog(recovered ? LOG_INFO : LOG_WARNING,
	     "[Auto Optimizer][YouTube Probe] Recovery drain result: recovered=%s, video_target=%d Kbps, elapsed=%lld ms, "
	     "healthy_samples=%u/%u, dropped=%u, last_congestion=%.3f, max_congestion=%.3f",
	     recovered ? "true" : "false", target, (long long)elapsedMs, (unsigned int)recoveryGate.consecutiveHealthySamples,
	     (unsigned int)recoveryGate.requiredHealthySamples, (unsigned int)droppedDelta, (double)lastCongestion, (double)maximumCongestion);
	if (!recovered) {
		result.stability = ProbeStability::Unstable;
		result.observedThroughputReliable = false;
		result.errorCode = "youtube_probe_recovery_timeout";
	}
	return recovered;
}

static bool youtubeSampleGroupFits(std::chrono::steady_clock::time_point deadline, uint64_t totalProbeBytes, int activeTarget, const std::vector<int> &targets,
				   const std::vector<std::pair<int, int>> &additionalSegments = {})
{
	long long requiredMs = kYoutubeProbeBudgetSlackMs;
	uint64_t requiredBytes = 0;
	for (const auto &[target, durationMs] : additionalSegments) {
		if (target <= 0 || durationMs <= 0)
			return false;
		requiredMs += durationMs;
		requiredBytes += estimatedYoutubeSampleBytes(target, durationMs);
	}
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
				  uint64_t &totalProbeBytes, double progress, const char *eventCode, YoutubeProbeSample &sample, ProbeResult &result,
				  int sampleDurationMs = kYoutubeProbeSampleMs)
{
	if (sampleDurationMs <= 0) {
		result.errorCode = "youtube_probe_invalid_sample_duration";
		return false;
	}
	totalProbeBytes = youtubeProbeBytesUsed(resources.output, budgetStartBytes);
	if (totalProbeBytes >= kYoutubeProbeMaxBytes) {
		result.errorCode = "youtube_probe_byte_budget_exhausted";
		return false;
	}

	int target = requestedTarget;
	const bool targetChanged = target != activeTarget;
	if (targetChanged)
		target = applyYoutubeProbeTarget(resources, target, activeTarget);

	if (target <= 0) {
		result.errorCode = "youtube_probe_invalid_applied_target";
		return false;
	}

	pushEvent(session, "progress", "bandwidth", progress, eventCode, probe.legId, "active", probe.probeId, probe.provider, (uint32_t)target);
	blog(LOG_INFO,
	     "[Auto Optimizer][YouTube Probe] Starting sample: purpose=%s, requested_video_target=%d Kbps, "
	     "applied_video_target=%d Kbps, duration=%d ms",
	     eventCode, requestedTarget, target, sampleDurationMs);

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
	const auto sampleDeadline = sampleStart + std::chrono::milliseconds(sampleDurationMs);
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
	// window catches multi-second stalls. The conservative value feeds the
	// recommendation and source-underfill diagnostic. Transport classification
	// uses only explicit drop/congestion evidence.
	sample.measuredAggregateKbps = std::min(sample.medianSubwindowAggregateKbps, sample.wholeWindowAggregateKbps);
	sample.sampleBytes = endBytes - startBytes;
	sample.frames = endFrames >= startFrames ? endFrames - startFrames : 0;
	sample.droppedFrames = endDropped >= startDropped ? endDropped - startDropped : 0;
	sample.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(sampleEnd - sampleStart).count();
	sample.congestionSamples = (uint32_t)congestionValues.size();
	for (float congestion : congestionValues) {
		sample.maximumCongestion = std::max(sample.maximumCongestion, congestion);
		if (congestion >= kProbeCongestionHigh)
			sample.congestionHighSamples++;
		if (congestion >= kProbeCongestionSevere)
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

	const probePolicy::YoutubeProbeSampleClass transportClass = probePolicy::classifyYoutubeProbeTransport(sample.metrics);
	blog(LOG_INFO,
	     "[Auto Optimizer][YouTube Probe] Sample result: purpose=%s, video_target=%d Kbps, expected_aggregate=%llu Kbps, "
	     "representative_aggregate=%llu Kbps, median_subwindow_aggregate=%llu Kbps, whole_window_aggregate=%llu Kbps, "
	     "elapsed=%lld ms, sample_bytes=%llu, frames=%u, "
	     "dropped=%u, max_congestion=%.3f, p95_congestion=%.3f, congestion_high=%u/%u, congestion_severe=%u/%u, "
	     "throughput_ratio=%.2f%%, throughput_at_target=%s, drop_ratio=%.2f%%, congestion_high_ratio=%.2f%%, "
	     "congestion_severe_ratio=%.2f%%, transport_class=%s",
	     eventCode, target, (unsigned long long)sample.expectedAggregateKbps, (unsigned long long)sample.measuredAggregateKbps,
	     (unsigned long long)sample.medianSubwindowAggregateKbps, (unsigned long long)sample.wholeWindowAggregateKbps, sample.elapsedMs,
	     (unsigned long long)sample.sampleBytes, (unsigned int)sample.frames, (unsigned int)sample.droppedFrames, (double)sample.maximumCongestion,
	     (double)sample.p95Congestion, (unsigned int)sample.congestionHighSamples, (unsigned int)sample.congestionSamples,
	     (unsigned int)sample.congestionSevereSamples, (unsigned int)sample.congestionSamples, (double)sample.metrics.throughputBasisPoints / 100.0,
	     probePolicy::youtubeThroughputAtTarget(sample.metrics) ? "true" : "false", (double)sample.metrics.dropBasisPoints / 100.0,
	     (double)sample.metrics.congestionHighBasisPoints / 100.0, (double)sample.metrics.congestionSevereBasisPoints / 100.0,
	     youtubeSampleClassName(transportClass));
	return true;
}

struct EnhancedBroadcastingAttempt {
	bool success = false;
	bool cancelled = false;
	uint32_t videoTrackCount = 0;
	uint64_t configuredAggregateBitrateKbps = 0;
	uint64_t outputBytes = 0;
	uint32_t outputDroppedFrames = 0;
	uint32_t inputFrames = 0;
	uint32_t inputSkippedFrames = 0;
	float maximumCongestion = 0.0f;
	std::vector<uint32_t> encodedFrames;
	std::vector<uint32_t> minimumEncodedFrames;
	std::string errorCode;
};

static uint64_t configuredBitrateKbps(const nlohmann::json &settings)
{
	const auto bitrate = settings.find("bitrate");
	if (bitrate == settings.end() || (!bitrate->is_number_integer() && !bitrate->is_number_unsigned()))
		return 0;
	uint64_t value = 0;
	if (bitrate->is_number_unsigned()) {
		value = bitrate->get<uint64_t>();
	} else {
		const int64_t signedValue = bitrate->get<int64_t>();
		if (signedValue <= 0)
			return 0;
		value = (uint64_t)signedValue;
	}
	return value <= kEnhancedBroadcastingMaximumTrackBitrateKbps ? value : 0;
}

static uint64_t configuredAggregateBitrateKbps(const osn::Config &config)
{
	uint64_t result = 0;
	for (const auto &video : config.encoder_configurations)
		result += configuredBitrateKbps(video.settings);
	for (const auto &audio : config.audio_configurations.live)
		result += configuredBitrateKbps(audio.settings);
	return result;
}

static bool validateEnhancedBroadcastingConfig(const osn::Config &config, const std::vector<enhancedBroadcastingPolicy::VideoCandidate> &candidates,
					       std::string &errorCode)
{
	if (candidates.empty() || candidates.size() > 2) {
		errorCode = "enhanced_broadcasting_invalid_canvas_topology";
		return false;
	}
	if (config.encoder_configurations.empty() || config.encoder_configurations.size() > MAX_OUTPUT_VIDEO_ENCODERS) {
		errorCode = "enhanced_broadcasting_invalid_video_ladder";
		return false;
	}
	if (config.audio_configurations.live.empty() || config.audio_configurations.live.size() > MAX_OUTPUT_AUDIO_ENCODERS) {
		errorCode = "enhanced_broadcasting_invalid_audio_ladder";
		return false;
	}
	for (const auto &audio : config.audio_configurations.live) {
		if (audio.channels == 0 || configuredBitrateKbps(audio.settings) == 0) {
			errorCode = "enhanced_broadcasting_invalid_audio_ladder";
			return false;
		}
	}
	std::vector<bool> candidateCovered(candidates.size(), false);
	for (const auto &video : config.encoder_configurations) {
		const char *codec = obs_get_encoder_codec(video.type.c_str());
		if (!enhancedBroadcastingPolicy::canvasIndexIsValid(video.canvas_index, candidates.size()) || !codec || asciiLowerCopy(codec) != "h264") {
			errorCode = "enhanced_broadcasting_unsupported_video_ladder";
			return false;
		}
		const auto &candidate = candidates[video.canvas_index];
		if (video.width == 0 || video.height == 0 || configuredBitrateKbps(video.settings) == 0) {
			errorCode = "enhanced_broadcasting_invalid_video_ladder";
			return false;
		}
		if (video.framerate && (video.framerate->numerator == 0 || video.framerate->denominator == 0)) {
			errorCode = "enhanced_broadcasting_invalid_video_ladder";
			return false;
		}
		const auto cadence = video.framerate.value_or(media_frames_per_second{candidate.fpsNum, candidate.fpsDen});
		if (enhancedBroadcastingPolicy::renditionExceedsCandidate(candidate, video.width, video.height, cadence.numerator, cadence.denominator)) {
			errorCode = "enhanced_broadcasting_ladder_exceeds_candidate";
			return false;
		}
		candidateCovered[video.canvas_index] =
			candidateCovered[video.canvas_index] ||
			enhancedBroadcastingPolicy::renditionCoversCandidate(candidate, video.width, video.height, cadence.numerator, cadence.denominator);
	}
	if (!enhancedBroadcastingPolicy::everyCanvasCovered(candidateCovered)) {
		errorCode = "enhanced_broadcasting_ladder_below_candidate";
		return false;
	}
	return true;
}

static media_frames_per_second effectiveEncoderCadence(uint32_t sourceFpsNum, uint32_t sourceFpsDen, uint32_t targetFpsNum, uint32_t targetFpsDen)
{
	media_frames_per_second result{targetFpsNum, std::max(1U, targetFpsDen)};
	if ((uint64_t)result.numerator * sourceFpsDen > (uint64_t)sourceFpsNum * result.denominator)
		result = {sourceFpsNum, std::max(1U, sourceFpsDen)};
	return result;
}

static media_frames_per_second effectiveTrackCadence(const osn::VideoEncoderConfiguration &configuration, uint32_t sourceFpsNum, uint32_t sourceFpsDen)
{
	const auto requested = configuration.framerate.value_or(media_frames_per_second{sourceFpsNum, std::max(1U, sourceFpsDen)});
	return effectiveEncoderCadence(sourceFpsNum, sourceFpsDen, requested.numerator, requested.denominator);
}

static bool runEnhancedBroadcastingOutputAttempt(const std::shared_ptr<Session> &session, const ProbeRequest &probe, const osn::Config &config,
						 const std::string &normalizedKey, const std::vector<enhancedBroadcastingPolicy::VideoCandidate> &candidates,
						 const std::vector<uint64_t> &canvasIds, uint32_t sourceFpsNum, uint32_t sourceFpsDen,
						 bool usePrivateTextureMix, const std::vector<CompanionWorkload> &companionWorkloads,
						 EnhancedBroadcastingAttempt &attempt)
{
	if (candidates.empty() || candidates.size() != canvasIds.size() || candidates.size() > 2) {
		attempt.errorCode = "enhanced_broadcasting_invalid_canvas_topology";
		return false;
	}
	// Declare the secondary canvas resources first so C++ destroys the primary
	// resources, and therefore the shared output, before them on every return
	// path.
	std::unique_ptr<ScratchResources> additionalResources;
	ScratchResources resources(*session);
	std::vector<std::unique_ptr<ScratchResources>> companionResources;
	attempt.videoTrackCount = (uint32_t)config.encoder_configurations.size();
	attempt.configuredAggregateBitrateKbps = configuredAggregateBitrateKbps(config);
	std::vector<obs_core_video_mix_t *> identitySourceMixes(candidates.size(), nullptr);
	if (usePrivateTextureMix) {
		const bool multipleRendering = obs_get_multiple_rendering();
		const obs_video_rendering_mode identityMode = videoMix::activeRenderingMode(multipleRendering);
		for (size_t index = 0; index < canvasIds.size(); index++) {
			obs_video_info *canvasIdentity = osn::Video::Manager::GetInstance().find(canvasIds[index]);
			if (canvasIdentity)
				identitySourceMixes[index] = obs_video_mix_get(canvasIdentity, identityMode);
			blog(LOG_INFO,
			     "[Auto Optimizer][Enhanced Broadcasting] Auxiliary mix identity %zu: canvas_id=%llu, mode=%s, "
			     "multiple_rendering=%s, available=%s",
			     index, (unsigned long long)canvasIds[index], identityMode == OBS_STREAMING_VIDEO_RENDERING ? "streaming" : "main",
			     multipleRendering ? "true" : "false", identitySourceMixes[index] ? "true" : "false");
		}
	}
	if (usePrivateTextureMix && std::any_of(identitySourceMixes.begin(), identitySourceMixes.end(), [](obs_core_video_mix_t *mix) { return !mix; })) {
		attempt.errorCode = "enhanced_broadcasting_canvas_identity_unavailable";
		return false;
	}
	if (!resources.createSyntheticVideo(candidates[0].width, candidates[0].height, sourceFpsNum, sourceFpsDen, usePrivateTextureMix, usePrivateTextureMix,
					    identitySourceMixes[0])) {
		attempt.errorCode = "enhanced_broadcasting_video_create_failed";
		return false;
	}
	if (usePrivateTextureMix) {
		sourceFpsNum = resources.videoFpsNum;
		sourceFpsDen = resources.videoFpsDen;
	}
	if (candidates.size() == 2) {
		additionalResources = std::make_unique<ScratchResources>(*session);
		if (!additionalResources->createSyntheticVideo(candidates[1].width, candidates[1].height, sourceFpsNum, sourceFpsDen, usePrivateTextureMix,
							       usePrivateTextureMix, identitySourceMixes[1])) {
			attempt.errorCode = "enhanced_broadcasting_additional_video_create_failed";
			return false;
		}
		if (usePrivateTextureMix &&
		    (uint64_t)additionalResources->videoFpsNum * sourceFpsDen != (uint64_t)sourceFpsNum * additionalResources->videoFpsDen) {
			attempt.errorCode = "enhanced_broadcasting_canvas_cadence_mismatch";
			return false;
		}
	}
	if (!usePrivateTextureMix)
		resources.startFeeder();
	if (!usePrivateTextureMix && additionalResources)
		additionalResources->startFeeder();
	if (!resources.createSyntheticAudio(true)) {
		attempt.errorCode = "enhanced_broadcasting_audio_create_failed";
		return false;
	}

	obs_video_info rawCanvas{};
	obs_video_info rawAdditionalCanvas{};
	auto configureRawCanvas = [&](obs_video_info &canvas, const enhancedBroadcastingPolicy::VideoCandidate &candidate) {
		canvas.base_width = candidate.width;
		canvas.base_height = candidate.height;
		canvas.output_width = candidate.width;
		canvas.output_height = candidate.height;
		canvas.fps_num = sourceFpsNum;
		canvas.fps_den = std::max(1U, sourceFpsDen);
		canvas.fps_type = 1;
		canvas.output_format = VIDEO_FORMAT_NV12;
		canvas.colorspace = VIDEO_CS_709;
		canvas.range = VIDEO_RANGE_PARTIAL;
		canvas.scale_type = OBS_SCALE_BILINEAR;
		canvas.adapter = 0;
		canvas.gpu_conversion = usePrivateTextureMix;
	};
	configureRawCanvas(rawCanvas, candidates[0]);
	std::vector<obs_video_info *> canvases{usePrivateTextureMix ? resources.scratchViewInfo.get() : &rawCanvas};
	if (additionalResources) {
		configureRawCanvas(rawAdditionalCanvas, candidates[1]);
		canvases.push_back(usePrivateTextureMix ? additionalResources->scratchViewInfo.get() : &rawAdditionalCanvas);
	}
	std::vector<uint32_t> trackFrameRateDivisors;
	trackFrameRateDivisors.reserve(config.encoder_configurations.size());
	for (const auto &configuration : config.encoder_configurations) {
		const auto cadence = effectiveTrackCadence(configuration, sourceFpsNum, sourceFpsDen);
		const auto divisor = qualityPolicy::frameRateDivisor(sourceFpsNum, sourceFpsDen, cadence.numerator, cadence.denominator);
		if (!divisor.supported) {
			attempt.errorCode = "enhanced_broadcasting_frame_rate_unsupported";
			return false;
		}
		trackFrameRateDivisors.push_back(divisor.value);
	}

	const int audioBitrate = osn::GetMultitrackAudioBitrate();
	const char *audioEncoderId = osn::GetSimpleAACEncoderForBitrate(audioBitrate);
	if (!audioEncoderId) {
		attempt.errorCode = "enhanced_broadcasting_audio_encoder_unavailable";
		return false;
	}

	OBSOutputAutoRelease output;
	try {
		output = osn::SetupOBSOutput("Auto Optimizer Enhanced Broadcasting", config, resources.multitrackAudioEncoders,
					     resources.multitrackVideoEncoderGroup, audioEncoderId, 0, std::nullopt, canvases, true);
	} catch (const std::exception &exception) {
		blog(LOG_WARNING, "[Auto Optimizer][Enhanced Broadcasting] Output setup failed: %s", boundedLogValue(exception.what()).c_str());
		attempt.errorCode = "enhanced_broadcasting_output_setup_failed";
		return false;
	}
	if (!output) {
		attempt.errorCode = "enhanced_broadcasting_output_setup_failed";
		return false;
	}
	resources.output = obs_output_get_ref(output);
	if (!resources.output) {
		attempt.errorCode = "enhanced_broadcasting_output_setup_failed";
		return false;
	}

	std::vector<bool> canvasInputsBound(candidates.size(), false);
	for (size_t index = 0; index < config.encoder_configurations.size(); index++) {
		obs_encoder_t *encoder = obs_output_get_video_encoder2(resources.output, index);
		if (!encoder) {
			attempt.errorCode = "enhanced_broadcasting_video_encoder_missing";
			return false;
		}
		const size_t canvasIndex = config.encoder_configurations[index].canvas_index;
		ScratchResources *videoResources = canvasIndex == 0 ? &resources : additionalResources.get();
		if (!videoResources) {
			attempt.errorCode = "enhanced_broadcasting_video_canvas_missing";
			return false;
		}
		if (usePrivateTextureMix)
			obs_encoder_set_video_mix(encoder, videoResources->scratchMix);
		else
			obs_encoder_set_video(encoder, videoResources->syntheticVideo);
		if (obs_encoder_parent_video(encoder) != videoResources->syntheticVideo) {
			attempt.errorCode = "enhanced_broadcasting_video_canvas_binding_failed";
			return false;
		}
		if (obs_encoder_get_frame_rate_divisor(encoder) != trackFrameRateDivisors[index]) {
			attempt.errorCode = "enhanced_broadcasting_frame_rate_divisor_failed";
			return false;
		}
		canvasInputsBound[canvasIndex] = true;
	}
	if (!enhancedBroadcastingPolicy::everyCanvasCovered(canvasInputsBound)) {
		attempt.errorCode = "enhanced_broadcasting_video_canvas_missing";
		return false;
	}

	struct CompanionSample {
		const CompanionWorkload *workload = nullptr;
		ScratchResources *resources = nullptr;
		media_frames_per_second cadence{0, 1};
		uint32_t encodedStart = 0;
		uint32_t outputFramesStart = 0;
		uint32_t droppedStart = 0;
	};
	std::vector<CompanionSample> companionSamples;
	companionResources.reserve(companionWorkloads.size());
	companionSamples.reserve(companionWorkloads.size());
	for (const auto &workload : companionWorkloads) {
		const size_t canvasIndex = workload.display == "horizontal" ? 0 : 1;
		ScratchResources *videoResources = canvasIndex == 0 ? &resources : additionalResources.get();
		if (!videoResources) {
			attempt.errorCode = "enhanced_broadcasting_companion_canvas_missing";
			return false;
		}
		const std::string resolvedEncoderId = resolveEncoderId(workload.value.encoderId);
		const bool useTextureEncoder = usePrivateTextureMix && resolvedEncoderId != ADVANCED_ENCODER_X264;
		const std::string encoderId = useTextureEncoder ? resolvedEncoderId : scratchEncoderId(workload.value.encoderId);
		if (encoderId.empty() || !obs_get_encoder_codec(encoderId.c_str())) {
			attempt.errorCode = "enhanced_broadcasting_companion_encoder_unavailable";
			return false;
		}

		auto companion = std::make_unique<ScratchResources>(*session, kHardwareStopTimeoutMs);
		OBSDataAutoRelease encoderSettings = obs_data_create();
		// Desktop validates the result against the exact concurrent workload. Test
		// the bitrate Desktop will apply; a lower internal test cap would
		// understate the live encoder load.
		obs_data_set_int(encoderSettings, "bitrate", workload.value.bitrateKbps);
		obs_data_set_string(encoderSettings, "rate_control", "CBR");
		obs_data_set_int(encoderSettings, "keyint_sec", 2);
		if (!workload.value.presetKey.empty() && !workload.value.preset.empty() &&
		    (resolvedEncoderId != ADVANCED_ENCODER_X264 || isX264Preset(workload.value.preset)))
			obs_data_set_string(encoderSettings, workload.value.presetKey.c_str(), workload.value.preset.c_str());
		const std::string encoderName = "auto_optimizer_enhanced_broadcasting_companion_encoder_" + workload.legId;
		companion->videoEncoder = obs_video_encoder_create(encoderId.c_str(), encoderName.c_str(), encoderSettings, nullptr);
		if (!companion->videoEncoder) {
			attempt.errorCode = "enhanced_broadcasting_companion_encoder_create_failed";
			return false;
		}
		OBSDataAutoRelease effectiveEncoderSettings = obs_encoder_get_settings(companion->videoEncoder);
		if (!effectiveEncoderSettings || obs_data_get_int(effectiveEncoderSettings, "bitrate") != workload.value.bitrateKbps) {
			attempt.errorCode = "enhanced_broadcasting_companion_encoder_settings_mismatch";
			return false;
		}
		if (useTextureEncoder)
			obs_encoder_set_video_mix(companion->videoEncoder, videoResources->scratchMix);
		else
			obs_encoder_set_video(companion->videoEncoder, videoResources->syntheticVideo);
		if (obs_encoder_parent_video(companion->videoEncoder) != videoResources->syntheticVideo) {
			attempt.errorCode = "enhanced_broadcasting_companion_canvas_binding_failed";
			return false;
		}
		const auto cadence = effectiveEncoderCadence(sourceFpsNum, sourceFpsDen, (uint32_t)std::max(1, workload.value.fpsNum),
							     (uint32_t)std::max(1, workload.value.fpsDen));
		const auto divisor = qualityPolicy::frameRateDivisor(sourceFpsNum, sourceFpsDen, cadence.numerator, cadence.denominator);
		if (!divisor.supported) {
			attempt.errorCode = "enhanced_broadcasting_companion_frame_rate_unsupported";
			return false;
		}
		if (divisor.value > 1 && !obs_encoder_set_frame_rate_divisor(companion->videoEncoder, divisor.value)) {
			attempt.errorCode = "enhanced_broadcasting_companion_frame_rate_divisor_failed";
			return false;
		}
		const std::string outputName = "auto_optimizer_enhanced_broadcasting_companion_output_" + workload.legId;
		companion->output = obs_output_create(kHardwareBenchmarkOutputId, outputName.c_str(), nullptr, nullptr);
		if (!companion->output) {
			attempt.errorCode = "enhanced_broadcasting_companion_output_create_failed";
			return false;
		}
		obs_output_set_video_encoder(companion->output, companion->videoEncoder);
		companion->publishOutput();
		companionSamples.push_back({&workload, companion.get(), cadence});
		companionResources.push_back(std::move(companion));
	}

	for (const auto &encoder : resources.multitrackAudioEncoders) {
		if (!encoder) {
			attempt.errorCode = "enhanced_broadcasting_audio_encoder_missing";
			return false;
		}
		// Never send the user's microphone or desktop audio during a bandwidth
		// test. Generate non-silent audio used only by the probe so the output
		// interleaver can establish A/V timing before releasing video packets.
		obs_encoder_set_audio(encoder, resources.syntheticAudio);
	}

	OBSServiceAutoRelease service;
	try {
		service = osn::create_service(config, std::nullopt, normalizedKey);
	} catch (const std::exception &exception) {
		blog(LOG_WARNING, "[Auto Optimizer][Enhanced Broadcasting] Service setup failed: %s", boundedLogValue(exception.what()).c_str());
		attempt.errorCode = "enhanced_broadcasting_service_create_failed";
		return false;
	}
	if (!service) {
		attempt.errorCode = "enhanced_broadcasting_service_create_failed";
		return false;
	}
	OBSDataAutoRelease serviceSettings = obs_service_get_settings(service);
	const std::string effectiveKey = obs_data_get_string(serviceSettings, "key");
	if (effectiveKey.empty() || effectiveKey.front() == '?' || !osn::HasExactlyOneTwitchBandwidthTestParameter(effectiveKey)) {
		attempt.errorCode = "enhanced_broadcasting_unsafe_stream_key";
		return false;
	}
	resources.service = obs_service_get_ref(service);
	obs_output_set_service(resources.output, resources.service);
	obs_output_set_reconnect_settings(resources.output, 0, 0);
	resources.publishOutput();

	if (session->cancelRequested.load()) {
		attempt.cancelled = true;
		return false;
	}
	for (CompanionSample &sample : companionSamples) {
		if (!obs_output_start(sample.resources->output)) {
			attempt.errorCode = "enhanced_broadcasting_companion_output_start_failed";
			return false;
		}
	}
	if (!obs_output_start(resources.output)) {
		attempt.errorCode = "enhanced_broadcasting_output_start_failed";
		return false;
	}
	const auto connectDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kProbeConnectTimeoutMs);
	while (!obs_output_active(resources.output) && std::chrono::steady_clock::now() < connectDeadline) {
		if (session->cancelRequested.load()) {
			attempt.cancelled = true;
			obs_output_force_stop(resources.output);
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	if (!obs_output_active(resources.output)) {
		attempt.errorCode = "enhanced_broadcasting_output_connect_failed";
		return false;
	}
	if (std::any_of(companionSamples.begin(), companionSamples.end(),
			[](const CompanionSample &sample) { return !obs_output_active(sample.resources->output); })) {
		attempt.errorCode = "enhanced_broadcasting_companion_output_stopped";
		return false;
	}

	ProbeResult intervalResult;
	intervalResult.provider = "twitch";
	if (!waitForProbeInterval(session, resources.output, std::chrono::steady_clock::now() + std::chrono::milliseconds(kEnhancedBroadcastingWarmupMs),
				  intervalResult)) {
		attempt.cancelled = intervalResult.cancelled;
		attempt.errorCode = attempt.cancelled ? std::string{} : "enhanced_broadcasting_output_stopped";
		return false;
	}
	if (std::any_of(companionSamples.begin(), companionSamples.end(),
			[](const CompanionSample &sample) { return !obs_output_active(sample.resources->output); })) {
		attempt.errorCode = "enhanced_broadcasting_companion_output_stopped";
		return false;
	}

	struct VideoInputSample {
		video_t *video = nullptr;
		size_t canvasIndex = 0;
		uint32_t totalFrames = 0;
		uint32_t skippedFrames = 0;
	};
	std::vector<VideoInputSample> videoInputSamples;
	std::vector<uint32_t> encodedStart;
	encodedStart.reserve(config.encoder_configurations.size());
	for (size_t index = 0; index < config.encoder_configurations.size(); index++) {
		obs_encoder_t *encoder = obs_output_get_video_encoder2(resources.output, index);
		if (!encoder) {
			attempt.errorCode = "enhanced_broadcasting_video_encoder_missing";
			return false;
		}
		encodedStart.push_back(obs_encoder_get_encoded_frames(encoder));
		/* Sample the post-rescale parent mix rather than obs_encoder_video().
		 * Divisor tracks expose an FPS-override child whose public counters do
		 * not resolve to the root mix that receives texture frames. */
		video_t *encoderVideo = obs_encoder_parent_video(encoder);
		if (!encoderVideo) {
			attempt.errorCode = "enhanced_broadcasting_video_input_missing";
			return false;
		}
		const size_t canvasIndex = config.encoder_configurations[index].canvas_index;
		const auto existingInput = std::find_if(videoInputSamples.begin(), videoInputSamples.end(),
							[encoderVideo](const VideoInputSample &sample) { return sample.video == encoderVideo; });
		if (existingInput == videoInputSamples.end()) {
			videoInputSamples.push_back(
				{encoderVideo, canvasIndex, video_output_get_total_frames(encoderVideo), video_output_get_skipped_frames(encoderVideo)});
		} else if (existingInput->canvasIndex != canvasIndex) {
			attempt.errorCode = "enhanced_broadcasting_video_canvas_binding_failed";
			return false;
		}
	}
	// A ladder may create multiple post-rescale parent inputs for one canvas.
	// Require coverage, not an exact input-to-canvas count, while rejecting a
	// parent input that was attributed to two different canvas identities above.
	std::vector<size_t> sampledCanvasIndexes;
	sampledCanvasIndexes.reserve(videoInputSamples.size());
	for (const VideoInputSample &sample : videoInputSamples)
		sampledCanvasIndexes.push_back(sample.canvasIndex);
	if (!enhancedBroadcastingPolicy::everyCanvasHasSampledInput(sampledCanvasIndexes, candidates.size())) {
		attempt.errorCode = "enhanced_broadcasting_video_canvas_missing";
		return false;
	}
	const uint64_t startBytes = obs_output_get_total_bytes(resources.output);
	const uint32_t startDropped = obs_output_get_frames_dropped(resources.output);
	for (CompanionSample &sample : companionSamples) {
		sample.encodedStart = obs_encoder_get_encoded_frames(sample.resources->videoEncoder);
		sample.outputFramesStart = obs_output_get_total_frames(sample.resources->output);
		sample.droppedStart = obs_output_get_frames_dropped(sample.resources->output);
	}
	const auto sampleDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kEnhancedBroadcastingSampleMs);
	while (std::chrono::steady_clock::now() < sampleDeadline) {
		if (session->cancelRequested.load()) {
			attempt.cancelled = true;
			obs_output_force_stop(resources.output);
			return false;
		}
		if (!obs_output_active(resources.output)) {
			attempt.errorCode = "enhanced_broadcasting_output_stopped";
			return false;
		}
		if (std::any_of(companionSamples.begin(), companionSamples.end(),
				[](const CompanionSample &sample) { return !obs_output_active(sample.resources->output); })) {
			attempt.errorCode = "enhanced_broadcasting_companion_output_stopped";
			return false;
		}
		attempt.maximumCongestion = std::max(attempt.maximumCongestion, obs_output_get_congestion(resources.output));
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	attempt.outputBytes = obs_output_get_total_bytes(resources.output) - startBytes;
	attempt.outputDroppedFrames = obs_output_get_frames_dropped(resources.output) - startDropped;
	// Render health is assessed independently for every unique parent input.
	bool mixPassed = true;
	for (size_t index = 0; index < videoInputSamples.size(); index++) {
		const VideoInputSample &sample = videoInputSamples[index];
		const uint32_t totalFrames = video_output_get_total_frames(sample.video) - sample.totalFrames;
		const uint32_t skippedFrames = video_output_get_skipped_frames(sample.video) - sample.skippedFrames;
		const bool inputPassed = totalFrames > 0 && skippedFrames <= enhancedBroadcastingPolicy::allowedSkippedFrames(totalFrames);
		attempt.inputFrames += totalFrames;
		attempt.inputSkippedFrames += skippedFrames;
		mixPassed = mixPassed && inputPassed;
		blog(inputPassed ? LOG_INFO : LOG_WARNING,
		     "[Auto Optimizer][Enhanced Broadcasting] Video input %zu sample: canvas=%zu, frames=%u, skipped=%u, passed=%s", index, sample.canvasIndex,
		     totalFrames, skippedFrames, inputPassed ? "true" : "false");
	}
	for (size_t index = 0; index < config.encoder_configurations.size(); index++) {
		obs_encoder_t *encoder = obs_output_get_video_encoder2(resources.output, index);
		const uint32_t encoded = obs_encoder_get_encoded_frames(encoder) - encodedStart[index];
		const auto cadence = effectiveTrackCadence(config.encoder_configurations[index], sourceFpsNum, sourceFpsDen);
		const uint32_t expected =
			(uint32_t)((uint64_t)kEnhancedBroadcastingSampleMs * cadence.numerator / (1000ULL * std::max(1U, cadence.denominator)));
		attempt.encodedFrames.push_back(encoded);
		attempt.minimumEncodedFrames.push_back(enhancedBroadcastingPolicy::minimumEncodedFrames(expected));
	}

	const char *outputError = obs_output_get_last_error(resources.output);
	const bool encoderFramesPassed = std::equal(attempt.encodedFrames.begin(), attempt.encodedFrames.end(), attempt.minimumEncodedFrames.begin(),
						    [](uint32_t encoded, uint32_t minimum) { return encoded >= minimum; });
	const bool transportPassed = attempt.outputDroppedFrames == 0 && attempt.maximumCongestion < kProbeCongestionHigh;
	bool companionsPassed = true;
	for (const CompanionSample &sample : companionSamples) {
		const uint32_t encoded = obs_encoder_get_encoded_frames(sample.resources->videoEncoder) - sample.encodedStart;
		const uint32_t outputFrames = obs_output_get_total_frames(sample.resources->output) - sample.outputFramesStart;
		const uint32_t dropped = obs_output_get_frames_dropped(sample.resources->output) - sample.droppedStart;
		const uint32_t expected =
			(uint32_t)((uint64_t)kEnhancedBroadcastingSampleMs * sample.cadence.numerator / (1000ULL * std::max(1U, sample.cadence.denominator)));
		const uint32_t minimum = enhancedBroadcastingPolicy::minimumEncodedFrames(expected);
		const char *companionError = obs_output_get_last_error(sample.resources->output);
		const bool passed = encoded >= minimum && outputFrames >= minimum && dropped == 0 && (!companionError || !*companionError);
		companionsPassed = companionsPassed && passed;
		blog(passed ? LOG_INFO : LOG_WARNING,
		     "[Auto Optimizer][Enhanced Broadcasting] Companion sample: leg=%s display=%s encoder=%s encoded=%u output=%u dropped=%u minimum=%u passed=%s",
		     sample.workload->legId.c_str(), sample.workload->display.c_str(), sample.workload->value.encoderId.c_str(), encoded, outputFrames, dropped,
		     minimum, passed ? "true" : "false");
	}
	attempt.success = attempt.outputBytes > 0 && encoderFramesPassed && mixPassed && transportPassed && companionsPassed && (!outputError || !*outputError);
	if (!attempt.success) {
		attempt.errorCode = attempt.outputBytes == 0 ? "enhanced_broadcasting_no_output"
				    : !encoderFramesPassed   ? "enhanced_broadcasting_encoder_underload"
				    : !mixPassed             ? "enhanced_broadcasting_render_overload"
				    : !transportPassed       ? "enhanced_broadcasting_transport_pressure"
				    : !companionsPassed      ? "enhanced_broadcasting_companion_overload"
							     : "enhanced_broadcasting_output_error";
	}
	for (size_t index = 0; index < attempt.encodedFrames.size(); index++) {
		blog(attempt.encodedFrames[index] >= attempt.minimumEncodedFrames[index] ? LOG_INFO : LOG_WARNING,
		     "[Auto Optimizer][Enhanced Broadcasting] Track %zu cadence sample: encoded=%u, minimum=%u", index, attempt.encodedFrames[index],
		     attempt.minimumEncodedFrames[index]);
	}
	blog(attempt.success ? LOG_INFO : LOG_WARNING,
	     "[Auto Optimizer][Enhanced Broadcasting] Workload sample: feed=%s, source=%u/%u FPS, tracks=%u, configured_aggregate=%llu Kbps, "
	     "output_bytes=%llu, output_dropped=%u, max_congestion=%.3f, input_mixes=%zu, input_frames=%u, input_skipped=%u, success=%s, "
	     "error=%s",
	     usePrivateTextureMix ? "private-texture" : "synthetic-raw-exact", sourceFpsNum, sourceFpsDen, attempt.videoTrackCount,
	     (unsigned long long)attempt.configuredAggregateBitrateKbps, (unsigned long long)attempt.outputBytes, attempt.outputDroppedFrames,
	     (double)attempt.maximumCongestion, videoInputSamples.size(), attempt.inputFrames, attempt.inputSkippedFrames, attempt.success ? "true" : "false",
	     attempt.errorCode.empty() ? "none" : attempt.errorCode.c_str());

	for (CompanionSample &sample : companionSamples)
		obs_output_stop(sample.resources->output);
	obs_output_stop(resources.output);
	for (CompanionSample &sample : companionSamples) {
		if (!waitForOutputInactive(sample.resources->output, kHardwareStopTimeoutMs)) {
			obs_output_force_stop(sample.resources->output);
			if (!waitForOutputInactive(sample.resources->output, kHardwareStopTimeoutMs)) {
				attempt.success = false;
				attempt.errorCode = "enhanced_broadcasting_companion_cleanup_timeout";
			}
		}
	}
	if (!waitForOutputInactive(resources.output, kProbeStopTimeoutMs)) {
		obs_output_force_stop(resources.output);
		if (!waitForOutputInactive(resources.output, kProbeStopTimeoutMs)) {
			attempt.success = false;
			attempt.errorCode = "enhanced_broadcasting_cleanup_timeout";
		}
	}
	return attempt.success;
}

static bool enhancedCandidateFitsCompanion(const enhancedBroadcastingPolicy::VideoCandidate &candidate, const CompanionWorkload &companion)
{
	return candidate.width <= (uint32_t)std::max(0, companion.value.width) && candidate.height <= (uint32_t)std::max(0, companion.value.height) &&
	       (uint64_t)candidate.fpsNum * std::max(1, companion.value.fpsDen) <= (uint64_t)std::max(0, companion.value.fpsNum) * candidate.fpsDen;
}

static ProbeResult runEnhancedBroadcastingProbe(const std::shared_ptr<Session> &session, ProbeRequest &probe, const LegRequest &leg, double slotStartProgress,
						double slotEndProgress, const std::vector<CompanionWorkload> &provisionalCompanions = {})
{
	ProbeResult result;
	result.provider = "twitch";
	result.legId = probe.legId;
	result.method = "twitch-enhanced-broadcasting-test";
	result.observedThroughputReliable = false;
	result.ceilingReached = false;

	std::string normalizedKey = osn::NormalizeTwitchBandwidthTestKey(probe.streamKey);
	probe.streamKey.clear();
	if (!osn::HasExactlyOneTwitchBandwidthTestParameter(normalizedKey)) {
		result.errorCode = "enhanced_broadcasting_unsafe_stream_key";
		return result;
	}

	obs_data_t *serviceSettings = obs_data_create();
	obs_data_set_string(serviceSettings, "service", "Twitch");
	obs_data_set_string(serviceSettings, "server", "auto");
	obs_data_set_string(serviceSettings, "key", normalizedKey.c_str());
	OBSServiceAutoRelease configurationService =
		obs_service_create_private("rtmp_common", "auto_optimizer_enhanced_broadcasting_config_service", serviceSettings);
	obs_data_release(serviceSettings);
	probe.server.clear();
	if (!configurationService) {
		result.errorCode = "enhanced_broadcasting_service_create_failed";
		return result;
	}
	const std::string autoConfigUrl = osn::MultitrackVideoAutoConfigURL(configurationService);
	if (autoConfigUrl.empty()) {
		result.errorCode = "enhanced_broadcasting_config_url_missing";
		return result;
	}

	obs_video_info *primaryIdentity = osn::Video::Manager::GetInstance().find(leg.current.canvasId);
	obs_video_info *additionalIdentity = leg.additionalVideo ? osn::Video::Manager::GetInstance().find(leg.additionalVideo->current.canvasId) : nullptr;
	if (!primaryIdentity || primaryIdentity->fps_num == 0 || (leg.additionalVideo && (!additionalIdentity || additionalIdentity->fps_num == 0))) {
		result.errorCode = "enhanced_broadcasting_video_unavailable";
		return result;
	}
	const uint32_t maxWidth = leg.limits.maxWidth > 0 ? (uint32_t)std::min(1920, leg.limits.maxWidth) : 1920U;
	const uint32_t maxHeight = leg.limits.maxHeight > 0 ? (uint32_t)std::min(1080, leg.limits.maxHeight) : 1080U;
	const uint32_t maxFpsNum = leg.limits.maxFpsNum > 0 ? (uint32_t)leg.limits.maxFpsNum : 0U;
	const uint32_t maxFpsDen = leg.limits.maxFpsNum > 0 ? (uint32_t)std::max(1, leg.limits.maxFpsDen) : 0U;
	const bool fractionalCadenceFamily =
		leg.current.fpsDen == 1001 || leg.limits.maxFpsDen == 1001 ||
		(leg.additionalVideo && (leg.additionalVideo->current.fpsDen == 1001 || leg.additionalVideo->limits.maxFpsDen == 1001));
	auto candidates = enhancedBroadcastingPolicy::candidates(maxWidth, maxHeight, maxFpsNum, maxFpsDen, fractionalCadenceFamily ? 1001U : 1U);
	if (leg.additionalVideo) {
		const Limits &additionalLimits = leg.additionalVideo->limits;
		std::erase_if(candidates, [&](const enhancedBroadcastingPolicy::VideoCandidate &candidate) {
			const auto vertical = enhancedBroadcastingPolicy::pairedVerticalCandidate(candidate);
			return !enhancedBroadcastingPolicy::candidateFitsLimits(vertical, (uint32_t)std::max(0, additionalLimits.maxWidth),
										(uint32_t)std::max(0, additionalLimits.maxHeight),
										(uint32_t)std::max(0, additionalLimits.maxFpsNum),
										(uint32_t)std::max(0, additionalLimits.maxFpsDen));
		});
	}
	if (!provisionalCompanions.empty()) {
		std::erase_if(candidates, [&](const enhancedBroadcastingPolicy::VideoCandidate &candidate) {
			const auto vertical = enhancedBroadcastingPolicy::pairedVerticalCandidate(candidate);
			return std::any_of(provisionalCompanions.begin(), provisionalCompanions.end(), [&](const CompanionWorkload &companion) {
				const auto &displayCandidate = companion.display == "horizontal" ? candidate : vertical;
				return !enhancedCandidateFitsCompanion(displayCandidate, companion);
			});
		});
	}
	if (candidates.empty()) {
		result.errorCode = "enhanced_broadcasting_no_candidate";
		return result;
	}

	for (size_t index = 0; index < candidates.size(); index++) {
		const auto &candidate = candidates[index];
		CurrentSettings eventVideo = leg.current;
		eventVideo.width = (int)candidate.width;
		eventVideo.height = (int)candidate.height;
		eventVideo.fpsNum = (int)candidate.fpsNum;
		eventVideo.fpsDen = (int)candidate.fpsDen;
		CurrentSettings eventAdditionalVideo;
		const CurrentSettings *eventAdditionalVideoPtr = nullptr;
		if (leg.additionalVideo) {
			eventAdditionalVideo = leg.additionalVideo->current;
			eventAdditionalVideo.width = (int)candidate.height;
			eventAdditionalVideo.height = (int)candidate.width;
			eventAdditionalVideo.fpsNum = (int)candidate.fpsNum;
			eventAdditionalVideo.fpsDen = (int)candidate.fpsDen;
			eventAdditionalVideoPtr = &eventAdditionalVideo;
		}
		const double candidateStart = slotStartProgress + (slotEndProgress - slotStartProgress) * (double)index / (double)candidates.size();
		const double candidateEnd = slotStartProgress + (slotEndProgress - slotStartProgress) * (double)(index + 1) / (double)candidates.size();
		pushEvent(session, "progress", "bandwidth", candidateStart, "enhanced_broadcasting_requesting_ladder", probe.legId, "active", probe.probeId,
			  probe.provider, 0, &eventVideo, 0, 0, eventAdditionalVideoPtr);

		std::vector<enhancedBroadcastingPolicy::VideoCandidate> candidateCanvases{candidate};
		if (leg.additionalVideo)
			candidateCanvases.push_back(enhancedBroadcastingPolicy::pairedVerticalCandidate(candidate));
		std::vector<CompanionWorkload> candidateCompanions = provisionalCompanions;
		for (auto &companion : candidateCompanions) {
			const auto &displayCandidate = companion.display == "horizontal" ? candidateCanvases[0] : candidateCanvases[1];
			companion.value.width = (int)displayCandidate.width;
			companion.value.height = (int)displayCandidate.height;
			companion.value.fpsNum = (int)displayCandidate.fpsNum;
			companion.value.fpsDen = (int)displayCandidate.fpsDen;
		}
		std::vector<obs_video_info> requestCanvases(candidateCanvases.size());
		std::vector<obs_video_info *> requestCanvasPointers;
		requestCanvasPointers.reserve(requestCanvases.size());
		for (size_t canvasIndex = 0; canvasIndex < candidateCanvases.size(); canvasIndex++) {
			const auto &canvasCandidate = candidateCanvases[canvasIndex];
			obs_video_info &requestCanvas = requestCanvases[canvasIndex];
			requestCanvas.base_width = canvasCandidate.width;
			requestCanvas.base_height = canvasCandidate.height;
			requestCanvas.output_width = canvasCandidate.width;
			requestCanvas.output_height = canvasCandidate.height;
			requestCanvas.fps_num = canvasCandidate.fpsNum;
			requestCanvas.fps_den = canvasCandidate.fpsDen;
			requestCanvas.fps_type = 1;
			requestCanvas.output_format = VIDEO_FORMAT_NV12;
			requestCanvas.colorspace = VIDEO_CS_709;
			requestCanvas.range = VIDEO_RANGE_PARTIAL;
			requestCanvas.scale_type = OBS_SCALE_BILINEAR;
			requestCanvas.adapter = canvasIndex == 0 ? primaryIdentity->adapter : additionalIdentity->adapter;
			requestCanvas.gpu_conversion = true;
			requestCanvasPointers.push_back(&requestCanvas);
		}

		osn::Config config;
		try {
			auto post = osn::constructGoLivePost(requestCanvasPointers, normalizedKey, std::nullopt, std::nullopt, false);
			post.client.supported_codecs.clear();
			post.client.supported_codecs.emplace("h264");
			config = osn::DownloadGoLiveConfig(autoConfigUrl, post);
		} catch (const std::exception &exception) {
			blog(LOG_WARNING, "[Auto Optimizer][Enhanced Broadcasting] Ladder request failed for %ux%u %u/%u FPS: %s", candidate.width,
			     candidate.height, candidate.fpsNum, candidate.fpsDen, boundedLogValue(exception.what()).c_str());
			result.errorCode = "enhanced_broadcasting_config_request_failed";
			return result;
		}
		if (!validateEnhancedBroadcastingConfig(config, candidateCanvases, result.errorCode)) {
			if (!enhancedBroadcastingPolicy::allowsCandidateDescent(result.errorCode))
				return result;
			pushEvent(session, "progress", "bandwidth", candidateEnd, "enhanced_broadcasting_candidate_rejected", probe.legId, "active",
				  probe.probeId, probe.provider, 0, &eventVideo, 0, 0, eventAdditionalVideoPtr);
			continue;
		}

		pushEvent(session, "progress", "bandwidth", candidateStart + (candidateEnd - candidateStart) * 0.25,
			  candidateCompanions.empty() ? "enhanced_broadcasting_testing_candidate" : "enhanced_broadcasting_testing_concurrent_outputs",
			  probe.legId, "active", probe.probeId, probe.provider, 0, &eventVideo, 0, 0, eventAdditionalVideoPtr);
		const bool primaryCadenceInsufficient = !enhancedBroadcastingPolicy::cadenceCanBeProvenByPrivateMix(candidate, primaryIdentity->fps_num,
														    std::max(1U, primaryIdentity->fps_den));
		const bool additionalCadenceInsufficient =
			leg.additionalVideo && !enhancedBroadcastingPolicy::cadenceCanBeProvenByPrivateMix(candidateCanvases[1], additionalIdentity->fps_num,
													   std::max(1U, additionalIdentity->fps_den));
		const bool targetAboveMainCadence = primaryCadenceInsufficient || additionalCadenceInsufficient;
		const obs_video_info *smokeIdentity = primaryIdentity;
		if (additionalIdentity &&
		    (uint64_t)additionalIdentity->fps_num * primaryIdentity->fps_den < (uint64_t)primaryIdentity->fps_num * additionalIdentity->fps_den)
			smokeIdentity = additionalIdentity;
		// Auxiliary mixes share the graphics thread's render frame rate. Lower
		// Twitch renditions and companion outputs are exercised with exact
		// encoder frame-rate divisors; a higher candidate still receives the
		// separate exact-frame-rate raw-input attempt below.
		const uint32_t smokeFpsNum = smokeIdentity->fps_num;
		const uint32_t smokeFpsDen = std::max(1U, smokeIdentity->fps_den);
		std::vector<uint64_t> canvasIds{leg.current.canvasId};
		if (leg.additionalVideo)
			canvasIds.push_back(leg.additionalVideo->current.canvasId);
		EnhancedBroadcastingAttempt textureAttempt;
		if (!runEnhancedBroadcastingOutputAttempt(session, probe, config, normalizedKey, candidateCanvases, canvasIds, smokeFpsNum, smokeFpsDen, true,
							  candidateCompanions, textureAttempt)) {
			if (textureAttempt.cancelled) {
				result.cancelled = true;
				return result;
			}
			result.errorCode = !candidateCompanions.empty() && enhancedBroadcastingPolicy::isCompositeCandidateLoadFailure(textureAttempt.errorCode)
						   ? "enhanced_broadcasting_companion_overload"
						   : textureAttempt.errorCode;
			if (!enhancedBroadcastingPolicy::allowsCandidateDescent(result.errorCode))
				return result;
			pushEvent(session, "progress", "bandwidth", candidateEnd, "enhanced_broadcasting_candidate_rejected", probe.legId, "active",
				  probe.probeId, probe.provider, 0, &eventVideo, 0, 0, eventAdditionalVideoPtr);
			continue;
		}

		EnhancedBroadcastingAttempt exactAttempt = textureAttempt;
		if (targetAboveMainCadence) {
			pushEvent(session, "progress", "bandwidth", candidateStart + (candidateEnd - candidateStart) * 0.65,
				  "enhanced_broadcasting_validating_target_cadence", probe.legId, "active", probe.probeId, probe.provider, 0, &eventVideo, 0, 0,
				  eventAdditionalVideoPtr);
			exactAttempt = {};
			if (!runEnhancedBroadcastingOutputAttempt(session, probe, config, normalizedKey, candidateCanvases, canvasIds, candidate.fpsNum,
								  candidate.fpsDen, false, candidateCompanions, exactAttempt)) {
				if (exactAttempt.cancelled) {
					result.cancelled = true;
					return result;
				}
				result.errorCode = !candidateCompanions.empty() &&
								   enhancedBroadcastingPolicy::isCompositeCandidateLoadFailure(exactAttempt.errorCode)
							   ? "enhanced_broadcasting_companion_overload"
							   : exactAttempt.errorCode;
				if (!enhancedBroadcastingPolicy::allowsCandidateDescent(result.errorCode))
					return result;
				pushEvent(session, "progress", "bandwidth", candidateEnd, "enhanced_broadcasting_candidate_rejected", probe.legId, "active",
					  probe.probeId, probe.provider, 0, &eventVideo, 0, 0, eventAdditionalVideoPtr);
				continue;
			}
		}

		result.success = true;
		result.errorCode.clear();
		result.testedWidth = candidate.width;
		result.testedHeight = candidate.height;
		result.testedFpsNum = candidate.fpsNum;
		result.testedFpsDen = candidate.fpsDen;
		if (leg.additionalVideo) {
			CurrentSettings testedAdditionalVideo;
			testedAdditionalVideo.width = (int)candidateCanvases[1].width;
			testedAdditionalVideo.height = (int)candidateCanvases[1].height;
			testedAdditionalVideo.fpsNum = (int)candidateCanvases[1].fpsNum;
			testedAdditionalVideo.fpsDen = (int)candidateCanvases[1].fpsDen;
			result.testedAdditionalVideo = std::move(testedAdditionalVideo);
		}
		result.videoTrackCount = exactAttempt.videoTrackCount;
		result.configuredAggregateBitrateKbps = exactAttempt.configuredAggregateBitrateKbps;
		result.pairedCadenceEvidence = targetAboveMainCadence;
		result.companionWorkloads = std::move(candidateCompanions);
		pushEvent(session, "progress", "bandwidth", candidateEnd, "enhanced_broadcasting_candidate_selected", probe.legId, "active", probe.probeId,
			  probe.provider, 0, &eventVideo, 0, 0, eventAdditionalVideoPtr);
		blog(LOG_INFO, "[Auto Optimizer][Enhanced Broadcasting] Candidate selected: %ux%u %u/%u FPS, tracks=%u, configured_aggregate=%llu Kbps",
		     result.testedWidth, result.testedHeight, result.testedFpsNum, result.testedFpsDen, result.videoTrackCount,
		     (unsigned long long)result.configuredAggregateBitrateKbps);
		return result;
	}

	if (result.errorCode.empty())
		result.errorCode = "enhanced_broadcasting_no_passing_candidate";
	return result;
}

static ProbeResult runRtmpProbe(const std::shared_ptr<Session> &session, ProbeRequest &probe, const LegRequest &leg, double slotStartProgress,
				double slotEndProgress)
{
	ProbeResult result;
	result.provider = probe.provider;
	result.legId = probe.legId;
	result.method = probe.provider == "youtube" ? "youtube-unbound-ramp" : "twitch-bandwidth-test";
	ScratchResources resources(*session);

	obs_data_t *serviceSettings = obs_data_create();
	obs_data_set_string(serviceSettings, "service", probe.provider == "youtube" ? "YouTube - RTMPS" : "Twitch");
	obs_data_set_string(serviceSettings, "server", probe.server.c_str());
	std::string serviceKey = probe.provider == "youtube" ? probe.streamKey : osn::NormalizeTwitchBandwidthTestKey(probe.streamKey);
	obs_data_set_string(serviceSettings, "key", serviceKey.c_str());
	resources.service = obs_service_create_private("rtmp_common", "auto_optimizer_probe_service", serviceSettings);
	obs_data_release(serviceSettings);

	// After the private OBS service copies the endpoint and stream key, clear
	// every temporary copy held by Auto Optimizer. These values are never
	// returned, serialized, or logged.
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
		if (!waitForProbeInterval(session, resources.output, std::chrono::steady_clock::now() + std::chrono::milliseconds(kProbeWarmupMs), result))
			return result;

		TwitchProbeSample sample;
		if (!runTwitchProbeSample(session, resources, probe, initialBitrate, kProbeSampleMs,
					  probePolicy::probeSubstepProgress(slotStartProgress, slotEndProgress, 0, 2), "twitch_probe_measuring", sample,
					  result))
			return result;
		probePolicy::TwitchProbeDecision decision = probePolicy::decideTwitchProbe(sample.measuredAggregateKbps, (uint64_t)std::max(0, initialBitrate),
											   sample.droppedFrames, sample.sustainedCongestion);
		bool extendedSampleUsed = false;
		if (decision.extendSample) {
			extendedSampleUsed = true;
			TwitchProbeSample extendedSample;
			if (!runTwitchProbeSample(session, resources, probe, initialBitrate, kProbeExtendedSampleMs,
						  probePolicy::probeSubstepProgress(slotStartProgress, slotEndProgress, 1, 2),
						  "twitch_probe_confirming_capacity", extendedSample, result))
				return result;
			sample = extendedSample;
			decision = probePolicy::decideTwitchProbe(sample.measuredAggregateKbps, (uint64_t)std::max(0, initialBitrate), sample.droppedFrames,
								  sample.sustainedCongestion, true);
		}

		result.measuredKbps = sample.measuredAggregateKbps;
		result.safeKbps = decision.recommendedVideoKbps;
		if (result.platformCapKbps > 0)
			result.safeKbps = std::min<uint64_t>(result.safeKbps, (uint64_t)result.platformCapKbps);
		if (leg.limits.maxBitrateKbps > 0)
			result.safeKbps = std::min<uint64_t>(result.safeKbps, (uint64_t)leg.limits.maxBitrateKbps);
		result.success = result.safeKbps > 0;
		result.stability = decision.targetPassed ? ProbeStability::Stable : ProbeStability::Degraded;
		const int effectiveCeilingKbps =
			probePolicy::effectiveProbeCeilingKbps(kProbeMaximumBitrateKbps, result.platformCapKbps, leg.limits.maxBitrateKbps);
		result.ceilingReached = decision.targetPassed && probePolicy::reachedEffectiveProbeCeiling(initialBitrate, effectiveCeilingKbps);
		blog(result.success ? LOG_INFO : LOG_WARNING,
		     "[Auto Optimizer][Twitch Probe] Measurement decision: target=%d Kbps, measured_aggregate=%llu Kbps, dropped_frames=%u, "
		     "sustained_congestion=%s, extended_sample=%s, target_passed=%s, recommended_video=%llu Kbps, platform_cap=%d Kbps, request_cap=%d Kbps",
		     initialBitrate, (unsigned long long)result.measuredKbps, sample.droppedFrames, sample.sustainedCongestion ? "true" : "false",
		     extendedSampleUsed ? "true" : "false", decision.targetPassed ? "true" : "false", (unsigned long long)result.safeKbps,
		     result.platformCapKbps, leg.limits.maxBitrateKbps);
	} else {
		const int ladder[] = {1000, 2000, 4000, 6000, 8000, 10000};
		uint64_t totalProbeBytes = youtubeProbeBytesUsed(resources.output, probeBudgetStartBytes);
		probePolicy::YoutubeRampEvidence rampEvidence;
		// maxBitrateKbps caps the recommendation returned to Desktop. YouTube may
		// test one higher bitrate to verify stability, but records the extra
		// capacity only as measurement evidence.
		const int effectiveCeilingKbps = probePolicy::effectiveProbeCeilingKbps(kYoutubeProbeMaximumBitrateKbps, result.platformCapKbps, 0);
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
		// recommendationEvidenceUsable records whether the probe produced enough
		// evidence for a conservative recommendation; it does not imply that the
		// physical path limit was measured.
		bool recommendationEvidenceUsable = false;
		probePolicy::YoutubeSourceUnderfillState sourceUnderfillState;
		const auto rampStarted = std::chrono::steady_clock::now();
		const auto probeElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(rampStarted - probeStarted).count();
		const auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(youtubeDeadline - rampStarted).count();
		blog(LOG_INFO,
		     "[Auto Optimizer][YouTube Probe] Adaptive ladder configuration: effective_video_ceiling=%d Kbps, settle=%d ms, "
		     "sample=%d ms, sustained_sample=%d ms, subwindow=%d ms, recovery_maximum=%d ms, recovery_healthy_window=%d ms, "
		     "total_timeout=%d ms, probe_elapsed=%lld ms, remaining=%lld ms, byte_budget=%llu, maximum_confirmation_episodes=%d",
		     effectiveCeilingKbps, kYoutubeProbeSettleMs, kYoutubeProbeSampleMs, kYoutubeProbeSustainedSampleMs, kYoutubeProbeSubwindowMs,
		     kYoutubeProbeRecoveryMaximumMs, kYoutubeProbeRecoveryPollMs * kYoutubeProbeRecoveryHealthySamples, kYoutubeProbeTotalTimeoutMs,
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

						const probePolicy::YoutubeProbeLoadResult baselineLoad =
							probePolicy::classifyYoutubeProbeLoad(baseline.reference, baseline);
						sourceUnderfillState.observeTransportClean(baselineLoad);
						blog(LOG_INFO, "[Auto Optimizer][YouTube Probe] Baseline load result: result=%s",
						     youtubeLoadResultName(baselineLoad));

						if (sourceUnderfillState.terminal)
							rampEvidence.observeTransportCleanLowerBound(baselineBasis, (uint64_t)baselineTarget);
						else
							rampEvidence.observeAcceptedTarget(baselineBasis, (uint64_t)baselineTarget);
						YoutubeProbeSample lastAccepted = baselineFirst;
						lastAccepted.targetVideoKbps = baselineTarget;
						lastAccepted.expectedAggregateKbps = (uint64_t)baselineTarget + kYoutubeProbeAudioBitrateKbps;
						lastAccepted.measuredAggregateKbps = baselineBasis;
						lastAccepted.metrics = baseline.reference;
						result.ceilingReached = probePolicy::reachedEffectiveProbeCeiling(baselineTarget, effectiveCeilingKbps);
						if (result.ceilingReached) {
							terminationReason = sourceUnderfillState.terminal ? "source_underfill_at_effective_ceiling_baseline"
													  : "effective_ceiling_reached_at_baseline";
							recommendationEvidenceUsable = true;
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

							const probePolicy::YoutubeProbeLoadResult highFirstLoad =
								probePolicy::classifyYoutubeProbeLoad(highFirst.metrics, baseline);
							blog(LOG_INFO, "[Auto Optimizer][YouTube Probe] Rung load result: target=%d Kbps, result=%s",
							     plannedTarget, youtubeLoadResultName(highFirstLoad));
							if (!probePolicy::youtubeRequiresCapacityConfirmation(highFirstLoad)) {
								const bool sourceUnderfill = highFirstLoad ==
											     probePolicy::YoutubeProbeLoadResult::SourceUnderfill;
								sourceUnderfillState.observeTransportClean(highFirstLoad);
								if (sourceUnderfill)
									rampEvidence.observeTransportCleanLowerBound(highFirst.measuredAggregateKbps,
														     (uint64_t)highFirst.targetVideoKbps);
								else
									rampEvidence.observeAcceptedTarget(highFirst.measuredAggregateKbps,
													   (uint64_t)highFirst.targetVideoKbps);
								result.measuredKbps = rampEvidence.recommendationBasisKbps;
								lastAccepted = highFirst;
								result.ceilingReached = probePolicy::reachedEffectiveProbeCeiling(highFirst.targetVideoKbps,
																  effectiveCeilingKbps);
								if (result.ceilingReached) {
									terminationReason = sourceUnderfill ? "source_underfill_at_effective_ceiling"
													    : "effective_ceiling_reached";
									recommendationEvidenceUsable = true;
								}
								if (highFirst.targetVideoKbps < ladderTarget) {
									terminationReason = sourceUnderfill ? "source_underfill_at_provider_or_request_cap"
													    : "provider_or_request_cap_reached";
									recommendationEvidenceUsable = true;
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
										    {lastAccepted.targetVideoKbps, plannedTarget},
										    {{baselineTarget, kYoutubeProbeRecoveryMaximumMs},
										     {plannedTarget, kYoutubeProbeSustainedSampleMs}})) {
								terminationReason = "confirmation_budget_exhausted_after_unconfirmed_failure";
								if (result.stability == ProbeStability::Stable)
									result.stability = ProbeStability::Variable;
								break;
							}

							YoutubeProbeSample lowControl;
							YoutubeProbeSample highRetry;
							if (!waitForYoutubeRecoveryDrain(session, resources, probe, baselineTarget, activeTarget,
											 youtubeDeadline, probeBudgetStartBytes, totalProbeBytes,
											 youtubeProbeStepProgress(slotStartProgress, slotEndProgress,
														  targetIndex, plannedTargets.size(), 0.35),
											 result)) {
								if (result.errorCode == "youtube_probe_recovery_timeout") {
									terminationReason = "recovery_drain_timeout";
									break;
								}
								return result;
							}
							if (!runYoutubeProbeSample(session, resources, probe, lastAccepted.targetVideoKbps, activeTarget,
										   youtubeDeadline, probeBudgetStartBytes, totalProbeBytes,
										   youtubeProbeStepProgress(slotStartProgress, slotEndProgress, targetIndex,
													    plannedTargets.size(), 0.50),
										   "youtube_probe_confirming_stability", lowControl, result))
								return result;
							if (!runYoutubeProbeSample(session, resources, probe, plannedTarget, activeTarget, youtubeDeadline,
										   probeBudgetStartBytes, totalProbeBytes,
										   youtubeProbeStepProgress(slotStartProgress, slotEndProgress, targetIndex,
													    plannedTargets.size(), 0.75),
										   "youtube_probe_retrying", highRetry, result))
								return result;

							const bool lowRecovered =
								probePolicy::youtubeLowControlRecovered(lowControl.metrics, lastAccepted.metrics, baseline);
							const probePolicy::YoutubeProbeLoadResult highRetryLoad =
								probePolicy::classifyYoutubeProbeLoad(highRetry.metrics, baseline);
							const bool highAccepted = !probePolicy::youtubeRequiresCapacityConfirmation(highRetryLoad);
							const probePolicy::YoutubeConfirmationDecision confirmation =
								probePolicy::decideYoutubeConfirmation(lowRecovered, highAccepted);
							blog(LOG_INFO,
							     "[Auto Optimizer][YouTube Probe] Confirmation decision: episode=%llu, high_target=%d Kbps, "
							     "low_recovered=%s, high_retry_result=%s, decision=%s",
							     (unsigned long long)confirmationEpisodes, plannedTarget, lowRecovered ? "true" : "false",
							     youtubeLoadResultName(highRetryLoad), youtubeConfirmationDecisionName(confirmation));

							if (confirmation == probePolicy::YoutubeConfirmationDecision::CapacityKnee) {
								// Two pressure observations at the high target with a
								// recovered low control are explicit transport evidence;
								// they supersede any earlier source-underfill diagnostic.
								sourceUnderfillState.confirmCapacityKnee();
								const uint64_t confirmedTarget = std::min<uint64_t>((uint64_t)highFirst.targetVideoKbps,
														    (uint64_t)highRetry.targetVideoKbps);
								const uint64_t confirmedCapacity = probePolicy::youtubeConfirmedPressureCapacityKbps(
									highFirst.measuredAggregateKbps, highRetry.measuredAggregateKbps, confirmedTarget);
								rampEvidence.observeConfirmedCapacity(confirmedCapacity, confirmedTarget);
								result.measuredKbps = rampEvidence.recommendationBasisKbps;
								terminationReason = "confirmed_capacity_knee";
								recommendationEvidenceUsable = true;
								break;
							}
							if (confirmation == probePolicy::YoutubeConfirmationDecision::TransientRecovered) {
								if (result.stability == ProbeStability::Stable)
									result.stability = ProbeStability::Variable;

								// A single clean five-second retry can be satisfied by
								// socket or traffic-shaper burst capacity. Keep the same
								// connection and target active long enough to exhaust that
								// burst before accepting the higher rung.
								YoutubeProbeSample sustainedHigh;
								if (!runYoutubeProbeSample(session, resources, probe, plannedTarget, activeTarget,
											   youtubeDeadline, probeBudgetStartBytes, totalProbeBytes,
											   youtubeProbeStepProgress(slotStartProgress, slotEndProgress,
														    targetIndex, plannedTargets.size(), 0.90),
											   "youtube_probe_confirming_stability", sustainedHigh, result,
											   kYoutubeProbeSustainedSampleMs))
									return result;

								const probePolicy::YoutubeProbeLoadResult sustainedLoad =
									probePolicy::classifyYoutubeProbeLoad(sustainedHigh.metrics, baseline);
								const probePolicy::YoutubeExtendedValidationDecision extendedDecision =
									probePolicy::decideYoutubeExtendedValidation(sustainedLoad);
								blog(LOG_INFO,
								     "[Auto Optimizer][YouTube Probe] Sustained validation decision: episode=%llu, "
								     "high_target=%d Kbps, initial_retry_result=%s, sustained_result=%s, decision=%s",
								     (unsigned long long)confirmationEpisodes, plannedTarget,
								     youtubeLoadResultName(highRetryLoad), youtubeLoadResultName(sustainedLoad),
								     youtubeExtendedValidationDecisionName(extendedDecision));

								if (extendedDecision == probePolicy::YoutubeExtendedValidationDecision::CapacityKnee) {
									// The recovered low control separates persistent
									// high-target pressure from baseline path loss. The
									// sustained window's delivered rate is the measured
									// capacity; do not apply a fixed reservation.
									sourceUnderfillState.confirmCapacityKnee();
									rampEvidence.observeConfirmedCapacity(sustainedHigh.measuredAggregateKbps,
													      (uint64_t)sustainedHigh.targetVideoKbps);
									result.measuredKbps = rampEvidence.recommendationBasisKbps;
									terminationReason = "confirmed_capacity_knee_after_sustained_retry";
									recommendationEvidenceUsable = true;
									break;
								}

								const bool sourceUnderfill = extendedDecision ==
											     probePolicy::YoutubeExtendedValidationDecision::SourceUnderfill;
								sourceUnderfillState.observeTransportClean(sustainedLoad);
								if (sourceUnderfill)
									rampEvidence.observeTransportCleanLowerBound(sustainedHigh.measuredAggregateKbps,
														     (uint64_t)sustainedHigh.targetVideoKbps);
								else
									rampEvidence.observeAcceptedTarget(sustainedHigh.measuredAggregateKbps,
													   (uint64_t)sustainedHigh.targetVideoKbps);
								result.measuredKbps = rampEvidence.recommendationBasisKbps;
								lastAccepted = sustainedHigh;
								result.ceilingReached = probePolicy::reachedEffectiveProbeCeiling(sustainedHigh.targetVideoKbps,
																  effectiveCeilingKbps);
								if (result.ceilingReached) {
									terminationReason = sourceUnderfill
												    ? "source_underfill_at_effective_ceiling_after_retry"
												    : "effective_ceiling_reached_after_retry";
									recommendationEvidenceUsable = true;
								}
								if (sustainedHigh.targetVideoKbps < ladderTarget) {
									terminationReason = sourceUnderfill
												    ? "source_underfill_at_provider_or_request_cap_after_retry"
												    : "provider_or_request_cap_reached_after_retry";
									recommendationEvidenceUsable = true;
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

		if (sourceUnderfillState.terminal && result.stability == ProbeStability::Stable)
			result.stability = ProbeStability::SourceUnderfill;

		if (result.stability != ProbeStability::Unstable && rampEvidence.passedStep && !recommendationEvidenceUsable) {
			result.observedThroughputReliable = false;
			if (result.errorCode.empty())
				result.errorCode = "youtube_probe_inconclusive";
		}

		uint64_t uncappedSafeKbps = 0;
		if (result.stability != ProbeStability::Unstable && rampEvidence.passedStep && recommendationEvidenceUsable) {
			uncappedSafeKbps = rampEvidence.recommendedVideoKbps();
			result.safeKbps = uncappedSafeKbps;
			if (result.platformCapKbps > 0)
				result.safeKbps = std::min<uint64_t>(result.safeKbps, (uint64_t)result.platformCapKbps);
			result.success = result.safeKbps > 0;
		} else {
			result.safeKbps = 0;
			result.success = false;
			if (result.errorCode.empty())
				result.errorCode = "youtube_probe_no_passing_step";
		}
		blog(result.success ? LOG_INFO : LOG_WARNING,
		     "[Auto Optimizer][YouTube Probe] Adaptive ladder summary: passed_step=%s, stability=%s, "
		     "observed_output_reliable_for_recommendation=%s, recommendation_basis=%llu Kbps, validated_video=%llu Kbps, "
		     "final_safe_video=%llu Kbps, platform_cap=%d Kbps, recommendation_cap=%d Kbps, probe_ceiling=%d Kbps, "
		     "total_output_bytes=%llu, ceiling_reached=%s, recommendation_evidence_usable=%s, confirmation_episodes=%llu, termination=%s, error=%s",
		     rampEvidence.passedStep ? "true" : "false", probeStabilityName(result.stability), result.observedThroughputReliable ? "true" : "false",
		     (unsigned long long)rampEvidence.recommendationBasisKbps, (unsigned long long)uncappedSafeKbps, (unsigned long long)result.safeKbps,
		     result.platformCapKbps, leg.limits.maxBitrateKbps, effectiveCeilingKbps, (unsigned long long)totalProbeBytes,
		     result.ceilingReached ? "true" : "false", recommendationEvidenceUsable ? "true" : "false", (unsigned long long)confirmationEpisodes,
		     terminationReason.c_str(), result.errorCode.empty() ? "none" : result.errorCode.c_str());
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

static CurrentSettings provisionalCompositeCompanionValue(const LegRequest &leg, const HardwareAssessment &hardware,
							  const std::vector<ProbeResult> &probeResults)
{
	CurrentSettings value = estimateRecommendation(leg, hardware);
	std::set<std::string> successfulProviders;
	uint64_t safeKbps = UINT64_MAX;
	for (const auto &result : probeResults) {
		if (result.legId != leg.legId)
			continue;
		if (result.success)
			successfulProviders.insert(result.provider);
		if (probePolicy::probeSafeValueContributesToActiveRecommendation(result.success, result.observedThroughputReliable, result.measuredKbps,
										 result.safeKbps))
			safeKbps = std::min(safeKbps, result.safeKbps);
	}
	const size_t expectedProviders = probeableProviderCount(leg);
	const bool completeActiveCoverage = expectedProviders > 0 && successfulProviders.size() == expectedProviders;
	if (safeKbps != UINT64_MAX)
		value.bitrateKbps = qualityPolicy::clampRecommendedBitrateKbps(safeKbps);
	if (leg.limits.maxBitrateKbps > 0)
		value.bitrateKbps = std::min(value.bitrateKbps, leg.limits.maxBitrateKbps);
	value.bitrateKbps = (int)probePolicy::roundDownRecommendationBitrateKbps((uint64_t)std::max(1, value.bitrateKbps));

	const CurrentSettings current = baseRecommendation(leg);
	const auto ceiling = qualityPolicy::recommendationCeiling({value.width, value.height, value.fpsNum, value.fpsDen},
								  {current.width, current.height, current.fpsNum, current.fpsDen}, completeActiveCoverage);
	const auto selected = qualityPolicy::select(ceiling, value.bitrateKbps, value.encoderFamily, qualityPolicy::QualityProfile::Generic);
	value.width = selected.video.width;
	value.height = selected.video.height;
	value.fpsNum = selected.video.fpsNum;
	value.fpsDen = selected.video.fpsDen;
	value.bitrateKbps = selected.bitrateKbps;
	return value;
}

static bool buildCompositeCompanionWorkloads(const std::vector<LegRequest> &legs, const std::vector<HardwareAssessment> &hardwareAssessments,
					     const std::vector<ProbeResult> &probeResults, std::vector<CompanionWorkload> &workloads)
{
	workloads.clear();
	for (size_t index = 0; index < legs.size(); index++) {
		if (legs[index].outputKind != "standard")
			continue;
		if (index >= hardwareAssessments.size() || !hardwareAssessments[index].passed)
			return false;
		workloads.push_back(
			{legs[index].legId, legs[index].display, provisionalCompositeCompanionValue(legs[index], hardwareAssessments[index], probeResults)});
	}
	if (workloads.empty())
		return false;

	const auto &first = workloads.front().value;
	const int commonBitrate = std::accumulate(workloads.begin(), workloads.end(), first.bitrateKbps,
						  [](int value, const CompanionWorkload &workload) { return std::min(value, workload.value.bitrateKbps); });
	for (auto &workload : workloads) {
		if (workload.value.encoderId != first.encoderId || workload.value.preset != first.preset || workload.value.presetKey != first.presetKey)
			return false;
		const auto selected = qualityPolicy::select({workload.value.width, workload.value.height, workload.value.fpsNum, workload.value.fpsDen},
							    commonBitrate, workload.value.encoderFamily, qualityPolicy::QualityProfile::Generic);
		workload.value.width = selected.video.width;
		workload.value.height = selected.video.height;
		workload.value.fpsNum = selected.video.fpsNum;
		workload.value.fpsDen = selected.video.fpsDen;
		workload.value.bitrateKbps = selected.bitrateKbps;
	}
	const int exactCommonBitrate =
		std::accumulate(workloads.begin(), workloads.end(), workloads.front().value.bitrateKbps,
				[](int value, const CompanionWorkload &workload) { return std::min(value, workload.value.bitrateKbps); });
	for (auto &workload : workloads)
		workload.value.bitrateKbps = exactCommonBitrate;
	return true;
}

static void runSession(const std::shared_ptr<Session> &session)
{
	pushEvent(session, "phase", "preflight", 0);
	if (session->cancelRequested.load()) {
		completeCancelled(session);
		return;
	}

	std::vector<LegRequest> preparedLegs;
	preparedLegs.reserve(session->legs.size());
	for (size_t index = 0; index < session->legs.size(); index++) {
		preparedLegs.push_back(withOfflinePlatformCaps(session->legs[index]));
	}
	std::vector<HardwareAssessment> hardwareAssessments(preparedLegs.size());
	std::vector<LegRequest> automaticLegs;
	std::vector<size_t> automaticLegIndices;
	for (size_t index = 0; index < preparedLegs.size(); index++) {
		if (providerOwnsEncoding(session->topology, preparedLegs[index])) {
			hardwareAssessments[index].value = baseRecommendation(preparedLegs[index]);
		} else {
			automaticLegs.push_back(preparedLegs[index]);
			automaticLegIndices.push_back(index);
		}
	}
	if (automaticLegs.empty()) {
		pushEvent(session, "phase", "hardware", 15, "hardware_provider_managed");
		pushEvent(session, "progress", "hardware", 30, "hardware_provider_managed");
	} else {
		pushEvent(session, "phase", "hardware", 15, "hardware_discovering_encoders");
		auto automaticAssessments = assessSessionHardware(session, automaticLegs);
		for (size_t index = 0; index < automaticAssessments.size(); index++)
			hardwareAssessments[automaticLegIndices[index]] = std::move(automaticAssessments[index]);
		for (size_t index = 0; index < preparedLegs.size(); index++) {
			if (providerOwnsEncoding(session->topology, preparedLegs[index]))
				pushEvent(session, "progress", "hardware", 30, "hardware_provider_managed", preparedLegs[index].legId);
		}
	}
	for (size_t index = 0; index < hardwareAssessments.size(); index++) {
		const HardwareAssessment &assessment = hardwareAssessments[index];
		if (assessment.cancelled || session->cancelRequested.load()) {
			completeCancelled(session);
			return;
		}
		if (assessment.fatal) {
			completeFailed(session, assessment.reason.empty() ? "hardware_benchmark_unavailable" : assessment.reason.c_str());
			return;
		}
	}

	std::vector<ProbeResult> probeResults;
	const size_t eligibleProbeCount =
		std::count_if(session->probes.begin(), session->probes.end(), [](const ProbeRequest &probe) { return probe.eligible; });
	std::vector<ProbeRequest *> orderedProbes;
	orderedProbes.reserve(eligibleProbeCount);
	// Emit all denials before active work advances through the bandwidth phase;
	// otherwise a later ineligible provider could regress progress back to 30.
	for (auto &probe : session->probes) {
		if (probe.eligible)
			orderedProbes.push_back(&probe);
		else
			pushEvent(session, "progress", "bandwidth", 30, probe.denialReason, probe.legId, "estimated", probe.probeId, probe.provider);
	}
	// Run the Twitch Enhanced Broadcasting workload after the standard companion
	// probes so the combined workload can use their bandwidth evidence.
	std::stable_sort(orderedProbes.begin(), orderedProbes.end(), [&](const ProbeRequest *left, const ProbeRequest *right) {
		const auto priority = [&](const ProbeRequest *value) {
			if (session->enhancedBroadcastingDualOutputWorkload && value->kind == "twitch-enhanced-broadcasting")
				return 3;
			return value->provider == "twitch" ? 0 : value->provider == "youtube" ? 2 : 1;
		};
		const int leftPriority = priority(left);
		const int rightPriority = priority(right);
		return leftPriority < rightPriority;
	});
	size_t completedProbeCount = 0;
	for (ProbeRequest *probePointer : orderedProbes) {
		ProbeRequest &probe = *probePointer;
		const auto legIt = std::find_if(preparedLegs.begin(), preparedLegs.end(), [&](const LegRequest &leg) { return leg.legId == probe.legId; });
		if (legIt == preparedLegs.end())
			continue;
		const double startProgress = 30.0 + (35.0 * (double)completedProbeCount / (double)std::max<size_t>(1, eligibleProbeCount));
		const double endProgress = 30.0 + (35.0 * (double)(completedProbeCount + 1) / (double)std::max<size_t>(1, eligibleProbeCount));
		const std::string startCode = probe.kind == "twitch-enhanced-broadcasting" ? "enhanced_broadcasting_requesting_ladder"
											   : probe.provider + "_probe_started";
		pushEvent(session, "phase", "bandwidth", startProgress, startCode, probe.legId, "active", probe.probeId, probe.provider);
		const auto probeRunStarted = std::chrono::steady_clock::now();
		std::vector<CompanionWorkload> provisionalCompanions;
		const bool companionWorkloadsReady = !session->enhancedBroadcastingDualOutputWorkload || probe.kind != "twitch-enhanced-broadcasting" ||
						     buildCompositeCompanionWorkloads(preparedLegs, hardwareAssessments, probeResults, provisionalCompanions);
		ProbeResult result;
		if (!companionWorkloadsReady) {
			result.provider = probe.provider;
			result.legId = probe.legId;
			result.method = "twitch-enhanced-broadcasting-test";
			result.observedThroughputReliable = false;
			result.errorCode = "enhanced_broadcasting_companion_workload_unavailable";
		} else {
			result = probe.kind == "twitch-enhanced-broadcasting"
					 ? runEnhancedBroadcastingProbe(session, probe, *legIt, startProgress, endProgress, provisionalCompanions)
					 : runRtmpProbe(session, probe, *legIt, startProgress, endProgress);
		}
		const auto probeRunElapsedMs =
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - probeRunStarted).count();
		if (result.provider == "youtube") {
			blog(result.success ? LOG_INFO : LOG_WARNING,
			     "[Auto Optimizer][YouTube Probe] Probe summary: success=%s, cancelled=%s, measured_aggregate=%llu Kbps, "
			     "safe_video=%llu Kbps, ceiling_reached=%s, stability=%s, observed_output_reliable_for_recommendation=%s, elapsed=%lld ms, "
			     "error=%s",
			     result.success ? "true" : "false", result.cancelled ? "true" : "false", (unsigned long long)result.measuredKbps,
			     (unsigned long long)result.safeKbps, result.ceilingReached ? "true" : "false", probeStabilityName(result.stability),
			     result.observedThroughputReliable ? "true" : "false", (long long)probeRunElapsedMs,
			     result.errorCode.empty() ? "none" : result.errorCode.c_str());
		} else if (result.method == "twitch-enhanced-broadcasting-test") {
			blog(result.success ? LOG_INFO : LOG_WARNING,
			     "[Auto Optimizer][Enhanced Broadcasting] Probe summary: success=%s, cancelled=%s, tested=%ux%u %u/%u FPS, "
			     "tracks=%u, configured_aggregate=%llu Kbps, elapsed=%lld ms, error=%s",
			     result.success ? "true" : "false", result.cancelled ? "true" : "false", result.testedWidth, result.testedHeight,
			     result.testedFpsNum, result.testedFpsDen, result.videoTrackCount, (unsigned long long)result.configuredAggregateBitrateKbps,
			     (long long)probeRunElapsedMs, result.errorCode.empty() ? "none" : result.errorCode.c_str());
		} else if (result.provider == "twitch") {
			blog(result.success ? LOG_INFO : LOG_WARNING,
			     "[Auto Optimizer][Twitch Probe] Probe summary: success=%s, cancelled=%s, measured_aggregate=%llu Kbps, "
			     "safe_video=%llu Kbps, ceiling_reached=%s, stability=%s, elapsed=%lld ms, error=%s",
			     result.success ? "true" : "false", result.cancelled ? "true" : "false", (unsigned long long)result.measuredKbps,
			     (unsigned long long)result.safeKbps, result.ceilingReached ? "true" : "false", probeStabilityName(result.stability),
			     (long long)probeRunElapsedMs, result.errorCode.empty() ? "none" : result.errorCode.c_str());
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
		const std::string completionCode =
			result.method == "twitch-enhanced-broadcasting-test"
				? (result.success ? "enhanced_broadcasting_candidate_selected" : "enhanced_broadcasting_candidate_rejected")
			: result.success && result.stability == ProbeStability::SourceUnderfill ? result.provider + "_probe_source_underfill_completed"
			: result.success                                                        ? result.provider + "_probe_completed"
			: result.stability == ProbeStability::Unstable                          ? result.provider + "_probe_unstable_estimate_used"
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

	std::optional<qualityPolicy::SharedTwoLegAllocation> dualOutputAllocation;
	if (session->dualOutputActiveProbePair) {
		const auto twitchResult =
			std::find_if(probeResults.begin(), probeResults.end(), [](const ProbeResult &result) { return result.provider == "twitch"; });
		const auto youtubeResult =
			std::find_if(probeResults.begin(), probeResults.end(), [](const ProbeResult &result) { return result.provider == "youtube"; });
		const auto usable = [](const ProbeResult &result) {
			return probePolicy::dualOutputProviderProbeIsUsable(result.success, result.observedThroughputReliable, result.measuredKbps,
									    result.safeKbps);
		};
		if (twitchResult != probeResults.end() && youtubeResult != probeResults.end()) {
			const bool allHardwareWorkloadsPassed = hardwareAssessments.size() == preparedLegs.size() &&
								std::all_of(hardwareAssessments.begin(), hardwareAssessments.end(),
									    [](const HardwareAssessment &assessment) { return assessment.passed; });
			const auto allocation = qualityPolicy::assembleSharedTwoLegAllocation(session->dualOutputActiveProbePair,
											      session->concurrentHardwareValidated, allHardwareWorkloadsPassed,
											      usable(*twitchResult), twitchResult->safeKbps,
											      usable(*youtubeResult), youtubeResult->safeKbps);
			if (allocation.valid) {
				dualOutputAllocation = allocation;
				blog(LOG_INFO,
				     "[Auto Optimizer][Dual Output] twitch_safe=%llu Kbps youtube_safe=%llu Kbps aggregate_safe=%llu Kbps "
				     "per_leg=%llu Kbps allocated=%llu Kbps concurrent_hardware=true",
				     (unsigned long long)twitchResult->safeKbps, (unsigned long long)youtubeResult->safeKbps,
				     (unsigned long long)allocation.aggregateSafeVideoKbps, (unsigned long long)allocation.perLegVideoKbps,
				     (unsigned long long)allocation.allocatedVideoKbps);
			}
		}
	}
	const bool dualOutputJointActive = dualOutputAllocation.has_value();
	std::optional<CombinedWorkloadResult> combinedWorkload;
	if (session->enhancedBroadcastingDualOutputWorkload) {
		const auto tested = std::find_if(probeResults.begin(), probeResults.end(), [](const ProbeResult &result) {
			return result.success && result.method == "twitch-enhanced-broadcasting-test" && !result.companionWorkloads.empty();
		});
		if (tested != probeResults.end() && tested->companionWorkloads.size() + 1 == preparedLegs.size())
			combinedWorkload = CombinedWorkloadResult{tested->legId, tested->companionWorkloads};
	}
	const bool compositeWorkloadValidated = combinedWorkload.has_value();

	pushEvent(session, "phase", "recommendation", 75);
	if (dualOutputJointActive)
		pushEvent(session, "progress", "recommendation", 75, "dual_output_allocating_upload", {}, "active", {}, {}, 0, nullptr,
			  (uint32_t)dualOutputAllocation->perLegVideoKbps, (uint32_t)dualOutputAllocation->aggregateSafeVideoKbps);
	std::vector<Recommendation> recommendations;
	for (size_t index = 0; index < preparedLegs.size(); index++) {
		const LegRequest &leg = preparedLegs[index];
		const HardwareAssessment &hardware = hardwareAssessments[index];
		Recommendation recommendation;
		recommendation.legId = leg.legId;
		recommendation.display = leg.display;
		recommendation.outputKind = leg.outputKind;
		recommendation.destinations = leg.destinations;
		recommendation.limits = leg.limits;
		recommendation.value = estimateRecommendation(leg, hardware);
		if (leg.additionalVideo)
			recommendation.additionalVideo = leg.additionalVideo->current;
		recommendation.reason = defaultEstimateReason(session->topology, leg);
		if (hardware.attempted && (!hardware.passed || hardware.constrained)) {
			recommendation.confidence = hardware.passed ? "medium" : "low";
			recommendation.reason = hardware.reason;
		}

		const size_t requiredProbeCount = std::count_if(session->probes.begin(), session->probes.end(),
								[&](const ProbeRequest &probe) { return probe.eligible && probe.legId == leg.legId; });
		std::vector<const ProbeResult *> legProbeResults;
		std::set<std::string> successfulProbeProviders;
		for (const auto &probeResult : probeResults) {
			if (probeResult.legId == leg.legId) {
				legProbeResults.push_back(&probeResult);
				if (probeResult.success)
					successfulProbeProviders.insert(probeResult.provider);
			}
		}
		const probePolicy::ProviderProbeCoverage coverage =
			probePolicy::classifyProviderProbeCoverage(probeableProviderCount(leg), successfulProbeProviders.size());
		const bool hasSuccessfulProbe = !successfulProbeProviders.empty();
		const bool hasPartialProviderCoverage = coverage == probePolicy::ProviderProbeCoverage::Partial;
		for (const ProbeResult *result : legProbeResults) {
			MeasurementProvenance provenance;
			provenance.provider = result->provider;
			provenance.method = result->method;
			provenance.measuredKbps = result->measuredKbps;
			provenance.safeKbps = result->safeKbps;
			provenance.headroomPercent = result->headroomPercent;
			provenance.success = result->success;
			provenance.ceilingReached = result->ceilingReached;
			provenance.testedWidth = result->testedWidth;
			provenance.testedHeight = result->testedHeight;
			provenance.testedFpsNum = result->testedFpsNum;
			provenance.testedFpsDen = result->testedFpsDen;
			provenance.testedAdditionalVideo = result->testedAdditionalVideo;
			provenance.videoTrackCount = result->videoTrackCount;
			provenance.configuredAggregateBitrateKbps = result->configuredAggregateBitrateKbps;
			recommendation.probes.push_back(std::move(provenance));
		}
		const auto enhancedResult = std::find_if(legProbeResults.begin(), legProbeResults.end(), [](const ProbeResult *result) {
			return result->success && result->method == "twitch-enhanced-broadcasting-test";
		});

		if (enhancedResult != legProbeResults.end()) {
			const ProbeResult &tested = **enhancedResult;
			recommendation.measurementMode = "active";
			recommendation.confidence = tested.pairedCadenceEvidence ? "medium" : "high";
			recommendation.reason.clear();
			recommendation.value.width = (int)tested.testedWidth;
			recommendation.value.height = (int)tested.testedHeight;
			recommendation.value.fpsNum = (int)tested.testedFpsNum;
			recommendation.value.fpsDen = (int)tested.testedFpsDen;
			recommendation.additionalVideo = tested.testedAdditionalVideo;
		} else if (hasSuccessfulProbe && (!session->dualOutputActiveProbePair || dualOutputJointActive) &&
			   (!session->enhancedBroadcastingDualOutputWorkload || compositeWorkloadValidated)) {
			recommendation.measurementMode = "active";
			if (hasPartialProviderCoverage) {
				recommendation.confidence = "low";
				// Keep a more actionable hardware failure/fallback reason when it
				// already explains why the recommendation is constrained.
				if (recommendation.reason.rfind("hardware_", 0) != 0)
					recommendation.reason = "partial_provider_probes";
			} else if (hardware.passed && session->topology == "cloud-multistream") {
				recommendation.confidence = "medium";
				recommendation.reason = "indirect_provider_probes";
			} else if (hardware.passed && !hardware.constrained) {
				recommendation.confidence = "high";
				recommendation.reason.clear();
			}
			uint64_t safeKbps = dualOutputJointActive ? dualOutputAllocation->perLegVideoKbps : UINT64_MAX;
			if (!dualOutputJointActive) {
				for (const ProbeResult *result : legProbeResults) {
					if (probePolicy::probeSafeValueContributesToActiveRecommendation(result->success, result->observedThroughputReliable,
													 result->measuredKbps, result->safeKbps))
						safeKbps = std::min(safeKbps, result->safeKbps);
				}
			}
			if (leg.limits.maxBitrateKbps > 0)
				safeKbps = std::min<uint64_t>(safeKbps, (uint64_t)leg.limits.maxBitrateKbps);
			// Never turn a low measurement into a higher recommendation merely to
			// satisfy a nominal bitrate floor. Surface the low-confidence result and
			// let Desktop decide how to explain an insufficient connection.
			const bool hasDegradedProbe = std::any_of(legProbeResults.begin(), legProbeResults.end(), [](const ProbeResult *result) {
				return result->stability == ProbeStability::Degraded || result->stability == ProbeStability::Unstable;
			});
			const bool hasVariableProbe = std::any_of(legProbeResults.begin(), legProbeResults.end(),
								  [](const ProbeResult *result) { return result->stability == ProbeStability::Variable; });
			const bool hasSourceUnderfillProbe = std::any_of(legProbeResults.begin(), legProbeResults.end(), [](const ProbeResult *result) {
				return result->stability == ProbeStability::SourceUnderfill;
			});
			if (hasDegradedProbe) {
				recommendation.confidence = "low";
				recommendation.reason = "unstable_connection";
			} else if (hasVariableProbe && recommendation.confidence == "high") {
				recommendation.confidence = "medium";
				recommendation.reason = "connection_variability_detected";
			} else if (hasSourceUnderfillProbe && recommendation.confidence == "high") {
				// The measured output is a useful conservative lower bound, but
				// source/encoder underfill did not establish the physical path limit.
				recommendation.confidence = "medium";
				recommendation.reason = "probe_source_underfill";
			}
			if (safeKbps < 500) {
				recommendation.confidence = "low";
				recommendation.reason = "insufficient_bandwidth";
			}
			recommendation.value.bitrateKbps = qualityPolicy::clampRecommendedBitrateKbps(safeKbps);
		} else if (requiredProbeCount > 0) {
			recommendation.confidence = "low";
			const bool hasUnstableProbe = std::any_of(legProbeResults.begin(), legProbeResults.end(),
								  [](const ProbeResult *result) { return result->stability == ProbeStability::Unstable; });
			recommendation.reason = session->enhancedBroadcastingDualOutputWorkload && !compositeWorkloadValidated
							? "enhanced_broadcasting_combined_workload_failed"
						: hasUnstableProbe                         ? "unstable_connection"
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
					recommendation.value.bitrateKbps, representableSafeKbps, qualityPolicy::kMaximumRecommendedBitrateKbps);
				if (observedSafeKbps < 500)
					recommendation.reason = "insufficient_bandwidth";
			}
		}

		// Keep exact throughput in measurement evidence and logs, but return a
		// stable, conservative bitrate. Round it before choosing resolution and
		// frame rate so the recommendation fits the applied bitrate.
		if (!providerOwnsEncoding(session->topology, leg)) {
			recommendation.value.bitrateKbps = qualityPolicy::clampRecommendedBitrateKbps(
				probePolicy::roundDownRecommendationBitrateKbps((uint64_t)std::max(0, recommendation.value.bitrateKbps)));
		}

		const double selectingProgress = 75.0 + 19.0 * (double)index / (double)std::max<size_t>(1, preparedLegs.size());
		const double selectedProgress = 75.0 + 19.0 * (double)(index + 1) / (double)std::max<size_t>(1, preparedLegs.size());
		if (!providerOwnsEncoding(session->topology, leg) && hardware.passed) {
			// Complete successful provider coverage is required before raising the
			// current resolution or frame rate. Estimate-only, failed, and partial
			// paths may still select lower tested resolution and frame-rate settings,
			// but never promote from
			// incomplete bandwidth evidence.
			const CurrentSettings currentCeiling = baseRecommendation(leg);
			const auto eligibleCeiling = qualityPolicy::recommendationCeiling(
				{recommendation.value.width, recommendation.value.height, recommendation.value.fpsNum, recommendation.value.fpsDen},
				{currentCeiling.width, currentCeiling.height, currentCeiling.fpsNum, currentCeiling.fpsDen},
				probePolicy::providerProbeCoverageAllowsQualityPromotion(recommendation.measurementMode == "active", coverage));
			recommendation.value.width = eligibleCeiling.width;
			recommendation.value.height = eligibleCeiling.height;
			recommendation.value.fpsNum = eligibleCeiling.fpsNum;
			recommendation.value.fpsDen = eligibleCeiling.fpsDen;
			pushEvent(session, "progress", "recommendation", selectingProgress, "recommendation_selecting_quality", leg.legId,
				  recommendation.measurementMode, {}, {}, 0, &recommendation.value, 0, (uint32_t)std::max(0, recommendation.value.bitrateKbps));
			const bool hasTwitchDestination = dualOutputJointActive ||
							  std::any_of(leg.destinations.begin(), leg.destinations.end(),
								      [](const Destination &destination) { return destination.platform == "twitch"; });
			const auto qualityProfile = hasTwitchDestination ? qualityPolicy::QualityProfile::Twitch : qualityPolicy::QualityProfile::Generic;
			const auto selected = qualityPolicy::select({recommendation.value.width, recommendation.value.height, recommendation.value.fpsNum,
								     recommendation.value.fpsDen},
								    recommendation.value.bitrateKbps, recommendation.value.encoderFamily, qualityProfile);
			recommendation.value.width = selected.video.width;
			recommendation.value.height = selected.video.height;
			recommendation.value.fpsNum = selected.video.fpsNum;
			recommendation.value.fpsDen = selected.video.fpsDen;
			recommendation.value.bitrateKbps = selected.bitrateKbps;
			if (selected.insufficientBandwidth) {
				recommendation.confidence = "low";
				recommendation.reason = "insufficient_bandwidth";
			} else if (qualityPolicy::isQualityPromotion({leg.current.width, leg.current.height, leg.current.fpsNum, leg.current.fpsDen},
								     selected.video) &&
				   recommendation.confidence != "low" && recommendation.reason != "probe_source_underfill") {
				// Synthetic encoder validation plus a successful provider probe is
				// enough to offer the higher tier, but not to claim the same
				// confidence as a recommendation that leaves video quality unchanged.
				recommendation.confidence = "medium";
				recommendation.reason = "quality_promotion_tested";
			}
			pushEvent(session, "progress", "recommendation", selectedProgress, "recommendation_quality_selected", leg.legId,
				  recommendation.measurementMode, {}, {}, 0, &recommendation.value, (uint32_t)std::max(0, recommendation.value.bitrateKbps));
		} else {
			pushEvent(session, "progress", "recommendation", selectedProgress, "recommendation_provider_managed", leg.legId,
				  recommendation.measurementMode, {}, {}, 0, &recommendation.value);
		}
		if (combinedWorkload && leg.outputKind == "standard") {
			const auto tested = std::find_if(combinedWorkload->companionLegs.begin(), combinedWorkload->companionLegs.end(),
							 [&](const CompanionWorkload &companion) { return companion.legId == leg.legId; });
			if (tested != combinedWorkload->companionLegs.end())
				recommendation.value = tested->value;
		}

		recommendations.push_back(std::move(recommendation));
	}

	std::optional<AggregateUploadResult> aggregateUpload;
	if (dualOutputJointActive)
		aggregateUpload = AggregateUploadResult{dualOutputAllocation->aggregateSafeVideoKbps, dualOutputAllocation->allocatedVideoKbps};
	{
		std::lock_guard<std::mutex> lock(session->mutex);
		session->resultJson = serializeResult(*session, "complete", recommendations, {}, aggregateUpload, combinedWorkload);
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
	if (state == SessionState::Running) {
		session->cancelRequested.store(true);
		session->probeConfirmationCondition.notify_all();
		{
			std::lock_guard<std::mutex> lock(session->probeMutex);
			for (obs_output_t *output : session->activeProbeOutputs) {
				if (output)
					obs_output_force_stop(output);
			}
		}
	}

	// The worker publishes terminal state immediately before returning, so close
	// may receive the final event before the worker finishes. Join that short
	// remaining execution before removing the session.
	if (session->worker.valid() && session->worker.wait_for(std::chrono::milliseconds(kCancelTimeoutMs)) != std::future_status::ready) {
		if (state == SessionState::Running)
			pushEvent(session, "error", "cleanup", 100, "cleanup_timeout");
		return false;
	}
	return true;
}

} // namespace

void RegisterOutputTypes()
{
	registerHardwareBenchmarkOutput();
}

void Register(ipc::server &srv)
{
	auto collection = std::make_shared<ipc::collection>("AutoConfig");

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
	if (session->events.empty()) {
		rval.push_back(ipc::value());
		return;
	}

	rval.push_back(ipc::value(serializeEvent(*session, session->events.front())));
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

bool CancelActiveSession()
{
	std::shared_ptr<Session> session;
	{
		std::lock_guard<std::mutex> lock(sessionsMutex);
		session = activeSession;
	}
	return !session || requestCancellation(session);
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
				for (obs_output_t *output : session->activeProbeOutputs) {
					if (output)
						obs_output_force_stop(output);
				}
			}
		}

		// Shutdown is the last barrier before libobs teardown. Unlike ordinary
		// cancellation, it must wait until the worker releases every temporary OBS
		// resource, even if cleanup exceeds the normal cancellation deadline.
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
