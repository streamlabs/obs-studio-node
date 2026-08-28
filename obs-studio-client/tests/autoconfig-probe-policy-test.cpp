#include "autoconfig-probe-policy.hpp"

#include <catch2/catch_test_macros.hpp>

using autoConfig::probePolicy::YoutubeRampEvidence;
using autoConfig::probePolicy::YoutubeBaselineAssessment;
using autoConfig::probePolicy::YoutubeBaselineDecision;
using autoConfig::probePolicy::YoutubeConfirmationDecision;
using autoConfig::probePolicy::YoutubeExtendedValidationDecision;
using autoConfig::probePolicy::YoutubeProbeLoadResult;
using autoConfig::probePolicy::YoutubeProbeSampleClass;
using autoConfig::probePolicy::YoutubeProbeSampleMetrics;
using autoConfig::probePolicy::YoutubeRecoveryGate;
using autoConfig::probePolicy::YoutubeSourceUnderfillState;
using autoConfig::probePolicy::ProviderProbeCoverage;
using autoConfig::probePolicy::assessYoutubeBaseline;
using autoConfig::probePolicy::classifyProviderProbeCoverage;
using autoConfig::probePolicy::providerProbeCoverageAllowsQualityPromotion;
using autoConfig::probePolicy::probeSafeValueContributesToActiveRecommendation;
using autoConfig::probePolicy::classifyYoutubeProbeLoad;
using autoConfig::probePolicy::classifyYoutubeProbeTransport;
using autoConfig::probePolicy::clampEstimateToObservedSafe;
using autoConfig::probePolicy::decideTwitchProbe;
using autoConfig::probePolicy::decideYoutubeConfirmation;
using autoConfig::probePolicy::decideYoutubeExtendedValidation;
using autoConfig::probePolicy::dualOutputProviderProbeIsUsable;
using autoConfig::probePolicy::effectiveProbeCeilingKbps;
using autoConfig::probePolicy::hasProbeThroughputMetrics;
using autoConfig::probePolicy::makeYoutubeProbeSampleMetrics;
using autoConfig::probePolicy::probeSubstepProgress;
using autoConfig::probePolicy::reachedEffectiveProbeCeiling;
using autoConfig::probePolicy::resolveYoutubeBaseline;
using autoConfig::probePolicy::roundDownRecommendationBitrateKbps;
using autoConfig::probePolicy::twitchCongestionIsSustained;
using autoConfig::probePolicy::youtubeLowControlRecovered;
using autoConfig::probePolicy::youtubeConfirmedPressureCapacityKbps;
using autoConfig::probePolicy::youtubeRequiresCapacityConfirmation;
using autoConfig::probePolicy::youtubeSampleAccepted;

TEST_CASE("Provider probe coverage distinguishes absent, partial, and complete evidence")
{
	CHECK(classifyProviderProbeCoverage(2, 0) == ProviderProbeCoverage::None);
	CHECK(classifyProviderProbeCoverage(2, 1) == ProviderProbeCoverage::Partial);
	CHECK(classifyProviderProbeCoverage(2, 2) == ProviderProbeCoverage::Complete);
	CHECK(classifyProviderProbeCoverage(1, 1) == ProviderProbeCoverage::Complete);
	CHECK_FALSE(providerProbeCoverageAllowsQualityPromotion(true, ProviderProbeCoverage::None));
	CHECK_FALSE(providerProbeCoverageAllowsQualityPromotion(true, ProviderProbeCoverage::Partial));
	CHECK(providerProbeCoverageAllowsQualityPromotion(true, ProviderProbeCoverage::Complete));
	CHECK_FALSE(providerProbeCoverageAllowsQualityPromotion(false, ProviderProbeCoverage::Complete));
	CHECK(probeSafeValueContributesToActiveRecommendation(true, false, 0, 6000));
	CHECK(probeSafeValueContributesToActiveRecommendation(false, true, 1800, 1600));
	CHECK_FALSE(probeSafeValueContributesToActiveRecommendation(false, false, 1800, 1600));
	CHECK_FALSE(probeSafeValueContributesToActiveRecommendation(false, true, 0, 1600));
	CHECK_FALSE(probeSafeValueContributesToActiveRecommendation(true, true, 6000, 0));
}

