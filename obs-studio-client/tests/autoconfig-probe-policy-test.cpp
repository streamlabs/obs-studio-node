#include "autoconfig-probe-policy.hpp"

#include <catch2/catch_test_macros.hpp>

using autoConfig::probePolicy::YoutubeRampEvidence;
using autoConfig::probePolicy::YoutubeBaselineAssessment;
using autoConfig::probePolicy::YoutubeBaselineDecision;
using autoConfig::probePolicy::YoutubeConfirmationDecision;
using autoConfig::probePolicy::YoutubeProbeSampleClass;
using autoConfig::probePolicy::YoutubeProbeSampleMetrics;
using autoConfig::probePolicy::assessYoutubeBaseline;
using autoConfig::probePolicy::classifyYoutubeProbeSample;
using autoConfig::probePolicy::clampEstimateToObservedSafe;
using autoConfig::probePolicy::decideYoutubeConfirmation;
using autoConfig::probePolicy::effectiveProbeCeilingKbps;
using autoConfig::probePolicy::hasProbeThroughputMetrics;
using autoConfig::probePolicy::makeYoutubeProbeSampleMetrics;
using autoConfig::probePolicy::probeSubstepProgress;
using autoConfig::probePolicy::reachedEffectiveProbeCeiling;
using autoConfig::probePolicy::resolveYoutubeBaseline;
using autoConfig::probePolicy::safeVideoKbps;
using autoConfig::probePolicy::youtubeLowControlRecovered;
using autoConfig::probePolicy::youtubeSampleAccepted;

TEST_CASE("YouTube probe metrics use deterministic basis-point ratios")
{
	const YoutubeProbeSampleMetrics sample = makeYoutubeProbeSampleMetrics(900, 1000, 3, 150, 10, 2, 100);

	CHECK(sample.throughputBasisPoints == 9000);
	CHECK(sample.dropBasisPoints == 200);
	CHECK(sample.congestionHighBasisPoints == 1000);
	CHECK(sample.congestionSevereBasisPoints == 200);
	CHECK(classifyYoutubeProbeSample(sample) == YoutubeProbeSampleClass::Clean);
}

TEST_CASE("YouTube probe sample classification honors clean and hard boundaries")
{
	CHECK(classifyYoutubeProbeSample({9000, 200, 1000, 200}) == YoutubeProbeSampleClass::Clean);
	CHECK(classifyYoutubeProbeSample({8999, 200, 1000, 200}) == YoutubeProbeSampleClass::Marginal);
	CHECK(classifyYoutubeProbeSample({7500, 500, 3000, 1000}) == YoutubeProbeSampleClass::Marginal);
	CHECK(classifyYoutubeProbeSample({7499, 0, 0, 0}) == YoutubeProbeSampleClass::Hard);
	CHECK(classifyYoutubeProbeSample({10000, 501, 0, 0}) == YoutubeProbeSampleClass::Hard);
	CHECK(classifyYoutubeProbeSample({10000, 0, 3001, 0}) == YoutubeProbeSampleClass::Hard);
	CHECK(classifyYoutubeProbeSample({10000, 0, 0, 1001}) == YoutubeProbeSampleClass::Hard);
}

TEST_CASE("YouTube probe ignores isolated congestion but rejects sustained congestion")
{
	const YoutubeProbeSampleMetrics isolatedSpike = makeYoutubeProbeSampleMetrics(950, 1000, 0, 150, 1, 0, 100);
	const YoutubeProbeSampleMetrics sustainedCongestion = makeYoutubeProbeSampleMetrics(950, 1000, 0, 150, 31, 0, 100);

	CHECK(classifyYoutubeProbeSample(isolatedSpike) == YoutubeProbeSampleClass::Clean);
	CHECK(classifyYoutubeProbeSample(sustainedCongestion) == YoutubeProbeSampleClass::Hard);
}

