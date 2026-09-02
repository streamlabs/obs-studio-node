#include "autoconfig-client-contract.hpp"

#include "nlohmann/json.hpp"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <optional>
#include <vector>

namespace contract = autoConfig::clientContract;
using json = nlohmann::json;

namespace {
json current(int width, int height, int bitrateKbps, uint64_t canvasId = 1)
{
	return {{"canvasId", canvasId},
		{"width", width},
		{"height", height},
		{"fpsNum", 60},
		{"fpsDen", 1},
		{"bitrateKbps", bitrateKbps},
		{"encoderId", "obs_nvenc_h264_tex"},
		{"preset", "p5"}};
}

json standardOutput(std::string id, std::string display, std::string platform, json probe = nullptr, uint64_t canvasId = 1)
{
	json output = {{"outputId", std::move(id)},
		       {"display", std::move(display)},
		       {"outputKind", "standard"},
		       {"destinations", json::array({platform})},
		       {"current", current(1280, 720, 3000, canvasId)},
		       {"limits", {{"maxBitrateKbps", 8000}, {"maxWidth", 1920}, {"maxHeight", 1080}, {"maxFpsNum", 60}, {"maxFpsDen", 1}}}};
	if (!probe.is_null())
		output["probes"] = json::array({std::move(probe)});
	return output;
}

json twitchProbe(std::string id = "twitch-probe")
{
	return {{"id", std::move(id)}, {"kind", "twitch-standard"}, {"server", "rtmp://live.twitch.tv/app"}, {"streamKey", "live_secret"}};
}

json youtubeProbe(std::string id = "youtube-probe")
{
	return {{"id", std::move(id)}, {"kind", "youtube-unbound"}, {"server", "rtmps://a.rtmps.youtube.com/live2"}, {"streamKey", "youtube-secret"}};
}

json recommendation(int width, int height, int bitrateKbps)
{
	return {{"width", width},
		{"height", height},
		{"fpsNum", 60},
		{"fpsDen", 1},
		{"bitrateKbps", bitrateKbps},
		{"encoderId", "obs_nvenc_h264_tex"},
		{"encoderFamily", "obs_nvenc_h264_tex"},
		{"encoderTitle", "NVIDIA NVENC H.264"},
		{"codec", "h264"},
		{"preset", "p5"}};
}

json returnedStandardOutput(std::string id, std::string display, std::string platform, std::string provider, std::string method, int safeKbps, int bitrateKbps)
{
	return {{"legId", std::move(id)},
		{"display", std::move(display)},
		{"outputKind", "standard"},
		{"destinations", json::array({{{"platform", std::move(platform)}}})},
		{"measurement",
		 {{"mode", "active"},
		  {"confidence", "high"},
		  {"probes", json::array({{{"provider", std::move(provider)},
					   {"method", std::move(method)},
					   {"success", true},
					   {"measuredKbps", safeKbps},
					   {"safeKbps", safeKbps},
					   {"ceilingReached", true}}})}}},
		{"recommendation", recommendation(1280, 720, bitrateKbps)},
		{"limits", {{"maxBitrateKbps", 8000}, {"maxWidth", 1920}, {"maxHeight", 1080}, {"maxFpsNum", 60}, {"maxFpsDen", 1}}}};
}

contract::PreparedRequest prepare(const json &request)
{
	const auto prepared = contract::prepareRequest(request.dump());
	REQUIRE(prepared.valid);
	return prepared;
}

struct ResultFixture {
	contract::PreparedRequest prepared;
	json result;
};

struct InvalidResultMutation {
	const char *name;
	std::function<void(json &)> apply;
};

void checkInvalidResultMutations(const ResultFixture &fixture, const std::vector<InvalidResultMutation> &mutations)
{
	for (const auto &mutation : mutations) {
		DYNAMIC_SECTION(mutation.name)
		{
			json candidate = fixture.result;
			mutation.apply(candidate);
			CHECK_FALSE(contract::projectResult(candidate.dump(), "run", fixture.prepared.context).valid);
		}
	}
}

ResultFixture dualOutputFixture()
{
	const auto prepared = prepare({{"streamSetup", "dual-output"},
				       {"outputs", json::array({standardOutput("horizontal", "horizontal", "twitch", twitchProbe(), 1),
								standardOutput("vertical", "vertical", "youtube", youtubeProbe(), 2)})}});
	json vertical = returnedStandardOutput("vertical", "vertical", "youtube", "youtube", "youtube-unbound-ramp", 10000, 5000);
	vertical["recommendation"]["width"] = 1280;
	vertical["recommendation"]["height"] = 720;
	return {prepared,
		{{"schemaVersion", 1},
		 {"sessionId", "run"},
		 {"status", "complete"},
		 {"aggregateUpload",
		  {{"method", "dual-output-isolated-lower-bound"},
		   {"safeVideoKbps", 10000},
		   {"allocatedVideoKbps", 10000},
		   {"concurrentHardwareValidated", true}}},
		 {"legs",
		  json::array({returnedStandardOutput("horizontal", "horizontal", "twitch", "twitch", "twitch-bandwidth-test", 6000, 5000), vertical})}}};
}

json enhancedBroadcastingOutput()
{
	return {{"outputId", "enhanced"},
		{"display", "both"},
		{"outputKind", "twitch-enhanced-broadcasting"},
		{"destinations", json::array({"twitch"})},
		{"current", current(1920, 1080, 6000, 1)},
		{"additionalVideo", {{"display", "vertical"}, {"current", current(1080, 1920, 6000, 2)}}},
		{"probes", json::array({{{"id", "enhanced-probe"}, {"kind", "twitch-enhanced-broadcasting"}, {"streamKey", "live?bandwidthtest=true"}}})}};
}

json returnedEnhancedBroadcastingOutput()
{
	json recommendationValue = recommendation(1920, 1080, 6000);
	recommendationValue["additionalVideo"] = {{"display", "vertical"}, {"width", 1080}, {"height", 1920}, {"fpsNum", 60}, {"fpsDen", 1}};
	return {{"legId", "enhanced"},
		{"display", "both"},
		{"outputKind", "twitch-enhanced-broadcasting"},
		{"destinations", json::array({{{"platform", "twitch"}}})},
		{"measurement",
		 {{"mode", "active"},
		  {"confidence", "high"},
		  {"probes",
		   json::array({{{"provider", "twitch"},
				 {"method", "twitch-enhanced-broadcasting-test"},
				 {"success", true},
				 {"ceilingReached", true},
				 {"testedWidth", 1920},
				 {"testedHeight", 1080},
				 {"testedFpsNum", 60},
				 {"testedFpsDen", 1},
				 {"testedAdditionalVideo", {{"display", "vertical"}, {"width", 1080}, {"height", 1920}, {"fpsNum", 60}, {"fpsDen", 1}}}}})}}},
		{"recommendation", std::move(recommendationValue)}};
}

ResultFixture enhancedBroadcastingFixture(bool includeVerticalCompanion = false)
{
	json horizontal = standardOutput("horizontal", "horizontal", "youtube", youtubeProbe("youtube-horizontal"), 1);
	horizontal["current"] = current(1920, 1080, 4500, 1);
	json outputs = json::array({enhancedBroadcastingOutput(), horizontal});
	if (includeVerticalCompanion) {
		json vertical = standardOutput("vertical", "vertical", "youtube", youtubeProbe("youtube-vertical"), 2);
		vertical["current"] = current(1080, 1920, 4500, 2);
		vertical["limits"]["maxWidth"] = 1080;
		vertical["limits"]["maxHeight"] = 1920;
		outputs.push_back(std::move(vertical));
	}
	const auto prepared = prepare({{"streamSetup", "enhanced-broadcasting-dual-output"}, {"outputs", std::move(outputs)}});

	json horizontalResult = returnedStandardOutput("horizontal", "horizontal", "youtube", "youtube", "youtube-unbound-ramp", 6000, 4500);
	horizontalResult["recommendation"]["width"] = 1920;
	horizontalResult["recommendation"]["height"] = 1080;
	json legs = json::array({returnedEnhancedBroadcastingOutput(), horizontalResult});
	json companionProofs = json::array({{{"legId", "horizontal"},
					     {"display", "horizontal"},
					     {"width", 1920},
					     {"height", 1080},
					     {"fpsNum", 60},
					     {"fpsDen", 1},
					     {"bitrateKbps", 4500},
					     {"encoderId", "obs_nvenc_h264_tex"},
					     {"preset", "p5"}}});
	if (includeVerticalCompanion) {
		json verticalResult = returnedStandardOutput("vertical", "vertical", "youtube", "youtube", "youtube-unbound-ramp", 6000, 4500);
		verticalResult["recommendation"]["width"] = 1080;
		verticalResult["recommendation"]["height"] = 1920;
		verticalResult["limits"]["maxWidth"] = 1080;
		verticalResult["limits"]["maxHeight"] = 1920;
		legs.push_back(std::move(verticalResult));
		companionProofs.push_back({{"legId", "vertical"},
					   {"display", "vertical"},
					   {"width", 1080},
					   {"height", 1920},
					   {"fpsNum", 60},
					   {"fpsDen", 1},
					   {"bitrateKbps", 4500},
					   {"encoderId", "obs_nvenc_h264_tex"},
					   {"preset", "p5"}});
	}

	return {prepared,
		{{"schemaVersion", 1},
		 {"sessionId", "run"},
		 {"status", "complete"},
		 {"combinedWorkload",
		  {{"method", "enhanced-broadcasting-dual-output-concurrent"},
		   {"enhancedBroadcastingLegId", "enhanced"},
		   {"validated", true},
		   {"companionLegs", std::move(companionProofs)}}},
		 {"legs", std::move(legs)}}};
}
} // namespace

