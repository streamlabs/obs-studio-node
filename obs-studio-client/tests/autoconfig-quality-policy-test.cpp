#include <catch2/catch_test_macros.hpp>

#include "autoconfig-quality-policy.hpp"

using autoConfig::qualityPolicy::VideoTuple;
using autoConfig::qualityPolicy::candidates;
using autoConfig::qualityPolicy::classifyHardwareSample;
using autoConfig::qualityPolicy::frameRateDivisor;
using autoConfig::qualityPolicy::hardwareFailureCode;
using autoConfig::qualityPolicy::hardwareFailureScope;
using autoConfig::qualityPolicy::hardwarePhaseTimeoutMs;
using autoConfig::qualityPolicy::hardwareTiers;
using autoConfig::qualityPolicy::roundedMinimumBitrateKbps;
using autoConfig::qualityPolicy::select;
using autoConfig::qualityPolicy::shouldAdoptHardwareControl;
using autoConfig::qualityPolicy::supports;

TEST_CASE("Auto Config quality policy exposes only the three approved tiers")
{
	const auto result = candidates({1920, 1080, 60, 1});
	REQUIRE(result.size() == 6);
	CHECK(result[0].width == 1920);
	CHECK(result[0].height == 1080);
	CHECK(result[2].width == 1280);
	CHECK(result[2].height == 720);
	CHECK(result[4].width == 960);
	CHECK(result[4].height == 540);
}

TEST_CASE("Auto Config hardware tests high frame rates in product-priority order")
{
	const VideoTuple ceiling{1920, 1080, 60, 1};
	std::vector<VideoTuple> result;
	for (const auto &tier : hardwareTiers()) {
		const VideoTuple candidate = autoConfig::qualityPolicy::fitTier(ceiling, tier.longEdge, tier.shortEdge, tier.lowerFps);
		if (std::none_of(result.begin(), result.end(),
				 [&](const VideoTuple &existing) { return autoConfig::qualityPolicy::sameVideo(existing, candidate); }))
			result.push_back(candidate);
	}

	REQUIRE(result.size() == 6);
	CHECK(result[0].width == 1920);
	CHECK(result[0].fpsNum == 60);
	CHECK(result[1].width == 1280);
	CHECK(result[1].fpsNum == 60);
	CHECK(result[2].width == 1920);
	CHECK(result[2].fpsNum == 30);
	CHECK(result[3].width == 960);
	CHECK(result[3].fpsNum == 60);
	CHECK(result[4].width == 1280);
	CHECK(result[4].fpsNum == 30);
	CHECK(result[5].width == 960);
	CHECK(result[5].fpsNum == 30);

	result.clear();
	const VideoTuple lowerCeiling{1280, 720, 30, 1};
	for (const auto &tier : hardwareTiers()) {
		const VideoTuple candidate = autoConfig::qualityPolicy::fitTier(lowerCeiling, tier.longEdge, tier.shortEdge, tier.lowerFps);
		if (std::none_of(result.begin(), result.end(),
				 [&](const VideoTuple &existing) { return autoConfig::qualityPolicy::sameVideo(existing, candidate); }))
			result.push_back(candidate);
	}
	REQUIRE(result.size() == 2);
	CHECK(result[0].width == 1280);
	CHECK(result[0].fpsNum == 30);
	CHECK(result[1].width == 960);
	CHECK(result[1].fpsNum == 30);
}

TEST_CASE("Auto Config hardware timeout scales with work and remains bounded")
{
	const int oneAttempt = hardwarePhaseTimeoutMs(1, 500, 1500, 3000);
	const int twelveAttempts = hardwarePhaseTimeoutMs(12, 500, 1500, 3000);
	const int twoLegWindowsPrimaryAttempts = hardwarePhaseTimeoutMs(48, 500, 1500, 3000);
	const int twoLegWindowsWithControls = hardwarePhaseTimeoutMs(54, 500, 1500, 3000);
	const int excessiveAttempts = hardwarePhaseTimeoutMs(10000, 500, 1500, 3000);
	CHECK(oneAttempt == 12000);
	CHECK(twelveAttempts > oneAttempt);
	CHECK(twoLegWindowsPrimaryAttempts == 254000);
	CHECK(twoLegWindowsWithControls == 285500);
	CHECK(twelveAttempts < twoLegWindowsPrimaryAttempts);
	CHECK(twoLegWindowsPrimaryAttempts < twoLegWindowsWithControls);
	CHECK(twoLegWindowsWithControls < excessiveAttempts);
	CHECK(excessiveAttempts == 300000);
}

TEST_CASE("Auto Config distinguishes no usable encoder from overload and timeout")
{
	CHECK(std::string(hardwareFailureCode(false, false)) == "hardware_no_usable_encoder");
	CHECK(std::string(hardwareFailureCode(false, true)) == "hardware_benchmark_overloaded");
	CHECK(std::string(hardwareFailureCode(true, true)) == "hardware_benchmark_timeout");
}

