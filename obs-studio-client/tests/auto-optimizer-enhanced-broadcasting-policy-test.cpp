#include <catch2/catch_test_macros.hpp>

#include "auto-optimizer-enhanced-broadcasting-policy.hpp"

namespace policy = autoOptimizer::enhancedBroadcastingPolicy;

TEST_CASE("Enhanced Broadcasting probes preserve discovered H.264 and HEVC independently of resolution")
{
	std::unordered_set<std::string> codecs{"h264", "h265", "av1", "vp9"};
	policy::filterProbeCodecs(codecs);
	CHECK(codecs == std::unordered_set<std::string>{"h264", "h265"});
}

TEST_CASE("Enhanced Broadcasting probes do not add undiscovered codecs")
{
	for (const auto &available : std::vector<std::unordered_set<std::string>>{{"h264"}, {"h265"}, {}}) {
		auto filtered = available;
		policy::filterProbeCodecs(filtered);
		CHECK(filtered == available);
	}
}

TEST_CASE("Enhanced Broadcasting probes exclude AV1 even when it is the only discovered codec")
{
	std::unordered_set<std::string> codecs{"av1"};
	policy::filterProbeCodecs(codecs);
	CHECK(codecs.empty());
}

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
	CHECK(policy::allowsCandidateDescent("enhanced_broadcasting_companion_overload"));
	CHECK_FALSE(policy::allowsCandidateDescent("enhanced_broadcasting_invalid_video_ladder"));
	CHECK_FALSE(policy::allowsCandidateDescent("enhanced_broadcasting_output_start_failed"));
	CHECK_FALSE(policy::allowsCandidateDescent("enhanced_broadcasting_unsafe_stream_key"));
	CHECK_FALSE(policy::allowsCandidateDescent("enhanced_broadcasting_cleanup_timeout"));
}

TEST_CASE("Composite Enhanced Broadcasting maps only local joint-load failures to candidate descent")
{
	CHECK(policy::isCompositeCandidateLoadFailure("enhanced_broadcasting_companion_encoder_create_failed"));
	CHECK(policy::isCompositeCandidateLoadFailure("enhanced_broadcasting_companion_output_start_failed"));
	CHECK(policy::isCompositeCandidateLoadFailure("enhanced_broadcasting_companion_output_stopped"));
	CHECK(policy::isCompositeCandidateLoadFailure("enhanced_broadcasting_output_start_failed"));
	CHECK_FALSE(policy::isCompositeCandidateLoadFailure("enhanced_broadcasting_companion_encoder_settings_mismatch"));
	CHECK_FALSE(policy::isCompositeCandidateLoadFailure("enhanced_broadcasting_output_connect_failed"));
	CHECK_FALSE(policy::isCompositeCandidateLoadFailure("enhanced_broadcasting_transport_pressure"));
}

TEST_CASE("Enhanced Broadcasting fallback reports retain earlier upload instability")
{
	policy::CandidateFallbackEvidence evidence;
	CHECK(evidence.reason().empty());
	evidence.record("enhanced_broadcasting_ladder_below_candidate");
	CHECK(evidence.reason() == "enhanced_broadcasting_configuration_fallback");
	CHECK_FALSE(evidence.workloadPressure);
	CHECK_FALSE(evidence.transportPressure);
	evidence.record("enhanced_broadcasting_transport_pressure");
	CHECK(evidence.reason() == "enhanced_broadcasting_transport_fallback");
	evidence.record("enhanced_broadcasting_ladder_below_candidate");
	evidence.record("");
	CHECK(evidence.reason() == "enhanced_broadcasting_transport_fallback");
	CHECK(policy::CandidateFallbackEvidence{}.reason().empty());
}

TEST_CASE("Enhanced Broadcasting configuration fallback does not imply measured overload")
{
	for (const auto error : {"enhanced_broadcasting_ladder_below_candidate", "enhanced_broadcasting_config_request_failed"}) {
		policy::CandidateFallbackEvidence evidence;
		evidence.record(error);
		CHECK(evidence.reason() == "enhanced_broadcasting_configuration_fallback");
		CHECK_FALSE(evidence.workloadPressure);
		CHECK_FALSE(evidence.transportPressure);
		evidence.record("enhanced_broadcasting_render_overload");
		CHECK(evidence.reason() == "enhanced_broadcasting_workload_fallback");
	}
}

TEST_CASE("Enhanced Broadcasting fallback reports distinguish workload and upload failures")
{
	for (const auto error :
	     {"enhanced_broadcasting_encoder_underload", "enhanced_broadcasting_render_overload", "enhanced_broadcasting_companion_overload"}) {
		CAPTURE(error);
		policy::CandidateFallbackEvidence evidence;
		evidence.record(error);
		CHECK(evidence.reason() == "enhanced_broadcasting_workload_fallback");
		evidence.record("enhanced_broadcasting_transport_pressure");
		CHECK(evidence.reason() == "enhanced_broadcasting_transport_and_workload_fallback");

		policy::CandidateFallbackEvidence reversed;
		reversed.record("enhanced_broadcasting_transport_pressure");
		reversed.record(error);
		CHECK(reversed.reason() == evidence.reason());
	}
}
TEST_CASE("Enhanced Broadcasting 1440p pairs with a 1080p vertical workload")
{
	const auto candidates = autoOptimizer::enhancedBroadcastingPolicy::candidates(2560, 1440, 60, 1);
	REQUIRE(candidates.size() == 7);
	CHECK(candidates.front().width == 2560);
	CHECK(candidates.front().height == 1440);
	const auto vertical = autoOptimizer::enhancedBroadcastingPolicy::pairedVerticalCandidate(candidates.front());
	CHECK(vertical.width == 1080);
	CHECK(vertical.height == 1920);
	CHECK(vertical.fpsNum == 60);
	CHECK(autoOptimizer::enhancedBroadcastingPolicy::candidates(1920, 1080, 60, 1).size() == 5);
}
