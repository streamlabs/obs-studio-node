/******************************************************************************
    Copyright (C) 2026 by Streamlabs

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace autoOptimizer::qualityPolicy {

inline constexpr size_t kMaximumUploadLegs = 2;
inline constexpr size_t kMaximumEnhancedBroadcastingDualOutputLegs = 3;
inline constexpr int kDefaultEstimatedBitrateKbps = 2500;
inline constexpr int kMaximumRecommendedBitrateKbps = 8000;

/** Accept only effective frame rates from 1 through 240 FPS. */
inline bool isValidFrameRate(int64_t numerator, int64_t denominator)
{
	if (numerator < denominator || denominator <= 0)
		return false;

	const int64_t wholeFrames = numerator / denominator;
	return wholeFrames < 240 || (wholeFrames == 240 && numerator % denominator == 0);
}

/** Keep measured capacity separate from the bitrate Desktop may apply. */
inline int clampRecommendedBitrateKbps(uint64_t bitrateKbps)
{
	return (int)std::clamp<uint64_t>(bitrateKbps, 1, kMaximumRecommendedBitrateKbps);
}

// Compose an estimate from the current setting, an optional request limit, and
// the strictest limit reported by the configured destinations. Non-positive
// limits mean that no limit was supplied. The product-wide recommendation cap
// applies even when neither caller-owned limit is present.
inline int composeEstimatedBitrateKbps(int currentBitrateKbps, int requestMaximumBitrateKbps = 0, int destinationMaximumBitrateKbps = 0)
{
	int bitrateKbps = currentBitrateKbps > 0 ? currentBitrateKbps : kDefaultEstimatedBitrateKbps;
	if (requestMaximumBitrateKbps > 0)
		bitrateKbps = std::min(bitrateKbps, requestMaximumBitrateKbps);
	if (destinationMaximumBitrateKbps > 0)
		bitrateKbps = std::min(bitrateKbps, destinationMaximumBitrateKbps);
	return clampRecommendedBitrateKbps((uint64_t)std::max(1, bitrateKbps));
}

enum class HardwareFailureScope {
	Workload,
	Encoder,
	Phase,
};

struct HardwareSampleClassification {
	bool success = false;
	const char *errorCode = nullptr;
};

struct FrameRateDivisor {
	bool supported = false;
	uint32_t value = 1;
};

inline FrameRateDivisor frameRateDivisor(uint32_t sourceNum, uint32_t sourceDen, uint32_t targetNum, uint32_t targetDen)
{
	if (sourceNum == 0 || sourceDen == 0 || targetNum == 0 || targetDen == 0)
		return {};
	const uint64_t numerator = (uint64_t)sourceNum * targetDen;
	const uint64_t denominator = (uint64_t)sourceDen * targetNum;
	if (numerator < denominator || numerator % denominator != 0 || numerator / denominator > std::numeric_limits<uint32_t>::max())
		return {};
	return {true, (uint32_t)(numerator / denominator)};
}

// Keep throughput classification independent of OBS resource lifetime so
// impossible results, such as success with zero output packets, cannot
// influence encoder selection.
inline HardwareSampleClassification classifyHardwareSample(bool feederHealthy, uint32_t totalFrames, uint32_t skippedFrames, uint32_t encodedFrames,
							   uint32_t outputFrames, uint32_t minimumEncodedFrames, uint32_t allowedSkippedFrames)
{
	if (!feederHealthy)
		return {false, "hardware_benchmark_feeder_stalled"};
	if (totalFrames == 0)
		return {false, "hardware_benchmark_no_input_frames"};
	if (encodedFrames == 0)
		return {false, "hardware_benchmark_no_encoded_packets"};
	if (outputFrames == 0)
		return {false, "hardware_benchmark_no_output_packets"};
	if (encodedFrames < minimumEncodedFrames || skippedFrames > allowedSkippedFrames)
		return {false, "hardware_benchmark_overloaded"};
	return {true, nullptr};
}

