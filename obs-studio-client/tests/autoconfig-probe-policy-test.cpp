#include "autoconfig-probe-policy.hpp"

#include <catch2/catch_test_macros.hpp>

namespace policy = autoConfig::probePolicy;

TEST_CASE("Provider probe coverage distinguishes absent, partial, and complete evidence")
{
	CHECK(policy::classifyProviderProbeCoverage(2, 0) == policy::ProviderProbeCoverage::None);
	CHECK(policy::classifyProviderProbeCoverage(2, 1) == policy::ProviderProbeCoverage::Partial);
	CHECK(policy::classifyProviderProbeCoverage(2, 2) == policy::ProviderProbeCoverage::Complete);
	CHECK(policy::classifyProviderProbeCoverage(1, 1) == policy::ProviderProbeCoverage::Complete);
	CHECK_FALSE(policy::providerProbeCoverageAllowsQualityPromotion(true, policy::ProviderProbeCoverage::None));
	CHECK_FALSE(policy::providerProbeCoverageAllowsQualityPromotion(true, policy::ProviderProbeCoverage::Partial));
	CHECK(policy::providerProbeCoverageAllowsQualityPromotion(true, policy::ProviderProbeCoverage::Complete));
	CHECK_FALSE(policy::providerProbeCoverageAllowsQualityPromotion(false, policy::ProviderProbeCoverage::Complete));
	CHECK(policy::probeSafeValueContributesToActiveRecommendation(true, false, 0, 6000));
	CHECK(policy::probeSafeValueContributesToActiveRecommendation(false, true, 1800, 1600));
	CHECK_FALSE(policy::probeSafeValueContributesToActiveRecommendation(false, false, 1800, 1600));
	CHECK_FALSE(policy::probeSafeValueContributesToActiveRecommendation(false, true, 0, 1600));
	CHECK_FALSE(policy::probeSafeValueContributesToActiveRecommendation(true, true, 6000, 0));
}

TEST_CASE("Dual Output requires two conclusive provider measurements")
{
	CHECK(policy::dualOutputProviderProbeIsUsable(true, true, 6000, 6000));
	CHECK_FALSE(policy::dualOutputProviderProbeIsUsable(false, true, 6000, 6000));
	CHECK_FALSE(policy::dualOutputProviderProbeIsUsable(true, false, 6000, 6000));
	CHECK_FALSE(policy::dualOutputProviderProbeIsUsable(true, true, 0, 6000));
	CHECK_FALSE(policy::dualOutputProviderProbeIsUsable(true, true, 6000, 0));
}

TEST_CASE("Final bitrate recommendations round down to whole hundreds")
{
	CHECK(policy::roundDownRecommendationBitrateKbps(0) == 0);
	CHECK(policy::roundDownRecommendationBitrateKbps(99) == 99);
	CHECK(policy::roundDownRecommendationBitrateKbps(100) == 100);
	CHECK(policy::roundDownRecommendationBitrateKbps(199) == 100);
	CHECK(policy::roundDownRecommendationBitrateKbps(5070) == 5000);
	CHECK(policy::roundDownRecommendationBitrateKbps(5679) == 5600);
	CHECK(policy::roundDownRecommendationBitrateKbps(6000) == 6000);
}

TEST_CASE("YouTube probe metrics use deterministic basis-point ratios")
{
	const policy::YoutubeProbeSampleMetrics sample = policy::makeYoutubeProbeSampleMetrics(900, 1000, 3, 150, 10, 2, 100);

	CHECK(sample.throughputBasisPoints == 9000);
	CHECK(sample.dropBasisPoints == 200);
	CHECK(sample.congestionHighBasisPoints == 1000);
	CHECK(sample.congestionSevereBasisPoints == 200);
	CHECK(policy::classifyYoutubeProbeTransport(sample) == policy::YoutubeProbeSampleClass::Clean);
}