TEST_CASE("AutoConfig client projects the public request to the private session schema")
{
	json request = {{"streamSetup", "direct-single"}, {"outputs", json::array({standardOutput("horizontal", "horizontal", "twitch", twitchProbe())})}};
	request["schemaVersion"] = 999;
	request["outputs"][0]["ignored"] = "not-forwarded";
	request["outputs"][0]["probes"][0]["outputId"] = "not-forwarded";

	const auto prepared = prepare(request);
	const json wire = json::parse(prepared.wireJson);
	CHECK(wire["schemaVersion"] == 1);
	CHECK(wire["topology"] == "direct-single");
	CHECK(wire["legs"][0]["legId"] == "horizontal");
	CHECK(wire["legs"][0]["destinations"] == json::array({{{"platform", "twitch"}}}));
	CHECK_FALSE(wire["legs"][0].contains("ignored"));
	CHECK(wire["activeProbes"][0]["probeId"] == "twitch-probe");
	CHECK(wire["activeProbes"][0]["legId"] == "horizontal");
	CHECK_FALSE(wire["activeProbes"][0].contains("outputId"));

	REQUIRE(prepared.context.outputs.size() == 1);
	REQUIRE(prepared.context.outputs[0].probes.size() == 1);
	CHECK(prepared.context.outputs[0].probes[0].id == "twitch-probe");
	CHECK(prepared.context.outputs[0].probes[0].kind == "twitch-standard");
	CHECK(prepared.context.outputs[0].probes[0].platform == "twitch");
}