// Hardware texture encoders require a private OBS video mix. If that mix cannot
// be created, reject only that hardware encoder and continue testing the
// independent raw-video path used by x264. Creation, start, and feeder failures
// may depend on resolution and frame rate, so continue with lower tiers.
inline HardwareFailureScope hardwareFailureScope(std::string_view code, bool timedOut = false)
{
	if (timedOut || code == "hardware_benchmark_video_create_failed" || code == "hardware_benchmark_audio_create_failed" ||
	    code == "hardware_benchmark_audio_encoder_create_failed" || code == "hardware_benchmark_output_create_failed" ||
	    code == "hardware_benchmark_no_output_packets" || code == "hardware_benchmark_cleanup_timeout")
		return HardwareFailureScope::Phase;
	if (code == "hardware_benchmark_encoder_unavailable" || code == "hardware_benchmark_video_mix_create_failed")
		return HardwareFailureScope::Encoder;
	return HardwareFailureScope::Workload;
}

// A feeder that cannot supply the requested cadence proves that the complete
// workload is not sustainable even when the encoder has not reported skipped
// frames. Preserve that evidence when all lower workload candidates also fail.
inline bool hardwareFailureIndicatesOverload(std::string_view code)
{
	return code == "hardware_benchmark_overloaded" || code == "hardware_benchmark_feeder_stalled";
}

// The control run uses the application's streaming mix to distinguish an
// encoder failure from a private-mix failure. Adopt success, cancellation, or a
// phase-wide infrastructure failure. Otherwise keep the original result so an
// inconclusive control does not reject the encoder globally.
inline bool shouldAdoptHardwareControl(bool success, bool cancelled, std::string_view errorCode, bool timedOut = false)
{
	return success || cancelled || hardwareFailureScope(errorCode, timedOut) == HardwareFailureScope::Phase;
}

// After the texture-encoder test succeeds, the raw-input test only determines
// whether the same encoder can sustain a higher frame rate. Ordinary failures
// reject that resolution and frame-rate candidate, not the encoder. Stop the
// phase only for a deadline or unsafe cleanup.
inline HardwareFailureScope exactCadenceValidationFailureScope(std::string_view code, bool timedOut = false)
{
	return timedOut || code == "hardware_benchmark_cleanup_timeout" ? HardwareFailureScope::Phase : HardwareFailureScope::Workload;
}

struct VideoTuple {
	int width = 0;
	int height = 0;
	int fpsNum = 0;
	int fpsDen = 1;
};

struct Selection {
	VideoTuple video;
	int bitrateKbps = 0;
	int minimumBitrateKbps = 0;
	bool bandwidthConstrained = false;
	bool insufficientBandwidth = false;
};

// Desktop applies one streaming bitrate to both direct Dual Output legs. A
// successful provider probe is an isolated lower bound on the shared uplink,
// so the larger of the two bounds is the aggregate budget that was actually
// demonstrated. Split that budget evenly while keeping each leg at or below
// both provider-safe values. No additional percentage reservation is applied.
struct SharedTwoLegAllocation {
	bool valid = false;
	uint64_t aggregateSafeVideoKbps = 0;
	uint64_t perLegVideoKbps = 0;
	uint64_t allocatedVideoKbps = 0;
};

inline SharedTwoLegAllocation allocateSharedTwoLegBandwidth(uint64_t firstSafeVideoKbps, uint64_t secondSafeVideoKbps)
{
	if (firstSafeVideoKbps == 0 || secondSafeVideoKbps == 0)
		return {};

	const uint64_t aggregateSafeVideoKbps = std::max(firstSafeVideoKbps, secondSafeVideoKbps);
	const uint64_t unroundedPerLegVideoKbps = std::min({firstSafeVideoKbps, secondSafeVideoKbps, aggregateSafeVideoKbps / 2});
	constexpr uint64_t quantumKbps = 100;
	const uint64_t perLegVideoKbps = unroundedPerLegVideoKbps - unroundedPerLegVideoKbps % quantumKbps;
	if (perLegVideoKbps == 0)
		return {};

	// perLegVideoKbps is bounded by floor(aggregateSafeVideoKbps / 2), so
	// doubling cannot overflow uint64_t.
	return {true, aggregateSafeVideoKbps, perLegVideoKbps, perLegVideoKbps * 2};
}

