#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace autoOptimizer::clientContract {

struct VideoContext {
	std::optional<uint64_t> canvasId;
	int width = 0;
	int height = 0;
	int fpsNum = 0;
	int fpsDen = 1;
	int bitrateKbps = 0;
	std::string encoderId;
	std::optional<std::string> preset;
};

struct LimitsContext {
	std::optional<int> maxBitrateKbps;
	std::optional<int> maxWidth;
	std::optional<int> maxHeight;
	std::optional<int> maxFpsNum;
	std::optional<int> maxFpsDen;
};

struct AdditionalVideoContext {
	std::string display;
	VideoContext current;
	LimitsContext limits;
};

struct ProbeContext {
	std::string id;
	std::string kind;
	std::string outputId;
	std::string platform;
};

struct OutputContext {
	std::string outputId;
	std::string display;
	std::string outputKind;
	std::vector<std::string> destinations;
	VideoContext current;
	LimitsContext limits;
	std::optional<AdditionalVideoContext> additionalVideo;
	std::vector<ProbeContext> probes;
};

/** Credential-free validation context retained for the lifetime of one run. */
struct RequestContext {
	std::string streamSetup;
	std::vector<OutputContext> outputs;
};

struct PreparedRequest {
	bool valid = false;
	std::string wireJson;
	RequestContext context;
	std::string error;
};

struct EventEnvelope {
	bool valid = false;
	bool terminal = false;
	uint64_t sequence = 0;
	std::string json;
	std::string error;
};

struct ProjectedResult {
	bool valid = false;
	std::string json;
	std::string error;
};

PreparedRequest prepareRequest(const std::string &value);
EventEnvelope projectEvent(const std::string &value, const std::string &sessionId, int64_t lastSequence, const RequestContext &context);
std::optional<EventEnvelope> decodePolledEvent(const std::optional<std::string> &value, const std::string &sessionId, int64_t lastSequence,
					       const RequestContext &context);
ProjectedResult projectResult(const std::string &value, const std::string &sessionId, const RequestContext &context);

class RunState {
public:
	bool beginFinish();
	void finishAttempt(bool closeSucceeded);
	bool isFinishing() const;
	bool isClosed() const;
	bool canRetryClose() const;

private:
	bool finishing = false;
	bool closed = false;
};

} // namespace autoOptimizer::clientContract