TEST_CASE("AutoConfig client flattens nested probes in output and probe order")
{
	const json request = {{"streamSetup", "dual-output"},
			      {"outputs", json::array({standardOutput("horizontal", "horizontal", "twitch", twitchProbe("first"), 1),
						       standardOutput("vertical", "vertical", "youtube", youtubeProbe("second"), 2)})}};
	const auto prepared = prepare(request);
	const json wire = json::parse(prepared.wireJson);
	REQUIRE(wire["activeProbes"].size() == 2);
	CHECK(wire["activeProbes"][0]["probeId"] == "first");
	CHECK(wire["activeProbes"][0]["legId"] == "horizontal");
	CHECK(wire["activeProbes"][1]["probeId"] == "second");
	CHECK(wire["activeProbes"][1]["legId"] == "vertical");
}

TEST_CASE("AutoConfig client rejects duplicate public output and probe identities")
{
	json duplicateOutputs = {{"streamSetup", "dual-output"},
				 {"outputs", json::array({standardOutput("same", "horizontal", "twitch"), standardOutput("same", "vertical", "youtube")})}};
	CHECK_FALSE(contract::prepareRequest(duplicateOutputs.dump()).valid);

	json duplicateProbes = {{"streamSetup", "dual-output"},
				{"outputs", json::array({standardOutput("horizontal", "horizontal", "twitch", twitchProbe("same"), 1),
							 standardOutput("vertical", "vertical", "youtube", youtubeProbe("same"), 2)})}};
	CHECK_FALSE(contract::prepareRequest(duplicateProbes.dump()).valid);

	json duplicateDestinations = {{"streamSetup", "direct-single"},
				      {"outputs", json::array({standardOutput("horizontal", "horizontal", "twitch", twitchProbe())})}};
	duplicateDestinations["outputs"][0]["destinations"].push_back("twitch");
	CHECK_FALSE(contract::prepareRequest(duplicateDestinations.dump()).valid);
}