// Return an active mixed-provider Dual Output allocation only when the request
// matches the supported stream setup, both encoders sustain the common frame rate,
// and both provider probes produce usable lower bounds. Otherwise return an
// invalid allocation so the caller keeps both outputs estimated; never activate
// only one output.
inline SharedTwoLegAllocation assembleSharedTwoLegAllocation(bool exactTopologyEligible, bool concurrentHardwareValidated, bool allHardwareWorkloadsPassed,
							     bool firstProviderProbeUsable, uint64_t firstSafeVideoKbps, bool secondProviderProbeUsable,
							     uint64_t secondSafeVideoKbps)
{
	if (!exactTopologyEligible || !concurrentHardwareValidated || !allHardwareWorkloadsPassed || !firstProviderProbeUsable || !secondProviderProbeUsable)
		return {};

	return allocateSharedTwoLegBandwidth(firstSafeVideoKbps, secondSafeVideoKbps);
}

enum class QualityProfile {
	Generic,
	Twitch,
};

struct HardwareTier {
	int longEdge = 0;
	int shortEdge = 0;
	bool lowerFps = false;
};

// Dual Output uses one frame rate for both independently rendered outputs. Keep
// each resolution, but test and recommend the lower exact frame rate so 60/30
// or 60000/1001 versus 30000/1001 cannot be treated as compatible.
inline void applySharedMinimumCadence(VideoTuple &first, VideoTuple &second)
{
	const bool firstIsLowerOrEqual = (int64_t)first.fpsNum * std::max(1, second.fpsDen) <= (int64_t)second.fpsNum * std::max(1, first.fpsDen);
	const int sharedFpsNum = firstIsLowerOrEqual ? first.fpsNum : second.fpsNum;
	const int sharedFpsDen = std::max(1, firstIsLowerOrEqual ? first.fpsDen : second.fpsDen);
	first.fpsNum = sharedFpsNum;
	first.fpsDen = sharedFpsDen;
	second.fpsNum = sharedFpsNum;
	second.fpsDen = sharedFpsDen;
}

// Hardware capacity is tested in product-priority order. At each resolution,
// the input frame-rate ceiling is preserved unless lowerFps is true. Keeping
// this order in the policy layer makes the user-visible fallback sequence
// deterministic and independently testable.
inline const std::vector<HardwareTier> &hardwareTiers()
{
	static const std::vector<HardwareTier> tiers = {
		{1920, 1080, false}, // 1080 high FPS
		{1280, 720, false},  // 720 high FPS
		{1920, 1080, true},  // 1080 low FPS
		{960, 540, false},   // 540 high FPS
		{1280, 720, true},   // 720 low FPS
		{960, 540, true},    // 540 low FPS
	};
	return tiers;
}

inline int hardwarePhaseTimeoutMs(size_t plannedAttempts, int warmupMs, int sampleMs, int stopMs)
{
	constexpr uint64_t minimumMs = 12000;
	constexpr uint64_t maximumMs = 300000;
	constexpr uint64_t setupSlackPerAttemptMs = 250;
	constexpr uint64_t phaseSlackMs = 2000;
	const uint64_t perAttemptMs =
		(uint64_t)std::max(0, warmupMs) + (uint64_t)std::max(0, sampleMs) + (uint64_t)std::max(0, stopMs) + setupSlackPerAttemptMs;
	const uint64_t requestedMs = phaseSlackMs + std::max<uint64_t>(1, plannedAttempts) * perAttemptMs;
	return (int)std::clamp<uint64_t>(requestedMs, minimumMs, maximumMs);
}

inline const char *hardwareFailureCode(bool deadlineExpired, bool overloadObserved)
{
	if (deadlineExpired)
		return "hardware_benchmark_timeout";
	if (overloadObserved)
		return "hardware_benchmark_overloaded";
	return "hardware_no_usable_encoder";
}