TEST_CASE("Auto Config failure scope preserves lower tiers and the x264 fallback")
{
	using autoConfig::qualityPolicy::HardwareFailureScope;

	CHECK(hardwareFailureScope("hardware_benchmark_encoder_create_failed") == HardwareFailureScope::Workload);
	CHECK(hardwareFailureScope("hardware_benchmark_start_failed") == HardwareFailureScope::Workload);
	CHECK(hardwareFailureScope("hardware_benchmark_output_stopped") == HardwareFailureScope::Workload);
	CHECK(hardwareFailureScope("hardware_benchmark_no_input_frames") == HardwareFailureScope::Workload);
	CHECK(hardwareFailureScope("hardware_benchmark_no_encoded_packets") == HardwareFailureScope::Workload);
	CHECK(hardwareFailureScope("hardware_benchmark_no_output_packets") == HardwareFailureScope::Phase);
	CHECK(hardwareFailureScope("hardware_benchmark_feeder_stalled") == HardwareFailureScope::Phase);
	CHECK(hardwareFailureScope("hardware_benchmark_encoder_unavailable") == HardwareFailureScope::Encoder);
	CHECK(hardwareFailureScope("hardware_benchmark_video_mix_create_failed") == HardwareFailureScope::Encoder);
	CHECK(hardwareFailureScope("hardware_benchmark_video_create_failed") == HardwareFailureScope::Phase);
	CHECK(hardwareFailureScope("hardware_benchmark_audio_create_failed") == HardwareFailureScope::Phase);
	CHECK(hardwareFailureScope("", true) == HardwareFailureScope::Phase);
}

TEST_CASE("Auto Config adopts only conclusive streaming-mix controls")
{
	CHECK(shouldAdoptHardwareControl(true, false, ""));
	CHECK(shouldAdoptHardwareControl(false, true, ""));
	CHECK(shouldAdoptHardwareControl(false, false, "hardware_benchmark_cleanup_timeout"));
	CHECK(shouldAdoptHardwareControl(false, false, "", true));
	CHECK_FALSE(shouldAdoptHardwareControl(false, false, "hardware_benchmark_video_mix_create_failed"));
	CHECK_FALSE(shouldAdoptHardwareControl(false, false, "hardware_benchmark_no_input_frames"));
	CHECK_FALSE(shouldAdoptHardwareControl(false, false, "hardware_benchmark_no_encoded_packets"));
}

TEST_CASE("Auto Config hardware sample classifier keeps zero packets distinct from overload")
{
	auto result = classifyHardwareSample(false, 0, 0, 0, 0, 34, 2);
	CHECK_FALSE(result.success);
	CHECK(std::string(result.errorCode) == "hardware_benchmark_feeder_stalled");

	result = classifyHardwareSample(true, 0, 0, 0, 0, 34, 2);
	CHECK_FALSE(result.success);
	CHECK(std::string(result.errorCode) == "hardware_benchmark_no_input_frames");

	result = classifyHardwareSample(true, 45, 0, 0, 0, 34, 2);
	CHECK_FALSE(result.success);
	CHECK(std::string(result.errorCode) == "hardware_benchmark_no_encoded_packets");

	result = classifyHardwareSample(true, 45, 0, 35, 0, 34, 2);
	CHECK_FALSE(result.success);
	CHECK(std::string(result.errorCode) == "hardware_benchmark_no_output_packets");

	result = classifyHardwareSample(false, 45, 0, 35, 35, 34, 2);
	CHECK_FALSE(result.success);
	CHECK(std::string(result.errorCode) == "hardware_benchmark_feeder_stalled");

	result = classifyHardwareSample(true, 45, 0, 20, 20, 34, 2);
	CHECK_FALSE(result.success);
	CHECK(std::string(result.errorCode) == "hardware_benchmark_overloaded");

	result = classifyHardwareSample(true, 45, 3, 35, 35, 34, 2);
	CHECK_FALSE(result.success);
	CHECK(std::string(result.errorCode) == "hardware_benchmark_overloaded");

	result = classifyHardwareSample(true, 45, 2, 35, 35, 34, 2);
	CHECK(result.success);
	CHECK(result.errorCode == nullptr);
}

TEST_CASE("Auto Config derives only exact frame-rate divisors")
{
	auto result = frameRateDivisor(60, 1, 30, 1);
	CHECK(result.supported);
	CHECK(result.value == 2);

	result = frameRateDivisor(60000, 1001, 30000, 1001);
	CHECK(result.supported);
	CHECK(result.value == 2);

	result = frameRateDivisor(50, 1, 50, 1);
	CHECK(result.supported);
	CHECK(result.value == 1);

	result = frameRateDivisor(60000, 1001, 30, 1);
	CHECK_FALSE(result.supported);
}

TEST_CASE("Auto Config native contract supports the two Desktop upload legs")
{
	CHECK(autoConfig::qualityPolicy::kMaximumUploadLegs == 2);
}