TEST_CASE("AutoConfig client projects ordered progress without private envelope fields")
{
	const auto prepared =
		prepare({{"streamSetup", "direct-single"}, {"outputs", json::array({standardOutput("horizontal", "horizontal", "twitch", twitchProbe())})}});
	const json raw = {{"schemaVersion", 1},
			  {"sessionId", "run"},
			  {"sequence", 8},
			  {"type", "progress"},
			  {"phase", "bandwidth"},
			  {"progress", 50},
			  {"code", "twitch_probe_started"},
			  {"legId", "horizontal"},
			  {"measurementMode", "active"},
			  {"probeId", "twitch-probe"},
			  {"provider", "twitch"},
			  {"targetBitrateKbps", 4500}};
	const auto event = contract::projectEvent(raw.dump(), "run", 7, prepared.context);
	REQUIRE(event.valid);
	CHECK(event.sequence == 8);
	const json projected = json::parse(event.json);
	CHECK(projected["outputId"] == "horizontal");
	CHECK(projected["probe"] == json{{"id", "twitch-probe"}, {"kind", "twitch-standard"}});
	CHECK_FALSE(projected.contains("schemaVersion"));
	CHECK_FALSE(projected.contains("sessionId"));
	CHECK_FALSE(projected.contains("sequence"));
	CHECK_FALSE(projected.contains("legId"));
	CHECK_FALSE(projected.contains("provider"));
	CHECK_FALSE(projected.contains("probeId"));

	json wrongProvider = raw;
	wrongProvider["provider"] = "youtube";
	CHECK_FALSE(contract::projectEvent(wrongProvider.dump(), "run", 7, prepared.context).valid);
	CHECK_FALSE(contract::projectEvent(raw.dump(), "run", 8, prepared.context).valid);
}

TEST_CASE("AutoConfig client omits probe credentials from public progress and results")
{
	const auto prepared =
		prepare({{"streamSetup", "direct-single"}, {"outputs", json::array({standardOutput("horizontal", "horizontal", "twitch", twitchProbe())})}});
	const json rawEvent = {{"schemaVersion", 1},
			       {"sessionId", "run"},
			       {"sequence", 1},
			       {"type", "progress"},
			       {"phase", "bandwidth"},
			       {"progress", 50},
			       {"legId", "horizontal"},
			       {"probeId", "twitch-probe"},
			       {"provider", "twitch"},
			       {"server", "rtmp://private.example/app"},
			       {"streamKey", "event-secret"}};
	const auto event = contract::projectEvent(rawEvent.dump(), "run", -1, prepared.context);
	REQUIRE(event.valid);
	const json projectedEvent = json::parse(event.json);
	CHECK_FALSE(projectedEvent.contains("server"));
	CHECK_FALSE(projectedEvent.contains("streamKey"));
	CHECK(event.json.find("private.example") == std::string::npos);
	CHECK(event.json.find("event-secret") == std::string::npos);

	json rawResult = {{"schemaVersion", 1},
			  {"sessionId", "run"},
			  {"status", "complete"},
			  {"server", "rtmp://private.example/app"},
			  {"streamKey", "result-secret"},
			  {"legs", json::array({returnedStandardOutput("horizontal", "horizontal", "twitch", "twitch", "twitch-bandwidth-test", 5000, 4500)})}};
	rawResult["legs"][0]["measurement"]["probes"][0]["server"] = "rtmp://nested-private.example/app";
	rawResult["legs"][0]["measurement"]["probes"][0]["streamKey"] = "nested-result-secret";
	const auto result = contract::projectResult(rawResult.dump(), "run", prepared.context);
	REQUIRE(result.valid);
	const json projectedResult = json::parse(result.json);
	CHECK_FALSE(projectedResult.contains("server"));
	CHECK_FALSE(projectedResult.contains("streamKey"));
	CHECK_FALSE(projectedResult["outputs"][0]["measurement"]["evidence"][0].contains("server"));
	CHECK_FALSE(projectedResult["outputs"][0]["measurement"]["evidence"][0].contains("streamKey"));
	CHECK(result.json.find("private.example") == std::string::npos);
	CHECK(result.json.find("result-secret") == std::string::npos);
}

