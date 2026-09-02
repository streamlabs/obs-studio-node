#include "autoconfig-client-contract.hpp"

#include "autoconfig-quality-policy.hpp"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace autoConfig::clientContract {
namespace {
using json = nlohmann::json;

constexpr uint64_t maxSafeJavascriptInteger = 9007199254740991ULL;

const std::set<std::string> streamSetups = {"direct-single",         "cloud-multistream",
					    "custom-rtmp",           "dual-output",
					    "enhanced-broadcasting", "enhanced-broadcasting-dual-output",
					    "stream-shift",          "mixed"};
const std::set<std::string> displays = {"horizontal", "vertical", "both"};
const std::set<std::string> outputKinds = {"standard", "twitch-enhanced-broadcasting"};
const std::set<std::string> platforms = {"twitch", "youtube", "facebook", "kick", "tiktok", "custom", "other"};
const std::set<std::string> estimateReasons = {"non_twitch",
					       "custom_rtmp",
					       "cloud_multistream",
					       "dual_output",
					       "enhanced_broadcasting",
					       "enhanced_broadcasting_dual_output",
					       "stream_shift",
					       "mixed_topology",
					       "probe_disabled",
					       "partial_provider_probes"};
const std::set<std::string> eventTypes = {"phase", "progress", "result", "error", "cancelled", "complete"};
const std::set<std::string> phases = {"preflight", "hardware", "bandwidth", "recommendation", "cleanup"};
const std::set<std::string> fatalErrorCodes = {"cancelled",
					       "hardware_no_usable_encoder",
					       "hardware_benchmark_overloaded",
					       "hardware_benchmark_timeout",
					       "hardware_benchmark_unavailable",
					       "autoconfig_worker_failed",
					       "autoconfig_worker_launch_failed"};

bool boundedString(const json &value, size_t maximum, bool allowEmpty = false)
{
	return value.is_string() && (allowEmpty || !value.get_ref<const std::string &>().empty()) && value.get_ref<const std::string &>().size() <= maximum;
}

bool integerInRange(const json &value, int64_t minimum, uint64_t maximum, int64_t &result)
{
	if (value.is_number_integer()) {
		const int64_t candidate = value.get<int64_t>();
		if (candidate < minimum || static_cast<uint64_t>(candidate) > maximum)
			return false;
		result = candidate;
		return true;
	}
	if (!value.is_number_unsigned())
		return false;
	const uint64_t candidate = value.get<uint64_t>();
	if (candidate > maximum || (minimum > 0 && candidate < static_cast<uint64_t>(minimum)))
		return false;
	result = static_cast<int64_t>(candidate);
	return true;
}

bool requiredInteger(const json &object, const char *key, int64_t minimum, uint64_t maximum, int &result)
{
	if (!object.contains(key))
		return false;
	int64_t value = 0;
	if (!integerInRange(object[key], minimum, maximum, value))
		return false;
	result = static_cast<int>(value);
	return true;
}

bool optionalPositiveInteger(const json &object, const char *key, uint64_t maximum, std::optional<int> &result)
{
	result.reset();
	if (!object.contains(key))
		return true;
	int64_t value = 0;
	if (!integerInRange(object[key], 0, maximum, value))
		return false;
	if (value > 0)
		result = static_cast<int>(value);
	return true;
}

std::optional<std::string> optionalString(const json &object, const char *key, size_t maximum)
{
	if (!object.contains(key))
		return std::nullopt;
	if (!boundedString(object[key], maximum))
		throw std::runtime_error(std::string("Invalid Auto Optimizer ") + key);
	return object[key].get<std::string>();
}

bool parseVideo(const json &value, VideoContext &result, json &wire)
{
	if (!value.is_object())
		return false;

	if (value.contains("canvasId")) {
		int64_t canvasId = 0;
		if (!integerInRange(value["canvasId"], 0, maxSafeJavascriptInteger, canvasId))
			return false;
		result.canvasId = static_cast<uint64_t>(canvasId);
		wire["canvasId"] = canvasId;
	}
	if (!requiredInteger(value, "width", 64, 8192, result.width) || !requiredInteger(value, "height", 64, 8192, result.height) ||
	    !requiredInteger(value, "fpsNum", 1, 240000, result.fpsNum) || !requiredInteger(value, "fpsDen", 1, 10000, result.fpsDen) ||
	    !requiredInteger(value, "bitrateKbps", 0, 100000, result.bitrateKbps) || !value.contains("encoderId") || !boundedString(value["encoderId"], 256))
		return false;
	if (!qualityPolicy::isValidFrameRate(result.fpsNum, result.fpsDen))
		return false;

	result.encoderId = value["encoderId"].get<std::string>();
	try {
		result.preset = optionalString(value, "preset", 128);
	} catch (...) {
		return false;
	}
	wire["width"] = result.width;
	wire["height"] = result.height;
	wire["fpsNum"] = result.fpsNum;
	wire["fpsDen"] = result.fpsDen;
	wire["bitrateKbps"] = result.bitrateKbps;
	wire["encoderId"] = result.encoderId;
	if (result.preset)
		wire["preset"] = *result.preset;
	return true;
}

bool parseLimits(const json *value, LimitsContext &result, json &wire)
{
	if (!value)
		return true;
	if (!value->is_object() || !optionalPositiveInteger(*value, "maxBitrateKbps", 100000, result.maxBitrateKbps) ||
	    !optionalPositiveInteger(*value, "maxWidth", 8192, result.maxWidth) || !optionalPositiveInteger(*value, "maxHeight", 8192, result.maxHeight) ||
	    !optionalPositiveInteger(*value, "maxFpsNum", 240000, result.maxFpsNum) || !optionalPositiveInteger(*value, "maxFpsDen", 10000, result.maxFpsDen))
		return false;
	if (result.maxWidth.has_value() != result.maxHeight.has_value() || (!result.maxFpsNum && result.maxFpsDen))
		return false;
	if ((result.maxWidth && *result.maxWidth < 64) || (result.maxHeight && *result.maxHeight < 64))
		return false;
	if (result.maxFpsNum && !result.maxFpsDen)
		result.maxFpsDen = 1;
	if (result.maxFpsNum && !qualityPolicy::isValidFrameRate(*result.maxFpsNum, *result.maxFpsDen))
		return false;
	if (result.maxBitrateKbps)
		wire["maxBitrateKbps"] = *result.maxBitrateKbps;
	if (result.maxWidth) {
		wire["maxWidth"] = *result.maxWidth;
		wire["maxHeight"] = *result.maxHeight;
	}
	if (result.maxFpsNum) {
		wire["maxFpsNum"] = *result.maxFpsNum;
		wire["maxFpsDen"] = *result.maxFpsDen;
	}
	return true;
}

bool parseAdditionalVideo(const json &value, AdditionalVideoContext &result, json &wire)
{
	if (!value.is_object() || !value.contains("display") || value["display"] != "vertical" || !value.contains("current"))
		return false;
	result.display = "vertical";
	wire["display"] = result.display;
	json current = json::object();
	if (!parseVideo(value["current"], result.current, current))
		return false;
	wire["current"] = std::move(current);
	json limits = json::object();
	const json *limitsValue = value.contains("limits") ? &value["limits"] : nullptr;
	if (!parseLimits(limitsValue, result.limits, limits))
		return false;
	if (!limits.empty())
		wire["limits"] = std::move(limits);
	return true;
}

std::string platformForProbeKind(std::string_view kind)
{
	if (kind == "twitch-standard" || kind == "twitch-enhanced-broadcasting")
		return "twitch";
	if (kind == "youtube-unbound")
		return "youtube";
	return {};
}

std::string methodForProbeKind(std::string_view kind)
{
	if (kind == "twitch-standard")
		return "twitch-bandwidth-test";
	if (kind == "twitch-enhanced-broadcasting")
		return "twitch-enhanced-broadcasting-test";
	if (kind == "youtube-unbound")
		return "youtube-unbound-ramp";
	return {};
}

const OutputContext *findOutput(const RequestContext &context, std::string_view outputId)
{
	const auto found =
		std::find_if(context.outputs.begin(), context.outputs.end(), [&](const OutputContext &output) { return output.outputId == outputId; });
	return found == context.outputs.end() ? nullptr : &*found;
}

const ProbeContext *findProbe(const RequestContext &context, std::string_view probeId)
{
	for (const auto &output : context.outputs) {
		const auto found = std::find_if(output.probes.begin(), output.probes.end(), [&](const ProbeContext &probe) { return probe.id == probeId; });
		if (found != output.probes.end())
			return &*found;
	}
	return nullptr;
}

bool copyOptionalString(const json &source, json &destination, const char *key, size_t maximum)
{
	if (!source.contains(key))
		return true;
	if (!boundedString(source[key], maximum))
		return false;
	destination[key] = source[key];
	return true;
}

bool copyOptionalInteger(const json &source, json &destination, const char *key, int64_t minimum, uint64_t maximum)
{
	if (!source.contains(key))
		return true;
	int64_t value = 0;
	if (!integerInRange(source[key], minimum, maximum, value))
		return false;
	destination[key] = value;
	return true;
}

bool copyAdditionalVideoTuple(const json &source, json &destination, const char *key)
{
	if (!source.contains(key))
		return true;
	const auto &value = source[key];
	int width = 0;
	int height = 0;
	int fpsNum = 0;
	int fpsDen = 0;
	if (!value.is_object() || value.value("display", "") != "vertical" || !requiredInteger(value, "width", 2, 16384, width) ||
	    !requiredInteger(value, "height", 2, 16384, height) || !requiredInteger(value, "fpsNum", 1, 1000000, fpsNum) ||
	    !requiredInteger(value, "fpsDen", 1, 1000000, fpsDen))
		return false;
	destination[key] = {{"display", "vertical"}, {"width", width}, {"height", height}, {"fpsNum", fpsNum}, {"fpsDen", fpsDen}};
	return true;
}
} // namespace