TEST_CASE("Auto Config quality policy never upscales the tested ceiling")
{
	const auto result = candidates({1280, 720, 30, 1});
	REQUIRE(result.size() == 2);
	CHECK(result.front().width == 1280);
	CHECK(result.front().height == 720);
	CHECK(result.front().fpsNum == 30);
	CHECK(select({1280, 720, 30, 1}, 10000, "obs_nvenc_h264_tex").video.width == 1280);
}

TEST_CASE("Auto Config quality policy preserves orientation aspect ratio and even dimensions")
{
	const auto result = candidates({1080, 1920, 60000, 1001});
	REQUIRE(result.size() == 6);
	CHECK(result[2].width == 720);
	CHECK(result[2].height == 1280);
	CHECK(result[2].fpsNum == 60000);
	CHECK(result[3].fpsNum == 30000);
	for (const auto &candidate : result) {
		CHECK(candidate.width % 2 == 0);
		CHECK(candidate.height % 2 == 0);
	}
}

TEST_CASE("Auto Config quality selection responds monotonically to bandwidth")
{
	const VideoTuple ceiling{1920, 1080, 60, 1};
	const auto high = select(ceiling, 6000, "obs_nvenc_h264_tex");
	const auto medium = select(ceiling, 3000, "obs_nvenc_h264_tex");
	const auto low = select(ceiling, 1200, "obs_nvenc_h264_tex");
	CHECK(high.video.width == 1920);
	CHECK(high.video.fpsNum == 60);
	CHECK(medium.video.width <= high.video.width);
	CHECK(low.video.width <= medium.video.width);
	CHECK(high.bitrateKbps <= 6000);
	CHECK(medium.bitrateKbps <= 3000);
	CHECK(low.bitrateKbps <= 1200);
}

TEST_CASE("Every approved quality rung changes eligibility exactly at its bitrate threshold")
{
	const auto rungs = candidates({1920, 1080, 60, 1});
	REQUIRE(rungs.size() == 6);
	for (const auto &rung : rungs) {
		const int threshold = roundedMinimumBitrateKbps(rung, "obs_nvenc_h264_tex");
		CAPTURE(rung.width, rung.height, rung.fpsNum, rung.fpsDen, threshold);
		CHECK_FALSE(supports(rung, threshold - 1, "obs_nvenc_h264_tex"));
		CHECK(supports(rung, threshold, "obs_nvenc_h264_tex"));
		CHECK(supports(rung, threshold + 1, "obs_nvenc_h264_tex"));
	}
}

TEST_CASE("Auto Config quality policy preserves 50 and 59.94 broadcast-rate families")
{
	const auto pal = candidates({1920, 1080, 50, 1});
	REQUIRE(pal.size() == 6);
	CHECK(pal[0].fpsNum == 50);
	CHECK(pal[0].fpsDen == 1);
	CHECK(pal[1].fpsNum == 25);
	CHECK(pal[1].fpsDen == 1);

	const auto ntsc = candidates({1920, 1080, 60000, 1001});
	REQUIRE(ntsc.size() == 6);
	CHECK(ntsc[0].fpsNum == 60000);
	CHECK(ntsc[0].fpsDen == 1001);
	CHECK(ntsc[1].fpsNum == 30000);
	CHECK(ntsc[1].fpsDen == 1001);
}

TEST_CASE("Auto Config quality policy caps high frame rate inputs to 60 or 59.94")
{
	const auto integer = candidates({1920, 1080, 120, 1});
	REQUIRE(integer.size() == 6);
	CHECK(integer[0].fpsNum == 60);
	CHECK(integer[0].fpsDen == 1);
	CHECK(integer[1].fpsNum == 30);
	CHECK(integer[1].fpsDen == 1);

	const auto ntsc = candidates({1920, 1080, 120000, 1001});
	REQUIRE(ntsc.size() == 6);
	CHECK(ntsc[0].fpsNum == 60000);
	CHECK(ntsc[0].fpsDen == 1001);
	CHECK(ntsc[1].fpsNum == 30000);
	CHECK(ntsc[1].fpsDen == 1001);
}

TEST_CASE("High FPS preference intentionally chooses 540p60 over 720p30 when both fit")
{
	const VideoTuple p720_30{1280, 720, 30, 1};
	const int budget = roundedMinimumBitrateKbps(p720_30, "obs_nvenc_h264_tex");
	const auto result = select({1920, 1080, 60, 1}, budget, "obs_nvenc_h264_tex");
	CHECK(result.video.width == 960);
	CHECK(result.video.height == 540);
	CHECK(result.video.fpsNum == 60);
}

TEST_CASE("Auto Config quality policy reports bandwidth below its minimum tier")
{
	const auto result = select({1920, 1080, 60, 1}, 100, "x264");
	CHECK(result.video.width == 960);
	CHECK(result.video.height == 540);
	CHECK(result.video.fpsNum == 30);
	CHECK(result.bitrateKbps == 100);
	CHECK(result.insufficientBandwidth);
}