TEST_CASE("AutoConfig client treats an empty poll as no event without consuming sequence")
{
	const auto prepared =
		prepare({{"streamSetup", "direct-single"}, {"outputs", json::array({standardOutput("horizontal", "horizontal", "twitch", twitchProbe())})}});
	CHECK_FALSE(contract::decodePolledEvent(std::nullopt, "run", 7, prepared.context));
	const json event = {{"schemaVersion", 1}, {"sessionId", "run"}, {"sequence", 8}, {"type", "phase"}, {"phase", "preflight"}, {"progress", 0}};
	const auto decoded = contract::decodePolledEvent(event.dump(), "run", 7, prepared.context);
	REQUIRE(decoded);
	CHECK(decoded->valid);
	CHECK(decoded->sequence == 8);
}

TEST_CASE("AutoConfig client validates and projects a standard result")
{
	const auto prepared =
		prepare({{"streamSetup", "direct-single"}, {"outputs", json::array({standardOutput("horizontal", "horizontal", "twitch", twitchProbe())})}});
	const json raw = {{"schemaVersion", 1},
			  {"sessionId", "run"},
			  {"status", "complete"},
			  {"legs", json::array({returnedStandardOutput("horizontal", "horizontal", "twitch", "twitch", "twitch-bandwidth-test", 5000, 4500)})},
			  {"privateProof", "not-forwarded"}};
	const auto result = contract::projectResult(raw.dump(), "run", prepared.context);
	REQUIRE(result.valid);
	const json projected = json::parse(result.json);
	CHECK(projected["status"] == "complete");
	REQUIRE(projected["outputs"].size() == 1);
	CHECK(projected["outputs"][0]["outputId"] == "horizontal");
	CHECK(projected["outputs"][0]["videos"] == json::array({{{"display", "horizontal"}, {"width", 1280}, {"height", 720}, {"fpsNum", 60}, {"fpsDen", 1}}}));
	CHECK(projected["outputs"][0]["encoding"]["bitrateKbps"] == 4500);
	CHECK(projected["outputs"][0]["measurement"]["evidence"] ==
	      json::array({{{"platform", "twitch"}, {"method", "twitch-bandwidth-test"}, {"success", true}}}));
	CHECK_FALSE(projected.contains("schemaVersion"));
	CHECK_FALSE(projected.contains("sessionId"));
	CHECK_FALSE(projected.contains("privateProof"));
	CHECK_FALSE(projected.contains("legs"));
	CHECK_FALSE(projected["outputs"][0].contains("display"));
	CHECK_FALSE(projected["outputs"][0].contains("outputKind"));
	CHECK_FALSE(projected["outputs"][0].contains("destinations"));
	CHECK_FALSE(projected["outputs"][0].contains("limits"));
	CHECK_FALSE(projected["outputs"][0]["measurement"]["evidence"][0].contains("safeKbps"));

	json mismatchedOutput = raw;
	mismatchedOutput["legs"][0]["legId"] = "other";
	CHECK_FALSE(contract::projectResult(mismatchedOutput.dump(), "run", prepared.context).valid);
	json mismatchedDestination = raw;
	mismatchedDestination["legs"][0]["destinations"][0]["platform"] = "youtube";
	CHECK_FALSE(contract::projectResult(mismatchedDestination.dump(), "run", prepared.context).valid);
	json mismatchedEvidence = raw;
	mismatchedEvidence["legs"][0]["measurement"]["probes"][0]["method"] = "youtube-unbound-ramp";
	CHECK_FALSE(contract::projectResult(mismatchedEvidence.dump(), "run", prepared.context).valid);
	json excessiveBitrate = raw;
	excessiveBitrate["legs"][0]["measurement"]["probes"][0]["safeKbps"] = 10000;
	excessiveBitrate["legs"][0]["recommendation"]["bitrateKbps"] = 8100;
	CHECK_FALSE(contract::projectResult(excessiveBitrate.dump(), "run", prepared.context).valid);
}