PreparedRequest prepareRequest(const std::string &value)
{
	PreparedRequest result;
	try {
		const json document = json::parse(value);
		if (!document.is_object() || !document.contains("streamSetup") || !boundedString(document["streamSetup"], 64) ||
		    !streamSetups.contains(document["streamSetup"].get<std::string>()) || !document.contains("outputs") || !document["outputs"].is_array()) {
			result.error = "Invalid Auto Optimizer request";
			return result;
		}

		result.context.streamSetup = document["streamSetup"].get<std::string>();
		const size_t maximumOutputs = result.context.streamSetup == "enhanced-broadcasting-dual-output"
						      ? qualityPolicy::kMaximumEnhancedBroadcastingDualOutputLegs
						      : qualityPolicy::kMaximumUploadLegs;
		if (document["outputs"].empty() || document["outputs"].size() > maximumOutputs) {
			result.error = "Invalid Auto Optimizer outputs";
			return result;
		}

		json wire = {{"schemaVersion", 1}, {"topology", result.context.streamSetup}, {"legs", json::array()}, {"activeProbes", json::array()}};
		std::set<std::string> outputIds;
		std::set<std::string> probeIds;
		size_t probeCount = 0;
		for (const auto &valueOutput : document["outputs"]) {
			if (!valueOutput.is_object() || !valueOutput.contains("outputId") || !boundedString(valueOutput["outputId"], 128) ||
			    !valueOutput.contains("display") || !boundedString(valueOutput["display"], 16) ||
			    !displays.contains(valueOutput["display"].get<std::string>()) || !valueOutput.contains("outputKind") ||
			    !boundedString(valueOutput["outputKind"], 64) || !outputKinds.contains(valueOutput["outputKind"].get<std::string>()) ||
			    !valueOutput.contains("destinations") || !valueOutput["destinations"].is_array() || valueOutput["destinations"].empty() ||
			    valueOutput["destinations"].size() > 16 || !valueOutput.contains("current")) {
				result.error = "Invalid Auto Optimizer output";
				return result;
			}

			OutputContext output;
			output.outputId = valueOutput["outputId"].get<std::string>();
			output.display = valueOutput["display"].get<std::string>();
			output.outputKind = valueOutput["outputKind"].get<std::string>();
			if (!outputIds.insert(output.outputId).second) {
				result.error = "Duplicate Auto Optimizer outputId";
				return result;
			}

			json wireOutput = {{"legId", output.outputId}, {"display", output.display}, {"outputKind", output.outputKind}};
			json destinations = json::array();
			std::set<std::string> destinationIds;
			for (const auto &destination : valueOutput["destinations"]) {
				if (!boundedString(destination, 32) || !platforms.contains(destination.get<std::string>()) ||
				    !destinationIds.insert(destination.get<std::string>()).second) {
					result.error = "Invalid Auto Optimizer destination";
					return result;
				}
				output.destinations.push_back(destination.get<std::string>());
				destinations.push_back({{"platform", destination.get<std::string>()}});
			}
			wireOutput["destinations"] = std::move(destinations);

			json current = json::object();
			if (!parseVideo(valueOutput["current"], output.current, current)) {
				result.error = "Invalid Auto Optimizer current settings";
				return result;
			}
			wireOutput["current"] = std::move(current);

			json limits = json::object();
			const json *limitsValue = valueOutput.contains("limits") ? &valueOutput["limits"] : nullptr;
			if (!parseLimits(limitsValue, output.limits, limits)) {
				result.error = "Invalid Auto Optimizer limits";
				return result;
			}
			if (!limits.empty())
				wireOutput["limits"] = std::move(limits);

			if (valueOutput.contains("additionalVideo")) {
				AdditionalVideoContext additional;
				json wireAdditional = json::object();
				if (output.outputKind != "twitch-enhanced-broadcasting" || output.display != "both" ||
				    !parseAdditionalVideo(valueOutput["additionalVideo"], additional, wireAdditional)) {
					result.error = "Invalid Auto Optimizer additional video";
					return result;
				}
				output.additionalVideo = std::move(additional);
				wireOutput["additionalVideo"] = std::move(wireAdditional);
			}

			if (valueOutput.contains("estimateReason")) {
				if (!boundedString(valueOutput["estimateReason"], 64) ||
				    !estimateReasons.contains(valueOutput["estimateReason"].get<std::string>())) {
					result.error = "Invalid Auto Optimizer estimate reason";
					return result;
				}
				wireOutput["estimateReason"] = valueOutput["estimateReason"];
			}

			if (valueOutput.contains("probes")) {
				if (!valueOutput["probes"].is_array() || valueOutput["probes"].size() > 16) {
					result.error = "Invalid Auto Optimizer probes";
					return result;
				}
				for (const auto &valueProbe : valueOutput["probes"]) {
					if (!valueProbe.is_object() || !valueProbe.contains("id") || !boundedString(valueProbe["id"], 128) ||
					    !valueProbe.contains("kind") || !boundedString(valueProbe["kind"], 64) || !valueProbe.contains("streamKey") ||
					    !boundedString(valueProbe["streamKey"], 4096)) {
						result.error = "Invalid Auto Optimizer probe";
						return result;
					}
					ProbeContext probe{valueProbe["id"].get<std::string>(), valueProbe["kind"].get<std::string>(), output.outputId,
							   platformForProbeKind(valueProbe["kind"].get<std::string>())};
					if (probe.platform.empty() || !probeIds.insert(probe.id).second || ++probeCount > 16) {
						result.error = "Invalid Auto Optimizer probe identity";
						return result;
					}
					json wireProbe = {{"probeId", probe.id},
							  {"kind", probe.kind},
							  {"legId", output.outputId},
							  {"streamKey", valueProbe["streamKey"]}};
					if (probe.kind != "twitch-enhanced-broadcasting") {
						if (!valueProbe.contains("server") || !boundedString(valueProbe["server"], 2048)) {
							result.error = "Invalid Auto Optimizer probe server";
							return result;
						}
						wireProbe["server"] = valueProbe["server"];
					}
					output.probes.push_back(probe);
					wire["activeProbes"].push_back(std::move(wireProbe));
				}
			}

			result.context.outputs.push_back(std::move(output));
			wire["legs"].push_back(std::move(wireOutput));
		}

		if (wire["activeProbes"].empty())
			wire.erase("activeProbes");
		result.wireJson = wire.dump();
		result.valid = true;
		return result;
	} catch (...) {
		result.error = "Malformed Auto Optimizer request JSON";
		return result;
	}
}