inline bool sameVideo(const VideoTuple &left, const VideoTuple &right)
{
	return left.width == right.width && left.height == right.height && left.fpsNum == right.fpsNum && left.fpsDen == right.fpsDen;
}

inline bool fpsGreaterThan(const VideoTuple &value, int numerator, int denominator = 1)
{
	return (int64_t)value.fpsNum * denominator > (int64_t)numerator * std::max(1, value.fpsDen);
}

// Libobs renders a private texture mix only at the main canvas frame rate. If a
// candidate uses a higher frame rate, separately test the same hardware encoder
// with raw input at the exact candidate resolution and frame rate. Software
// encoders already use that exact raw-input path.
inline bool requiresExactHardwareCadenceValidation(bool hardware, uint32_t sourceNum, uint32_t sourceDen, uint32_t targetNum, uint32_t targetDen)
{
	if (!hardware || sourceNum == 0 || sourceDen == 0 || targetNum == 0 || targetDen == 0)
		return false;
	return (uint64_t)targetNum * sourceDen > (uint64_t)sourceNum * targetDen;
}

inline bool hasSupportedTierAspectRatio(const VideoTuple &value)
{
	if (value.width <= 0 || value.height <= 0)
		return false;
	const int64_t longEdge = std::max(value.width, value.height);
	const int64_t shortEdge = std::min(value.width, value.height);
	return longEdge * 9 == shortEdge * 16;
}

inline VideoTuple fitTier(const VideoTuple &ceiling, int longEdge, int shortEdge, bool lowerFps)
{
	VideoTuple result = ceiling;
	// The optimizer supports only the 1920x1080, 1280x720, and 960x540 tiers,
	// and their portrait equivalents. Preserve custom-aspect geometry and frame
	// rate instead of constructing a resolution Desktop cannot apply as a
	// supported tier.
	if (!hasSupportedTierAspectRatio(ceiling))
		return result;

	const bool landscape = ceiling.width >= ceiling.height;
	const int maxWidth = landscape ? longEdge : shortEdge;
	const int maxHeight = landscape ? shortEdge : longEdge;
	if (ceiling.width > maxWidth || ceiling.height > maxHeight) {
		result.width = maxWidth;
		result.height = maxHeight;
	}
	result.fpsDen = std::max(1, ceiling.fpsDen);
	result.fpsNum = std::max(1, ceiling.fpsNum);
	if (fpsGreaterThan(result, 60)) {
		if (result.fpsDen == 1001) {
			result.fpsNum = 60000;
			result.fpsDen = 1001;
		} else {
			result.fpsNum = 60;
			result.fpsDen = 1;
		}
	}
	if (lowerFps && fpsGreaterThan(result, 30)) {
		// Preserve broadcast-rate families: 60/1 -> 30/1, 60000/1001 ->
		// 30000/1001, and 50/1 -> 25/1.
		result.fpsNum = std::max(1, result.fpsNum / 2);
	}
	return result;
}

// When the caller supplies a complete smaller resolution limit, lower the
// current resolution to a supported tier. benchmarkCeiling handles promotion
// separately without changing application settings. Missing dimensions, a
// changed orientation, or a custom aspect ratio leave the current resolution
// unchanged, so OSN never assumes permission to resize.
inline VideoTuple boundCurrentToSupportedTier(const VideoTuple &current, int maxWidth, int maxHeight)
{
	if (maxWidth <= 0 || maxHeight <= 0)
		return current;
	if (!hasSupportedTierAspectRatio(current) || (current.width >= current.height) != (maxWidth >= maxHeight))
		return current;
	if (current.width <= maxWidth && current.height <= maxHeight)
		return current;

	const int tiers[][2] = {{1920, 1080}, {1280, 720}, {960, 540}};
	for (const auto &tier : tiers) {
		VideoTuple candidate = fitTier(current, tier[0], tier[1], false);
		if (candidate.width <= maxWidth && candidate.height <= maxHeight) {
			candidate.fpsNum = current.fpsNum;
			candidate.fpsDen = current.fpsDen;
			return candidate;
		}
	}
	return current;
}