TEST_CASE("YouTube recovery drain requires one uninterrupted healthy window")
{
	policy::YoutubeRecoveryGate gate(5);

	for (int index = 0; index < 4; index++)
		CHECK_FALSE(gate.observe(true, true));
	CHECK(gate.observe(true, true));

	CHECK_FALSE(gate.observe(false, true));
	for (int index = 0; index < 4; index++)
		CHECK_FALSE(gate.observe(true, true));
	CHECK(gate.observe(true, true));

	CHECK_FALSE(gate.observe(true, false));
	for (int index = 0; index < 4; index++)
		CHECK_FALSE(gate.observe(true, true));
	CHECK(gate.observe(true, true));
}

TEST_CASE("YouTube probe load classification separates source underfill from transport pressure")
{
	const policy::YoutubeBaselineAssessment cleanBaseline{policy::YoutubeBaselineDecision::Clean, {9500, 100, 500, 100}};

	CHECK(policy::classifyYoutubeProbeLoad({9000, 200, 1000, 200}, cleanBaseline) == policy::YoutubeProbeLoadResult::Accepted);
	CHECK(policy::classifyYoutubeProbeLoad({8999, 200, 1000, 200}, cleanBaseline) == policy::YoutubeProbeLoadResult::SourceUnderfill);
	CHECK(policy::classifyYoutubeProbeLoad({1, 0, 0, 0}, cleanBaseline) == policy::YoutubeProbeLoadResult::SourceUnderfill);
	CHECK(policy::classifyYoutubeProbeLoad({10000, 201, 0, 0}, cleanBaseline) == policy::YoutubeProbeLoadResult::TransportPressure);
	CHECK(policy::classifyYoutubeProbeLoad({10000, 0, 1001, 0}, cleanBaseline) == policy::YoutubeProbeLoadResult::TransportPressure);
	CHECK(policy::classifyYoutubeProbeLoad({10000, 0, 0, 201}, cleanBaseline) == policy::YoutubeProbeLoadResult::TransportPressure);
	CHECK(policy::classifyYoutubeProbeLoad({10000, 501, 0, 0}, cleanBaseline) == policy::YoutubeProbeLoadResult::TransportPressure);
	CHECK(policy::classifyYoutubeProbeLoad({10000, 0, 3001, 0}, cleanBaseline) == policy::YoutubeProbeLoadResult::TransportPressure);
	CHECK(policy::classifyYoutubeProbeLoad({10000, 0, 0, 1001}, cleanBaseline) == policy::YoutubeProbeLoadResult::TransportPressure);
}

TEST_CASE("YouTube probe ignores isolated congestion but rejects sustained congestion")
{
	const policy::YoutubeProbeSampleMetrics isolatedSpike = policy::makeYoutubeProbeSampleMetrics(950, 1000, 0, 150, 1, 0, 100);
	const policy::YoutubeProbeSampleMetrics sustainedCongestion = policy::makeYoutubeProbeSampleMetrics(950, 1000, 0, 150, 31, 0, 100);

	CHECK(policy::classifyYoutubeProbeTransport(isolatedSpike) == policy::YoutubeProbeSampleClass::Clean);
	CHECK(policy::classifyYoutubeProbeTransport(sustainedCongestion) == policy::YoutubeProbeSampleClass::Hard);
}

