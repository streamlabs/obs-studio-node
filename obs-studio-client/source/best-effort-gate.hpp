#pragma once

#include <chrono>
#include <cstdint>

// Paces best-effort work during congestion by allowing it only on selected cycles.
// Healthy cycles reset the gate; congested cycles progressively increase the skip window up to maxInterval.
class BestEffortGate {
public:
	// Creates a gate that starts at baseInterval and backs off to at most maxInterval.
	BestEffortGate(std::chrono::milliseconds baseInterval, std::chrono::milliseconds maxInterval);

	// Starts a best-effort cycle and records whether best-effort work is allowed this time.
	// Returns true when the caller should run the best-effort work.
	bool startCycle();

	// Returns the current target interval between best-effort runs.
	std::chrono::milliseconds currentInterval() const;

	// Finishes the current cycle and updates the gate from the congestion state.
	// The gate uses the state recorded by startCycle() to decide whether to back off or consume a skipped cycle.
	void finishCycle(bool congested);

private:
	uint32_t skipCyclesFor(std::chrono::milliseconds interval) const;

	std::chrono::milliseconds baseInterval;
	std::chrono::milliseconds maxInterval;
	std::chrono::milliseconds activeInterval;
	uint32_t skipCyclesRemaining = 0;
	bool ranBestEffortThisCycle = false;
};
