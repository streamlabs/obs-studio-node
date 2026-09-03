#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace autoOptimizer {
namespace probePolicy {

constexpr uint32_t kBasisPointsScale = 10000;
constexpr uint32_t kYoutubeCleanThroughputMinimumBasisPoints = 9000;
constexpr uint32_t kYoutubeCleanDropMaximumBasisPoints = 200;
constexpr uint32_t kYoutubeCleanCongestionHighMaximumBasisPoints = 1000;
constexpr uint32_t kYoutubeCleanCongestionSevereMaximumBasisPoints = 200;
constexpr uint32_t kYoutubeHardDropMaximumBasisPoints = 500;
constexpr uint32_t kYoutubeHardCongestionHighMaximumBasisPoints = 3000;
constexpr uint32_t kYoutubeHardCongestionSevereMaximumBasisPoints = 1000;
constexpr uint32_t kTwitchDirectPassMinimumBasisPoints = 9500;
constexpr uint32_t kTwitchCongestionHighMaximumBasisPoints = 1000;
constexpr uint32_t kTwitchCongestionSevereMaximumBasisPoints = 200;

namespace detail {

inline std::string asciiLowerCopy(std::string value)
{
	for (char &character : value)
		character = character >= 'A' && character <= 'Z' ? static_cast<char>(character + ('a' - 'A')) : character;
	return value;
}

inline bool containsWhitespaceOrControl(const std::string &value)
{
	return std::any_of(value.begin(), value.end(), [](unsigned char character) { return std::isspace(character) || std::iscntrl(character); });
}

} // namespace detail

/** Return whether a standard Twitch probe may connect to this server. */
inline bool isOfficialTwitchServer(const std::string &server)
{
	const std::string value = detail::asciiLowerCopy(server);
	if (value == "auto")
		return true;

	size_t hostStart = 0;
	if (value.starts_with("rtmp://"))
		hostStart = 7;
	else if (value.starts_with("rtmps://"))
		hostStart = 8;
	else
		return false;

	const size_t hostEnd = value.find_first_of("/:?#", hostStart);
	const std::string host = value.substr(hostStart, hostEnd == std::string::npos ? std::string::npos : hostEnd - hostStart);
	if (host.empty() || host.find('@') != std::string::npos)
		return false;

	return host == "live.twitch.tv" || host.ends_with(".twitch.tv") || host == "live-video.net" || host.ends_with(".live-video.net");
}

/** Bound Twitch credentials before a standard or Enhanced Broadcasting probe. */
inline bool isBoundedTwitchKey(const std::string &key)
{
	return !key.empty() && key.size() <= 4096 && !detail::containsWhitespaceOrControl(key);
}

/** Bound an unbound YouTube stream key and reject characters with URL meaning. */
inline bool isBoundedYoutubeKey(const std::string &key)
{
	if (key.empty() || key.size() > 1024 || detail::containsWhitespaceOrControl(key))
		return false;
	return key.find_first_of("/\\?#@:") == std::string::npos;
}

/** Return whether an unbound YouTube probe may connect to this RTMPS endpoint. */
inline bool isOfficialYoutubeRtmpsServer(const std::string &server)
{
	if (server.empty() || server.size() > 2048 || detail::containsWhitespaceOrControl(server))
		return false;

	const std::string value = detail::asciiLowerCopy(server);
	constexpr std::string_view scheme = "rtmps://";
	if (!value.starts_with(scheme) || value.find_first_of("?#") != std::string::npos)
		return false;

	const size_t authorityStart = scheme.size();
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

struct ActiveProbeEligibility {
	bool eligible = false;
	std::string_view denialReason = "active_probe_not_eligible";
};

/** Require two registered, distinct canvases assigned to opposite displays. */
inline bool standardDualOutputCanvasPairIsValid(std::string_view firstDisplay, uint64_t firstCanvasId, bool firstCanvasRegistered,
						std::string_view secondDisplay, uint64_t secondCanvasId, bool secondCanvasRegistered)
{
	const bool displaysValid = (firstDisplay == "horizontal" && secondDisplay == "vertical") ||
				   (firstDisplay == "vertical" && secondDisplay == "horizontal");
	return displaysValid && firstCanvasRegistered && secondCanvasRegistered && firstCanvasId != secondCanvasId;
}

/**
 * Make the final active-probe decision after the caller validates topology and
 * any live canvas references. A multi-leg Dual Output request receives its
 * more specific denial reason only when it is not the supported joint pair.
 */
inline ActiveProbeEligibility decideActiveProbeEligibility(bool providerKnown, bool destinationPresent, bool topologyEligible, bool providerContractValid,
							   bool uniqueLegProviderProbe, bool multipleDualOutputLegs, bool supportedJointProbePair)
{
	const bool jointProbeShapeAllowed = !multipleDualOutputLegs || supportedJointProbePair;
	const bool eligible = providerKnown && destinationPresent && topologyEligible && providerContractValid && uniqueLegProviderProbe &&
			      jointProbeShapeAllowed;
	if (eligible)
		return {true, {}};
	return {false, multipleDualOutputLegs && !supportedJointProbePair ? "dual_output_multiple_active_legs" : "active_probe_not_eligible"};
}

struct YoutubeProbeSampleMetrics {
	// The observed-output/target ratio is useful for identifying source or
	// encoder underfill and for choosing a conservative recommendation. It is
	// not transport-impairment evidence by itself.
	uint32_t throughputBasisPoints = 0;
	uint32_t dropBasisPoints = 0;
	uint32_t congestionHighBasisPoints = 0;
	uint32_t congestionSevereBasisPoints = 0;
};

enum class YoutubeProbeSampleClass { Clean, Marginal, Hard };

enum class YoutubeProbeLoadResult { Accepted, SourceUnderfill, TransportPressure };

enum class YoutubeBaselineDecision { Clean, Impaired, NeedsThird, Unstable };

struct YoutubeBaselineAssessment {
	YoutubeBaselineDecision decision = YoutubeBaselineDecision::NeedsThird;
	YoutubeProbeSampleMetrics reference;
};

struct YoutubeRecoveryGate {
	explicit YoutubeRecoveryGate(uint32_t requiredHealthySamples_) : requiredHealthySamples(std::max(1U, requiredHealthySamples_)) {}

	bool observe(bool congestionHealthy, bool droppedFramesUnchanged)
	{
		if (!congestionHealthy || !droppedFramesUnchanged) {
			consecutiveHealthySamples = 0;
			return false;
		}

		consecutiveHealthySamples++;
		return consecutiveHealthySamples >= requiredHealthySamples;
	}

	uint32_t requiredHealthySamples = 1;
	uint32_t consecutiveHealthySamples = 0;
};

enum class YoutubeConfirmationDecision { CapacityKnee, TransientRecovered, PathUnstable, Inconsistent };

enum class YoutubeExtendedValidationDecision { TargetAccepted, SourceUnderfill, CapacityKnee };

enum class ProviderProbeCoverage { None, Partial, Complete };

/**
 * Report whether none, some, or all measurable providers on an output were
 * probed. The caller counts each provider once. Partial coverage remains useful
 * but must have lower confidence than complete coverage.
 */
inline ProviderProbeCoverage classifyProviderProbeCoverage(size_t expectedProviderCount, size_t eligibleProviderCount)
{
	if (eligibleProviderCount == 0)
		return ProviderProbeCoverage::None;
	if (expectedProviderCount > eligibleProviderCount)
		return ProviderProbeCoverage::Partial;
	return ProviderProbeCoverage::Complete;
}

inline bool providerProbeCoverageAllowsQualityPromotion(bool activeMeasurement, ProviderProbeCoverage coverage)
{
	return activeMeasurement && coverage == ProviderProbeCoverage::Complete;
}

inline bool probeSafeValueContributesToActiveRecommendation(bool success, bool observedThroughputReliable, uint64_t measuredKbps, uint64_t safeKbps)
{
	return safeKbps > 0 && (success || (observedThroughputReliable && measuredKbps > 0));
}

// Combine the two sequential provider probes into one shared-upload allocation
// only when both produced reliable measurements. A conservative or failed
// observation may still support one estimated output, but cannot prove that
// both outputs are safe simultaneously.
inline bool dualOutputProviderProbeIsUsable(bool success, bool observedThroughputReliable, uint64_t measuredKbps, uint64_t safeKbps)
{
	return success && observedThroughputReliable && measuredKbps > 0 && safeKbps > 0;
}

inline uint64_t roundDownRecommendationBitrateKbps(uint64_t bitrateKbps)
{
	constexpr uint64_t quantumKbps = 100;
	// A positive value below the rounding increment cannot be rounded safely: up
	// would exceed the measurement and down would become zero. Preserve it
	// unchanged.
	if (bitrateKbps < quantumKbps)
		return bitrateKbps;
	return bitrateKbps - bitrateKbps % quantumKbps;
}

inline uint32_t ratioBasisPoints(uint32_t numerator, uint32_t denominator)
{
	if (denominator == 0)
		return 0;

	const uint64_t scaled = (uint64_t)numerator * kBasisPointsScale / denominator;
	return (uint32_t)std::min<uint64_t>(scaled, UINT32_MAX);
}

inline YoutubeProbeSampleMetrics makeYoutubeProbeSampleMetrics(uint32_t measuredAggregateKbps, uint32_t expectedAggregateKbps, uint32_t droppedFrames,
							       uint32_t totalFrames, uint32_t congestionHighSamples, uint32_t congestionSevereSamples,
							       uint32_t congestionSamples)
{
	return {ratioBasisPoints(measuredAggregateKbps, expectedAggregateKbps), ratioBasisPoints(droppedFrames, totalFrames),
		ratioBasisPoints(congestionHighSamples, congestionSamples), ratioBasisPoints(congestionSevereSamples, congestionSamples)};
}

inline YoutubeProbeSampleClass classifyYoutubeProbeTransport(const YoutubeProbeSampleMetrics &sample)
{
	if (sample.dropBasisPoints > kYoutubeHardDropMaximumBasisPoints || sample.congestionHighBasisPoints > kYoutubeHardCongestionHighMaximumBasisPoints ||
	    sample.congestionSevereBasisPoints > kYoutubeHardCongestionSevereMaximumBasisPoints)
		return YoutubeProbeSampleClass::Hard;

	if (sample.dropBasisPoints <= kYoutubeCleanDropMaximumBasisPoints &&
	    sample.congestionHighBasisPoints <= kYoutubeCleanCongestionHighMaximumBasisPoints &&
	    sample.congestionSevereBasisPoints <= kYoutubeCleanCongestionSevereMaximumBasisPoints)
		return YoutubeProbeSampleClass::Clean;

	return YoutubeProbeSampleClass::Marginal;
}

inline bool youtubeThroughputAtTarget(const YoutubeProbeSampleMetrics &sample)
{
	return sample.throughputBasisPoints >= kYoutubeCleanThroughputMinimumBasisPoints;
}

inline uint32_t absoluteDifference(uint32_t left, uint32_t right)
{
	return left > right ? left - right : right - left;
}

inline uint32_t medianOfThree(uint32_t first, uint32_t second, uint32_t third)
{
	return (uint32_t)(((uint64_t)first + second + third) - std::min({first, second, third}) - std::max({first, second, third}));
}

inline YoutubeProbeSampleMetrics conservativeReference(const YoutubeProbeSampleMetrics &first, const YoutubeProbeSampleMetrics &second)
{
	return {std::min(first.throughputBasisPoints, second.throughputBasisPoints), std::max(first.dropBasisPoints, second.dropBasisPoints),
		std::max(first.congestionHighBasisPoints, second.congestionHighBasisPoints),
		std::max(first.congestionSevereBasisPoints, second.congestionSevereBasisPoints)};
}

inline bool youtubeBaselineSamplesSimilar(const YoutubeProbeSampleMetrics &first, const YoutubeProbeSampleMetrics &second)
{
	return absoluteDifference(first.dropBasisPoints, second.dropBasisPoints) <= 100 &&
	       absoluteDifference(first.congestionHighBasisPoints, second.congestionHighBasisPoints) <= 1000 &&
	       absoluteDifference(first.congestionSevereBasisPoints, second.congestionSevereBasisPoints) <= 500;
}

inline YoutubeBaselineAssessment assessYoutubeBaseline(const YoutubeProbeSampleMetrics &first, const YoutubeProbeSampleMetrics &second)
{
	const YoutubeProbeSampleClass firstClass = classifyYoutubeProbeTransport(first);
	const YoutubeProbeSampleClass secondClass = classifyYoutubeProbeTransport(second);
	const YoutubeProbeSampleMetrics reference = conservativeReference(first, second);

	if (firstClass == YoutubeProbeSampleClass::Hard && secondClass == YoutubeProbeSampleClass::Hard)
		return {YoutubeBaselineDecision::Unstable, reference};
	if (firstClass == YoutubeProbeSampleClass::Clean && secondClass == YoutubeProbeSampleClass::Clean)
		return {youtubeBaselineSamplesSimilar(first, second) ? YoutubeBaselineDecision::Clean : YoutubeBaselineDecision::NeedsThird, reference};
	if (firstClass == YoutubeProbeSampleClass::Marginal && secondClass == YoutubeProbeSampleClass::Marginal && youtubeBaselineSamplesSimilar(first, second))
		return {YoutubeBaselineDecision::Impaired, reference};
	return {YoutubeBaselineDecision::NeedsThird, reference};
}

inline YoutubeBaselineAssessment resolveYoutubeBaseline(const YoutubeProbeSampleMetrics &first, const YoutubeProbeSampleMetrics &second,
							const YoutubeProbeSampleMetrics &third)
{
	YoutubeProbeSampleMetrics reference{medianOfThree(first.throughputBasisPoints, second.throughputBasisPoints, third.throughputBasisPoints),
					    medianOfThree(first.dropBasisPoints, second.dropBasisPoints, third.dropBasisPoints),
					    medianOfThree(first.congestionHighBasisPoints, second.congestionHighBasisPoints, third.congestionHighBasisPoints),
					    medianOfThree(first.congestionSevereBasisPoints, second.congestionSevereBasisPoints,
							  third.congestionSevereBasisPoints)};
	const size_t hardSamples = (classifyYoutubeProbeTransport(first) == YoutubeProbeSampleClass::Hard ? 1 : 0) +
				   (classifyYoutubeProbeTransport(second) == YoutubeProbeSampleClass::Hard ? 1 : 0) +
				   (classifyYoutubeProbeTransport(third) == YoutubeProbeSampleClass::Hard ? 1 : 0);
	if (hardSamples >= 2)
		return {YoutubeBaselineDecision::Unstable, reference};
	const YoutubeProbeSampleClass referenceClass = classifyYoutubeProbeTransport(reference);
	if (referenceClass == YoutubeProbeSampleClass::Hard)
		return {YoutubeBaselineDecision::Unstable, reference};
	if (referenceClass == YoutubeProbeSampleClass::Clean)
		return {YoutubeBaselineDecision::Clean, reference};
	return {YoutubeBaselineDecision::Impaired, reference};
}

inline bool youtubeTransportAccepted(const YoutubeProbeSampleMetrics &sample, const YoutubeBaselineAssessment &baseline)
{
	if (classifyYoutubeProbeTransport(sample) == YoutubeProbeSampleClass::Clean)
		return true;
	if (baseline.decision != YoutubeBaselineDecision::Impaired)
		return false;

	const YoutubeProbeSampleMetrics &reference = baseline.reference;
	const uint32_t maximumDrops = std::min<uint32_t>(kYoutubeHardDropMaximumBasisPoints, reference.dropBasisPoints + 100);
	const uint32_t maximumHighCongestion = std::min<uint32_t>(kYoutubeHardCongestionHighMaximumBasisPoints, reference.congestionHighBasisPoints + 1000);
	const uint32_t maximumSevereCongestion =
		std::min<uint32_t>(kYoutubeHardCongestionSevereMaximumBasisPoints, reference.congestionSevereBasisPoints + 500);
	return sample.dropBasisPoints <= maximumDrops && sample.congestionHighBasisPoints <= maximumHighCongestion &&
	       sample.congestionSevereBasisPoints <= maximumSevereCongestion;
}

inline YoutubeProbeLoadResult classifyYoutubeProbeLoad(const YoutubeProbeSampleMetrics &sample, const YoutubeBaselineAssessment &baseline)
{
	if (!youtubeTransportAccepted(sample, baseline))
		return YoutubeProbeLoadResult::TransportPressure;
	return youtubeThroughputAtTarget(sample) ? YoutubeProbeLoadResult::Accepted : YoutubeProbeLoadResult::SourceUnderfill;
}

inline bool youtubeRequiresCapacityConfirmation(YoutubeProbeLoadResult loadResult)
{
	return loadResult == YoutubeProbeLoadResult::TransportPressure;
}

struct YoutubeSourceUnderfillState {
	bool terminal = false;

	void observeTransportClean(YoutubeProbeLoadResult loadResult)
	{
		if (!youtubeRequiresCapacityConfirmation(loadResult))
			terminal = loadResult == YoutubeProbeLoadResult::SourceUnderfill;
	}

	void confirmCapacityKnee() { terminal = false; }
};

inline bool youtubeSampleAccepted(const YoutubeProbeSampleMetrics &sample, const YoutubeBaselineAssessment &baseline)
{
	return !youtubeRequiresCapacityConfirmation(classifyYoutubeProbeLoad(sample, baseline));
}

inline bool youtubeLowControlRecovered(const YoutubeProbeSampleMetrics &control, const YoutubeProbeSampleMetrics &original,
				       const YoutubeBaselineAssessment &baseline)
{
	if (!youtubeTransportAccepted(control, baseline))
		return false;
	return control.dropBasisPoints <= (uint64_t)original.dropBasisPoints + 100 &&
	       control.congestionHighBasisPoints <= (uint64_t)original.congestionHighBasisPoints + 1000 &&
	       control.congestionSevereBasisPoints <= (uint64_t)original.congestionSevereBasisPoints + 500;
}

inline YoutubeConfirmationDecision decideYoutubeConfirmation(bool lowControlRecovered, bool highRetryAccepted)
{
	if (lowControlRecovered)
		return highRetryAccepted ? YoutubeConfirmationDecision::TransientRecovered : YoutubeConfirmationDecision::CapacityKnee;
	return highRetryAccepted ? YoutubeConfirmationDecision::Inconsistent : YoutubeConfirmationDecision::PathUnstable;
}

inline YoutubeExtendedValidationDecision decideYoutubeExtendedValidation(YoutubeProbeLoadResult loadResult)
{
	switch (loadResult) {
	case YoutubeProbeLoadResult::Accepted:
		return YoutubeExtendedValidationDecision::TargetAccepted;
	case YoutubeProbeLoadResult::SourceUnderfill:
		return YoutubeExtendedValidationDecision::SourceUnderfill;
	case YoutubeProbeLoadResult::TransportPressure:
		return YoutubeExtendedValidationDecision::CapacityKnee;
	}
	return YoutubeExtendedValidationDecision::CapacityKnee;
}

inline uint64_t youtubeConfirmedPressureCapacityKbps(uint64_t firstMeasuredKbps, uint64_t retryMeasuredKbps, uint64_t testedTargetKbps)
{
	return std::min({firstMeasuredKbps, retryMeasuredKbps, testedTargetKbps});
}

struct YoutubeRampEvidence {
	bool passedStep = false;
	uint64_t recommendationBasisKbps = 0;
	uint64_t validatedVideoKbps = 0;

	void observeAcceptedTarget(uint64_t measuredAggregateKbps, uint64_t targetVideoKbps)
	{
		passedStep = true;
		recommendationBasisKbps = measuredAggregateKbps;
		validatedVideoKbps = targetVideoKbps;
	}

	void observeTransportCleanLowerBound(uint64_t measuredAggregateKbps, uint64_t targetVideoKbps)
	{
		passedStep = true;
		// An underfilled but transport-clean observation establishes only a
		// lower bound. Preserve the highest such bound instead of regressing to
		// a noisier later sample. Aggregate bytes can include audio, so never
		// let that lower bound exceed the requested video target.
		recommendationBasisKbps = std::max(recommendationBasisKbps, measuredAggregateKbps);
		validatedVideoKbps = std::max(validatedVideoKbps, std::min(measuredAggregateKbps, targetVideoKbps));
	}

	void observeConfirmedCapacity(uint64_t measuredAggregateKbps, uint64_t targetVideoKbps)
	{
		passedStep = true;
		// A sustained pressure window measures the path's delivered capacity.
		// Use it directly, without a fixed percentage reservation, while never
		// recommending more video bitrate than the tested target.
		recommendationBasisKbps = measuredAggregateKbps;
		validatedVideoKbps = std::min(measuredAggregateKbps, targetVideoKbps);
	}

	uint64_t recommendedVideoKbps() const { return validatedVideoKbps; }
};

struct TwitchProbeDecision {
	bool targetPassed = false;
	bool extendSample = false;
	uint64_t recommendedVideoKbps = 0;
};

inline bool twitchCongestionIsSustained(uint32_t congestionHighSamples, uint32_t congestionSevereSamples, uint32_t congestionSamples)
{
	return ratioBasisPoints(congestionHighSamples, congestionSamples) > kTwitchCongestionHighMaximumBasisPoints ||
	       ratioBasisPoints(congestionSevereSamples, congestionSamples) > kTwitchCongestionSevereMaximumBasisPoints;
}

// A materially underfilled but transport-clean initial window is ambiguous:
// socket buffering can temporarily hide a path limit, while sender underfill
// alone is not transport evidence. Ask the caller for one longer same-target
// window. A clean extended window validates the target; explicit pressure uses
// the sustained observed rate directly, without a percentage reservation.
inline TwitchProbeDecision decideTwitchProbe(uint64_t measuredAggregateKbps, uint64_t targetVideoKbps, uint32_t droppedFrames, bool sustainedCongestion = false,
					     bool extendedSample = false)
{
	if (measuredAggregateKbps == 0 || targetVideoKbps == 0)
		return {};
	const bool transportPressure = droppedFrames > 0 || sustainedCongestion;
	const bool directPass = measuredAggregateKbps * kBasisPointsScale >= targetVideoKbps * kTwitchDirectPassMinimumBasisPoints;
	if (!transportPressure && !extendedSample && !directPass)
		return {false, true, 0};
	if (!transportPressure)
		return {true, false, targetVideoKbps};
	return {false, false, std::min(measuredAggregateKbps, targetVideoKbps)};
}

inline bool hasProbeThroughputMetrics(bool success, uint64_t measuredKbps)
{
	return success || measuredKbps > 0;
}

inline int clampEstimateToObservedSafe(int estimatedKbps, uint64_t observedSafeKbps, int maximumKbps)
{
	if (observedSafeKbps == 0 || maximumKbps <= 0)
		return estimatedKbps;
	return std::min(estimatedKbps, (int)std::min<uint64_t>(observedSafeKbps, (uint64_t)maximumKbps));
}

inline int effectiveProbeCeilingKbps(int probeMaximumKbps, int platformMaximumKbps, int requestMaximumKbps)
{
	int effective = probeMaximumKbps;
	if (platformMaximumKbps > 0)
		effective = std::min(effective, platformMaximumKbps);
	if (requestMaximumKbps > 0)
		effective = std::min(effective, requestMaximumKbps);
	return effective;
}

inline bool reachedEffectiveProbeCeiling(int targetKbps, int effectiveCeilingKbps)
{
	return targetKbps > 0 && effectiveCeilingKbps > 0 && targetKbps >= effectiveCeilingKbps;
}

inline double probeSubstepProgress(double slotStart, double slotEnd, size_t stepIndex, size_t stepCount)
{
	if (stepCount == 0 || slotEnd <= slotStart)
		return slotStart;

	const size_t clampedIndex = std::min(stepIndex, stepCount - 1);
	return slotStart + (slotEnd - slotStart) * (double)(clampedIndex + 1) / (double)(stepCount + 1);
}

} // namespace probePolicy
} // namespace autoOptimizer