TEST_CASE("YouTube 89.23 and 87.31 percent source underfill cannot become capacity pressure without transport evidence")
{
	const policy::YoutubeProbeSampleMetrics first{8923, 0, 0, 0};
	const policy::YoutubeProbeSampleMetrics second{8731, 0, 0, 0};
	const policy::YoutubeBaselineAssessment baseline = policy::assessYoutubeBaseline(first, second);

	CHECK(first.throughputBasisPoints < 9000);
	CHECK(second.throughputBasisPoints < 9000);
	CHECK(baseline.decision == policy::YoutubeBaselineDecision::Clean);
	CHECK(policy::classifyYoutubeProbeLoad(first, baseline) == policy::YoutubeProbeLoadResult::SourceUnderfill);
	CHECK(policy::classifyYoutubeProbeLoad(second, baseline) == policy::YoutubeProbeLoadResult::SourceUnderfill);
	CHECK(policy::youtubeSampleAccepted(first, baseline));
	CHECK(policy::youtubeSampleAccepted(second, baseline));
	CHECK_FALSE(policy::youtubeRequiresCapacityConfirmation(policy::classifyYoutubeProbeLoad(first, baseline)));
	CHECK_FALSE(policy::youtubeRequiresCapacityConfirmation(policy::classifyYoutubeProbeLoad(second, baseline)));
}

TEST_CASE("Two YouTube baseline samples select every initial decision")
{
	const policy::YoutubeProbeSampleMetrics cleanA{9500, 100, 500, 100};
	const policy::YoutubeProbeSampleMetrics cleanB{9300, 150, 700, 150};
	const policy::YoutubeProbeSampleMetrics marginalA{8500, 300, 1500, 300};
	const policy::YoutubeProbeSampleMetrics marginalB{8200, 350, 2000, 400};
	const policy::YoutubeProbeSampleMetrics hardA{7000, 501, 1500, 300};
	const policy::YoutubeProbeSampleMetrics hardB{8000, 300, 3001, 300};

	CHECK(policy::assessYoutubeBaseline(cleanA, cleanB).decision == policy::YoutubeBaselineDecision::Clean);
	CHECK(policy::assessYoutubeBaseline(cleanA, {10500, 200, 1000, 200}).decision == policy::YoutubeBaselineDecision::Clean);
	CHECK(policy::assessYoutubeBaseline(marginalA, marginalB).decision == policy::YoutubeBaselineDecision::Impaired);
	CHECK(policy::assessYoutubeBaseline(cleanA, marginalA).decision == policy::YoutubeBaselineDecision::NeedsThird);
	CHECK(policy::assessYoutubeBaseline(hardA, hardB).decision == policy::YoutubeBaselineDecision::Unstable);
}

TEST_CASE("A third YouTube baseline sample resolves from component medians")
{
	const policy::YoutubeProbeSampleMetrics clean{9500, 100, 500, 100};
	const policy::YoutubeProbeSampleMetrics marginal{8500, 300, 1500, 300};
	const policy::YoutubeProbeSampleMetrics hard{6000, 700, 4000, 1500};

	const policy::YoutubeBaselineAssessment impaired = policy::resolveYoutubeBaseline(clean, hard, marginal);
	CHECK(impaired.decision == policy::YoutubeBaselineDecision::Impaired);
	CHECK(impaired.reference.throughputBasisPoints == marginal.throughputBasisPoints);
	CHECK(impaired.reference.dropBasisPoints == marginal.dropBasisPoints);
	CHECK(impaired.reference.congestionHighBasisPoints == marginal.congestionHighBasisPoints);
	CHECK(impaired.reference.congestionSevereBasisPoints == marginal.congestionSevereBasisPoints);

	CHECK(policy::resolveYoutubeBaseline(clean, hard, clean).decision == policy::YoutubeBaselineDecision::Clean);
	CHECK(policy::resolveYoutubeBaseline(hard, clean, hard).decision == policy::YoutubeBaselineDecision::Unstable);
	CHECK(policy::resolveYoutubeBaseline({7000, 100, 500, 100}, clean, {9500, 600, 500, 100}).decision == policy::YoutubeBaselineDecision::Clean);
}

