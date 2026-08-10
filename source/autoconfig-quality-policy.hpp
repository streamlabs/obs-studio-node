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

namespace autoConfig::qualityPolicy {

inline constexpr size_t kMaximumUploadLegs = 2;

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

// Keep the throughput decision independent from OBS resource management so
// contradictory states (for example success=true with zero output packets)
// cannot escape into encoder selection.
inline HardwareSampleClassification classifyHardwareSample(bool feederHealthy, uint32_t totalFrames, uint32_t skippedFrames,
						     uint32_t encodedFrames, uint32_t outputFrames, uint32_t minimumEncodedFrames,
						     uint32_t allowedSkippedFrames)
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

// A private OBS video mix is required only by the hardware path. Failure to
// create it rejects hardware, but must not suppress the independent raw-video
// path used by the guaranteed x264 fallback. Encoder creation/start failures
// can depend on the requested tuple, so lower tiers must still be attempted.
inline HardwareFailureScope hardwareFailureScope(std::string_view code, bool timedOut = false)
{
	if (timedOut || code == "hardware_benchmark_video_create_failed" || code == "hardware_benchmark_audio_create_failed" ||
	    code == "hardware_benchmark_audio_encoder_create_failed" || code == "hardware_benchmark_output_create_failed" ||
	    code == "hardware_benchmark_no_output_packets" || code == "hardware_benchmark_feeder_stalled" ||
	    code == "hardware_benchmark_cleanup_timeout")
		return HardwareFailureScope::Phase;
	if (code == "hardware_benchmark_encoder_unavailable" || code == "hardware_benchmark_video_mix_create_failed")
		return HardwareFailureScope::Encoder;
	return HardwareFailureScope::Workload;
}

// The streaming-mix control is diagnostic. A successful control proves the
// concrete encoder can produce packets; cancellation and shared infrastructure
// failures must also escape immediately. An unavailable or inconclusive
// control must not turn a private-mix symptom into an encoder-global rejection.
inline bool shouldAdoptHardwareControl(bool success, bool cancelled, std::string_view errorCode, bool timedOut = false)
{
	return success || cancelled || hardwareFailureScope(errorCode, timedOut) == HardwareFailureScope::Phase;
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

struct HardwareTier {
	int longEdge = 0;
	int shortEdge = 0;
	bool lowerFps = false;
};

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

inline VideoTuple fitTier(const VideoTuple &ceiling, int longEdge, int shortEdge, bool lowerFps)
{
	VideoTuple result = ceiling;
	const bool landscape = ceiling.width >= ceiling.height;
	const int maxWidth = landscape ? longEdge : shortEdge;
	const int maxHeight = landscape ? shortEdge : longEdge;
	const double scale = std::min({1.0, (double)maxWidth / std::max(1, ceiling.width), (double)maxHeight / std::max(1, ceiling.height)});
	result.width = std::max(2, ((int)std::floor((double)ceiling.width * scale)) & ~1);
	result.height = std::max(2, ((int)std::floor((double)ceiling.height * scale)) & ~1);
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

inline bool supports(const VideoTuple &video, int safeVideoBitrateKbps, const std::string &encoderFamily)
{
	return safeVideoBitrateKbps >= roundedMinimumBitrateKbps(video, encoderFamily);
}

inline Selection select(const VideoTuple &ceiling, int safeVideoBitrateKbps, const std::string &encoderFamily)
{
	Selection result;
	const auto options = candidates(ceiling);
	if (options.empty()) {
		result.video = ceiling;
		result.bitrateKbps = std::max(1, safeVideoBitrateKbps);
		result.insufficientBandwidth = true;
		return result;
	}

	std::vector<size_t> eligible;
	for (size_t index = 0; index < options.size(); index++) {
		if (supports(options[index], safeVideoBitrateKbps, encoderFamily))
			eligible.push_back(index);
	}

	size_t selectedIndex = options.size() - 1;
	if (!eligible.empty()) {
		selectedIndex = eligible.front();
		// Mirror OBS's high-FPS preference: when bandwidth rejects a higher
		// resolution at high FPS but admits it at low FPS, prefer the next lower
		// high-FPS tier as long as that tier is at least 960x540.
		if (eligible.size() > 1) {
			const VideoTuple &first = options[eligible[0]];
			const VideoTuple &second = options[eligible[1]];
			const bool firstLow = !fpsGreaterThan(first, 30);
			const bool secondHigh = fpsGreaterThan(second, 30);
			if (firstLow && secondHigh && second.width * second.height >= 960 * 540)
				selectedIndex = eligible[1];
		}
	}

	result.video = options[selectedIndex];
	result.minimumBitrateKbps = roundedMinimumBitrateKbps(result.video, encoderFamily);
	result.insufficientBandwidth = eligible.empty();
	result.bandwidthConstrained = !sameVideo(result.video, ceiling);
	// The provider-safe budget remains the recommended bitrate. Resolution and
	// frame rate are the variables selected here; useful-maximum bitrate caps are
	// a separate product policy and must not silently discard measured capacity.
	result.bitrateKbps = std::max(1, safeVideoBitrateKbps);
	return result;
}

} // namespace autoConfig::qualityPolicy
