#include "autoconfig-probe-policy.hpp"

#include <catch2/catch_test_macros.hpp>

using autoConfig::probePolicy::YoutubeRampEvidence;
using autoConfig::probePolicy::clampEstimateToObservedSafe;
using autoConfig::probePolicy::effectiveProbeCeilingKbps;
using autoConfig::probePolicy::hasProbeThroughputMetrics;
using autoConfig::probePolicy::probeSubstepProgress;
using autoConfig::probePolicy::reachedEffectiveProbeCeiling;
using autoConfig::probePolicy::safeVideoKbps;

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
