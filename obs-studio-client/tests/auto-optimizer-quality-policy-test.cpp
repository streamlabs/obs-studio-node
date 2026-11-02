#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <initializer_list>
#include <limits>
#include <utility>

#include "auto-optimizer-quality-policy.hpp"
#include "auto-optimizer-probe-policy.hpp"

namespace policy = autoOptimizer::qualityPolicy;

TEST_CASE("Multistream bitrate policy counts platforms rather than canvases")
{
	CHECK_FALSE(policy::isMultiplatformStream({}));
	CHECK_FALSE(policy::isMultiplatformStream({"twitch", "twitch"}));
	CHECK_FALSE(policy::isMultiplatformStream({"youtube", "youtube"}));
	CHECK(policy::isMultiplatformStream({"twitch", "youtube"}));
	CHECK(policy::isMultiplatformStream({"youtube", "kick"}));
	CHECK(policy::kMultistreamVideoBitrateLimitKbps == 6000);
}

TEST_CASE("Multistream bitrate limits preserve single-platform allowances and Twitch-managed ladders")
{
	// This is a policy assertion, not a requirement that the CI machine pass a
	// real encoder benchmark. Startup and cancellation remain integration-tested.
	for (const auto &[platforms, limited] : std::vector<std::pair<std::vector<std::string>, bool>>{{{"youtube", "kick"}, true},
												       {{"twitch", "youtube", "facebook"}, true},
												       {{"twitch", "youtube", "custom"}, true},
												       {{"twitch", "other"}, true},
												       {{"youtube", "unknown"}, true},
												       {{"twitch", "youtube"}, false},
												       {{"youtube"}, false},
												       {{"youtube", "youtube"}, false},
												       {{"twitch", "twitch"}, false},
												       {{"kick"}, false},
												       {{}, false}}) {
		CAPTURE(platforms);
		const int limit = policy::applyMultistreamBitrateLimitKbps(8000, platforms, true);
		CHECK(limit == (limited ? 6000 : 8000));
		CHECK(policy::composeEstimatedBitrateKbps(8000, limit) == limit);
		CHECK(policy::applyMultistreamBitrateLimitKbps(0, platforms, true) == (limited ? 6000 : 0));
		CHECK(policy::applyMultistreamBitrateLimitKbps(8000, platforms, false) == 8000);
		CHECK(policy::applyMultistreamBitrateLimitKbps(0, platforms, false) == 0);
		for (int stricterLimit : {2500, 4500, 6000}) {
			CAPTURE(stricterLimit);
			CHECK(policy::applyMultistreamBitrateLimitKbps(stricterLimit, platforms, true) == stricterLimit);
			CHECK(policy::applyMultistreamBitrateLimitKbps(stricterLimit, platforms, false) == stricterLimit);
		}
	}
}

TEST_CASE("Auto Optimizer accepts only effective frame rates from 1 through 240 FPS")
{
	CHECK_FALSE(policy::isValidFrameRate(0, 1));
	CHECK_FALSE(policy::isValidFrameRate(1, 0));
	CHECK_FALSE(policy::isValidFrameRate(1, 10000));
	CHECK(policy::isValidFrameRate(1, 1));
	CHECK(policy::isValidFrameRate(30000, 1001));
	CHECK(policy::isValidFrameRate(240000, 1000));
	CHECK_FALSE(policy::isValidFrameRate(240001, 1000));
}

TEST_CASE("Auto Optimizer Dual Output uses one shared minimum frame rate")
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