TEST_CASE("AutoConfig client requires the exact active Dual Output aggregate proof")
{
	const auto fixture = dualOutputFixture();
	REQUIRE(contract::projectResult(fixture.result.dump(), "run", fixture.prepared.context).valid);
	auto estimated = fixture.result;
	estimated.erase("aggregateUpload");
	for (auto &output : estimated["legs"]) {
		output["measurement"]["mode"] = "estimated";
		output["measurement"]["confidence"] = "medium";
		output["measurement"].erase("probes");
		output["recommendation"]["bitrateKbps"] = 3000;
	}
	REQUIRE(contract::projectResult(estimated.dump(), "run", fixture.prepared.context).valid);
	checkInvalidResultMutations(
		fixture, {{"partial result status", [](json &value) { value["status"] = "partial"; }},
			  {"missing aggregate proof", [](json &value) { value.erase("aggregateUpload"); }},
			  {"wrong aggregate proof method", [](json &value) { value["aggregateUpload"]["method"] = "unexpected"; }},
			  {"missing aggregate proof method", [](json &value) { value["aggregateUpload"].erase("method"); }},
			  {"false concurrent hardware flag", [](json &value) { value["aggregateUpload"]["concurrentHardwareValidated"] = false; }},
			  {"missing concurrent hardware flag", [](json &value) { value["aggregateUpload"].erase("concurrentHardwareValidated"); }},
			  {"missing output", [](json &value) { value["legs"].erase(1); }},
			  {"duplicate output", [](json &value) { value["legs"][1] = value["legs"][0]; }},
			  {"extra output", [](json &value) { value["legs"].push_back(value["legs"][0]); }},
			  {"partially estimated output",
			   [](json &value) {
				   auto &output = value["legs"][1];
				   output["measurement"]["mode"] = "estimated";
				   output["measurement"]["confidence"] = "low";
				   output["measurement"].erase("probes");
				   output["recommendation"]["bitrateKbps"] = 3000;
			   }},
			  {"wrong evidence provider", [](json &value) { value["legs"][1]["measurement"]["probes"][0]["provider"] = "twitch"; }},
			  {"wrong evidence method", [](json &value) { value["legs"][1]["measurement"]["probes"][0]["method"] = "twitch-bandwidth-test"; }},
			  {"failed evidence", [](json &value) { value["legs"][1]["measurement"]["probes"][0]["success"] = false; }},
			  {"missing evidence", [](json &value) { value["legs"][1]["measurement"].erase("probes"); }},
			  {"mismatched aggregate safe value", [](json &value) { value["aggregateUpload"]["safeVideoKbps"] = 9999; }},
			  {"mismatched aggregate allocation", [](json &value) { value["aggregateUpload"]["allocatedVideoKbps"] = 9800; }},
			  {"mismatched provider safe value", [](json &value) { value["legs"][1]["measurement"]["probes"][0]["safeKbps"] = 9900; }},
			  {"divergent bitrate", [](json &value) { value["legs"][1]["recommendation"]["bitrateKbps"] = 4900; }},
			  {"divergent frame rate", [](json &value) { value["legs"][1]["recommendation"]["fpsNum"] = 30; }},
			  {"divergent encoder", [](json &value) { value["legs"][1]["recommendation"]["encoderId"] = "obs_x264"; }},
			  {"divergent encoder family", [](json &value) { value["legs"][1]["recommendation"]["encoderFamily"] = "x264"; }},
			  {"divergent preset", [](json &value) { value["legs"][1]["recommendation"]["preset"] = "veryfast"; }}});
}

