#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <limits>
#include <utility>

#include "autoconfig-quality-policy.hpp"

namespace policy = autoConfig::qualityPolicy;

TEST_CASE("Auto Config Dual Output uses one shared minimum frame rate")
{
	SECTION("integer 60 and 30 FPS")
	{
		policy::VideoTuple horizontal{1920, 1080, 60, 1};
		policy::VideoTuple vertical{1080, 1920, 30, 1};
		policy::applySharedMinimumCadence(horizontal, vertical);
		CHECK(horizontal.fpsNum == 30);
		CHECK(horizontal.fpsDen == 1);
		CHECK(vertical.fpsNum == 30);
		CHECK(vertical.fpsDen == 1);
	}

	SECTION("NTSC 59.94 and 29.97 FPS")
	{
		policy::VideoTuple horizontal{1920, 1080, 60000, 1001};
		policy::VideoTuple vertical{1080, 1920, 30000, 1001};
		policy::applySharedMinimumCadence(horizontal, vertical);
		CHECK(horizontal.fpsNum == 30000);
		CHECK(horizontal.fpsDen == 1001);
		CHECK(vertical.fpsNum == 30000);
		CHECK(vertical.fpsDen == 1001);
	}
}

TEST_CASE("Auto Config allocates a shared uplink equally across two direct legs")
{
	struct Expectation {
		uint64_t firstSafeVideoKbps;
		uint64_t secondSafeVideoKbps;
		uint64_t expectedPerLegVideoKbps;
	};
	const Expectation expectations[] = {
		{6000, 10000, 5000}, {6000, 9000, 4500}, {5000, 10000, 5000}, {4000, 10000, 4000}, {5000, 5000, 2500},
	};

	for (const auto &expectation : expectations) {
		CAPTURE(expectation.firstSafeVideoKbps, expectation.secondSafeVideoKbps);
		const auto allocation = policy::allocateSharedTwoLegBandwidth(expectation.firstSafeVideoKbps, expectation.secondSafeVideoKbps);
		CHECK(allocation.valid);
		CHECK(allocation.aggregateSafeVideoKbps == std::max(expectation.firstSafeVideoKbps, expectation.secondSafeVideoKbps));
		CHECK(allocation.perLegVideoKbps == expectation.expectedPerLegVideoKbps);
		CHECK(allocation.allocatedVideoKbps == expectation.expectedPerLegVideoKbps * 2);
	}
}

TEST_CASE("Auto Config shared two-leg allocation is symmetric")
{
	const auto forward = policy::allocateSharedTwoLegBandwidth(6000, 10000);
	const auto reversed = policy::allocateSharedTwoLegBandwidth(10000, 6000);
	CHECK(forward.valid);
	CHECK(reversed.valid);
	CHECK(forward.aggregateSafeVideoKbps == reversed.aggregateSafeVideoKbps);
	CHECK(forward.perLegVideoKbps == reversed.perLegVideoKbps);
	CHECK(forward.allocatedVideoKbps == reversed.allocatedVideoKbps);
}

TEST_CASE("Auto Config shared two-leg allocation rejects zero and sub-quantum budgets")
{
	for (const auto &[firstSafeVideoKbps, secondSafeVideoKbps] :
	     std::initializer_list<std::pair<uint64_t, uint64_t>>{{0, 10000}, {10000, 0}, {0, 0}, {1, 1}, {199, 199}}) {
		CAPTURE(firstSafeVideoKbps, secondSafeVideoKbps);
		const auto allocation = policy::allocateSharedTwoLegBandwidth(firstSafeVideoKbps, secondSafeVideoKbps);
		CHECK_FALSE(allocation.valid);
		CHECK(allocation.aggregateSafeVideoKbps == 0);
		CHECK(allocation.perLegVideoKbps == 0);
		CHECK(allocation.allocatedVideoKbps == 0);
	}

	const auto minimum = policy::allocateSharedTwoLegBandwidth(200, 200);
	CHECK(minimum.valid);
	CHECK(minimum.aggregateSafeVideoKbps == 200);
	CHECK(minimum.perLegVideoKbps == 100);
	CHECK(minimum.allocatedVideoKbps == 200);
}