EventEnvelope projectEvent(const std::string &value, const std::string &sessionId, int64_t lastSequence, const RequestContext &context)
{
	EventEnvelope envelope;
	try {
		const json document = json::parse(value);
		int64_t sequenceValue = 0;
		if (!document.is_object() || document.value("schemaVersion", 0) != 1 || !document.contains("sessionId") || document["sessionId"] != sessionId ||
		    !document.contains("sequence") || !integerInRange(document["sequence"], 0, maxSafeJavascriptInteger, sequenceValue) ||
		    (lastSequence >= 0 && sequenceValue <= lastSequence) || !document.contains("type") || !boundedString(document["type"], 16) ||
		    !eventTypes.contains(document["type"].get<std::string>()) || !document.contains("phase") || !boundedString(document["phase"], 32) ||
		    !phases.contains(document["phase"].get<std::string>()) || !document.contains("progress") || !document["progress"].is_number()) {
			envelope.error = "Native Auto Optimizer returned an invalid event envelope";
			return envelope;
		}
		const double progress = document["progress"].get<double>();
		if (!std::isfinite(progress) || progress < 0 || progress > 100) {
			envelope.error = "Native Auto Optimizer returned an invalid event envelope";
			return envelope;
		}

		json projected = {{"type", document["type"]}, {"phase", document["phase"]}, {"progress", progress}};
		const OutputContext *output = nullptr;
		if (document.contains("legId")) {
			if (!boundedString(document["legId"], 128) || !(output = findOutput(context, document["legId"].get<std::string>()))) {
				envelope.error = "Native Auto Optimizer event referenced an unknown output";
				return envelope;
			}
			projected["outputId"] = output->outputId;
		}

		if (document.contains("probeId")) {
			if (!boundedString(document["probeId"], 128) || !document.contains("provider") || !boundedString(document["provider"], 16)) {
				envelope.error = "Native Auto Optimizer returned invalid probe progress";
				return envelope;
			}
			const ProbeContext *probe = findProbe(context, document["probeId"].get<std::string>());
			if (!probe || document["provider"].get<std::string>() != probe->platform || !output || output->outputId != probe->outputId) {
				envelope.error = "Native Auto Optimizer returned mismatched probe progress";
				return envelope;
			}
			projected["probe"] = {{"id", probe->id}, {"kind", probe->kind}};
		} else if (document.contains("provider")) {
			envelope.error = "Native Auto Optimizer returned invalid probe progress";
			return envelope;
		}

		if (!copyOptionalString(document, projected, "code", 128) || !copyOptionalString(document, projected, "encoderId", 256) ||
		    !copyOptionalString(document, projected, "encoderFamily", 128) || !copyOptionalString(document, projected, "encoderTitle", 256) ||
		    !copyOptionalInteger(document, projected, "targetBitrateKbps", 1, 100000) || !copyOptionalInteger(document, projected, "width", 2, 16384) ||
		    !copyOptionalInteger(document, projected, "height", 2, 16384) || !copyOptionalInteger(document, projected, "fpsNum", 1, 1000000) ||
		    !copyOptionalInteger(document, projected, "fpsDen", 1, 1000000) ||
		    !copyOptionalInteger(document, projected, "selectedBitrateKbps", 1, 100000) ||
		    !copyOptionalInteger(document, projected, "availableBitrateKbps", 1, 200000) ||
		    !copyAdditionalVideoTuple(document, projected, "additionalVideo")) {
			envelope.error = "Native Auto Optimizer returned invalid progress details";
			return envelope;
		}
		if (document.contains("measurementMode")) {
			if (!document["measurementMode"].is_string() ||
			    (document["measurementMode"] != "active" && document["measurementMode"] != "estimated")) {
				envelope.error = "Native Auto Optimizer returned invalid progress details";
				return envelope;
			}
			projected["measurementMode"] = document["measurementMode"];
		}

		envelope.valid = true;
		envelope.terminal = document["type"] == "complete" || document["type"] == "cancelled";
		envelope.sequence = static_cast<uint64_t>(sequenceValue);
		envelope.json = projected.dump();
		return envelope;
	} catch (...) {
		envelope.error = "Native Auto Optimizer returned malformed event JSON";
		return envelope;
	}
}