// Promote only inside a complete, orientation-compatible bound for a supported
// 16:9 or 9:16 tier. The optional frame-rate bound permits testing a higher
// frame rate, but returning it still requires successful active bandwidth
// evidence.
inline VideoTuple benchmarkCeiling(const VideoTuple &current, int maxWidth, int maxHeight, int maxFpsNum = 0, int maxFpsDen = 1)
{
	const VideoTuple bounded = boundCurrentToSupportedTier(current, maxWidth, maxHeight);
	if (maxWidth <= 0 || maxHeight <= 0)
		return current;

	if (!hasSupportedTierAspectRatio(current) || (current.width >= current.height) != (maxWidth >= maxHeight))
		return current;

	VideoTuple result = bounded;
	const bool landscape = current.width >= current.height;
	if (sameVideo(bounded, current)) {
		const int tiers[][2] = {{1920, 1080}, {1280, 720}, {960, 540}};
		for (const auto &tier : tiers) {
			const int width = landscape ? tier[0] : tier[1];
			const int height = landscape ? tier[1] : tier[0];
			if (width <= maxWidth && height <= maxHeight && width > current.width && height > current.height) {
				result.width = width;
				result.height = height;
				break;
			}
		}
	}

	if (maxFpsNum > 0) {
		VideoTuple requested = result;
		requested.fpsNum = maxFpsNum;
		requested.fpsDen = std::max(1, maxFpsDen);
		if (fpsGreaterThan(requested, 60)) {
			if (requested.fpsDen == 1001) {
				requested.fpsNum = 60000;
				requested.fpsDen = 1001;
			} else {
				requested.fpsNum = 60;
				requested.fpsDen = 1;
			}
		}
		if (fpsGreaterThan(requested, current.fpsNum, current.fpsDen)) {
			result.fpsNum = requested.fpsNum;
			result.fpsDen = requested.fpsDen;
		}
	}
	return result;
}

// A recommendation may exceed the current resolution or frame rate only after
// a successful active provider probe. Estimated and failed-probe results may
// choose a lower tested setting but cannot promote.
inline VideoTuple recommendationCeiling(const VideoTuple &tested, const VideoTuple &current, bool allowPromotion)
{
	if (allowPromotion)
		return tested;

	if (tested.width > current.width || tested.height > current.height)
		return current;

	VideoTuple result = tested;
	if (fpsGreaterThan(result, current.fpsNum, current.fpsDen)) {
		result.fpsNum = current.fpsNum;
		result.fpsDen = std::max(1, current.fpsDen);
	}
	return result;
}

inline bool isQualityPromotion(const VideoTuple &current, const VideoTuple &selected)
{
	return (int64_t)selected.width * selected.height > (int64_t)current.width * current.height || fpsGreaterThan(selected, current.fpsNum, current.fpsDen);
}

inline std::vector<VideoTuple> candidates(const VideoTuple &ceiling)
{
	std::vector<VideoTuple> result;
	const int tiers[][2] = {{1920, 1080}, {1280, 720}, {960, 540}};
	for (const auto &tier : tiers) {
		for (bool lowerFps : {false, true}) {
			VideoTuple candidate = fitTier(ceiling, tier[0], tier[1], lowerFps);
			if (candidate.width <= 0 || candidate.height <= 0 || candidate.fpsNum <= 0)
				continue;
			if (std::none_of(result.begin(), result.end(), [&](const VideoTuple &existing) { return sameVideo(existing, candidate); }))
				result.push_back(candidate);
		}
	}
	return result;
}

inline long double bitrateComplexity(const VideoTuple &video)
{
	const long double fps = (long double)video.fpsNum / (long double)std::max(1, video.fpsDen);
	const long double area = (long double)video.width * (long double)video.height;
	return std::pow(area, 0.85L) * std::sqrt(std::pow(fps, 1.1L));
}