TEST_CASE("Auto Config shared two-leg allocation cannot overflow its aggregate budget")
{
	const uint64_t maximum = std::numeric_limits<uint64_t>::max();
	const auto allocation = policy::allocateSharedTwoLegBandwidth(maximum, maximum);
	REQUIRE(allocation.valid);
	CHECK(allocation.aggregateSafeVideoKbps == maximum);
	CHECK(allocation.perLegVideoKbps % 100 == 0);
	CHECK(allocation.allocatedVideoKbps == allocation.perLegVideoKbps * 2);
	CHECK(allocation.allocatedVideoKbps <= allocation.aggregateSafeVideoKbps);
}

TEST_CASE("Auto Config assembles an active Dual Output result only from a complete joint proof")
{
	const auto result = policy::assembleSharedTwoLegAllocation(true, true, true, true, 6000, true, 10000);
	REQUIRE(result.valid);
	CHECK(result.aggregateSafeVideoKbps == 10000);
	CHECK(result.perLegVideoKbps == 5000);
	CHECK(result.allocatedVideoKbps == 10000);
}

TEST_CASE("Auto Config keeps both Dual Output legs estimated when any joint proof is missing")
{
	struct Evidence {
		bool exactTopologyEligible;
		bool concurrentHardwareValidated;
		bool allHardwareWorkloadsPassed;
		bool firstProviderProbeUsable;
		uint64_t firstSafeVideoKbps;
		bool secondProviderProbeUsable;
		uint64_t secondSafeVideoKbps;
	};
	const Evidence incompleteEvidence[] = {
		{false, true, true, true, 6000, true, 10000}, {true, false, true, true, 6000, true, 10000}, {true, true, false, true, 6000, true, 10000},
		{true, true, true, false, 6000, true, 10000}, {true, true, true, true, 6000, false, 10000}, {true, true, true, true, 0, true, 10000},
		{true, true, true, true, 6000, true, 0},
	};

	for (const auto &evidence : incompleteEvidence) {
		CAPTURE(evidence.exactTopologyEligible, evidence.concurrentHardwareValidated, evidence.allHardwareWorkloadsPassed,
			evidence.firstProviderProbeUsable, evidence.firstSafeVideoKbps, evidence.secondProviderProbeUsable, evidence.secondSafeVideoKbps);
		const auto result = policy::assembleSharedTwoLegAllocation(evidence.exactTopologyEligible, evidence.concurrentHardwareValidated,
									   evidence.allHardwareWorkloadsPassed, evidence.firstProviderProbeUsable,
									   evidence.firstSafeVideoKbps, evidence.secondProviderProbeUsable,
									   evidence.secondSafeVideoKbps);
		CHECK_FALSE(result.valid);
		CHECK(result.aggregateSafeVideoKbps == 0);
		CHECK(result.perLegVideoKbps == 0);
		CHECK(result.allocatedVideoKbps == 0);
	}
}