std::optional<EventEnvelope> decodePolledEvent(const std::optional<std::string> &value, const std::string &sessionId, int64_t lastSequence,
					       const RequestContext &context)
{
	if (!value)
		return std::nullopt;
	return projectEvent(*value, sessionId, lastSequence, context);
}

namespace {
struct Evidence {
	std::string platform;
	std::string method;
	bool success = false;
	std::optional<uint64_t> measuredKbps;
	std::optional<uint64_t> safeKbps;
	std::optional<int> testedWidth;
	std::optional<int> testedHeight;
	std::optional<int> testedFpsNum;
	std::optional<int> testedFpsDen;
	std::optional<json> testedAdditionalVideo;
};

struct RecommendationValue {
	int width = 0;
	int height = 0;
	int fpsNum = 0;
	int fpsDen = 1;
	int bitrateKbps = 0;
	std::string encoderId;
	std::string encoderFamily;
	std::string encoderTitle;
	std::string codec;
	std::optional<std::string> preset;
	std::optional<json> additionalVideo;
};

struct ParsedOutput {
	const OutputContext *expected = nullptr;
	std::string measurementMode;
	std::string confidence;
	std::optional<std::string> reason;
	std::vector<Evidence> evidence;
	RecommendationValue recommendation;
	json projected;
};

bool optionalUnsigned(const json &object, const char *key, uint64_t maximum, std::optional<uint64_t> &result)
{
	result.reset();
	if (!object.contains(key))
		return true;
	int64_t value = 0;
	if (!integerInRange(object[key], 0, maximum, value))
		return false;
	result = static_cast<uint64_t>(value);
	return true;
}

bool optionalResultInteger(const json &object, const char *key, uint64_t maximum, std::optional<int> &result)
{
	result.reset();
	if (!object.contains(key))
		return true;
	int64_t value = 0;
	if (!integerInRange(object[key], 1, maximum, value))
		return false;
	result = static_cast<int>(value);
	return true;
}

bool sameTuple(const RecommendationValue &left, const json &right)
{
	return right.is_object() && right.value("width", 0) == left.width && right.value("height", 0) == left.height &&
	       right.value("fpsNum", 0) == left.fpsNum && right.value("fpsDen", 0) == left.fpsDen;
}

bool sameAdditionalTuple(const std::optional<json> &left, const std::optional<json> &right)
{
	if (left.has_value() != right.has_value())
		return false;
	if (!left)
		return true;
	return *left == *right;
}

bool recommendationTupleIsValid(const RecommendationValue &value)
{
	return value.width >= 2 && value.width <= 16384 && value.width % 2 == 0 && value.height >= 2 && value.height <= 16384 && value.height % 2 == 0 &&
	       value.fpsNum >= 1 && value.fpsNum <= 1000000 && value.fpsDen >= 1 && value.fpsDen <= 1000000 &&
	       static_cast<int64_t>(value.fpsNum) <= static_cast<int64_t>(240) * value.fpsDen && value.bitrateKbps >= 1 && value.bitrateKbps <= 100000;
}

bool tupleWithinLimits(int width, int height, int fpsNum, int fpsDen, const LimitsContext &limits)
{
	return (!limits.maxWidth || width <= *limits.maxWidth) && (!limits.maxHeight || height <= *limits.maxHeight) &&
	       (!limits.maxFpsNum || static_cast<int64_t>(fpsNum) * *limits.maxFpsDen <= static_cast<int64_t>(*limits.maxFpsNum) * fpsDen);
}

bool tuplePromotes(const RecommendationValue &recommendation, const VideoContext &current)
{
	return recommendation.width > current.width || recommendation.height > current.height ||
	       static_cast<int64_t>(recommendation.fpsNum) * current.fpsDen > static_cast<int64_t>(current.fpsNum) * recommendation.fpsDen;
}

bool parseAdditionalTuple(const json &value, std::optional<json> &result)
{
	result.reset();
	if (value.is_null())
		return true;
	int width = 0;
	int height = 0;
	int fpsNum = 0;
	int fpsDen = 0;
	if (!value.is_object() || value.value("display", "") != "vertical" || !requiredInteger(value, "width", 2, 16384, width) || width % 2 != 0 ||
	    !requiredInteger(value, "height", 2, 16384, height) || height % 2 != 0 || !requiredInteger(value, "fpsNum", 1, 1000000, fpsNum) ||
	    !requiredInteger(value, "fpsDen", 1, 1000000, fpsDen) || static_cast<int64_t>(fpsNum) > static_cast<int64_t>(240) * fpsDen)
		return false;
	result = json{{"display", "vertical"}, {"width", width}, {"height", height}, {"fpsNum", fpsNum}, {"fpsDen", fpsDen}};
	return true;
}

bool hasRequestedEvidenceOwner(const OutputContext &output, std::string_view platform, std::string_view method)
{
	return std::count_if(output.probes.begin(), output.probes.end(),
			     [&](const ProbeContext &probe) { return probe.platform == platform && methodForProbeKind(probe.kind) == method; }) == 1;
}

bool parseEvidence(const json &value, const OutputContext &output, Evidence &result, json &projected)
{
	if (!value.is_object() || !value.contains("provider") || !boundedString(value["provider"], 16) || !value.contains("method") ||
	    !boundedString(value["method"], 64) || !value.contains("success") || !value["success"].is_boolean())
		return false;
	result.platform = value["provider"].get<std::string>();
	result.method = value["method"].get<std::string>();
	result.success = value["success"].get<bool>();
	if (!hasRequestedEvidenceOwner(output, result.platform, result.method))
		return false;
	if (!optionalUnsigned(value, "measuredKbps", 100000, result.measuredKbps) || !optionalUnsigned(value, "safeKbps", 100000, result.safeKbps) ||
	    !optionalResultInteger(value, "testedWidth", 16384, result.testedWidth) ||
	    !optionalResultInteger(value, "testedHeight", 16384, result.testedHeight) ||
	    !optionalResultInteger(value, "testedFpsNum", 1000000, result.testedFpsNum) ||
	    !optionalResultInteger(value, "testedFpsDen", 1000000, result.testedFpsDen))
		return false;
	if (value.contains("testedAdditionalVideo") && !parseAdditionalTuple(value["testedAdditionalVideo"], result.testedAdditionalVideo))
		return false;
	projected = {{"platform", result.platform}, {"method", result.method}, {"success", result.success}};
	return true;
}

const std::map<std::string, std::string> encoderFamilies = {{"obs_nvenc_h264_tex", "obs_nvenc_h264_tex"},
							    {"obs_qsv11_v2", "qsv"},
							    {"h264_texture_amf", "amd"},
							    {"com.apple.videotoolbox.videoencoder.h264.gva", "apple"},
							    {"com.apple.videotoolbox.videoencoder.ave.avc", "apple"},
							    {"obs_x264", "x264"}};
const std::map<std::string, std::string> encoderPresets = {{"obs_nvenc_h264_tex", "p5"},
							   {"obs_qsv11_v2", "TU4"},
							   {"h264_texture_amf", "quality"},
							   {"com.apple.videotoolbox.videoencoder.h264.gva", "high"},
							   {"com.apple.videotoolbox.videoencoder.ave.avc", "high"},
							   {"obs_x264", "veryfast"}};

bool parseRecommendation(const json &value, const OutputContext &output, const std::string &measurementMode, const std::vector<Evidence> &evidence,
			 RecommendationValue &result, json &projected)
{
	if (!value.is_object() || !requiredInteger(value, "width", 2, 16384, result.width) || !requiredInteger(value, "height", 2, 16384, result.height) ||
	    !requiredInteger(value, "fpsNum", 1, 1000000, result.fpsNum) || !requiredInteger(value, "fpsDen", 1, 1000000, result.fpsDen) ||
	    !requiredInteger(value, "bitrateKbps", 1, 100000, result.bitrateKbps) || !recommendationTupleIsValid(result))
		return false;
	if (value.contains("additionalVideo") && !parseAdditionalTuple(value["additionalVideo"], result.additionalVideo))
		return false;
	if (result.additionalVideo.has_value() != output.additionalVideo.has_value() ||
	    !tupleWithinLimits(result.width, result.height, result.fpsNum, result.fpsDen, output.limits))
		return false;

	if (result.additionalVideo) {
		const auto &additional = *result.additionalVideo;
		const auto &additionalContext = *output.additionalVideo;
		if (additional["width"] != result.height || additional["height"] != result.width ||
		    (measurementMode == "active" && static_cast<int64_t>(additional["fpsNum"].get<int>()) * result.fpsDen !=
							    static_cast<int64_t>(result.fpsNum) * additional["fpsDen"].get<int>()) ||
		    !tupleWithinLimits(additional["width"], additional["height"], additional["fpsNum"], additional["fpsDen"], additionalContext.limits))
			return false;
		if (measurementMode == "estimated" &&
		    (additional["width"] != additionalContext.current.width || additional["height"] != additionalContext.current.height ||
		     static_cast<int64_t>(additional["fpsNum"].get<int>()) * additionalContext.current.fpsDen !=
			     static_cast<int64_t>(additionalContext.current.fpsNum) * additional["fpsDen"].get<int>()))
			return false;
	}

	const bool providerOwned = output.outputKind == "twitch-enhanced-broadcasting";
	if (providerOwned) {
		if ((output.display == "both") != result.additionalVideo.has_value() || (output.display != "both" && output.display != "horizontal"))
			return false;
	} else {
		if (output.display == "both" || result.additionalVideo)
			return false;
		if (!value.contains("encoderId") || !boundedString(value["encoderId"], 256) || !value.contains("encoderFamily") ||
		    !boundedString(value["encoderFamily"], 128) || !value.contains("encoderTitle") || !boundedString(value["encoderTitle"], 256) ||
		    value.value("codec", "") != "h264")
			return false;
		result.encoderId = value["encoderId"].get<std::string>();
		result.encoderFamily = value["encoderFamily"].get<std::string>();
		result.encoderTitle = value["encoderTitle"].get<std::string>();
		result.codec = "h264";
		try {
			result.preset = optionalString(value, "preset", 128);
		} catch (...) {
			return false;
		}
		const auto family = encoderFamilies.find(result.encoderId);
		const auto preset = encoderPresets.find(result.encoderId);
		if (family == encoderFamilies.end() || preset == encoderPresets.end() || family->second != result.encoderFamily || !result.preset ||
		    preset->second != *result.preset || result.bitrateKbps > qualityPolicy::kMaximumRecommendedBitrateKbps ||
		    (output.limits.maxBitrateKbps && result.bitrateKbps > *output.limits.maxBitrateKbps))
			return false;
	}

	if (measurementMode == "estimated" &&
	    (tuplePromotes(result, output.current) || (output.current.bitrateKbps > 0 && result.bitrateKbps > output.current.bitrateKbps)))
		return false;
	if (measurementMode == "active") {
		if (providerOwned) {
			const auto matching = std::find_if(evidence.begin(), evidence.end(), [&](const Evidence &item) {
				return item.success && item.platform == "twitch" && item.method == "twitch-enhanced-broadcasting-test" &&
				       item.testedWidth == result.width && item.testedHeight == result.height && item.testedFpsNum == result.fpsNum &&
				       item.testedFpsDen == result.fpsDen && sameAdditionalTuple(item.testedAdditionalVideo, result.additionalVideo);
			});
			if (matching == evidence.end())
				return false;
		} else {
			std::vector<uint64_t> safeValues;
			for (const auto &item : evidence) {
				if (item.success && item.safeKbps)
					safeValues.push_back(*item.safeKbps);
			}
			if (safeValues.empty() || result.bitrateKbps > *std::min_element(safeValues.begin(), safeValues.end()))
				return false;
		}
	}

	const std::string primaryDisplay = output.display == "both" ? "horizontal" : output.display;
	projected["videos"] = json::array(
		{{{"display", primaryDisplay}, {"width", result.width}, {"height", result.height}, {"fpsNum", result.fpsNum}, {"fpsDen", result.fpsDen}}});
	if (result.additionalVideo)
		projected["videos"].push_back(*result.additionalVideo);
	if (!providerOwned) {
		projected["encoding"] = {{"bitrateKbps", result.bitrateKbps},
					 {"encoderId", result.encoderId},
					 {"encoderFamily", result.encoderFamily},
					 {"encoderTitle", result.encoderTitle},
					 {"codec", result.codec}};
		if (result.preset)
			projected["encoding"]["preset"] = *result.preset;
	}
	return true;
}

bool parseOutputResult(const json &value, const OutputContext &expected, ParsedOutput &result)
{
	if (!value.is_object() || value.value("legId", "") != expected.outputId || value.value("display", "") != expected.display ||
	    value.value("outputKind", "") != expected.outputKind || !value.contains("destinations") || !value["destinations"].is_array() ||
	    value["destinations"].size() != expected.destinations.size() || !value.contains("measurement") || !value["measurement"].is_object() ||
	    !value.contains("recommendation"))
		return false;
	for (size_t index = 0; index < expected.destinations.size(); index++) {
		const auto &destination = value["destinations"][index];
		if (!destination.is_object() || destination.value("platform", "") != expected.destinations[index])
			return false;
	}

	LimitsContext returnedLimits;
	json ignoredLimits = json::object();
	const json *limits = value.contains("limits") ? &value["limits"] : nullptr;
	// The server may lower request limits to provider caps before returning a
	// result. Validate that echoed request data is well formed, but compare the
	// public result against the original credential-free request context.
	if (!parseLimits(limits, returnedLimits, ignoredLimits))
		return false;

	const auto &measurement = value["measurement"];
	result.measurementMode = measurement.value("mode", "");
	result.confidence = measurement.value("confidence", "");
	if ((result.measurementMode != "active" && result.measurementMode != "estimated") ||
	    (result.confidence != "high" && result.confidence != "medium" && result.confidence != "low"))
		return false;
	try {
		result.reason = optionalString(measurement, "reason", 128);
	} catch (...) {
		return false;
	}

	json evidenceProjection = json::array();
	if (measurement.contains("probes")) {
		if (!measurement["probes"].is_array() || measurement["probes"].size() > expected.probes.size())
			return false;
		std::set<std::pair<std::string, std::string>> identities;
		for (const auto &valueEvidence : measurement["probes"]) {
			Evidence evidence;
			json projected;
			if (!parseEvidence(valueEvidence, expected, evidence, projected) || !identities.emplace(evidence.platform, evidence.method).second)
				return false;
			result.evidence.push_back(std::move(evidence));
			evidenceProjection.push_back(std::move(projected));
		}
	}
	if (result.measurementMode == "active" &&
	    std::none_of(result.evidence.begin(), result.evidence.end(), [](const Evidence &item) { return item.success; }))
		return false;

	result.expected = &expected;
	result.projected = {{"outputId", expected.outputId}};
	json recommendationProjection = json::object();
	if (!parseRecommendation(value["recommendation"], expected, result.measurementMode, result.evidence, result.recommendation, recommendationProjection))
		return false;
	result.projected.update(std::move(recommendationProjection));
	result.projected["measurement"] = {{"mode", result.measurementMode}, {"confidence", result.confidence}};
	if (result.reason)
		result.projected["measurement"]["reason"] = *result.reason;
	if (!evidenceProjection.empty())
		result.projected["measurement"]["evidence"] = std::move(evidenceProjection);
	return true;
}

const Evidence *findEvidence(const std::vector<ParsedOutput> &outputs, std::string_view platform)
{
	const Evidence *result = nullptr;
	for (const auto &output : outputs) {
		for (const auto &evidence : output.evidence) {
			if (evidence.platform != platform)
				continue;
			if (result)
				return nullptr;
			result = &evidence;
		}
	}
	return result;
}

bool validDualOutputProof(const json &document, const RequestContext &context, const std::vector<ParsedOutput> &outputs)
{
	if (context.streamSetup != "dual-output" || context.outputs.size() != 2)
		return !document.contains("aggregateUpload");
	if (document.value("status", "") != "complete")
		return false;
	const bool anyActive = std::any_of(outputs.begin(), outputs.end(), [](const ParsedOutput &output) { return output.measurementMode == "active"; });
	if (!anyActive) {
		return !document.contains("aggregateUpload") &&
		       std::all_of(outputs.begin(), outputs.end(), [](const ParsedOutput &output) { return output.measurementMode == "estimated"; });
	}
	if (!std::all_of(outputs.begin(), outputs.end(), [](const ParsedOutput &output) { return output.measurementMode == "active"; }) ||
	    !document.contains("aggregateUpload") || !document["aggregateUpload"].is_object())
		return false;

	const Evidence *twitch = findEvidence(outputs, "twitch");
	const Evidence *youtube = findEvidence(outputs, "youtube");
	if (!twitch || !youtube || twitch->method != "twitch-bandwidth-test" || youtube->method != "youtube-unbound-ramp" || !twitch->success ||
	    !youtube->success || !twitch->safeKbps || !youtube->safeKbps)
		return false;
	const auto allocation = qualityPolicy::allocateSharedTwoLegBandwidth(*twitch->safeKbps, *youtube->safeKbps);
	if (!allocation.valid)
		return false;

	const auto &aggregate = document["aggregateUpload"];
	int safeVideoKbps = 0;
	int allocatedVideoKbps = 0;
	if (aggregate.value("method", "") != "dual-output-isolated-lower-bound" || aggregate.value("concurrentHardwareValidated", false) != true ||
	    !requiredInteger(aggregate, "safeVideoKbps", 1, 200000, safeVideoKbps) ||
	    !requiredInteger(aggregate, "allocatedVideoKbps", 1, 200000, allocatedVideoKbps) ||
	    safeVideoKbps != static_cast<int>(allocation.aggregateSafeVideoKbps) || allocatedVideoKbps != static_cast<int>(allocation.allocatedVideoKbps))
		return false;

	const auto &first = outputs[0].recommendation;
	const auto &second = outputs[1].recommendation;
	const bool sameFps = static_cast<int64_t>(first.fpsNum) * second.fpsDen == static_cast<int64_t>(second.fpsNum) * first.fpsDen;
	const bool sameEncoder = first.encoderId == second.encoderId && first.encoderFamily == second.encoderFamily && first.preset == second.preset;
	return first.bitrateKbps == static_cast<int>(allocation.perLegVideoKbps) && second.bitrateKbps == static_cast<int>(allocation.perLegVideoKbps) &&
	       sameFps && sameEncoder && static_cast<uint64_t>(first.bitrateKbps + second.bitrateKbps) == allocation.allocatedVideoKbps &&
	       allocation.allocatedVideoKbps <= allocation.aggregateSafeVideoKbps;
}

const ParsedOutput *findParsedOutput(const std::vector<ParsedOutput> &outputs, std::string_view outputId)
{
	const auto found = std::find_if(outputs.begin(), outputs.end(), [&](const ParsedOutput &output) { return output.expected->outputId == outputId; });
	return found == outputs.end() ? nullptr : &*found;
}

bool validEnhancedBroadcastingCombinedProof(const json &document, const RequestContext &context, const std::vector<ParsedOutput> &outputs)
{
	if (context.streamSetup != "enhanced-broadcasting-dual-output")
		return !document.contains("combinedWorkload");
	if (document.value("status", "") != "complete" || !document.contains("combinedWorkload") || !document["combinedWorkload"].is_object())
		return false;

	std::vector<const OutputContext *> enhanced;
	std::vector<const OutputContext *> companions;
	for (const auto &output : context.outputs) {
		(output.outputKind == "twitch-enhanced-broadcasting" ? enhanced : companions).push_back(&output);
	}
	if (enhanced.size() != 1 || enhanced[0]->display != "both" || companions.empty() || companions.size() > 2)
		return false;
	std::set<std::string> companionDisplays;
	for (const auto *companion : companions) {
		if ((companion->display != "horizontal" && companion->display != "vertical") || !companionDisplays.insert(companion->display).second)
			return false;
	}

	const auto &proof = document["combinedWorkload"];
	if (proof.value("method", "") != "enhanced-broadcasting-dual-output-concurrent" || proof.value("validated", false) != true ||
	    proof.value("enhancedBroadcastingLegId", "") != enhanced[0]->outputId || !proof.contains("companionLegs") || !proof["companionLegs"].is_array() ||
	    proof["companionLegs"].size() != companions.size())
		return false;
	std::set<std::string> proofIds;
	for (const auto &companion : proof["companionLegs"]) {
		if (!companion.is_object() || !companion.contains("legId") || !boundedString(companion["legId"], 128) ||
		    !proofIds.insert(companion["legId"].get<std::string>()).second)
			return false;
	}

	const ParsedOutput *enhancedResult = findParsedOutput(outputs, enhanced[0]->outputId);
	if (!enhancedResult || enhancedResult->measurementMode != "active" || !enhancedResult->recommendation.additionalVideo)
		return false;
	const auto enhancedEvidence = std::find_if(enhancedResult->evidence.begin(), enhancedResult->evidence.end(), [&](const Evidence &item) {
		return item.platform == "twitch" && item.method == "twitch-enhanced-broadcasting-test" && item.success &&
		       item.testedWidth == enhancedResult->recommendation.width && item.testedHeight == enhancedResult->recommendation.height &&
		       item.testedFpsNum == enhancedResult->recommendation.fpsNum && item.testedFpsDen == enhancedResult->recommendation.fpsDen &&
		       sameAdditionalTuple(item.testedAdditionalVideo, enhancedResult->recommendation.additionalVideo);
	});
	if (enhancedEvidence == enhancedResult->evidence.end())
		return false;

	std::set<std::string> outputSignatures;
	for (const auto *expected : companions) {
		const ParsedOutput *output = findParsedOutput(outputs, expected->outputId);
		if (!output)
			return false;
		outputSignatures.insert(output->recommendation.encoderId + "\n" + output->recommendation.encoderFamily + "\n" +
					(output->recommendation.preset ? *output->recommendation.preset : "") + "\n" +
					std::to_string(output->recommendation.bitrateKbps));
	}
	if (outputSignatures.size() != 1)
		return false;

	for (const auto *expected : companions) {
		const ParsedOutput *output = findParsedOutput(outputs, expected->outputId);
		const auto proofCompanion = std::find_if(proof["companionLegs"].begin(), proof["companionLegs"].end(),
							 [&](const json &value) { return value.value("legId", "") == expected->outputId; });
		if (!output || proofCompanion == proof["companionLegs"].end() || proofCompanion->value("display", "") != expected->display)
			return false;
		const auto &recommendation = output->recommendation;
		int width = 0;
		int height = 0;
		int fpsNum = 0;
		int fpsDen = 0;
		int bitrateKbps = 0;
		if (!requiredInteger(*proofCompanion, "width", 2, 16384, width) || width % 2 != 0 ||
		    !requiredInteger(*proofCompanion, "height", 2, 16384, height) || height % 2 != 0 ||
		    !requiredInteger(*proofCompanion, "fpsNum", 1, 1000000, fpsNum) || !requiredInteger(*proofCompanion, "fpsDen", 1, 1000000, fpsDen) ||
		    !requiredInteger(*proofCompanion, "bitrateKbps", 1, 100000, bitrateKbps) || !proofCompanion->contains("encoderId") ||
		    !boundedString((*proofCompanion)["encoderId"], 256))
			return false;
		std::optional<std::string> proofPreset;
		if (proofCompanion->contains("preset")) {
			if (!boundedString((*proofCompanion)["preset"], 128))
				return false;
			proofPreset = (*proofCompanion)["preset"].get<std::string>();
		}
		const json &canvasTuple = expected->display == "horizontal" ? enhancedResult->projected["videos"][0] : enhancedResult->projected["videos"][1];
		if (width != recommendation.width || height != recommendation.height || fpsNum != recommendation.fpsNum || fpsDen != recommendation.fpsDen ||
		    bitrateKbps != recommendation.bitrateKbps || (*proofCompanion)["encoderId"] != recommendation.encoderId ||
		    proofPreset != recommendation.preset || width != canvasTuple["width"] || height != canvasTuple["height"] ||
		    fpsNum != canvasTuple["fpsNum"] || fpsDen != canvasTuple["fpsDen"])
			return false;
	}
	return true;
}
} // namespace

