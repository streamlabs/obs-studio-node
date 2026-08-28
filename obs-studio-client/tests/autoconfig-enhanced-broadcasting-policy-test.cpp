#include <catch2/catch_test_macros.hpp>

#include "autoconfig-enhanced-broadcasting-policy.hpp"

namespace policy = autoConfig::enhancedBroadcastingPolicy;

TEST_CASE("Enhanced Broadcasting candidates are ordered and capped at 1080p")
{
	const auto result = policy::candidates(1920, 1080, 60, 1);
	REQUIRE(result.size() == 5);
	CHECK(result[0].width == 1920);
	CHECK(result[0].fpsNum == 60);
	CHECK(result[1].width == 1920);
	CHECK(result[1].fpsNum == 30);
	CHECK(result[2].width == 1280);
	CHECK(result[2].fpsNum == 60);
	CHECK(result[4].width == 960);
	CHECK(result[4].fpsNum == 30);
}

TEST_CASE("Enhanced Broadcasting candidates obey request limits")
{
	const auto result = policy::candidates(1280, 720, 30, 1);
	REQUIRE(result.size() == 2);
	CHECK(result[0].width == 1280);
	CHECK(result[0].fpsNum == 30);
	CHECK(result[1].width == 960);
}

TEST_CASE("Enhanced Broadcasting candidates preserve a fractional 60000/1001 cadence family")
{
	const auto result = policy::candidates(1920, 1080, 60000, 1001, 1001);
	REQUIRE(result.size() == 5);
	CHECK(result[0].fpsNum == 60000);
	CHECK(result[0].fpsDen == 1001);
	CHECK(result[1].fpsNum == 30000);
	CHECK(result[1].fpsDen == 1001);
}

TEST_CASE("Enhanced Broadcasting fractional 30000/1001 ceiling retains 30 FPS candidates")
{
	const auto result = policy::candidates(1920, 1080, 30000, 1001, 1001);
	REQUIRE(result.size() == 3);
	CHECK(result[0].width == 1920);
	CHECK(result[0].fpsNum == 30000);
	CHECK(result[0].fpsDen == 1001);
	CHECK(result[2].width == 960);
}

TEST_CASE("Enhanced Broadcasting private-mix evidence never invents a higher cadence")
{
	const policy::VideoCandidate sixty{1920, 1080, 60, 1};
	CHECK_FALSE(policy::cadenceCanBeProvenByPrivateMix(sixty, 30, 1));
	CHECK(policy::cadenceCanBeProvenByPrivateMix(sixty, 60, 1));
	CHECK_FALSE(policy::cadenceCanBeProvenByPrivateMix(sixty, 60000, 1001));
	const policy::VideoCandidate fractionalSixty{1920, 1080, 60000, 1001};
	CHECK(policy::cadenceCanBeProvenByPrivateMix(fractionalSixty, 60000, 1001));
}

TEST_CASE("Enhanced Broadcasting requires a returned rendition that covers the selected tuple")
{
	const policy::VideoCandidate candidate{1920, 1080, 60000, 1001};
	CHECK(policy::renditionCoversCandidate(candidate, 1920, 1080, 60000, 1001));
	CHECK_FALSE(policy::renditionCoversCandidate(candidate, 1280, 720, 60000, 1001));
	CHECK_FALSE(policy::renditionCoversCandidate(candidate, 1920, 1080, 30000, 1001));
	CHECK_FALSE(policy::renditionExceedsCandidate(candidate, 1920, 1080, 60000, 1001));
	CHECK(policy::renditionExceedsCandidate(candidate, 2560, 1440, 60000, 1001));
	CHECK(policy::renditionExceedsCandidate(candidate, 1920, 1080, 60, 1));
}

TEST_CASE("Enhanced Broadcasting workload thresholds include pipeline and mix allowances")
{
	CHECK(policy::minimumEncodedFrames(300) == 252);
	CHECK(policy::allowedSkippedFrames(300) == 15);
	CHECK(policy::allowedSkippedFrames(19) == 0);
}

TEST_CASE("Enhanced Broadcasting descends only after candidate-specific evidence")
{
	CHECK(policy::allowsCandidateDescent("enhanced_broadcasting_ladder_below_candidate"));
	CHECK(policy::allowsCandidateDescent("enhanced_broadcasting_encoder_underload"));
	CHECK(policy::allowsCandidateDescent("enhanced_broadcasting_render_overload"));
	CHECK(policy::allowsCandidateDescent("enhanced_broadcasting_transport_pressure"));
	CHECK_FALSE(policy::allowsCandidateDescent("enhanced_broadcasting_invalid_video_ladder"));
	CHECK_FALSE(policy::allowsCandidateDescent("enhanced_broadcasting_output_start_failed"));
	CHECK_FALSE(policy::allowsCandidateDescent("enhanced_broadcasting_unsafe_stream_key"));
	CHECK_FALSE(policy::allowsCandidateDescent("enhanced_broadcasting_cleanup_timeout"));
}