TEST_CASE("Auto Config quality policy exposes only the three approved tiers")
{
	const auto result = policy::candidates({1920, 1080, 60, 1});
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
	const policy::VideoTuple ceiling{1920, 1080, 60, 1};
	std::vector<policy::VideoTuple> result;
	for (const auto &tier : policy::hardwareTiers()) {
		const policy::VideoTuple candidate = policy::fitTier(ceiling, tier.longEdge, tier.shortEdge, tier.lowerFps);
		if (std::none_of(result.begin(), result.end(), [&](const policy::VideoTuple &existing) { return policy::sameVideo(existing, candidate); }))
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
	const policy::VideoTuple lowerCeiling{1280, 720, 30, 1};
	for (const auto &tier : policy::hardwareTiers()) {
		const policy::VideoTuple candidate = policy::fitTier(lowerCeiling, tier.longEdge, tier.shortEdge, tier.lowerFps);
		if (std::none_of(result.begin(), result.end(), [&](const policy::VideoTuple &existing) { return policy::sameVideo(existing, candidate); }))
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
	const int oneAttempt = policy::hardwarePhaseTimeoutMs(1, 500, 1500, 3000);
	const int twelveAttempts = policy::hardwarePhaseTimeoutMs(12, 500, 1500, 3000);
	const int twoLegWindowsPrimaryAttempts = policy::hardwarePhaseTimeoutMs(48, 500, 1500, 3000);
	const int twoLegWindowsWithControls = policy::hardwarePhaseTimeoutMs(54, 500, 1500, 3000);
	const int excessiveAttempts = policy::hardwarePhaseTimeoutMs(10000, 500, 1500, 3000);
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
	CHECK(std::string(policy::hardwareFailureCode(false, false)) == "hardware_no_usable_encoder");
	CHECK(std::string(policy::hardwareFailureCode(false, true)) == "hardware_benchmark_overloaded");
	CHECK(std::string(policy::hardwareFailureCode(true, true)) == "hardware_benchmark_timeout");
}

TEST_CASE("Auto Config failure scope preserves lower tiers and the x264 fallback")
{
	CHECK(policy::hardwareFailureScope("hardware_benchmark_encoder_create_failed") == policy::HardwareFailureScope::Workload);
	CHECK(policy::hardwareFailureScope("hardware_benchmark_start_failed") == policy::HardwareFailureScope::Workload);
	CHECK(policy::hardwareFailureScope("hardware_benchmark_output_stopped") == policy::HardwareFailureScope::Workload);
	CHECK(policy::hardwareFailureScope("hardware_benchmark_no_input_frames") == policy::HardwareFailureScope::Workload);
	CHECK(policy::hardwareFailureScope("hardware_benchmark_no_encoded_packets") == policy::HardwareFailureScope::Workload);
	CHECK(policy::hardwareFailureScope("hardware_benchmark_no_output_packets") == policy::HardwareFailureScope::Phase);
	CHECK(policy::hardwareFailureScope("hardware_benchmark_feeder_stalled") == policy::HardwareFailureScope::Phase);
	CHECK(policy::hardwareFailureScope("hardware_benchmark_encoder_unavailable") == policy::HardwareFailureScope::Encoder);
	CHECK(policy::hardwareFailureScope("hardware_benchmark_video_mix_create_failed") == policy::HardwareFailureScope::Encoder);
	CHECK(policy::hardwareFailureScope("hardware_benchmark_video_create_failed") == policy::HardwareFailureScope::Phase);
	CHECK(policy::hardwareFailureScope("hardware_benchmark_audio_create_failed") == policy::HardwareFailureScope::Phase);
	CHECK(policy::hardwareFailureScope("", true) == policy::HardwareFailureScope::Phase);
}

TEST_CASE("Auto Config adopts only conclusive streaming-mix controls")
{
	CHECK(policy::shouldAdoptHardwareControl(true, false, ""));
	CHECK(policy::shouldAdoptHardwareControl(false, true, ""));
	CHECK(policy::shouldAdoptHardwareControl(false, false, "hardware_benchmark_cleanup_timeout"));
	CHECK(policy::shouldAdoptHardwareControl(false, false, "", true));
	CHECK_FALSE(policy::shouldAdoptHardwareControl(false, false, "hardware_benchmark_video_mix_create_failed"));
	CHECK_FALSE(policy::shouldAdoptHardwareControl(false, false, "hardware_benchmark_no_input_frames"));
	CHECK_FALSE(policy::shouldAdoptHardwareControl(false, false, "hardware_benchmark_no_encoded_packets"));
}

TEST_CASE("Auto Config hardware sample classifier keeps zero packets distinct from overload")
{
	auto result = policy::classifyHardwareSample(false, 0, 0, 0, 0, 34, 2);
	CHECK_FALSE(result.success);
	CHECK(std::string(result.errorCode) == "hardware_benchmark_feeder_stalled");

	result = policy::classifyHardwareSample(true, 0, 0, 0, 0, 34, 2);
	CHECK_FALSE(result.success);
	CHECK(std::string(result.errorCode) == "hardware_benchmark_no_input_frames");

	result = policy::classifyHardwareSample(true, 45, 0, 0, 0, 34, 2);
	CHECK_FALSE(result.success);
	CHECK(std::string(result.errorCode) == "hardware_benchmark_no_encoded_packets");

	result = policy::classifyHardwareSample(true, 45, 0, 35, 0, 34, 2);
	CHECK_FALSE(result.success);
	CHECK(std::string(result.errorCode) == "hardware_benchmark_no_output_packets");

	result = policy::classifyHardwareSample(false, 45, 0, 35, 35, 34, 2);
	CHECK_FALSE(result.success);
	CHECK(std::string(result.errorCode) == "hardware_benchmark_feeder_stalled");

	result = policy::classifyHardwareSample(true, 45, 0, 20, 20, 34, 2);
	CHECK_FALSE(result.success);
	CHECK(std::string(result.errorCode) == "hardware_benchmark_overloaded");

	result = policy::classifyHardwareSample(true, 45, 3, 35, 35, 34, 2);
	CHECK_FALSE(result.success);
	CHECK(std::string(result.errorCode) == "hardware_benchmark_overloaded");

	result = policy::classifyHardwareSample(true, 45, 2, 35, 35, 34, 2);
	CHECK(result.success);
	CHECK(result.errorCode == nullptr);
}

TEST_CASE("Auto Config derives only exact frame-rate divisors")
{
	auto result = policy::frameRateDivisor(60, 1, 30, 1);
	CHECK(result.supported);
	CHECK(result.value == 2);

	result = policy::frameRateDivisor(60000, 1001, 30000, 1001);
	CHECK(result.supported);
	CHECK(result.value == 2);

	result = policy::frameRateDivisor(50, 1, 50, 1);
	CHECK(result.supported);
	CHECK(result.value == 1);

	result = policy::frameRateDivisor(60000, 1001, 30, 1);
	CHECK_FALSE(result.supported);

	result = policy::frameRateDivisor(30, 1, 60, 1);
	CHECK_FALSE(result.supported);

	result = policy::frameRateDivisor(60, 1, 50, 1);
	CHECK_FALSE(result.supported);
}

TEST_CASE("Auto Config supports two Desktop-managed upload outputs")
{
	CHECK(policy::kMaximumUploadLegs == 2);
}

TEST_CASE("Auto Config quality policy never upscales the tested ceiling")
{
	const auto result = policy::candidates({1280, 720, 30, 1});
	REQUIRE(result.size() == 2);
	CHECK(result.front().width == 1280);
	CHECK(result.front().height == 720);
	CHECK(result.front().fpsNum == 30);
	CHECK(policy::select({1280, 720, 30, 1}, 10000, "obs_nvenc_h264_tex").video.width == 1280);
}

TEST_CASE("Auto Config preserves probe headroom while capping the recommended bitrate")
{
	const auto stableHeadroom = policy::select({1920, 1080, 60, 1}, 10000, "obs_nvenc_h264_tex");
	CHECK(stableHeadroom.bitrateKbps == policy::kMaximumRecommendedBitrateKbps);
	CHECK(stableHeadroom.video.width == 1920);
	CHECK(stableHeadroom.video.height == 1080);
	CHECK(policy::select({1920, 1080, 60, 1}, 7900, "obs_nvenc_h264_tex").bitrateKbps == 7900);
}

TEST_CASE("Auto Config benchmark ceiling explicitly permits isolated promotion above the current canvas")
{
	const policy::VideoTuple current{1280, 720, 30, 1};
	CHECK(policy::sameVideo(policy::benchmarkCeiling(current, 0, 0), current));
	CHECK(policy::sameVideo(policy::benchmarkCeiling(current, 1920, 0, 60, 1), current));

	const auto promoted = policy::benchmarkCeiling(current, 1920, 1080, 60, 1);
	CHECK(promoted.width == 1920);
	CHECK(promoted.height == 1080);
	CHECK(promoted.fpsNum == 60);
	CHECK(promoted.fpsDen == 1);
	CHECK(policy::select(promoted, 6000, "obs_nvenc_h264_tex").video.width == 1920);
	CHECK(policy::select(promoted, 6000, "obs_nvenc_h264_tex").video.fpsNum == 60);
	CHECK(policy::select(promoted, 3000, "obs_nvenc_h264_tex").video.width == 1280);

	const auto canvasBound = policy::benchmarkCeiling(current, 1280, 720);
	CHECK(policy::select(canvasBound, 10000, "obs_nvenc_h264_tex").video.width == 1280);
	CHECK(canvasBound.fpsNum == 30);

	const auto frameRateOnly = policy::benchmarkCeiling(current, 1280, 720, 60, 1);
	CHECK(frameRateOnly.width == 1280);
	CHECK(frameRateOnly.height == 720);
	CHECK(frameRateOnly.fpsNum == 60);
	CHECK(frameRateOnly.fpsDen == 1);

	const auto ntsc = policy::benchmarkCeiling({1280, 720, 30000, 1001}, 1280, 720, 60000, 1001);
	CHECK(ntsc.fpsNum == 60000);
	CHECK(ntsc.fpsDen == 1001);

	const auto cappedInteger = policy::benchmarkCeiling(current, 1280, 720, 120, 1);
	CHECK(cappedInteger.fpsNum == 60);
	CHECK(cappedInteger.fpsDen == 1);
	const auto cappedNtsc = policy::benchmarkCeiling({1280, 720, 30000, 1001}, 1280, 720, 120000, 1001);
	CHECK(cappedNtsc.fpsNum == 60000);
	CHECK(cappedNtsc.fpsDen == 1001);

	const auto productBound = policy::benchmarkCeiling(current, 3840, 2160);
	CHECK(productBound.width == 1920);
	CHECK(productBound.height == 1080);

	const auto lowerProductBound = policy::benchmarkCeiling({1920, 1080, 30, 1}, 1280, 720);
	CHECK(lowerProductBound.width == 1280);
	CHECK(lowerProductBound.height == 720);
	const auto boundingBox = policy::boundCurrentToSupportedTier({1920, 1080, 30, 1}, 1366, 768);
	CHECK(boundingBox.width == 1280);
	CHECK(boundingBox.height == 720);
}

TEST_CASE("Auto Config adds a raw-input hardware check only above the rendered frame rate")
{
	CHECK(policy::requiresExactHardwareCadenceValidation(true, 30, 1, 60, 1));
	CHECK(policy::requiresExactHardwareCadenceValidation(true, 30000, 1001, 60000, 1001));
	CHECK_FALSE(policy::requiresExactHardwareCadenceValidation(true, 60, 1, 60, 1));
	CHECK_FALSE(policy::requiresExactHardwareCadenceValidation(true, 60, 1, 30, 1));
	CHECK_FALSE(policy::requiresExactHardwareCadenceValidation(false, 30, 1, 60, 1));
	CHECK_FALSE(policy::requiresExactHardwareCadenceValidation(true, 0, 1, 60, 1));
}

TEST_CASE("Auto Config raw-input failures at the exact frame rate reject only that workload")
{
	CHECK(policy::exactCadenceValidationFailureScope("hardware_benchmark_encoder_unavailable") == policy::HardwareFailureScope::Workload);
	CHECK(policy::exactCadenceValidationFailureScope("hardware_benchmark_video_create_failed") == policy::HardwareFailureScope::Workload);
	CHECK(policy::exactCadenceValidationFailureScope("hardware_benchmark_no_output_packets") == policy::HardwareFailureScope::Workload);
	CHECK(policy::exactCadenceValidationFailureScope("hardware_benchmark_feeder_stalled") == policy::HardwareFailureScope::Workload);
	CHECK(policy::exactCadenceValidationFailureScope("hardware_benchmark_overloaded") == policy::HardwareFailureScope::Workload);
	CHECK(policy::exactCadenceValidationFailureScope("hardware_benchmark_cleanup_timeout") == policy::HardwareFailureScope::Phase);
	CHECK(policy::exactCadenceValidationFailureScope("", true) == policy::HardwareFailureScope::Phase);
}

TEST_CASE("Auto Config benchmark ceiling preserves portrait orientation")
{
	const auto promoted = policy::benchmarkCeiling({720, 1280, 30, 1}, 1080, 1920);
	CHECK(promoted.width == 1080);
	CHECK(promoted.height == 1920);
	CHECK(policy::select(promoted, 6000, "obs_nvenc_h264_tex").video.height == 1920);
}

TEST_CASE("Auto Config promotes only supported 16:9 and 9:16 aspect ratios")
{
	const std::vector<policy::VideoTuple> custom = {
		{1600, 1000, 30, 1}, // 16:10
		{1024, 768, 30, 1},  // 4:3
		{2560, 1080, 30, 1}, // ultrawide
	};
	for (const auto &current : custom) {
		CAPTURE(current.width, current.height);
		CHECK(policy::sameVideo(policy::boundCurrentToSupportedTier(current, 640, 360), current));
		const auto ceiling = policy::benchmarkCeiling(current, 1920, 1080, 60, 1);
		CHECK(policy::sameVideo(ceiling, current));
		const auto options = policy::candidates(ceiling);
		REQUIRE(options.size() == 1);
		CHECK(policy::sameVideo(options.front(), current));
		CHECK(policy::sameVideo(policy::select(ceiling, 100, "x264").video, current));
		CHECK(options.front().width >= 64);
		CHECK(options.front().height >= 64);
	}

	const policy::VideoTuple minimumSafe{8192, 64, 30, 1};
	const auto minimumOptions = policy::candidates(minimumSafe);
	REQUIRE(minimumOptions.size() == 1);
	CHECK(policy::sameVideo(minimumOptions.front(), minimumSafe));
}

TEST_CASE("Auto Config recommendation promotion requires successful active evidence")
{
	const policy::VideoTuple current{1280, 720, 30, 1};
	const policy::VideoTuple tested{1920, 1080, 60, 1};

	const auto active = policy::recommendationCeiling(tested, current, true);
	CHECK(policy::select(active, 6000, "obs_nvenc_h264_tex").video.width == 1920);
	CHECK(policy::isQualityPromotion(current, policy::select(active, 6000, "obs_nvenc_h264_tex").video));
	CHECK(policy::select(active, 6000, "obs_nvenc_h264_tex").video.fpsNum == 60);
	CHECK(policy::select(active, 3000, "obs_nvenc_h264_tex").video.width == 1280);

	const auto estimated = policy::recommendationCeiling(tested, current, false);
	CHECK(policy::sameVideo(estimated, current));
	CHECK(policy::select(estimated, 6000, "obs_nvenc_h264_tex").video.width == 1280);

	// A failed probe is represented by the same no-promotion decision even when
	// it leaves usable throughput evidence for a conservative bitrate estimate.
	const auto failedProbe = policy::recommendationCeiling(tested, current, false);
	CHECK(policy::select(failedProbe, 6000, "obs_nvenc_h264_tex").video.width == 1280);

	const policy::VideoTuple testedFrameRateOnly{1280, 720, 60, 1};
	CHECK(policy::isQualityPromotion(current, policy::recommendationCeiling(testedFrameRateOnly, current, true)));
	CHECK(policy::sameVideo(policy::recommendationCeiling(testedFrameRateOnly, current, false), current));
}

TEST_CASE("Auto Config quality policy preserves orientation aspect ratio and even dimensions")
{
	const auto result = policy::candidates({1080, 1920, 60000, 1001});
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
	const policy::VideoTuple ceiling{1920, 1080, 60, 1};
	const auto high = policy::select(ceiling, 6000, "obs_nvenc_h264_tex");
	const auto medium = policy::select(ceiling, 3000, "obs_nvenc_h264_tex");
	const auto low = policy::select(ceiling, 1200, "obs_nvenc_h264_tex");
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
	const auto rungs = policy::candidates({1920, 1080, 60, 1});
	REQUIRE(rungs.size() == 6);
	for (const auto &rung : rungs) {
		const int threshold = policy::roundedMinimumBitrateKbps(rung, "obs_nvenc_h264_tex");
		CAPTURE(rung.width, rung.height, rung.fpsNum, rung.fpsDen, threshold);
		CHECK_FALSE(policy::supports(rung, threshold - 1, "obs_nvenc_h264_tex"));
		CHECK(policy::supports(rung, threshold, "obs_nvenc_h264_tex"));
		CHECK(policy::supports(rung, threshold + 1, "obs_nvenc_h264_tex"));
	}
}

TEST_CASE("Auto Config quality policy preserves 50 and 59.94 broadcast-rate families")
{
	const auto pal = policy::candidates({1920, 1080, 50, 1});
	REQUIRE(pal.size() == 6);
	CHECK(pal[0].fpsNum == 50);
	CHECK(pal[0].fpsDen == 1);
	CHECK(pal[1].fpsNum == 25);
	CHECK(pal[1].fpsDen == 1);

	const auto ntsc = policy::candidates({1920, 1080, 60000, 1001});
	REQUIRE(ntsc.size() == 6);
	CHECK(ntsc[0].fpsNum == 60000);
	CHECK(ntsc[0].fpsDen == 1001);
	CHECK(ntsc[1].fpsNum == 30000);
	CHECK(ntsc[1].fpsDen == 1001);
}

TEST_CASE("Auto Config quality policy caps high frame rate inputs to 60 or 59.94")
{
	const auto integer = policy::candidates({1920, 1080, 120, 1});
	REQUIRE(integer.size() == 6);
	CHECK(integer[0].fpsNum == 60);
	CHECK(integer[0].fpsDen == 1);
	CHECK(integer[1].fpsNum == 30);
	CHECK(integer[1].fpsDen == 1);

	const auto ntsc = policy::candidates({1920, 1080, 120000, 1001});
	REQUIRE(ntsc.size() == 6);
	CHECK(ntsc[0].fpsNum == 60000);
	CHECK(ntsc[0].fpsDen == 1001);
	CHECK(ntsc[1].fpsNum == 30000);
	CHECK(ntsc[1].fpsDen == 1001);
}

TEST_CASE("High FPS preference intentionally chooses 540p60 over 720p30 when both fit")
{
	const policy::VideoTuple p720_30{1280, 720, 30, 1};
	const int budget = policy::roundedMinimumBitrateKbps(p720_30, "obs_nvenc_h264_tex");
	const auto result = policy::select({1920, 1080, 60, 1}, budget, "obs_nvenc_h264_tex");
	CHECK(result.video.width == 960);
	CHECK(result.video.height == 540);
	CHECK(result.video.fpsNum == 60);
}

TEST_CASE("Twitch quality profile follows the product bitrate ladder at every boundary")
{
	const policy::VideoTuple ceiling{1920, 1080, 60, 1};
	struct Expectation {
		int bitrateKbps;
		int width;
		int height;
		int fpsNum;
	};
	const Expectation expectations[] = {
		{6000, 1920, 1080, 60}, {5500, 1920, 1080, 60}, {5499, 1920, 1080, 30}, {5000, 1920, 1080, 30}, {4999, 1280, 720, 60}, {4500, 1280, 720, 60},
		{4499, 1280, 720, 30},  {3968, 1280, 720, 30},  {3145, 1280, 720, 30},  {3000, 1280, 720, 30},  {2999, 960, 540, 60},
	};

	for (const auto &expectation : expectations) {
		CAPTURE(expectation.bitrateKbps);
		const auto result = policy::select(ceiling, expectation.bitrateKbps, "obs_nvenc_h264_tex", policy::QualityProfile::Twitch);
		CHECK(result.video.width == expectation.width);
		CHECK(result.video.height == expectation.height);
		CHECK(result.video.fpsNum == expectation.fpsNum);
	}
}

TEST_CASE("Generic quality profile keeps the upstream high frame-rate preference")
{
	for (const int bitrateKbps : {3145, 2999}) {
		CAPTURE(bitrateKbps);
		const auto result = policy::select({1920, 1080, 60, 1}, bitrateKbps, "obs_nvenc_h264_tex", policy::QualityProfile::Generic);
		CHECK(result.video.width == 1280);
		CHECK(result.video.height == 720);
		CHECK(result.video.fpsNum == 60);
	}
}

TEST_CASE("Twitch quality profile respects the tested video ceiling")
{
	const auto result = policy::select({1280, 720, 60, 1}, 6000, "obs_nvenc_h264_tex", policy::QualityProfile::Twitch);
	CHECK(result.video.width == 1280);
	CHECK(result.video.height == 720);
	CHECK(result.video.fpsNum == 60);
}

TEST_CASE("Twitch quality profile is independent of the selected H264 encoder family")
{
	for (const char *encoderFamily : {"obs_nvenc_h264_tex", "qsv", "amd", "x264"}) {
		CAPTURE(encoderFamily);
		const auto result = policy::select({1920, 1080, 60, 1}, 3145, encoderFamily, policy::QualityProfile::Twitch);
		CHECK(result.video.width == 1280);
		CHECK(result.video.height == 720);
		CHECK(result.video.fpsNum == 30);
	}
}

TEST_CASE("Twitch quality profile preserves orientation and broadcast-rate families")
{
	const auto portrait = policy::select({1080, 1920, 60000, 1001}, 4499, "obs_nvenc_h264_tex", policy::QualityProfile::Twitch);
	CHECK(portrait.video.width == 720);
	CHECK(portrait.video.height == 1280);
	CHECK(portrait.video.fpsNum == 30000);
	CHECK(portrait.video.fpsDen == 1001);

	const auto pal = policy::select({1920, 1080, 50, 1}, 5499, "obs_nvenc_h264_tex", policy::QualityProfile::Twitch);
	CHECK(pal.video.width == 1920);
	CHECK(pal.video.height == 1080);
	CHECK(pal.video.fpsNum == 25);
	CHECK(pal.video.fpsDen == 1);
}

TEST_CASE("Auto Config quality policy reports bandwidth below its minimum tier")
{
	const auto result = policy::select({1920, 1080, 60, 1}, 100, "x264");
	CHECK(result.video.width == 960);
	CHECK(result.video.height == 540);
	CHECK(result.video.fpsNum == 30);
	CHECK(result.bitrateKbps == 100);
	CHECK(result.insufficientBandwidth);
}
