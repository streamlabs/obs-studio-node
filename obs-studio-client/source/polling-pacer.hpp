#pragma once

#include <chrono>
#include <cstdint>

namespace osn {

// Tracks poll loop timing and decides whether the worker should sleep after each cycle.
// Overruns are treated as congestion; sustained overruns yield briefly to avoid a tight CPU spin.
class PollingPacer {
public:
	// Creates a pacer for a worker that normally polls once every interval.
	// yieldAfterConsecutiveOverruns controls how many overrun cycles can run without sleeping.
	explicit PollingPacer(std::chrono::milliseconds interval, uint32_t yieldAfterConsecutiveOverruns = 2,
			      std::chrono::milliseconds sustainedOverrunYield = std::chrono::milliseconds(1));

	// Records the elapsed duration for one poll cycle and updates the latest pacing state.
	// Returns true when the caller should sleep for sleepDuration().
	bool finishCycle(std::chrono::milliseconds duration);

	// Returns the sleep duration computed by the most recent finishCycle() call.
	std::chrono::milliseconds sleepDuration() const;

	// Returns whether the most recent finishCycle() call detected an interval overrun.
	bool congested() const;

	// Returns the current run length of consecutive overrun cycles.
	uint32_t consecutiveOverruns() const;

private:
	std::chrono::milliseconds interval;
	uint32_t yieldAfterConsecutiveOverruns;
	std::chrono::milliseconds sustainedOverrunYield;
	std::chrono::milliseconds lastSleepDuration = std::chrono::milliseconds(0);
	bool lastCongested = false;
	uint32_t lastConsecutiveOverruns = 0;
};

} // namespace osn