TEST_CASE("Auto Optimizer allocates a shared uplink equally across two direct legs")
{
	struct Expectation {
		uint64_t firstSafeVideoKbps;
		uint64_t secondSafeVideoKbps;
		uint64_t expectedPerLegVideoKbps;
	};
	const Expectation expectations[] = {
		{6000, 10000, 5000},  {6000, 9000, 4500}, {5000, 10000, 5000}, {4000, 10000, 4000}, {5000, 5000, 2500},
		{12000, 12000, 6000}, {0, 12000, 6000},   {12000, 0, 6000},    {0, 10000, 5000},    {10000, 0, 5000},
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

TEST_CASE("Auto Optimizer Dual Output composes provider probes into the allowed shared bitrate")
{
	namespace probes = autoOptimizer::probePolicy;
	struct Scenario {
		std::vector<std::string> platforms;
		const char *horizontalProvider;
		const char *verticalProvider;
		bool horizontal1440p;
		int requestLimit;
		int expectedPerOutput;
		int expectedYoutubeProbe;
	};
	const Scenario scenarios[] = {
		{{"twitch", "youtube"}, "twitch", "youtube", true, 8000, 7500, 15000},
		{{"youtube", "twitch"}, "youtube", "twitch", true, 8000, 7500, 15000},
		{{"youtube", "youtube"}, "youtube", "youtube", true, 8000, 8000, 16000},
		{{"twitch", "youtube", "kick"}, "twitch", "youtube", true, 8000, 6000, 12000},
		{{"youtube", "twitch", "facebook"}, "youtube", "twitch", true, 8000, 6000, 12000},
		{{"youtube", "youtube", "custom"}, "youtube", "youtube", true, 8000, 6000, 12000},
		{{"youtube", "twitch"}, "youtube", "twitch", false, 8000, 6000, 12000},
		{{"youtube", "twitch"}, "youtube", "twitch", true, 4500, 4500, 10000},
	};
	for (const auto &scenario : scenarios) {
		CAPTURE(scenario.platforms, scenario.horizontalProvider, scenario.verticalProvider, scenario.horizontal1440p, scenario.requestLimit);
		const bool experimental = policy::usesExperimentalTwitchBitrate(scenario.platforms, scenario.horizontal1440p);
		const int experimentalMaximum = experimental ? policy::kExperimentalTwitch1440pBitrateKbps : 0;
		std::vector<int> limits;
		for (const std::string provider : {scenario.horizontalProvider, scenario.verticalProvider}) {
			const auto service = probes::rtmpProbeServiceBitratePolicy(provider, provider == "twitch" ? 6000 : 0, experimentalMaximum);
			const int platformLimit = policy::applyMultistreamBitrateLimitKbps(scenario.requestLimit, scenario.platforms, true);
			limits.push_back(policy::composeEstimatedBitrateKbps(8000, platformLimit, service.maximumKbps));
		}
		const int commonLimit = policy::commonOutputBitrateLimitKbps(limits);
		CHECK(commonLimit == scenario.expectedPerOutput);
		const int youtubeMaximum = probes::youtubeProbeMaximumBitrateKbps(true, commonLimit);
		CHECK(youtubeMaximum == scenario.expectedYoutubeProbe);
		std::vector<uint64_t> safeBitrates;
		for (const std::string provider : {scenario.horizontalProvider, scenario.verticalProvider}) {
			const auto service = probes::rtmpProbeServiceBitratePolicy(provider, provider == "twitch" ? 6000 : youtubeMaximum, experimentalMaximum);
			const int ceiling = probes::effectiveProbeCeilingKbps(provider == "youtube" ? youtubeMaximum : 10000, service.maximumKbps,
									      provider == "youtube" ? 0 : commonLimit);
			// Model successful samples at every planned target. Real transport and
			// hardware failures are tested separately and must still block allocation.
			if (provider == "youtube") {
				probes::YoutubeRampEvidence evidence;
				for (const auto &[rung, target] : probes::youtubeProbeTargets(ceiling))
					evidence.observeAcceptedTarget(target + 128, target);
				safeBitrates.push_back(evidence.recommendedVideoKbps());
			} else {
				safeBitrates.push_back(probes::decideTwitchProbe(ceiling + 128, ceiling, 0).recommendedVideoKbps);
			}
		}
		const auto allocation = policy::assembleSharedTwoLegAllocation(true, true, true, safeBitrates[0], safeBitrates[1], commonLimit);
		REQUIRE(allocation.valid);
		CHECK(allocation.perLegVideoKbps == scenario.expectedPerOutput);
		CHECK(allocation.allocatedVideoKbps == 2 * scenario.expectedPerOutput);
		CHECK_FALSE(policy::assembleSharedTwoLegAllocation(true, true, false, safeBitrates[0], safeBitrates[1], commonLimit).valid);
		CHECK_FALSE(policy::assembleSharedTwoLegAllocation(true, false, true, safeBitrates[0], safeBitrates[1], commonLimit).valid);
	}
}

TEST_CASE("Auto Optimizer shared YouTube probes can confirm pressure at the final rung")
{
	namespace probes = autoOptimizer::probePolicy;
	for (const auto &providers : std::vector<std::vector<std::string>>{{"twitch", "youtube"}, {"youtube", "twitch"}, {"youtube", "youtube"}}) {
		for (const bool finalPressure : {false, true}) {
			CAPTURE(providers, finalPressure);
			const int outputLimit = policy::allowsExperimentalTwitchBitrate(providers) ? 7500 : 8000;
			const int ceiling = probes::youtubeProbeMaximumBitrateKbps(true, outputLimit);
			const auto budget = probes::youtubeProbeBudgetLimits(true, ceiling);
			// Include maximum connection/ingest waits and two clean baseline samples.
			int64_t elapsedMs = 8000 + 15000 + 10000;
			uint64_t bytesUsed = 1128ULL * (15000 + 10000) / 8;
			int activeTarget = 1000;
			probes::YoutubeRampEvidence evidence;
			evidence.observeAcceptedTarget(1128, 1000);
			const probes::YoutubeBaselineAssessment baseline{probes::YoutubeBaselineDecision::Clean, {10000, 0, 0, 0}};
			for (const auto &[rung, target] : probes::youtubeProbeTargets(ceiling)) {
				if (target == 1000)
					continue;
				const int previousTarget = activeTarget;
				REQUIRE(probes::decideYoutubeRampStep(budget, budget.timeoutMs - elapsedMs, bytesUsed, activeTarget, 1000, previousTarget,
								      target, evidence.recommendedVideoKbps(), 0) == probes::YoutubeRampStepAction::Measure);
				const bool pressure = finalPressure && target == ceiling;
				const uint64_t measuredAggregate = pressure ? 13000 : target + 128;
				const int duration = probes::youtubeProbeRampSampleMs(target) + probes::kYoutubeProbeSettleMs;
				bytesUsed += measuredAggregate * duration / 8;
				elapsedMs += duration;
				activeTarget = target;
				if (!pressure) {
					evidence.observeAcceptedTarget(measuredAggregate, target);
					continue;
				}
				const auto cost = probes::youtubeConfirmationCost(1000, previousTarget, target);
				// The old64 MiB budget failed here even though12 Mbps was validated.
				CHECK_FALSE(probes::youtubeBudgetFits(probes::youtubeProbeBudgetLimits(true, 12000), 100000, bytesUsed, cost));
				REQUIRE(probes::youtubeBudgetFits(budget, budget.timeoutMs - elapsedMs, bytesUsed, cost));
				const probes::YoutubeProbeSampleMetrics pressureMetrics{8000, 0, 4000, 0};
				REQUIRE(probes::youtubeRequiresCapacityConfirmation(probes::classifyYoutubeProbeLoad(pressureMetrics, baseline)));
				const bool lowRecovered = probes::youtubeLowControlRecovered(baseline.reference, baseline.reference, baseline);
				const bool retryAccepted =
					!probes::youtubeRequiresCapacityConfirmation(probes::classifyYoutubeProbeLoad(pressureMetrics, baseline));
				REQUIRE(probes::decideYoutubeConfirmation(lowRecovered, retryAccepted) == probes::YoutubeConfirmationDecision::CapacityKnee);
				evidence.observeConfirmedCapacity(probes::youtubeConfirmedPressureCapacityKbps(13000, 13000, target), target);
			}
			CHECK(evidence.recommendedVideoKbps() == (finalPressure ? 13000 : ceiling));
			const auto safe = [&](const std::string &provider) -> uint64_t {
				return provider == "youtube" ? evidence.recommendedVideoKbps() : outputLimit;
			};
			const auto allocation = policy::assembleSharedTwoLegAllocation(true, true, true, safe(providers[0]), safe(providers[1]), outputLimit);
			REQUIRE(allocation.valid);
			CHECK(allocation.perLegVideoKbps == (finalPressure ? 6500 : outputLimit));
		}
	}
}

TEST_CASE("Auto Optimizer shared probe ceilings respect the stricter output request")
{
	CHECK(policy::commonOutputBitrateLimitKbps({8000, 4500}) == 4500);
	CHECK(policy::commonOutputBitrateLimitKbps({4500, 8000}) == 4500);
	CHECK(policy::commonOutputBitrateLimitKbps({0, 7500}) == 7500);
	CHECK(policy::commonOutputBitrateLimitKbps({0, 0}) == 8000);
	CHECK_FALSE(policy::usesExperimentalTwitchBitrate({"youtube", "youtube"}, true));
	CHECK_FALSE(policy::usesExperimentalTwitchBitrate({}, true));
}

TEST_CASE("Auto Optimizer reselects asymmetric canvas tiers at one shared cadence")
{
	policy::VideoTuple horizontal{1920, 1080, 60, 1};
	policy::VideoTuple vertical{720, 1280, 60, 1};
	auto first = policy::select(horizontal, 3000, "nvenc", policy::QualityProfile::Twitch).video;
	auto second = policy::select(vertical, 3000, "nvenc").video;
	REQUIRE(first.fpsNum != second.fpsNum);
	policy::applySharedMinimumCadence(first, second);
	horizontal.fpsNum = first.fpsNum;
	horizontal.fpsDen = first.fpsDen;
	vertical.fpsNum = second.fpsNum;
	vertical.fpsDen = second.fpsDen;
	first = policy::select(horizontal, 3000, "nvenc", policy::QualityProfile::Twitch).video;
	second = policy::select(vertical, 3000, "nvenc").video;
	CHECK(first.fpsNum == 30);
	CHECK(second.fpsNum == first.fpsNum);
	CHECK(second.fpsDen == first.fpsDen);
}

TEST_CASE("Auto Optimizer shared fallback preserves estimates and partial probe bounds")
{
	CHECK(policy::composeSharedFallbackBitrateKbps(4500, 6000) == 4500);
	CHECK(policy::composeSharedFallbackBitrateKbps(6000, 4500) == 4500);
	CHECK(policy::composeSharedFallbackBitrateKbps(4500, 6000, 6000) == 3000);
	CHECK(policy::composeSharedFallbackBitrateKbps(6000, 4500, 6000) == 3000);
	CHECK(policy::composeSharedFallbackBitrateKbps(4500, 6000, 12000) == 4500);
	CHECK(policy::composeSharedFallbackBitrateKbps(4500, 6000, 5999) == 2999);
	CHECK(policy::composeSharedFallbackBitrateKbps(4500, 6000, 1) == 1);
	CHECK(policy::composeSharedFallbackBitrateKbps(4500, 6000, std::numeric_limits<uint64_t>::max()) == 4500);
	const int requestLimitedEstimate = policy::composeEstimatedBitrateKbps(4500, 1800);
	CHECK(policy::composeSharedFallbackBitrateKbps(requestLimitedEstimate, 6000, 6000) == 1800);
}

TEST_CASE("Auto Optimizer keeps unmeasured asymmetric canvases on one conservative bitrate and frame rate")
{
	// Exercise fallback selection without making policy coverage depend on the
	// runner sustaining two real encoder workloads. Integration tests cover their
	// startup and cancellation separately.
	const policy::VideoTuple horizontalCurrent{1920, 1080, 30, 1};
	const policy::VideoTuple verticalCurrent{720, 1280, 60, 1};
	for (const uint64_t observedSafeKbps : {0ULL, 6000ULL}) {
		CAPTURE(observedSafeKbps);
		const int bitrate = policy::composeSharedFallbackBitrateKbps(4500, 6000, observedSafeKbps);
		CHECK(bitrate == (observedSafeKbps == 0 ? 4500 : 3000));
		for (const auto *encoderFamily : {"obs_nvenc_h264_tex", "x264"}) {
			CAPTURE(encoderFamily);
			auto horizontal = policy::select(horizontalCurrent, bitrate, encoderFamily).video;
			auto vertical = policy::select(verticalCurrent, bitrate, encoderFamily).video;
			REQUIRE(horizontal.fpsNum != vertical.fpsNum);
			policy::applySharedMinimumCadence(horizontal, vertical);
			const auto first = policy::select(horizontal, bitrate, encoderFamily);
			const auto second = policy::select(vertical, bitrate, encoderFamily);
			CHECK(first.bitrateKbps == bitrate);
			CHECK(second.bitrateKbps == first.bitrateKbps);
			CHECK(first.video.fpsNum == 30);
			CHECK(first.video.fpsDen == 1);
			CHECK(second.video.fpsNum * first.video.fpsDen == first.video.fpsNum * second.video.fpsDen);
			CHECK_FALSE(policy::isQualityPromotion(horizontalCurrent, first.video));
			CHECK_FALSE(policy::isQualityPromotion(verticalCurrent, second.video));
		}
	}
}

TEST_CASE("Auto Optimizer shared two-leg allocation is symmetric")
{
	const auto forward = policy::allocateSharedTwoLegBandwidth(6000, 10000);
	const auto reversed = policy::allocateSharedTwoLegBandwidth(10000, 6000);
	CHECK(forward.valid);
	CHECK(reversed.valid);
	CHECK(forward.aggregateSafeVideoKbps == reversed.aggregateSafeVideoKbps);
	CHECK(forward.perLegVideoKbps == reversed.perLegVideoKbps);
	CHECK(forward.allocatedVideoKbps == reversed.allocatedVideoKbps);
}

TEST_CASE("Auto Optimizer shared two-leg allocation rejects zero and sub-quantum budgets")
{
	for (const auto &[firstSafeVideoKbps, secondSafeVideoKbps] : std::initializer_list<std::pair<uint64_t, uint64_t>>{{0, 0}, {1, 1}, {199, 199}}) {
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

TEST_CASE("Auto Optimizer shared two-leg allocation cannot overflow its aggregate budget")
{
	const uint64_t maximum = std::numeric_limits<uint64_t>::max();
	const auto allocation = policy::allocateSharedTwoLegBandwidth(maximum, maximum);
	REQUIRE(allocation.valid);
	CHECK(allocation.aggregateSafeVideoKbps == maximum);
	CHECK(allocation.perLegVideoKbps % 100 == 0);
	CHECK(allocation.allocatedVideoKbps == allocation.perLegVideoKbps * 2);
	CHECK(allocation.allocatedVideoKbps <= allocation.aggregateSafeVideoKbps);
}

TEST_CASE("Auto Optimizer allocates shared upload only after complete supported coverage and concurrent encoding")
{
	const auto both = policy::assembleSharedTwoLegAllocation(true, true, true, 6000, 10000);
	REQUIRE(both.valid);
	CHECK(both.perLegVideoKbps == 5000);
	const auto youtubeOnly = policy::assembleSharedTwoLegAllocation(true, true, true, 0, 12000);
	REQUIRE(youtubeOnly.valid);
	CHECK(youtubeOnly.perLegVideoKbps == 6000);
	CHECK(policy::assembleSharedTwoLegAllocation(true, true, true, 0, 12000, 4500).perLegVideoKbps == 4500);
	CHECK_FALSE(policy::assembleSharedTwoLegAllocation(false, true, true, 6000, 10000).valid);
	CHECK_FALSE(policy::assembleSharedTwoLegAllocation(true, false, true, 6000, 10000).valid);
	CHECK_FALSE(policy::assembleSharedTwoLegAllocation(true, true, false, 0, 12000).valid);
	CHECK_FALSE(policy::assembleSharedTwoLegAllocation(true, true, true, 0, 0).valid);
}

TEST_CASE("Auto Optimizer quality policy exposes only the three approved tiers")
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

TEST_CASE("Auto Optimizer hardware tests high frame rates in product-priority order")
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

TEST_CASE("Auto Optimizer permits only Twitch and YouTube destinations above 1080p")
{
	for (const auto &platforms :
	     std::vector<std::vector<std::string>>{{"twitch"}, {"youtube"}, {"youtube", "youtube"}, {"twitch", "youtube"}, {"youtube", "twitch", "twitch"}}) {
		CAPTURE(platforms);
		CHECK(policy::isTwitchOrYouTubeOnlyStream(platforms));
		CHECK(policy::allowsExperimentalTwitchBitrate(platforms) == (std::find(platforms.begin(), platforms.end(), "twitch") != platforms.end()));
	}
	for (const auto &other : {"kick", "facebook", "tiktok", "custom", "other", "unknown"}) {
		for (const auto &platforms :
		     std::vector<std::vector<std::string>>{{other}, {"youtube", other}, {"twitch", other}, {"twitch", "youtube", other}}) {
			CAPTURE(platforms);
			CHECK_FALSE(policy::isTwitchOrYouTubeOnlyStream(platforms));
			CHECK_FALSE(policy::allowsExperimentalTwitchBitrate(platforms));
		}
	}
	CHECK_FALSE(policy::isTwitchOrYouTubeOnlyStream({}));
	CHECK_FALSE(policy::allowsExperimentalTwitchBitrate({}));
}

TEST_CASE("Auto Optimizer retains the experimental Twitch measured 7.5 Mbps quality budget")
{
	CHECK(policy::isTwitchOnlyStream({"twitch", "twitch"}));
	CHECK_FALSE(policy::isTwitchOnlyStream({"twitch", "youtube"}));
	CHECK_FALSE(policy::isTwitchOnlyStream({"twitch", "custom"}));
	CHECK_FALSE(policy::isTwitchOnlyStream({}));
	const policy::VideoTuple ceiling{2560, 1440, 60, 1};
	CHECK(policy::select(ceiling, 7500, "nvenc", policy::QualityProfile::Twitch).video.width == 2560);
	CHECK(policy::select(ceiling, 7499, "nvenc", policy::QualityProfile::Twitch).video.width == 1920);
	CHECK(policy::select(ceiling, 6000, "nvenc", policy::QualityProfile::Twitch).video.width == 1920);
	CHECK(policy::benchmarkCeiling({1280, 720, 30, 1}, 2560, 1440, 60, 1).width == 2560);
}

TEST_CASE("Auto Optimizer hardware timeout scales with work and remains bounded")
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

TEST_CASE("Auto Optimizer distinguishes no usable encoder from overload and timeout")
{
	CHECK(std::string(policy::hardwareFailureCode(false, false)) == "hardware_no_usable_encoder");
	CHECK(std::string(policy::hardwareFailureCode(false, true)) == "hardware_benchmark_overloaded");
	CHECK(std::string(policy::hardwareFailureCode(true, true)) == "hardware_benchmark_timeout");
}

TEST_CASE("Auto Optimizer failure scope preserves lower tiers and the x264 fallback")
{
	CHECK(policy::hardwareFailureScope("hardware_benchmark_encoder_create_failed") == policy::HardwareFailureScope::Workload);
	CHECK(policy::hardwareFailureScope("hardware_benchmark_start_failed") == policy::HardwareFailureScope::Workload);
	CHECK(policy::hardwareFailureScope("hardware_benchmark_output_stopped") == policy::HardwareFailureScope::Workload);
	CHECK(policy::hardwareFailureScope("hardware_benchmark_no_input_frames") == policy::HardwareFailureScope::Workload);
	CHECK(policy::hardwareFailureScope("hardware_benchmark_no_encoded_packets") == policy::HardwareFailureScope::Workload);
	CHECK(policy::hardwareFailureScope("hardware_benchmark_no_output_packets") == policy::HardwareFailureScope::Phase);
	CHECK(policy::hardwareFailureScope("hardware_benchmark_feeder_stalled") == policy::HardwareFailureScope::Workload);
	CHECK(policy::hardwareFailureScope("hardware_benchmark_encoder_unavailable") == policy::HardwareFailureScope::Encoder);
	CHECK(policy::hardwareFailureScope("hardware_benchmark_video_mix_create_failed") == policy::HardwareFailureScope::Encoder);
	CHECK(policy::hardwareFailureScope("hardware_benchmark_video_create_failed") == policy::HardwareFailureScope::Phase);
	CHECK(policy::hardwareFailureScope("hardware_benchmark_audio_create_failed") == policy::HardwareFailureScope::Phase);
	CHECK(policy::hardwareFailureScope("", true) == policy::HardwareFailureScope::Phase);
}

TEST_CASE("Auto Optimizer preserves workload pressure from feeder stalls")
{
	CHECK(policy::hardwareFailureIndicatesOverload("hardware_benchmark_overloaded"));
	CHECK(policy::hardwareFailureIndicatesOverload("hardware_benchmark_feeder_stalled"));
	CHECK_FALSE(policy::hardwareFailureIndicatesOverload("hardware_benchmark_start_failed"));
}

TEST_CASE("Auto Optimizer adopts only conclusive streaming-mix controls")
{
	CHECK(policy::shouldAdoptHardwareControl(true, false, ""));
	CHECK(policy::shouldAdoptHardwareControl(false, true, ""));
	CHECK(policy::shouldAdoptHardwareControl(false, false, "hardware_benchmark_cleanup_timeout"));
	CHECK(policy::shouldAdoptHardwareControl(false, false, "", true));
	CHECK_FALSE(policy::shouldAdoptHardwareControl(false, false, "hardware_benchmark_video_mix_create_failed"));
	CHECK_FALSE(policy::shouldAdoptHardwareControl(false, false, "hardware_benchmark_no_input_frames"));
	CHECK_FALSE(policy::shouldAdoptHardwareControl(false, false, "hardware_benchmark_no_encoded_packets"));
}

TEST_CASE("Auto Optimizer hardware sample classifier keeps zero packets distinct from overload")
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

TEST_CASE("Auto Optimizer derives only exact frame-rate divisors")
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

TEST_CASE("Auto Optimizer supports two Desktop-managed upload outputs")
{
	CHECK(policy::kMaximumUploadLegs == 2);
}

TEST_CASE("Auto Optimizer quality policy never upscales the tested ceiling")
{
	const auto result = policy::candidates({1280, 720, 30, 1});
	REQUIRE(result.size() == 2);
	CHECK(result.front().width == 1280);
	CHECK(result.front().height == 720);
	CHECK(result.front().fpsNum == 30);
	CHECK(policy::select({1280, 720, 30, 1}, 10000, "obs_nvenc_h264_tex").video.width == 1280);
}

TEST_CASE("Auto Optimizer preserves probe headroom while capping the recommended bitrate")
{
	const auto stableHeadroom = policy::select({1920, 1080, 60, 1}, 10000, "obs_nvenc_h264_tex");
	CHECK(stableHeadroom.bitrateKbps == policy::kMaximumRecommendedBitrateKbps);
	CHECK(stableHeadroom.video.width == 1920);
	CHECK(stableHeadroom.video.height == 1080);
	CHECK(policy::select({1920, 1080, 60, 1}, 7900, "obs_nvenc_h264_tex").bitrateKbps == 7900);
}

TEST_CASE("Auto Optimizer composes estimated bitrate limits deterministically")
{
	SECTION("the strictest destination limit wins")
	{
		CHECK(policy::composeEstimatedBitrateKbps(8000, 0, 6000) == 6000);
	}

	SECTION("a lower current bitrate is preserved")
	{
		CHECK(policy::composeEstimatedBitrateKbps(2500, 0, 6000) == 2500);
	}

	SECTION("the product limit applies without a destination limit")
	{
		CHECK(policy::composeEstimatedBitrateKbps(12000) == policy::kMaximumRecommendedBitrateKbps);
	}

	SECTION("a request limit can be stricter than destination limits")
	{
		CHECK(policy::composeEstimatedBitrateKbps(8000, 1800, 6000) == 1800);
	}

	SECTION("missing current bitrate uses the product default")
	{
		CHECK(policy::composeEstimatedBitrateKbps(0) == policy::kDefaultEstimatedBitrateKbps);
		CHECK(policy::composeEstimatedBitrateKbps(-1) == policy::kDefaultEstimatedBitrateKbps);
	}
}

TEST_CASE("Auto Optimizer benchmark ceiling explicitly permits isolated promotion above the current canvas")
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
	CHECK(productBound.width == 2560);
	CHECK(productBound.height == 1440);

	const auto lowerProductBound = policy::benchmarkCeiling({1920, 1080, 30, 1}, 1280, 720);
	CHECK(lowerProductBound.width == 1280);
	CHECK(lowerProductBound.height == 720);
	const auto boundingBox = policy::boundCurrentToSupportedTier({1920, 1080, 30, 1}, 1366, 768);
	CHECK(boundingBox.width == 1280);
	CHECK(boundingBox.height == 720);
}

TEST_CASE("Auto Optimizer adds a raw-input hardware check only above the rendered frame rate")
{
	CHECK(policy::requiresExactHardwareCadenceValidation(true, 30, 1, 60, 1));
	CHECK(policy::requiresExactHardwareCadenceValidation(true, 30000, 1001, 60000, 1001));
	CHECK_FALSE(policy::requiresExactHardwareCadenceValidation(true, 60, 1, 60, 1));
	CHECK_FALSE(policy::requiresExactHardwareCadenceValidation(true, 60, 1, 30, 1));
	CHECK_FALSE(policy::requiresExactHardwareCadenceValidation(false, 30, 1, 60, 1));
	CHECK_FALSE(policy::requiresExactHardwareCadenceValidation(true, 0, 1, 60, 1));
}

TEST_CASE("Auto Optimizer raw-input failures at the exact frame rate reject only that workload")
{
	CHECK(policy::exactCadenceValidationFailureScope("hardware_benchmark_encoder_unavailable") == policy::HardwareFailureScope::Workload);
	CHECK(policy::exactCadenceValidationFailureScope("hardware_benchmark_video_create_failed") == policy::HardwareFailureScope::Workload);
	CHECK(policy::exactCadenceValidationFailureScope("hardware_benchmark_no_output_packets") == policy::HardwareFailureScope::Workload);
	CHECK(policy::exactCadenceValidationFailureScope("hardware_benchmark_feeder_stalled") == policy::HardwareFailureScope::Workload);
	CHECK(policy::exactCadenceValidationFailureScope("hardware_benchmark_overloaded") == policy::HardwareFailureScope::Workload);
	CHECK(policy::exactCadenceValidationFailureScope("hardware_benchmark_cleanup_timeout") == policy::HardwareFailureScope::Phase);
	CHECK(policy::exactCadenceValidationFailureScope("", true) == policy::HardwareFailureScope::Phase);
}

TEST_CASE("Auto Optimizer benchmark ceiling preserves portrait orientation")
{
	const auto promoted = policy::benchmarkCeiling({720, 1280, 30, 1}, 1080, 1920);
	CHECK(promoted.width == 1080);
	CHECK(promoted.height == 1920);
	CHECK(policy::select(promoted, 6000, "obs_nvenc_h264_tex").video.height == 1920);
}

TEST_CASE("Auto Optimizer preserves ultrawide outputs within active and estimated limits")
{
	const int width = GENERATE(3340, 3440);
	const policy::VideoTuple current{width, 1440, 60, 1};
	for (const auto maximum : {policy::VideoTuple{2560, 1440, 60, 1}, policy::VideoTuple{1920, 1080, 60, 1}}) {
		const policy::VideoTuple expected{maximum.width, (1440 * maximum.width / width / 2) * 2, 60, 1};
		const auto bounded = policy::boundCurrentToSupportedTier(current, maximum.width, maximum.height);
		CHECK(policy::sameVideo(bounded, expected));
		const auto tested = policy::benchmarkCeiling(current, maximum.width, maximum.height, 60, 1);
		CHECK(policy::sameVideo(tested, expected));
		CHECK(policy::sameVideo(policy::select(tested, maximum.height == 1440 ? 7500 : 6000, "nvenc", policy::QualityProfile::Twitch).video, expected));
		for (const auto &candidate : policy::candidates(tested)) {
			CHECK(candidate.width <= maximum.width);
			CHECK(candidate.height <= maximum.height);
			CHECK(policy::sameVideo(candidate, expected));
		}
		CHECK(policy::sameVideo(policy::recommendationCeiling(tested, bounded, false), expected));
	}
	const auto preScaled = policy::boundCurrentToSupportedTier({1920, 802, 30, 1}, 2560, 1440);
	CHECK(policy::sameVideo(preScaled, {1920, 802, 30, 1}));
	CHECK(policy::sameVideo(policy::benchmarkCeiling({1920, 802, 30, 1}, 2560, 1440, 60, 1), preScaled));
}

TEST_CASE("Auto Optimizer preserves other custom aspects within caller limits")
{
	const std::vector<policy::VideoTuple> custom = {
		{1600, 1000, 30, 1}, // 16:10
		{1024, 768, 30, 1},  // 4:3
	};
	for (const auto &current : custom) {
		CAPTURE(current.width, current.height);
		const auto bounded = policy::boundCurrentToSupportedTier(current, 640, 360);
		CHECK(bounded.width <= 640);
		CHECK(bounded.height <= 360);
		CHECK(bounded.fpsNum == current.fpsNum);
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

TEST_CASE("Auto Optimizer recommendation promotion requires successful active evidence")
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

TEST_CASE("Auto Optimizer quality policy preserves orientation aspect ratio and even dimensions")
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

TEST_CASE("Auto Optimizer quality selection responds monotonically to bandwidth")
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

TEST_CASE("Auto Optimizer quality policy preserves 50 and 59.94 broadcast-rate families")
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

TEST_CASE("Auto Optimizer quality policy caps high frame rate inputs to 60 or 59.94")
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

TEST_CASE("Auto Optimizer quality policy reports bandwidth below its minimum tier")
{
	const auto result = policy::select({1920, 1080, 60, 1}, 100, "x264");
	CHECK(result.video.width == 960);
	CHECK(result.video.height == 540);
	CHECK(result.video.fpsNum == 30);
	CHECK(result.bitrateKbps == 100);
	CHECK(result.insufficientBandwidth);
}