TEST_CASE("AutoConfig client requires exact Enhanced Broadcasting combined workload proof")
{
	const auto fixture = enhancedBroadcastingFixture();
	const auto result = contract::projectResult(fixture.result.dump(), "run", fixture.prepared.context);
	REQUIRE(result.valid);
	const json projected = json::parse(result.json);
	CHECK(projected["outputs"][0]["videos"].size() == 2);
	CHECK_FALSE(projected["outputs"][0].contains("encoding"));
	CHECK_FALSE(projected.contains("combinedWorkload"));

	checkInvalidResultMutations(
		fixture,
		{{"partial result status", [](json &value) { value["status"] = "partial"; }},
		 {"missing combined proof", [](json &value) { value.erase("combinedWorkload"); }},
		 {"wrong combined proof method", [](json &value) { value["combinedWorkload"]["method"] = "unexpected"; }},
		 {"missing combined proof method", [](json &value) { value["combinedWorkload"].erase("method"); }},
		 {"false combined validation flag", [](json &value) { value["combinedWorkload"]["validated"] = false; }},
		 {"missing combined validation flag", [](json &value) { value["combinedWorkload"].erase("validated"); }},
		 {"wrong Enhanced Broadcasting output", [](json &value) { value["combinedWorkload"]["enhancedBroadcastingLegId"] = "horizontal"; }},
		 {"missing Enhanced Broadcasting output", [](json &value) { value["combinedWorkload"].erase("enhancedBroadcastingLegId"); }},
		 {"missing companion proof", [](json &value) { value["combinedWorkload"]["companionLegs"] = json::array(); }},
		 {"duplicate companion proof",
		  [](json &value) { value["combinedWorkload"]["companionLegs"].push_back(value["combinedWorkload"]["companionLegs"][0]); }},
		 {"extra companion proof",
		  [](json &value) {
			  json extra = value["combinedWorkload"]["companionLegs"][0];
			  extra["legId"] = "vertical";
			  extra["display"] = "vertical";
			  extra["width"] = 1080;
			  extra["height"] = 1920;
			  value["combinedWorkload"]["companionLegs"].push_back(std::move(extra));
		  }},
		 {"wrong Enhanced Broadcasting evidence provider", [](json &value) { value["legs"][0]["measurement"]["probes"][0]["provider"] = "youtube"; }},
		 {"wrong Enhanced Broadcasting evidence method",
		  [](json &value) { value["legs"][0]["measurement"]["probes"][0]["method"] = "twitch-bandwidth-test"; }},
		 {"failed Enhanced Broadcasting evidence", [](json &value) { value["legs"][0]["measurement"]["probes"][0]["success"] = false; }},
		 {"missing Enhanced Broadcasting evidence", [](json &value) { value["legs"][0]["measurement"].erase("probes"); }},
		 {"mismatched tested primary tuple", [](json &value) { value["legs"][0]["measurement"]["probes"][0]["testedWidth"] = 1280; }},
		 {"missing tested primary tuple", [](json &value) { value["legs"][0]["measurement"]["probes"][0].erase("testedWidth"); }},
		 {"mismatched tested additional tuple",
		  [](json &value) { value["legs"][0]["measurement"]["probes"][0]["testedAdditionalVideo"]["width"] = 720; }},
		 {"missing tested additional tuple", [](json &value) { value["legs"][0]["measurement"]["probes"][0].erase("testedAdditionalVideo"); }},
		 {"mismatched recommended primary and additional tuples",
		  [](json &value) { value["legs"][0]["recommendation"]["additionalVideo"]["width"] = 720; }},
		 {"wrong companion display", [](json &value) { value["combinedWorkload"]["companionLegs"][0]["display"] = "vertical"; }},
		 {"wrong companion width", [](json &value) { value["combinedWorkload"]["companionLegs"][0]["width"] = 1280; }},
		 {"wrong companion height", [](json &value) { value["combinedWorkload"]["companionLegs"][0]["height"] = 720; }},
		 {"wrong companion frame rate numerator", [](json &value) { value["combinedWorkload"]["companionLegs"][0]["fpsNum"] = 30; }},
		 {"wrong companion frame rate denominator", [](json &value) { value["combinedWorkload"]["companionLegs"][0]["fpsDen"] = 1001; }},
		 {"wrong companion bitrate", [](json &value) { value["combinedWorkload"]["companionLegs"][0]["bitrateKbps"] = 4400; }},
		 {"wrong companion encoder", [](json &value) { value["combinedWorkload"]["companionLegs"][0]["encoderId"] = "obs_x264"; }},
		 {"wrong companion preset", [](json &value) { value["combinedWorkload"]["companionLegs"][0]["preset"] = "veryfast"; }},
		 {"missing companion preset", [](json &value) { value["combinedWorkload"]["companionLegs"][0].erase("preset"); }}});
}