TEST_CASE("Dual Output requires two conclusive provider measurements")
{
	CHECK(dualOutputProviderProbeIsUsable(true, true, 6000, 6000));
	CHECK_FALSE(dualOutputProviderProbeIsUsable(false, true, 6000, 6000));
	CHECK_FALSE(dualOutputProviderProbeIsUsable(true, false, 6000, 6000));
	CHECK_FALSE(dualOutputProviderProbeIsUsable(true, true, 0, 6000));
	CHECK_FALSE(dualOutputProviderProbeIsUsable(true, true, 6000, 0));
}

TEST_CASE("Final bitrate recommendations round down to whole hundreds")
{
	CHECK(roundDownRecommendationBitrateKbps(0) == 0);
	CHECK(roundDownRecommendationBitrateKbps(99) == 99);
	CHECK(roundDownRecommendationBitrateKbps(100) == 100);
	CHECK(roundDownRecommendationBitrateKbps(199) == 100);
	CHECK(roundDownRecommendationBitrateKbps(5070) == 5000);
	CHECK(roundDownRecommendationBitrateKbps(5679) == 5600);
	CHECK(roundDownRecommendationBitrateKbps(6000) == 6000);
}

TEST_CASE("YouTube probe metrics use deterministic basis-point ratios")
{
	const YoutubeProbeSampleMetrics sample = makeYoutubeProbeSampleMetrics(900, 1000, 3, 150, 10, 2, 100);

	CHECK(sample.throughputBasisPoints == 9000);
	CHECK(sample.dropBasisPoints == 200);
	CHECK(sample.congestionHighBasisPoints == 1000);
	CHECK(sample.congestionSevereBasisPoints == 200);
	CHECK(classifyYoutubeProbeTransport(sample) == YoutubeProbeSampleClass::Clean);
}

TEST_CASE("YouTube recovery drain requires one uninterrupted healthy window")
{
	YoutubeRecoveryGate gate(5);

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
	const YoutubeBaselineAssessment cleanBaseline{YoutubeBaselineDecision::Clean, {9500, 100, 500, 100}};

	CHECK(classifyYoutubeProbeLoad({9000, 200, 1000, 200}, cleanBaseline) == YoutubeProbeLoadResult::Accepted);
	CHECK(classifyYoutubeProbeLoad({8999, 200, 1000, 200}, cleanBaseline) == YoutubeProbeLoadResult::SourceUnderfill);
	CHECK(classifyYoutubeProbeLoad({1, 0, 0, 0}, cleanBaseline) == YoutubeProbeLoadResult::SourceUnderfill);
	CHECK(classifyYoutubeProbeLoad({10000, 201, 0, 0}, cleanBaseline) == YoutubeProbeLoadResult::TransportPressure);
	CHECK(classifyYoutubeProbeLoad({10000, 0, 1001, 0}, cleanBaseline) == YoutubeProbeLoadResult::TransportPressure);
	CHECK(classifyYoutubeProbeLoad({10000, 0, 0, 201}, cleanBaseline) == YoutubeProbeLoadResult::TransportPressure);
	CHECK(classifyYoutubeProbeLoad({10000, 501, 0, 0}, cleanBaseline) == YoutubeProbeLoadResult::TransportPressure);
	CHECK(classifyYoutubeProbeLoad({10000, 0, 3001, 0}, cleanBaseline) == YoutubeProbeLoadResult::TransportPressure);
	CHECK(classifyYoutubeProbeLoad({10000, 0, 0, 1001}, cleanBaseline) == YoutubeProbeLoadResult::TransportPressure);
}

