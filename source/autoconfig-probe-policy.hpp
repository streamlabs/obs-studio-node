#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace autoConfig {
namespace probePolicy {

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