TEST_CASE("Impaired YouTube baseline accepts only bounded relative transport degradation")
{
	const policy::YoutubeBaselineAssessment baseline{policy::YoutubeBaselineDecision::Impaired, {8500, 300, 1500, 300}};

	CHECK(policy::youtubeSampleAccepted({8000, 400, 2500, 800}, baseline));
	CHECK(policy::youtubeSampleAccepted({7999, 400, 2500, 800}, baseline));
	CHECK(policy::youtubeSampleAccepted({1, 400, 2500, 800}, baseline));
	CHECK_FALSE(policy::youtubeSampleAccepted({8000, 401, 2500, 800}, baseline));
	CHECK_FALSE(policy::youtubeSampleAccepted({8000, 400, 2501, 800}, baseline));
	CHECK_FALSE(policy::youtubeSampleAccepted({8000, 400, 2500, 801}, baseline));

	const policy::YoutubeBaselineAssessment cleanBaseline{policy::YoutubeBaselineDecision::Clean, {9500, 100, 500, 100}};
	CHECK(policy::youtubeSampleAccepted({9200, 100, 500, 100}, cleanBaseline));
	CHECK(policy::youtubeSampleAccepted({8500, 100, 500, 100}, cleanBaseline));
	CHECK(policy::classifyYoutubeProbeLoad({8500, 100, 500, 100}, cleanBaseline) == policy::YoutubeProbeLoadResult::SourceUnderfill);
}

TEST_CASE("YouTube low control recovery depends on transport evidence, not source throughput")
{
	const policy::YoutubeBaselineAssessment baseline{policy::YoutubeBaselineDecision::Impaired, {8500, 300, 1500, 300}};
	const policy::YoutubeProbeSampleMetrics original{8800, 300, 1000, 200};

	CHECK(policy::youtubeLowControlRecovered({8300, 400, 2000, 700}, original, baseline));
	CHECK(policy::youtubeLowControlRecovered({8299, 400, 2000, 700}, original, baseline));
	CHECK(policy::youtubeLowControlRecovered({1, 400, 2000, 700}, original, baseline));
	CHECK_FALSE(policy::youtubeLowControlRecovered({8300, 401, 2000, 700}, original, baseline));
	CHECK_FALSE(policy::youtubeLowControlRecovered({8300, 400, 2001, 700}, original, baseline));
	CHECK_FALSE(policy::youtubeLowControlRecovered({8300, 400, 2000, 701}, original, baseline));
}

TEST_CASE("YouTube high-low-high confirmation distinguishes all four outcomes")
{
	CHECK(policy::decideYoutubeConfirmation(true, false) == policy::YoutubeConfirmationDecision::CapacityKnee);
	CHECK(policy::decideYoutubeConfirmation(true, true) == policy::YoutubeConfirmationDecision::TransientRecovered);
	CHECK(policy::decideYoutubeConfirmation(false, false) == policy::YoutubeConfirmationDecision::PathUnstable);
	CHECK(policy::decideYoutubeConfirmation(false, true) == policy::YoutubeConfirmationDecision::Inconsistent);

	CHECK(policy::decideYoutubeConfirmation(true, !policy::youtubeRequiresCapacityConfirmation(policy::YoutubeProbeLoadResult::TransportPressure)) ==
	      policy::YoutubeConfirmationDecision::CapacityKnee);
	CHECK(policy::decideYoutubeConfirmation(true, !policy::youtubeRequiresCapacityConfirmation(policy::YoutubeProbeLoadResult::SourceUnderfill)) ==
	      policy::YoutubeConfirmationDecision::TransientRecovered);
}

TEST_CASE("YouTube transient recovery requires a transport-aware extended window")
{
	CHECK(policy::decideYoutubeExtendedValidation(policy::YoutubeProbeLoadResult::Accepted) == policy::YoutubeExtendedValidationDecision::TargetAccepted);
	CHECK(policy::decideYoutubeExtendedValidation(policy::YoutubeProbeLoadResult::SourceUnderfill) ==
	      policy::YoutubeExtendedValidationDecision::SourceUnderfill);
	CHECK(policy::decideYoutubeExtendedValidation(policy::YoutubeProbeLoadResult::TransportPressure) ==
	      policy::YoutubeExtendedValidationDecision::CapacityKnee);
}