TEST_CASE("YouTube probe ignores isolated congestion but rejects sustained congestion")
{
	const YoutubeProbeSampleMetrics isolatedSpike = makeYoutubeProbeSampleMetrics(950, 1000, 0, 150, 1, 0, 100);
	const YoutubeProbeSampleMetrics sustainedCongestion = makeYoutubeProbeSampleMetrics(950, 1000, 0, 150, 31, 0, 100);

	CHECK(classifyYoutubeProbeTransport(isolatedSpike) == YoutubeProbeSampleClass::Clean);
	CHECK(classifyYoutubeProbeTransport(sustainedCongestion) == YoutubeProbeSampleClass::Hard);
}

TEST_CASE("YouTube 89.23 and 87.31 percent source underfill cannot become capacity pressure without transport evidence")
{
	const YoutubeProbeSampleMetrics first{8923, 0, 0, 0};
	const YoutubeProbeSampleMetrics second{8731, 0, 0, 0};
	const YoutubeBaselineAssessment baseline = assessYoutubeBaseline(first, second);

	CHECK(first.throughputBasisPoints < 9000);
	CHECK(second.throughputBasisPoints < 9000);
	CHECK(baseline.decision == YoutubeBaselineDecision::Clean);
	CHECK(classifyYoutubeProbeLoad(first, baseline) == YoutubeProbeLoadResult::SourceUnderfill);
	CHECK(classifyYoutubeProbeLoad(second, baseline) == YoutubeProbeLoadResult::SourceUnderfill);
	CHECK(youtubeSampleAccepted(first, baseline));
	CHECK(youtubeSampleAccepted(second, baseline));
	CHECK_FALSE(youtubeRequiresCapacityConfirmation(classifyYoutubeProbeLoad(first, baseline)));
	CHECK_FALSE(youtubeRequiresCapacityConfirmation(classifyYoutubeProbeLoad(second, baseline)));
}

TEST_CASE("Two YouTube baseline samples select every initial decision")
{
	const YoutubeProbeSampleMetrics cleanA{9500, 100, 500, 100};
	const YoutubeProbeSampleMetrics cleanB{9300, 150, 700, 150};
	const YoutubeProbeSampleMetrics marginalA{8500, 300, 1500, 300};
	const YoutubeProbeSampleMetrics marginalB{8200, 350, 2000, 400};
	const YoutubeProbeSampleMetrics hardA{7000, 501, 1500, 300};
	const YoutubeProbeSampleMetrics hardB{8000, 300, 3001, 300};

	CHECK(assessYoutubeBaseline(cleanA, cleanB).decision == YoutubeBaselineDecision::Clean);
	CHECK(assessYoutubeBaseline(cleanA, {10500, 200, 1000, 200}).decision == YoutubeBaselineDecision::Clean);
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
	CHECK(resolveYoutubeBaseline({7000, 100, 500, 100}, clean, {9500, 600, 500, 100}).decision == YoutubeBaselineDecision::Clean);
}

TEST_CASE("Impaired YouTube baseline accepts only bounded relative transport degradation")
{
	const YoutubeBaselineAssessment baseline{YoutubeBaselineDecision::Impaired, {8500, 300, 1500, 300}};

	CHECK(youtubeSampleAccepted({8000, 400, 2500, 800}, baseline));
	CHECK(youtubeSampleAccepted({7999, 400, 2500, 800}, baseline));
	CHECK(youtubeSampleAccepted({1, 400, 2500, 800}, baseline));
	CHECK_FALSE(youtubeSampleAccepted({8000, 401, 2500, 800}, baseline));
	CHECK_FALSE(youtubeSampleAccepted({8000, 400, 2501, 800}, baseline));
	CHECK_FALSE(youtubeSampleAccepted({8000, 400, 2500, 801}, baseline));

	const YoutubeBaselineAssessment cleanBaseline{YoutubeBaselineDecision::Clean, {9500, 100, 500, 100}};
	CHECK(youtubeSampleAccepted({9200, 100, 500, 100}, cleanBaseline));
	CHECK(youtubeSampleAccepted({8500, 100, 500, 100}, cleanBaseline));
	CHECK(classifyYoutubeProbeLoad({8500, 100, 500, 100}, cleanBaseline) == YoutubeProbeLoadResult::SourceUnderfill);
}