TEST_CASE("AutoConfig client requires one common Enhanced Broadcasting companion configuration")
{
	const auto fixture = enhancedBroadcastingFixture(true);
	REQUIRE(contract::projectResult(fixture.result.dump(), "run", fixture.prepared.context).valid);
	checkInvalidResultMutations(fixture,
				    {{"divergent companion bitrate",
				      [](json &value) {
					      value["legs"][2]["recommendation"]["bitrateKbps"] = 4400;
					      value["combinedWorkload"]["companionLegs"][1]["bitrateKbps"] = 4400;
				      }},
				     {"divergent companion frame rate",
				      [](json &value) {
					      value["legs"][2]["recommendation"]["fpsNum"] = 30;
					      value["combinedWorkload"]["companionLegs"][1]["fpsNum"] = 30;
				      }},
				     {"divergent companion encoder configuration",
				      [](json &value) {
					      auto &recommendationValue = value["legs"][2]["recommendation"];
					      recommendationValue["encoderId"] = "obs_x264";
					      recommendationValue["encoderFamily"] = "x264";
					      recommendationValue["preset"] = "veryfast";
					      auto &proof = value["combinedWorkload"]["companionLegs"][1];
					      proof["encoderId"] = "obs_x264";
					      proof["preset"] = "veryfast";
				      }},
				     {"divergent companion encoder family", [](json &value) { value["legs"][2]["recommendation"]["encoderFamily"] = "x264"; }},
				     {"divergent companion preset", [](json &value) { value["legs"][2]["recommendation"]["preset"] = "veryfast"; }}});
}

TEST_CASE("AutoConfig client projects terminal failures without private output data")
{
	const auto prepared = prepare({{"streamSetup", "direct-single"}, {"outputs", json::array({standardOutput("horizontal", "horizontal", "twitch")})}});
	const json raw = {{"schemaVersion", 1},
			  {"sessionId", "run"},
			  {"status", "failed"},
			  {"error", {{"code", "autoconfig_worker_failed"}, {"privateDetail", "not-forwarded"}}},
			  {"legs", json::array()}};
	const auto result = contract::projectResult(raw.dump(), "run", prepared.context);
	REQUIRE(result.valid);
	const json projected = json::parse(result.json);
	CHECK(projected == json{{"status", "failed"}, {"error", {{"code", "autoconfig_worker_failed"}}}, {"outputs", json::array()}});
}

TEST_CASE("AutoConfig Close failure leaves exactly one retryable finish state")
{
	contract::RunState state;
	CHECK(state.beginFinish());
	CHECK_FALSE(state.beginFinish());
	state.finishAttempt(false);
	CHECK(state.canRetryClose());
	CHECK(state.beginFinish());
	state.finishAttempt(true);
	CHECK(state.isClosed());
	CHECK_FALSE(state.canRetryClose());
	CHECK_FALSE(state.beginFinish());
}