TEST_CASE("YouTube terminal source-underfill state follows the strongest latest evidence")
{
	policy::YoutubeSourceUnderfillState state;
	state.observeTransportClean(policy::YoutubeProbeLoadResult::SourceUnderfill);
	CHECK(state.terminal);

	state.observeTransportClean(policy::YoutubeProbeLoadResult::Accepted);
	CHECK_FALSE(state.terminal);

	state.observeTransportClean(policy::YoutubeProbeLoadResult::SourceUnderfill);
	state.confirmCapacityKnee();
	CHECK_FALSE(state.terminal);
}

TEST_CASE("A clean Twitch sample recommends the validated target without a fixed haircut")
{
	const auto exactTarget = policy::decideTwitchProbe(6013, 6000, 0);
	CHECK(exactTarget.targetPassed);
	CHECK_FALSE(exactTarget.extendSample);
	CHECK(exactTarget.recommendedVideoKbps == 6000);

	const auto healthyUnderfill = policy::decideTwitchProbe(5868, 6000, 0);
	CHECK(healthyUnderfill.targetPassed);
	CHECK_FALSE(healthyUnderfill.extendSample);
	CHECK(healthyUnderfill.recommendedVideoKbps == 6000);

	const auto threshold = policy::decideTwitchProbe(5700, 6000, 0);
	CHECK(threshold.targetPassed);
	CHECK_FALSE(threshold.extendSample);
	CHECK(threshold.recommendedVideoKbps == 6000);
}

TEST_CASE("A materially underfilled clean Twitch sample requests one extended window")
{
	const auto justBelowThreshold = policy::decideTwitchProbe(5699, 6000, 0);
	CHECK_FALSE(justBelowThreshold.targetPassed);
	CHECK(justBelowThreshold.extendSample);
	CHECK(justBelowThreshold.recommendedVideoKbps == 0);

	const auto initial = policy::decideTwitchProbe(5568, 6000, 0);
	CHECK_FALSE(initial.targetPassed);
	CHECK(initial.extendSample);
	CHECK(initial.recommendedVideoKbps == 0);

	const auto extendedClean = policy::decideTwitchProbe(5000, 6000, 0, false, true);
	CHECK(extendedClean.targetPassed);
	CHECK_FALSE(extendedClean.extendSample);
	CHECK(extendedClean.recommendedVideoKbps == 6000);
}

TEST_CASE("Twitch transport pressure uses the raw sustained observation without a reservation")
{
	const auto extendedPressure = policy::decideTwitchProbe(5000, 6000, 0, true, true);
	CHECK_FALSE(extendedPressure.targetPassed);
	CHECK_FALSE(extendedPressure.extendSample);
	CHECK(extendedPressure.recommendedVideoKbps == 5000);

	const auto droppedFrames = policy::decideTwitchProbe(6000, 6000, 1);
	CHECK_FALSE(droppedFrames.targetPassed);
	CHECK_FALSE(droppedFrames.extendSample);
	CHECK(droppedFrames.recommendedVideoKbps == 6000);
}

TEST_CASE("Twitch congestion must be sustained before it becomes transport pressure")
{
	CHECK_FALSE(policy::twitchCongestionIsSustained(10, 2, 100));
	CHECK(policy::twitchCongestionIsSustained(11, 2, 100));
	CHECK(policy::twitchCongestionIsSustained(10, 3, 100));
	CHECK_FALSE(policy::twitchCongestionIsSustained(0, 0, 0));
}

TEST_CASE("A clean YouTube rung recommends its validated video target")
{
	policy::YoutubeRampEvidence evidence;
	evidence.observeAcceptedTarget(5868, 6000);

	CHECK(evidence.passedStep);
	CHECK(evidence.recommendationBasisKbps == 5868);
	CHECK(evidence.recommendedVideoKbps() == 6000);
	CHECK(policy::clampEstimateToObservedSafe(7000, evidence.recommendedVideoKbps(), 10000) == 6000);
}