ProjectedResult projectResult(const std::string &value, const std::string &sessionId, const RequestContext &context)
{
	ProjectedResult result;
	try {
		const json document = json::parse(value);
		if (!document.is_object() || document.value("schemaVersion", 0) != 1 || document.value("sessionId", "") != sessionId ||
		    !document.contains("status") || !boundedString(document["status"], 16) || !document.contains("legs") || !document["legs"].is_array()) {
			result.error = "Native Auto Optimizer returned an invalid result envelope";
			return result;
		}
		const std::string status = document["status"].get<std::string>();
		if (status != "complete" && status != "partial" && status != "cancelled" && status != "failed") {
			result.error = "Native Auto Optimizer returned an invalid result envelope";
			return result;
		}

		json projected = {{"status", status}, {"outputs", json::array()}};
		const bool hasError = document.contains("error");
		if (hasError) {
			if (!document["error"].is_object() || !document["error"].contains("code") || !boundedString(document["error"]["code"], 128)) {
				result.error = "Native Auto Optimizer returned an invalid result error";
				return result;
			}
			if (!fatalErrorCodes.contains(document["error"]["code"].get<std::string>())) {
				result.error = "Native Auto Optimizer returned an invalid result error";
				return result;
			}
			projected["error"] = {{"code", document["error"]["code"]}};
		}

		if (status == "cancelled" || status == "failed") {
			if (!hasError || !document["legs"].empty() || document.contains("aggregateUpload") || document.contains("combinedWorkload")) {
				result.error = "Native Auto Optimizer returned invalid terminal output data";
				return result;
			}
			result.valid = true;
			result.json = projected.dump();
			return result;
		}
		if (hasError) {
			result.error = "Native Auto Optimizer returned an invalid result error";
			return result;
		}
		if (document["legs"].size() != context.outputs.size()) {
			result.error = "Native Auto Optimizer returned mismatched outputs";
			return result;
		}

		std::vector<ParsedOutput> parsedOutputs;
		parsedOutputs.reserve(context.outputs.size());
		for (size_t index = 0; index < context.outputs.size(); index++) {
			ParsedOutput output;
			if (!parseOutputResult(document["legs"][index], context.outputs[index], output)) {
				result.error = "Native Auto Optimizer returned an invalid output recommendation";
				return result;
			}
			parsedOutputs.push_back(std::move(output));
		}
		if (!validDualOutputProof(document, context, parsedOutputs)) {
			result.error = "Native Auto Optimizer returned invalid Dual Output aggregate proof";
			return result;
		}
		if (!validEnhancedBroadcastingCombinedProof(document, context, parsedOutputs)) {
			result.error = "Native Auto Optimizer returned invalid Enhanced Broadcasting workload proof";
			return result;
		}
		for (auto &output : parsedOutputs)
			projected["outputs"].push_back(std::move(output.projected));

		result.valid = true;
		result.json = projected.dump();
		return result;
	} catch (...) {
		result.error = "Native Auto Optimizer returned malformed result JSON";
		return result;
	}
}

bool RunState::beginFinish()
{
	if (closed || finishing)
		return false;
	finishing = true;
	return true;
}

void RunState::finishAttempt(bool closeSucceeded)
{
	finishing = false;
	closed = closeSucceeded;
}

bool RunState::isFinishing() const
{
	return finishing;
}

bool RunState::isClosed() const
{
	return closed;
}

bool RunState::canRetryClose() const
{
	return !closed && !finishing;
}

} // namespace autoConfig::clientContract
