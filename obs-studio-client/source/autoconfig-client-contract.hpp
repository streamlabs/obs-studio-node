#pragma once

#include "nlohmann/json.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace autoConfig::clientContract {

constexpr uint64_t maxSafeJavascriptInteger = 9007199254740991ULL;

struct EventEnvelope {
	bool valid = false;
	bool terminal = false;
	uint64_t sequence = 0;
	std::string error;
};

inline EventEnvelope validateEvent(const std::string &value, const std::string &sessionId, int64_t lastSequence)
{
	EventEnvelope envelope;
	try {
		const auto document = nlohmann::json::parse(value);
		if (!document.is_object() || document.value("schemaVersion", 0) != 1 || !document.contains("sessionId") || !document["sessionId"].is_string() ||
		    document["sessionId"].get<std::string>() != sessionId || !document.contains("sequence") ||
		    !(document["sequence"].is_number_unsigned() || document["sequence"].is_number_integer()) || !document.contains("type") ||
		    !document["type"].is_string() || !document.contains("progress") || !document["progress"].is_number()) {
			envelope.error = "Native Auto Optimizer returned an invalid event envelope";
			return envelope;
		}

		if (document["sequence"].is_number_integer() && document["sequence"].get<int64_t>() < 0) {
			envelope.error = "Native Auto Optimizer returned an invalid event envelope";
			return envelope;
		}
		const uint64_t sequence = document["sequence"].get<uint64_t>();
		if (sequence > maxSafeJavascriptInteger || (lastSequence >= 0 && sequence <= static_cast<uint64_t>(lastSequence))) {
			envelope.error = "Native Auto Optimizer returned an invalid event envelope";
			return envelope;
		}

		const std::string type = document["type"].get<std::string>();
		if (type != "phase" && type != "progress" && type != "result" && type != "error" && type != "cancelled" && type != "complete") {
			envelope.error = "Native Auto Optimizer returned an invalid event envelope";
			return envelope;
		}

		const double progress = document["progress"].get<double>();
		if (!std::isfinite(progress)) {
			envelope.error = "Native Auto Optimizer returned an invalid event envelope";
			return envelope;
		}

		envelope.valid = true;
		envelope.terminal = type == "complete" || type == "cancelled";
		envelope.sequence = sequence;
		return envelope;
	} catch (...) {
		envelope.error = "Native Auto Optimizer returned malformed event JSON";
		return envelope;
	}
}

inline std::optional<EventEnvelope> decodePolledEvent(const std::optional<std::string> &value, const std::string &sessionId, int64_t lastSequence)
{
	if (!value)
		return std::nullopt;

	return validateEvent(*value, sessionId, lastSequence);
}

inline std::string validateResult(const std::string &value, const std::string &sessionId)
{
	try {
		const auto document = nlohmann::json::parse(value);
		if (!document.is_object() || document.value("schemaVersion", 0) != 1 || !document.contains("sessionId") || !document["sessionId"].is_string() ||
		    document["sessionId"].get<std::string>() != sessionId || !document.contains("status") || !document["status"].is_string() ||
		    !document.contains("legs") || !document["legs"].is_array())
			return "Native Auto Optimizer returned an invalid result envelope";

		const std::string status = document["status"].get<std::string>();
		if (status != "complete" && status != "partial" && status != "cancelled" && status != "failed")
			return "Native Auto Optimizer returned an invalid result envelope";
		return {};
	} catch (...) {
		return "Native Auto Optimizer returned malformed result JSON";
	}
}

class RunState {
public:
	bool beginFinish()
	{
		if (closed || finishing)
			return false;
		finishing = true;
		return true;
	}

	void finishAttempt(bool closeSucceeded)
	{
		finishing = false;
		closed = closeSucceeded;
	}

	bool isFinishing() const { return finishing; }
	bool isClosed() const { return closed; }
	bool canRetryClose() const { return !closed && !finishing; }

private:
	bool finishing = false;
	bool closed = false;
};

} // namespace autoConfig::clientContract