inline int twitchMinimumBitrateKbps(const VideoTuple &video)
{
	// Use these bitrate thresholds for every locally encoded Twitch output.
	// 1080p60 tolerates a small shortfall from Twitch's published 6000 Kbps
	// recommendation; lower qualities use explicit product thresholds instead
	// of the generic OBS complexity estimate.
	const int longEdge = std::max(video.width, video.height);
	const int shortEdge = std::min(video.width, video.height);
	const bool highFps = fpsGreaterThan(video, 30);
	if (longEdge == 1920 && shortEdge == 1080)
		return highFps ? 5500 : 5000;
	if (longEdge == 1280 && shortEdge == 720)
		return highFps ? 4500 : 3000;
	return 0;
}

inline int roundedMinimumBitrateKbps(const VideoTuple &video, const std::string &encoderFamily)
{
	const VideoTuple reference{1920, 1080, 60, 1};
	long double minimum = bitrateComplexity(video) / (bitrateComplexity(reference) / 5800.0L);
	// Match upstream OBS: modern NVENC and x264 do not need the conservative
	// quality-to-bitrate adjustment used for the other hardware families.
	if (encoderFamily != "obs_nvenc_h264_tex" && encoderFamily != "nvenc" && encoderFamily != "x264")
		minimum *= 1.14L;
	return std::max(1, (int)(std::ceil(minimum / 50.0L) * 50.0L));
}

inline int profileMinimumBitrateKbps(const VideoTuple &video, const std::string &encoderFamily, QualityProfile profile)
{
	if (profile == QualityProfile::Twitch) {
		const int twitchMinimum = twitchMinimumBitrateKbps(video);
		if (twitchMinimum > 0)
			return twitchMinimum;
	}
	return roundedMinimumBitrateKbps(video, encoderFamily);
}

inline bool supports(const VideoTuple &video, int safeVideoBitrateKbps, const std::string &encoderFamily)
{
	return safeVideoBitrateKbps >= roundedMinimumBitrateKbps(video, encoderFamily);
}

inline Selection select(const VideoTuple &ceiling, int safeVideoBitrateKbps, const std::string &encoderFamily, QualityProfile profile = QualityProfile::Generic)
{
	Selection result;
	const int recommendedBitrateKbps = clampRecommendedBitrateKbps((uint64_t)std::max(1, safeVideoBitrateKbps));
	const auto options = candidates(ceiling);
	if (options.empty()) {
		result.video = ceiling;
		result.bitrateKbps = recommendedBitrateKbps;
		result.insufficientBandwidth = true;
		return result;
	}

	std::vector<size_t> eligible;
	for (size_t index = 0; index < options.size(); index++) {
		if (recommendedBitrateKbps >= profileMinimumBitrateKbps(options[index], encoderFamily, profile))
			eligible.push_back(index);
	}

	size_t selectedIndex = options.size() - 1;
	if (!eligible.empty()) {
		selectedIndex = eligible.front();
		// Mirror OBS's high-FPS preference: when bandwidth rejects a higher
		// resolution at high FPS but admits it at low FPS, prefer the next lower
		// high-FPS tier as long as that tier is at least 960x540.
		// The Twitch profile already defines the preferred resolution and frame rate at
		// each boundary. Do not let the generic high-FPS override reorder it.
		if (profile == QualityProfile::Generic && eligible.size() > 1) {
			const VideoTuple &first = options[eligible[0]];
			const VideoTuple &second = options[eligible[1]];
			const bool firstLow = !fpsGreaterThan(first, 30);
			const bool secondHigh = fpsGreaterThan(second, 30);
			if (firstLow && secondHigh && second.width * second.height >= 960 * 540)
				selectedIndex = eligible[1];
		}
	}

	result.video = options[selectedIndex];
	result.minimumBitrateKbps = profileMinimumBitrateKbps(result.video, encoderFamily, profile);
	result.insufficientBandwidth = eligible.empty();
	result.bandwidthConstrained = !sameVideo(result.video, ceiling);
	// Keep the full measured capacity in the evidence, but cap the bitrate
	// recommendation that Desktop may apply.
	result.bitrateKbps = recommendedBitrateKbps;
	return result;
}

} // namespace autoOptimizer::qualityPolicy
