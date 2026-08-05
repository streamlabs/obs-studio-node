#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace autoConfig {
namespace probePolicy {

constexpr uint32_t kBasisPointsScale = 10000;
constexpr uint32_t kYoutubeCleanThroughputMinimumBasisPoints = 9000;
constexpr uint32_t kYoutubeCleanDropMaximumBasisPoints = 200;
constexpr uint32_t kYoutubeCleanCongestionHighMaximumBasisPoints = 1000;
constexpr uint32_t kYoutubeCleanCongestionSevereMaximumBasisPoints = 200;
constexpr uint32_t kYoutubeHardThroughputMinimumBasisPoints = 7500;
constexpr uint32_t kYoutubeHardDropMaximumBasisPoints = 500;
constexpr uint32_t kYoutubeHardCongestionHighMaximumBasisPoints = 3000;
constexpr uint32_t kYoutubeHardCongestionSevereMaximumBasisPoints = 1000;

struct YoutubeProbeSampleMetrics {
	uint32_t throughputBasisPoints = 0;
	uint32_t dropBasisPoints = 0;
	uint32_t congestionHighBasisPoints = 0;
	uint32_t congestionSevereBasisPoints = 0;
};

enum class YoutubeProbeSampleClass { Clean, Marginal, Hard };

enum class YoutubeBaselineDecision { Clean, Impaired, NeedsThird, Unstable };

struct YoutubeBaselineAssessment {
	YoutubeBaselineDecision decision = YoutubeBaselineDecision::NeedsThird;
	YoutubeProbeSampleMetrics reference;
};

enum class YoutubeConfirmationDecision { CapacityKnee, TransientRecovered, PathUnstable, Inconsistent };

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

inline YoutubeProbeSampleClass classifyYoutubeProbeSample(const YoutubeProbeSampleMetrics &sample)
{
	if (sample.throughputBasisPoints < kYoutubeHardThroughputMinimumBasisPoints || sample.dropBasisPoints > kYoutubeHardDropMaximumBasisPoints ||
	    sample.congestionHighBasisPoints > kYoutubeHardCongestionHighMaximumBasisPoints ||
	    sample.congestionSevereBasisPoints > kYoutubeHardCongestionSevereMaximumBasisPoints)
		return YoutubeProbeSampleClass::Hard;

	if (sample.throughputBasisPoints >= kYoutubeCleanThroughputMinimumBasisPoints && sample.dropBasisPoints <= kYoutubeCleanDropMaximumBasisPoints &&
	    sample.congestionHighBasisPoints <= kYoutubeCleanCongestionHighMaximumBasisPoints &&
	    sample.congestionSevereBasisPoints <= kYoutubeCleanCongestionSevereMaximumBasisPoints)
		return YoutubeProbeSampleClass::Clean;

	return YoutubeProbeSampleClass::Marginal;
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
	return absoluteDifference(first.throughputBasisPoints, second.throughputBasisPoints) <= 500 &&
	       absoluteDifference(first.dropBasisPoints, second.dropBasisPoints) <= 100 &&
	       absoluteDifference(first.congestionHighBasisPoints, second.congestionHighBasisPoints) <= 1000 &&
	       absoluteDifference(first.congestionSevereBasisPoints, second.congestionSevereBasisPoints) <= 500;
}

inline YoutubeBaselineAssessment assessYoutubeBaseline(const YoutubeProbeSampleMetrics &first, const YoutubeProbeSampleMetrics &second)
{
	const YoutubeProbeSampleClass firstClass = classifyYoutubeProbeSample(first);
	const YoutubeProbeSampleClass secondClass = classifyYoutubeProbeSample(second);
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
	const size_t hardSamples = (classifyYoutubeProbeSample(first) == YoutubeProbeSampleClass::Hard ? 1 : 0) +
				   (classifyYoutubeProbeSample(second) == YoutubeProbeSampleClass::Hard ? 1 : 0) +
				   (classifyYoutubeProbeSample(third) == YoutubeProbeSampleClass::Hard ? 1 : 0);
	if (hardSamples >= 2)
		return {YoutubeBaselineDecision::Unstable, reference};
	const YoutubeProbeSampleClass referenceClass = classifyYoutubeProbeSample(reference);
	if (referenceClass == YoutubeProbeSampleClass::Hard)
		return {YoutubeBaselineDecision::Unstable, reference};
	if (referenceClass == YoutubeProbeSampleClass::Clean)
		return {YoutubeBaselineDecision::Clean, reference};
	return {YoutubeBaselineDecision::Impaired, reference};
}

inline bool youtubeSampleAccepted(const YoutubeProbeSampleMetrics &sample, const YoutubeBaselineAssessment &baseline)
{
	if (classifyYoutubeProbeSample(sample) == YoutubeProbeSampleClass::Clean)
		return true;
	if (baseline.decision != YoutubeBaselineDecision::Impaired)
		return false;

	const YoutubeProbeSampleMetrics &reference = baseline.reference;
	const uint32_t minimumThroughput =
		std::max<uint32_t>(kYoutubeHardThroughputMinimumBasisPoints, reference.throughputBasisPoints > 500 ? reference.throughputBasisPoints - 500 : 0);
	const uint32_t maximumDrops = std::min<uint32_t>(kYoutubeHardDropMaximumBasisPoints, reference.dropBasisPoints + 100);
	const uint32_t maximumHighCongestion = std::min<uint32_t>(kYoutubeHardCongestionHighMaximumBasisPoints, reference.congestionHighBasisPoints + 1000);
	const uint32_t maximumSevereCongestion =
		std::min<uint32_t>(kYoutubeHardCongestionSevereMaximumBasisPoints, reference.congestionSevereBasisPoints + 500);
	return sample.throughputBasisPoints >= minimumThroughput && sample.dropBasisPoints <= maximumDrops &&
	       sample.congestionHighBasisPoints <= maximumHighCongestion && sample.congestionSevereBasisPoints <= maximumSevereCongestion;
}

inline bool youtubeLowControlRecovered(const YoutubeProbeSampleMetrics &control, const YoutubeProbeSampleMetrics &original,
				       const YoutubeBaselineAssessment &baseline)
{
	if (!youtubeSampleAccepted(control, baseline))
		return false;
	return (uint64_t)control.throughputBasisPoints + 500 >= original.throughputBasisPoints &&
	       control.dropBasisPoints <= (uint64_t)original.dropBasisPoints + 100 &&
	       control.congestionHighBasisPoints <= (uint64_t)original.congestionHighBasisPoints + 1000 &&
	       control.congestionSevereBasisPoints <= (uint64_t)original.congestionSevereBasisPoints + 500;
}

inline YoutubeConfirmationDecision decideYoutubeConfirmation(bool lowControlRecovered, bool highRetryAccepted)
{
	if (lowControlRecovered)
		return highRetryAccepted ? YoutubeConfirmationDecision::TransientRecovered : YoutubeConfirmationDecision::CapacityKnee;
	return highRetryAccepted ? YoutubeConfirmationDecision::Inconsistent : YoutubeConfirmationDecision::PathUnstable;
}

struct YoutubeRampEvidence {
	bool passedStep = false;
	uint64_t recommendationBasisKbps = 0;
	uint64_t failedUpperBoundKbps = 0;