TEST_CASE("Two YouTube baseline samples select every initial decision")
{
	const YoutubeProbeSampleMetrics cleanA{9500, 100, 500, 100};
	const YoutubeProbeSampleMetrics cleanB{9300, 150, 700, 150};
	const YoutubeProbeSampleMetrics marginalA{8500, 300, 1500, 300};
	const YoutubeProbeSampleMetrics marginalB{8200, 350, 2000, 400};
	const YoutubeProbeSampleMetrics hardA{7000, 300, 1500, 300};
	const YoutubeProbeSampleMetrics hardB{8000, 600, 1500, 300};

	CHECK(assessYoutubeBaseline(cleanA, cleanB).decision == YoutubeBaselineDecision::Clean);
	CHECK(assessYoutubeBaseline(cleanA, {10500, 200, 1000, 200}).decision == YoutubeBaselineDecision::NeedsThird);
	CHECK(assessYoutubeBaseline(marginalA, marginalB).decision == YoutubeBaselineDecision::Impaired);
	CHECK(assessYoutubeBaseline(cleanA, marginalA).decision == YoutubeBaselineDecision::NeedsThird);
	CHECK(assessYoutubeBaseline(hardA, hardB).decision == YoutubeBaselineDecision::Unstable);
}

TEST_CASE("A third YouTube baseline sample resolves from component medians")
{
	const YoutubeProbeSampleMetrics clean{9500, 100, 500, 100};
	const YoutubeProbeSampleMetrics marginal{8500, 300, 1500, 300};
	const YoutubeProbeSampleMetrics hard{6000, 700, 4000, 1500};

	const YoutubeBaselineAssessment impaired = resolveYoutubeBaseline(clean, hard, marginal);
	CHECK(impaired.decision == YoutubeBaselineDecision::Impaired);
	CHECK(impaired.reference.throughputBasisPoints == marginal.throughputBasisPoints);
	CHECK(impaired.reference.dropBasisPoints == marginal.dropBasisPoints);
	CHECK(impaired.reference.congestionHighBasisPoints == marginal.congestionHighBasisPoints);
	CHECK(impaired.reference.congestionSevereBasisPoints == marginal.congestionSevereBasisPoints);

	CHECK(resolveYoutubeBaseline(clean, hard, clean).decision == YoutubeBaselineDecision::Clean);
	CHECK(resolveYoutubeBaseline(hard, clean, hard).decision == YoutubeBaselineDecision::Unstable);
	CHECK(resolveYoutubeBaseline({7000, 100, 500, 100}, clean, {9500, 600, 500, 100}).decision == YoutubeBaselineDecision::Unstable);
}

TEST_CASE("Impaired YouTube baseline accepts only bounded relative degradation")
{
	const YoutubeBaselineAssessment baseline{YoutubeBaselineDecision::Impaired, {8500, 300, 1500, 300}};

	CHECK(youtubeSampleAccepted({8000, 400, 2500, 800}, baseline));
	CHECK_FALSE(youtubeSampleAccepted({7999, 400, 2500, 800}, baseline));
	CHECK_FALSE(youtubeSampleAccepted({8000, 401, 2500, 800}, baseline));
	CHECK_FALSE(youtubeSampleAccepted({8000, 400, 2501, 800}, baseline));
	CHECK_FALSE(youtubeSampleAccepted({8000, 400, 2500, 801}, baseline));

	const YoutubeBaselineAssessment cleanBaseline{YoutubeBaselineDecision::Clean, {9500, 100, 500, 100}};
	CHECK(youtubeSampleAccepted({9200, 100, 500, 100}, cleanBaseline));
	CHECK_FALSE(youtubeSampleAccepted({8500, 100, 500, 100}, cleanBaseline));
}

TEST_CASE("YouTube low control must recover acceptance and the previous rung")
{
	const YoutubeBaselineAssessment baseline{YoutubeBaselineDecision::Impaired, {8500, 300, 1500, 300}};
	const YoutubeProbeSampleMetrics original{8800, 300, 1000, 200};

	CHECK(youtubeLowControlRecovered({8300, 400, 2000, 700}, original, baseline));
	CHECK_FALSE(youtubeLowControlRecovered({8299, 400, 2000, 700}, original, baseline));
	CHECK_FALSE(youtubeLowControlRecovered({8300, 401, 2000, 700}, original, baseline));
	CHECK_FALSE(youtubeLowControlRecovered({8300, 400, 2001, 700}, original, baseline));
	CHECK_FALSE(youtubeLowControlRecovered({8300, 400, 2000, 701}, original, baseline));
}