TEST_CASE("YouTube source-underfilled rungs preserve the highest transport-clean lower bound")
{
	policy::YoutubeRampEvidence evidence;
	evidence.observeTransportCleanLowerBound(1899, 2000);
	evidence.observeTransportCleanLowerBound(1858, 4000);

	CHECK(evidence.passedStep);
	CHECK(evidence.recommendationBasisKbps == 1899);
	CHECK(evidence.recommendedVideoKbps() == 1899);
}

TEST_CASE("YouTube confirmed capacity uses the sustained delivered bitrate without a reservation")
{
	policy::YoutubeRampEvidence evidence;
	evidence.observeAcceptedTarget(4000, 4000);
	evidence.observeConfirmedCapacity(5037, 8000);

	CHECK(evidence.passedStep);
	CHECK(evidence.recommendationBasisKbps == 5037);
	CHECK(evidence.recommendedVideoKbps() == 5037);

	evidence.observeConfirmedCapacity(9000, 8000);
	CHECK(evidence.recommendationBasisKbps == 9000);
	CHECK(evidence.recommendedVideoKbps() == 8000);
}

TEST_CASE("YouTube double-pressure confirmation uses the lower delivered high-target rate")
{
	const uint64_t confirmedCapacity = policy::youtubeConfirmedPressureCapacityKbps(5162, 5070, 8000);
	CHECK(confirmedCapacity == 5070);

	policy::YoutubeRampEvidence evidence;
	evidence.observeConfirmedCapacity(confirmedCapacity, 8000);
	CHECK(evidence.recommendationBasisKbps == 5070);
	CHECK(evidence.recommendedVideoKbps() == 5070);

	CHECK(policy::youtubeConfirmedPressureCapacityKbps(9000, 8500, 8000) == 8000);
	CHECK(policy::youtubeConfirmedPressureCapacityKbps(7800, 7600, 7500) == 7500);
}

TEST_CASE("Successful zero-safe probe metrics remain present in result provenance")
{
	CHECK(policy::hasProbeThroughputMetrics(true, 0));
	CHECK(policy::hasProbeThroughputMetrics(false, 128));
	CHECK_FALSE(policy::hasProbeThroughputMetrics(false, 0));
}

TEST_CASE("YouTube retains a 10000 Kbps stability probe above recommendation caps")
{
	const int stabilityCeiling = policy::effectiveProbeCeilingKbps(10000, 0, 0);
	CHECK(stabilityCeiling == 10000);
	CHECK(policy::reachedEffectiveProbeCeiling(10000, stabilityCeiling));

	const int requestCeiling = policy::effectiveProbeCeilingKbps(10000, 0, 6000);
	CHECK(requestCeiling == 6000);
	CHECK_FALSE(policy::reachedEffectiveProbeCeiling(4000, requestCeiling));
	CHECK(policy::reachedEffectiveProbeCeiling(6000, requestCeiling));

	const int platformCeiling = policy::effectiveProbeCeilingKbps(10000, 4500, 6000);
	CHECK(platformCeiling == 4500);
	CHECK(policy::reachedEffectiveProbeCeiling(4500, platformCeiling));
}

TEST_CASE("Probe substeps advance monotonically within their provider progress slot")
{
	double previous = 30.0;
	for (size_t index = 0; index < 7; index++) {
		const double progress = policy::probeSubstepProgress(30.0, 65.0, index, 7);
		CHECK(progress > previous);
		CHECK(progress < 65.0);
		previous = progress;
	}

	CHECK(policy::probeSubstepProgress(30.0, 47.5, 6, 7) < 47.5);
	CHECK(policy::probeSubstepProgress(47.5, 65.0, 0, 1) > 47.5);
	CHECK(policy::probeSubstepProgress(30.0, 65.0, 0, 0) == 30.0);
}