TEST_CASE("YouTube low control recovery depends on transport evidence, not source throughput")
{
	const YoutubeBaselineAssessment baseline{YoutubeBaselineDecision::Impaired, {8500, 300, 1500, 300}};
	const YoutubeProbeSampleMetrics original{8800, 300, 1000, 200};

	CHECK(youtubeLowControlRecovered({8300, 400, 2000, 700}, original, baseline));
	CHECK(youtubeLowControlRecovered({8299, 400, 2000, 700}, original, baseline));
	CHECK(youtubeLowControlRecovered({1, 400, 2000, 700}, original, baseline));
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

	CHECK(decideYoutubeConfirmation(true, !youtubeRequiresCapacityConfirmation(YoutubeProbeLoadResult::TransportPressure)) ==
	      YoutubeConfirmationDecision::CapacityKnee);
	CHECK(decideYoutubeConfirmation(true, !youtubeRequiresCapacityConfirmation(YoutubeProbeLoadResult::SourceUnderfill)) ==
	      YoutubeConfirmationDecision::TransientRecovered);
}

TEST_CASE("YouTube transient recovery requires a transport-aware extended window")
{
	CHECK(decideYoutubeExtendedValidation(YoutubeProbeLoadResult::Accepted) == YoutubeExtendedValidationDecision::TargetAccepted);
	CHECK(decideYoutubeExtendedValidation(YoutubeProbeLoadResult::SourceUnderfill) == YoutubeExtendedValidationDecision::SourceUnderfill);
	CHECK(decideYoutubeExtendedValidation(YoutubeProbeLoadResult::TransportPressure) == YoutubeExtendedValidationDecision::CapacityKnee);
}

TEST_CASE("YouTube terminal source-underfill state follows the strongest latest evidence")
{
	YoutubeSourceUnderfillState state;
	state.observeTransportClean(YoutubeProbeLoadResult::SourceUnderfill);
	CHECK(state.terminal);

	state.observeTransportClean(YoutubeProbeLoadResult::Accepted);
	CHECK_FALSE(state.terminal);

	state.observeTransportClean(YoutubeProbeLoadResult::SourceUnderfill);
	state.confirmCapacityKnee();
	CHECK_FALSE(state.terminal);
}

TEST_CASE("A clean Twitch sample recommends the validated target without a fixed haircut")
{
	const auto exactTarget = decideTwitchProbe(6013, 6000, 0);
	CHECK(exactTarget.targetPassed);
	CHECK_FALSE(exactTarget.extendSample);
	CHECK(exactTarget.recommendedVideoKbps == 6000);

	const auto healthyUnderfill = decideTwitchProbe(5868, 6000, 0);
	CHECK(healthyUnderfill.targetPassed);
	CHECK_FALSE(healthyUnderfill.extendSample);
	CHECK(healthyUnderfill.recommendedVideoKbps == 6000);

	const auto threshold = decideTwitchProbe(5700, 6000, 0);
	CHECK(threshold.targetPassed);
	CHECK_FALSE(threshold.extendSample);
	CHECK(threshold.recommendedVideoKbps == 6000);
}

TEST_CASE("A materially underfilled clean Twitch sample requests one extended window")
{
	const auto justBelowThreshold = decideTwitchProbe(5699, 6000, 0);
	CHECK_FALSE(justBelowThreshold.targetPassed);
	CHECK(justBelowThreshold.extendSample);
	CHECK(justBelowThreshold.recommendedVideoKbps == 0);

	const auto initial = decideTwitchProbe(5568, 6000, 0);
	CHECK_FALSE(initial.targetPassed);
	CHECK(initial.extendSample);
	CHECK(initial.recommendedVideoKbps == 0);

	const auto extendedClean = decideTwitchProbe(5000, 6000, 0, false, true);
	CHECK(extendedClean.targetPassed);
	CHECK_FALSE(extendedClean.extendSample);
	CHECK(extendedClean.recommendedVideoKbps == 6000);
}

