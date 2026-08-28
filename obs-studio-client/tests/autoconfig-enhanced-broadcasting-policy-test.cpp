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

TEST_CASE("Enhanced Broadcasting pairs horizontal candidates with an exact vertical cadence")
{
	const policy::VideoCandidate primary{1920, 1080, 60000, 1001};
	const auto vertical = policy::pairedVerticalCandidate(primary);
	CHECK(vertical.width == 1080);
	CHECK(vertical.height == 1920);
	CHECK(vertical.fpsNum == 60000);
	CHECK(vertical.fpsDen == 1001);
	CHECK(policy::candidateFitsLimits(vertical, 1080, 1920, 60000, 1001));
	CHECK_FALSE(policy::candidateFitsLimits(vertical, 720, 1280, 60000, 1001));
	CHECK_FALSE(policy::candidateFitsLimits(vertical, 1080, 1920, 30000, 1001));
}

TEST_CASE("Enhanced Broadcasting requires coverage for every requested canvas")
{
	CHECK(policy::canvasIndexIsValid(0, 2));
	CHECK(policy::canvasIndexIsValid(1, 2));
	CHECK_FALSE(policy::canvasIndexIsValid(2, 2));
	CHECK(policy::everyCanvasCovered({true, true}));
	CHECK_FALSE(policy::everyCanvasCovered({true, false}));
	CHECK_FALSE(policy::everyCanvasCovered({}));
	CHECK(policy::everyCanvasHasSampledInput({0, 0, 1, 1}, 2));
	CHECK(policy::everyCanvasHasSampledInput({0, 0}, 1));
	CHECK_FALSE(policy::everyCanvasHasSampledInput({0, 0}, 2));
	CHECK_FALSE(policy::everyCanvasHasSampledInput({0, 2}, 2));
	CHECK_FALSE(policy::everyCanvasHasSampledInput({}, 2));
	CHECK_FALSE(policy::everyCanvasHasSampledInput({}, 0));
}

TEST_CASE("Enhanced Broadcasting accepts canvas identity zero and requires distinct live identities")
{
	const auto firstTwoCanvasesExist = [](uint64_t canvasId) { return canvasId == 0 || canvasId == 1; };
	CHECK(policy::canvasReferencesAreValid(0, std::nullopt, firstTwoCanvasesExist));
	CHECK(policy::canvasReferencesAreValid(0, 1, firstTwoCanvasesExist));
	CHECK_FALSE(policy::canvasReferencesAreValid(osn::common::INVALID_ID, std::nullopt, firstTwoCanvasesExist));
	CHECK_FALSE(policy::canvasReferencesAreValid(0, osn::common::INVALID_ID, firstTwoCanvasesExist));
	CHECK_FALSE(policy::canvasReferencesAreValid(0, 0, firstTwoCanvasesExist));
	CHECK_FALSE(policy::canvasReferencesAreValid(0, 2, firstTwoCanvasesExist));
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