TEST_CASE("YouTube high-low-high confirmation distinguishes all four outcomes")
{
	CHECK(decideYoutubeConfirmation(true, false) == YoutubeConfirmationDecision::CapacityKnee);
	CHECK(decideYoutubeConfirmation(true, true) == YoutubeConfirmationDecision::TransientRecovered);
	CHECK(decideYoutubeConfirmation(false, false) == YoutubeConfirmationDecision::PathUnstable);
	CHECK(decideYoutubeConfirmation(false, true) == YoutubeConfirmationDecision::Inconsistent);
}

TEST_CASE("YouTube first-rung failure retains a conservative observed cap")
{
	YoutubeRampEvidence evidence;
	evidence.observe(750, false, 1128);

	CHECK_FALSE(evidence.passedStep);
	CHECK(evidence.recommendationBasisKbps == 750);
	CHECK(evidence.safeVideoKbps(80, 128) == 472);
	CHECK(clampEstimateToObservedSafe(6000, evidence.safeVideoKbps(80, 128), 12000) == 472);
}

TEST_CASE("YouTube failed first rung cannot recommend above the attempted rate after a burst")
{
	YoutubeRampEvidence evidence;
	evidence.observe(2000, false, 1128);

	CHECK_FALSE(evidence.passedStep);
	CHECK(evidence.recommendationBasisKbps == 2000);
	CHECK(evidence.failedUpperBoundKbps == 1128);
	CHECK(evidence.safeVideoKbps(80, 128) == 774);
	CHECK(clampEstimateToObservedSafe(6000, evidence.safeVideoKbps(80, 128), 12000) == 774);
}

TEST_CASE("YouTube failed higher rung cannot raise the last passing recommendation")
{
	YoutubeRampEvidence evidence;
	evidence.observe(2100, true, 2128);
	evidence.observe(1900, false, 4128);

	CHECK(evidence.passedStep);
	CHECK(evidence.recommendationBasisKbps == 2100);
	CHECK(evidence.safeVideoKbps(80, 128) == 1392);
	CHECK(clampEstimateToObservedSafe(1400, evidence.safeVideoKbps(80, 128), 12000) == 1392);
}

TEST_CASE("Aggregate probe throughput reserves audio before recommending video bitrate")
{
	CHECK(safeVideoKbps(6000, 70, 32) == 4168);
	CHECK(safeVideoKbps(128, 80, 128) == 0);
}

TEST_CASE("Successful zero-safe probe metrics remain present in result provenance")
{
	CHECK(hasProbeThroughputMetrics(true, 0));
	CHECK(hasProbeThroughputMetrics(false, 128));
	CHECK_FALSE(hasProbeThroughputMetrics(false, 0));
}

TEST_CASE("YouTube reports the exact effective probe cap as ceiling reached")
{
	const int requestCeiling = effectiveProbeCeilingKbps(12000, 0, 6000);
	CHECK(requestCeiling == 6000);
	CHECK_FALSE(reachedEffectiveProbeCeiling(4000, requestCeiling));
	CHECK(reachedEffectiveProbeCeiling(6000, requestCeiling));

	const int platformCeiling = effectiveProbeCeilingKbps(12000, 4500, 6000);
	CHECK(platformCeiling == 4500);
	CHECK(reachedEffectiveProbeCeiling(4500, platformCeiling));
}

TEST_CASE("Probe substeps advance monotonically within their provider progress slot")
{
	double previous = 30.0;
	for (size_t index = 0; index < 7; index++) {
		const double progress = probeSubstepProgress(30.0, 65.0, index, 7);
		CHECK(progress > previous);
		CHECK(progress < 65.0);
		previous = progress;
	}

	CHECK(probeSubstepProgress(30.0, 47.5, 6, 7) < 47.5);
	CHECK(probeSubstepProgress(47.5, 65.0, 0, 1) > 47.5);
	CHECK(probeSubstepProgress(30.0, 65.0, 0, 0) == 30.0);
}