TEST_CASE("Twitch transport pressure uses the raw sustained observation without a reservation")
{
	const auto extendedPressure = decideTwitchProbe(5000, 6000, 0, true, true);
	CHECK_FALSE(extendedPressure.targetPassed);
	CHECK_FALSE(extendedPressure.extendSample);
	CHECK(extendedPressure.recommendedVideoKbps == 5000);

	const auto droppedFrames = decideTwitchProbe(6000, 6000, 1);
	CHECK_FALSE(droppedFrames.targetPassed);
	CHECK_FALSE(droppedFrames.extendSample);
	CHECK(droppedFrames.recommendedVideoKbps == 6000);
}

TEST_CASE("Twitch congestion must be sustained before it becomes transport pressure")
{
	CHECK_FALSE(twitchCongestionIsSustained(10, 2, 100));
	CHECK(twitchCongestionIsSustained(11, 2, 100));
	CHECK(twitchCongestionIsSustained(10, 3, 100));
	CHECK_FALSE(twitchCongestionIsSustained(0, 0, 0));
}

TEST_CASE("A clean YouTube rung recommends its validated video target")
{
	YoutubeRampEvidence evidence;
	evidence.observeAcceptedTarget(5868, 6000);

	CHECK(evidence.passedStep);
	CHECK(evidence.recommendationBasisKbps == 5868);
	CHECK(evidence.recommendedVideoKbps() == 6000);
	CHECK(clampEstimateToObservedSafe(7000, evidence.recommendedVideoKbps(), 10000) == 6000);
}

TEST_CASE("YouTube source-underfilled rungs preserve the highest transport-clean lower bound")
{
	YoutubeRampEvidence evidence;
	evidence.observeTransportCleanLowerBound(1899, 2000);
	evidence.observeTransportCleanLowerBound(1858, 4000);

	CHECK(evidence.passedStep);
	CHECK(evidence.recommendationBasisKbps == 1899);
	CHECK(evidence.recommendedVideoKbps() == 1899);
}

TEST_CASE("YouTube confirmed capacity uses the sustained delivered bitrate without a reservation")
{
	YoutubeRampEvidence evidence;
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
	const uint64_t confirmedCapacity = youtubeConfirmedPressureCapacityKbps(5162, 5070, 8000);
	CHECK(confirmedCapacity == 5070);

	YoutubeRampEvidence evidence;
	evidence.observeConfirmedCapacity(confirmedCapacity, 8000);
	CHECK(evidence.recommendationBasisKbps == 5070);
	CHECK(evidence.recommendedVideoKbps() == 5070);

	CHECK(youtubeConfirmedPressureCapacityKbps(9000, 8500, 8000) == 8000);
	CHECK(youtubeConfirmedPressureCapacityKbps(7800, 7600, 7500) == 7500);
}

TEST_CASE("Successful zero-safe probe metrics remain present in result provenance")
{
	CHECK(hasProbeThroughputMetrics(true, 0));
	CHECK(hasProbeThroughputMetrics(false, 128));
	CHECK_FALSE(hasProbeThroughputMetrics(false, 0));
}

TEST_CASE("YouTube retains a 10000 Kbps stability probe above recommendation caps")
{
	const int stabilityCeiling = effectiveProbeCeilingKbps(10000, 0, 0);
	CHECK(stabilityCeiling == 10000);
	CHECK(reachedEffectiveProbeCeiling(10000, stabilityCeiling));

	const int requestCeiling = effectiveProbeCeilingKbps(10000, 0, 6000);
	CHECK(requestCeiling == 6000);
	CHECK_FALSE(reachedEffectiveProbeCeiling(4000, requestCeiling));
	CHECK(reachedEffectiveProbeCeiling(6000, requestCeiling));

	const int platformCeiling = effectiveProbeCeilingKbps(10000, 4500, 6000);
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
