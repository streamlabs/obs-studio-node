#include "polling-pacer.hpp"

PollingPacer::PollingPacer(std::chrono::milliseconds interval, uint32_t yieldAfterConsecutiveOverruns, std::chrono::milliseconds sustainedOverrunYield)
	: interval(interval), yieldAfterConsecutiveOverruns(yieldAfterConsecutiveOverruns), sustainedOverrunYield(sustainedOverrunYield)
{
}

bool PollingPacer::finishCycle(std::chrono::milliseconds duration)
{
	if (duration < interval) {
		lastConsecutiveOverruns = 0;
		lastSleepDuration = interval - duration;
		lastCongested = false;
		return true;
	}

	lastConsecutiveOverruns++;
	lastSleepDuration = lastConsecutiveOverruns > yieldAfterConsecutiveOverruns ? sustainedOverrunYield : std::chrono::milliseconds(0);
	lastCongested = true;

	return lastSleepDuration > std::chrono::milliseconds(0);
}

std::chrono::milliseconds PollingPacer::sleepDuration() const
{
	return lastSleepDuration;
}

bool PollingPacer::congested() const
{
	return lastCongested;
}

uint32_t PollingPacer::consecutiveOverruns() const
{
	return lastConsecutiveOverruns;
}