	void observe(uint64_t measuredKbps, bool passed, uint64_t attemptedAggregateKbps = 0)
	{
		if (passed) {
			passedStep = true;
			recommendationBasisKbps = measuredKbps;
			return;
		}

		failedUpperBoundKbps = attemptedAggregateKbps > 0 ? std::min(measuredKbps, attemptedAggregateKbps) : measuredKbps;
		if (!passedStep)
			recommendationBasisKbps = measuredKbps;
	}

	uint64_t safeVideoKbps(int safeMultiplierPercent, uint64_t audioKbps) const
	{
		if (recommendationBasisKbps == 0 || safeMultiplierPercent <= 0)
			return 0;

		uint64_t safe = std::max<uint64_t>(1, recommendationBasisKbps * (uint64_t)safeMultiplierPercent / 100ULL);
		if (failedUpperBoundKbps > 0) {
			const uint64_t failedSafe = std::max<uint64_t>(1, failedUpperBoundKbps * (uint64_t)safeMultiplierPercent / 100ULL);
			safe = std::min(safe, failedSafe);
		}
		return safe > audioKbps ? safe - audioKbps : 0;
	}
};

inline uint64_t safeVideoKbps(uint64_t measuredAggregateKbps, int safeMultiplierPercent, uint64_t audioKbps)
{
	if (measuredAggregateKbps == 0 || safeMultiplierPercent <= 0)
		return 0;
	const uint64_t safeAggregateKbps = measuredAggregateKbps * (uint64_t)safeMultiplierPercent / 100ULL;
	return safeAggregateKbps > audioKbps ? safeAggregateKbps - audioKbps : 0;
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
} // namespace autoConfig
