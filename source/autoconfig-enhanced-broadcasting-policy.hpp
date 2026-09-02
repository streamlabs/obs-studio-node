#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "osn-common.hpp"

namespace autoConfig::enhancedBroadcastingPolicy {

struct VideoCandidate {
	uint32_t width;
	uint32_t height;
	uint32_t fpsNum;
	uint32_t fpsDen;
};

inline bool fpsAtMost(const VideoCandidate &candidate, uint32_t maxNum, uint32_t maxDen)
{
	if (maxNum == 0)
		return true;
	maxDen = std::max(1U, maxDen);
	return (uint64_t)candidate.fpsNum * maxDen <= (uint64_t)maxNum * candidate.fpsDen;
}

inline VideoCandidate pairedVerticalCandidate(const VideoCandidate &primary)
{
	return {primary.height, primary.width, primary.fpsNum, primary.fpsDen};
}

inline bool candidateFitsLimits(const VideoCandidate &candidate, uint32_t maxWidth, uint32_t maxHeight, uint32_t maxFpsNum, uint32_t maxFpsDen)
{
	return (maxWidth == 0 || candidate.width <= maxWidth) && (maxHeight == 0 || candidate.height <= maxHeight) &&
	       fpsAtMost(candidate, maxFpsNum, maxFpsDen);
}

inline bool canvasIndexIsValid(size_t canvasIndex, size_t canvasCount)
{
	return canvasIndex < canvasCount;
}

inline bool everyCanvasCovered(const std::vector<bool> &covered)
{
	return !covered.empty() && std::all_of(covered.begin(), covered.end(), [](bool value) { return value; });
}

/**
 * Validates the canvas coverage represented by distinct sampled video inputs.
 * A Twitch ladder may expose multiple post-rescale inputs for one canvas, so
 * the number of sampled inputs is not required to equal the canvas count.
 */
inline bool everyCanvasHasSampledInput(const std::vector<size_t> &sampledCanvasIndexes, size_t canvasCount)
{
	if (canvasCount == 0)
		return false;
	std::vector<bool> covered(canvasCount, false);
	for (const size_t canvasIndex : sampledCanvasIndexes) {
		if (!canvasIndexIsValid(canvasIndex, canvasCount))
			return false;
		covered[canvasIndex] = true;
	}
	return everyCanvasCovered(covered);
}

/**
 * Validates the registered canvas identities used by an Enhanced Broadcasting
 * workload. Canvas ID 0 is valid; only INVALID_ID means that no canvas was
 * supplied. A paired workload requires two distinct registered canvas IDs that
 * remain live.
 */
template<typename CanvasExists>
inline bool canvasReferencesAreValid(uint64_t primaryCanvasId, std::optional<uint64_t> additionalCanvasId, CanvasExists &&canvasExists)
{
	if (primaryCanvasId == osn::common::INVALID_ID || !canvasExists(primaryCanvasId))
		return false;
	if (!additionalCanvasId)
		return true;
	return *additionalCanvasId != osn::common::INVALID_ID && *additionalCanvasId != primaryCanvasId && canvasExists(*additionalCanvasId);
}

inline std::vector<VideoCandidate> candidates(uint32_t maxWidth, uint32_t maxHeight, uint32_t maxFpsNum, uint32_t maxFpsDen, uint32_t cadenceFamilyDen = 1)
{
	const bool fractionalCadence = maxFpsDen == 1001 || cadenceFamilyDen == 1001;
	const uint32_t sixty = fractionalCadence ? 60000U : 60U;
	const uint32_t thirty = fractionalCadence ? 30000U : 30U;
	const uint32_t denominator = fractionalCadence ? 1001U : 1U;
	const VideoCandidate ordered[] = {
		{1920, 1080, sixty, denominator}, {1920, 1080, thirty, denominator}, {1280, 720, sixty, denominator},
		{1280, 720, thirty, denominator}, {960, 540, thirty, denominator},
	};

	std::vector<VideoCandidate> result;
	for (const auto &candidate : ordered) {
		if (!candidateFitsLimits(candidate, maxWidth, maxHeight, maxFpsNum, maxFpsDen))
			continue;
		result.push_back(candidate);
	}
	return result;
}

inline bool cadenceCanBeProvenByPrivateMix(const VideoCandidate &candidate, uint32_t sourceFpsNum, uint32_t sourceFpsDen)
{
	if (sourceFpsNum == 0)
		return false;
	sourceFpsDen = std::max(1U, sourceFpsDen);
	return (uint64_t)sourceFpsNum * candidate.fpsDen >= (uint64_t)candidate.fpsNum * sourceFpsDen;
}

inline bool renditionCoversCandidate(const VideoCandidate &candidate, uint32_t width, uint32_t height, uint32_t fpsNum, uint32_t fpsDen)
{
	if (width < candidate.width || height < candidate.height || fpsNum == 0)
		return false;
	fpsDen = std::max(1U, fpsDen);
	return (uint64_t)fpsNum * candidate.fpsDen >= (uint64_t)candidate.fpsNum * fpsDen;
}

inline bool renditionExceedsCandidate(const VideoCandidate &candidate, uint32_t width, uint32_t height, uint32_t fpsNum, uint32_t fpsDen)
{
	if (width > candidate.width || height > candidate.height || fpsNum == 0 || fpsDen == 0)
		return true;
	return (uint64_t)fpsNum * candidate.fpsDen > (uint64_t)candidate.fpsNum * fpsDen;
}

inline uint32_t minimumEncodedFrames(uint32_t expectedFrames)
{
	const uint32_t pipelineAllowance = std::min(4U, expectedFrames);
	return std::max(3U, ((expectedFrames - pipelineAllowance) * 85U + 99U) / 100U);
}

inline uint32_t allowedSkippedFrames(uint32_t totalFrames)
{
	return totalFrames * 5U / 100U;
}

// These failures occur only after an Enhanced Broadcasting Dual Output
// candidate starts its additional locally encoded workload. Because they can
// indicate exhausted GPU or encoder capacity, retry the next lower quality
// tier. For single-output Enhanced Broadcasting and for transport or
// configuration failures, stop without retrying.
inline bool isCompositeCandidateLoadFailure(std::string_view errorCode)
{
	return errorCode == "enhanced_broadcasting_companion_encoder_create_failed" || errorCode == "enhanced_broadcasting_companion_output_start_failed" ||
	       errorCode == "enhanced_broadcasting_companion_output_stopped" || errorCode == "enhanced_broadcasting_output_start_failed";
}

inline bool allowsCandidateDescent(std::string_view errorCode)
{
	return errorCode == "enhanced_broadcasting_ladder_below_candidate" || errorCode == "enhanced_broadcasting_encoder_underload" ||
	       errorCode == "enhanced_broadcasting_render_overload" || errorCode == "enhanced_broadcasting_transport_pressure" ||
	       errorCode == "enhanced_broadcasting_companion_overload";
}

} // namespace autoConfig::enhancedBroadcastingPolicy
