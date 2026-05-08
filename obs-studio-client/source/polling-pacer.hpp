#pragma once

#include <chrono>
#include <cstdint>

/// Tracks poll loop timing and decides whether the worker should sleep after each cycle.
/// Overruns are treated as congestion; sustained overruns yield briefly to avoid a tight CPU spin.
class PollingPacer {
public:
	/// Creates a pacer for a worker that normally polls once every interval.
	/// yieldAfterConsecutiveOverruns controls how many overrun cycles can run without sleeping.
	explicit PollingPacer(std::chrono::milliseconds interval,
			      uint32_t yieldAfterConsecutiveOverruns = 2,
			      std::chrono::milliseconds sustainedOverrunYield = std::chrono::milliseconds(1))
		: interval(interval),
		  yieldAfterConsecutiveOverruns(yieldAfterConsecutiveOverruns),
		  sustainedOverrunYield(sustainedOverrunYield)
	{
	}

	/// Records the elapsed duration for one poll cycle and updates the latest pacing state.
	/// Returns true when the caller should sleep for sleepDuration().
	bool finishCycle(std::chrono::milliseconds duration)
	{
		if (duration < interval) {
			lastConsecutiveOverruns = 0;
			lastSleepDuration = interval - duration;
			lastCongested = false;
			return true;
		}

		lastConsecutiveOverruns++;
		lastSleepDuration =
			lastConsecutiveOverruns > yieldAfterConsecutiveOverruns ? sustainedOverrunYield : std::chrono::milliseconds(0);
		lastCongested = true;

		return lastSleepDuration > std::chrono::milliseconds(0);
	}

	/// Returns the sleep duration computed by the most recent finishCycle() call.
	std::chrono::milliseconds sleepDuration() const
	{
		return lastSleepDuration;
	}

	/// Returns whether the most recent finishCycle() call detected an interval overrun.
	bool congested() const
	{
		return lastCongested;
	}

	/// Returns the current run length of consecutive overrun cycles.
	uint32_t consecutiveOverruns() const
	{
		return lastConsecutiveOverruns;
	}

private:
	std::chrono::milliseconds interval;
	uint32_t yieldAfterConsecutiveOverruns;
	std::chrono::milliseconds sustainedOverrunYield;
	std::chrono::milliseconds lastSleepDuration = std::chrono::milliseconds(0);
	bool lastCongested = false;
	uint32_t lastConsecutiveOverruns = 0;
};
