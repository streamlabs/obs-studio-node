#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

/// Paces best-effort work during congestion by allowing it only on selected cycles.
/// Healthy cycles reset the gate; congested cycles progressively increase the skip window up to maxInterval.
class BestEffortGate {
public:
	/// Creates a gate that starts at baseInterval and backs off to at most maxInterval.
	BestEffortGate(std::chrono::milliseconds baseInterval, std::chrono::milliseconds maxInterval)
		: baseInterval(baseInterval > std::chrono::milliseconds(0) ? baseInterval : std::chrono::milliseconds(1)),
		  maxInterval(std::max(maxInterval, this->baseInterval)),
		  activeInterval(this->baseInterval)
	{
	}

	/// Starts a best-effort cycle and records whether best-effort work is allowed this time.
	/// Returns true when the caller should run the best-effort work.
	bool startCycle()
	{
		ranBestEffortThisCycle = skipCyclesRemaining == 0;
		return ranBestEffortThisCycle;
	}

	/// Returns the current target interval between best-effort runs.
	std::chrono::milliseconds currentInterval() const
	{
		return activeInterval;
	}

	/// Finishes the current cycle and updates the gate from the congestion state.
	/// The gate uses the state recorded by startCycle() to decide whether to back off or consume a skipped cycle.
	void finishCycle(bool congested)
	{
		if (!congested) {
			activeInterval = baseInterval;
			skipCyclesRemaining = 0;
			ranBestEffortThisCycle = false;
			return;
		}

		if (!ranBestEffortThisCycle) {
			if (skipCyclesRemaining > 0)
				skipCyclesRemaining--;
			return;
		}

		activeInterval = std::min(activeInterval * 2, maxInterval);
		skipCyclesRemaining = skipCyclesFor(activeInterval);
	}

private:
	uint32_t skipCyclesFor(std::chrono::milliseconds interval) const
	{
		const auto baseCount = baseInterval.count();
		const auto intervalCount = interval.count();
		const auto cycles = (intervalCount + baseCount - 1) / baseCount;

		return cycles > 0 ? static_cast<uint32_t>(cycles - 1) : 0;
	}

	std::chrono::milliseconds baseInterval;
	std::chrono::milliseconds maxInterval;
	std::chrono::milliseconds activeInterval;
	uint32_t skipCyclesRemaining = 0;
	bool ranBestEffortThisCycle = false;
};
