#include "best-effort-gate.hpp"

#include <algorithm>

BestEffortGate::BestEffortGate(std::chrono::milliseconds baseInterval, std::chrono::milliseconds maxInterval)
	: baseInterval(baseInterval > std::chrono::milliseconds(0) ? baseInterval : std::chrono::milliseconds(1)),
	  maxInterval(std::max(maxInterval, this->baseInterval)),
	  activeInterval(this->baseInterval)
{
}

bool BestEffortGate::startCycle()
{
	ranBestEffortThisCycle = skipCyclesRemaining == 0;
	return ranBestEffortThisCycle;
}

std::chrono::milliseconds BestEffortGate::currentInterval() const
{
	return activeInterval;
}

void BestEffortGate::finishCycle(bool congested)
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

uint32_t BestEffortGate::skipCyclesFor(std::chrono::milliseconds interval) const
{
	const auto baseCount = baseInterval.count();
	const auto intervalCount = interval.count();
	const auto cycles = (intervalCount + baseCount - 1) / baseCount;

	return cycles > 0 ? static_cast<uint32_t>(cycles - 1) : 0;
}
