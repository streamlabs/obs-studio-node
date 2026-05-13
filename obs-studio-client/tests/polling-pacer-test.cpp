#include "polling-pacer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace std::chrono_literals;

TEST_CASE("PollingPacer tracks healthy cycles and congestion")
{
	PollingPacer pacer(50ms);

	// A cycle shorter than the interval sleeps for the remaining time and stays healthy.
	auto shouldSleep = pacer.finishCycle(20ms);
	CHECK(shouldSleep);
	CHECK(pacer.sleepDuration() == 30ms);
	CHECK_FALSE(pacer.congested());
	CHECK(pacer.consecutiveOverruns() == 0u);

	// The first interval overrun is treated as congestion, but does not sleep yet.
	shouldSleep = pacer.finishCycle(50ms);
	CHECK_FALSE(shouldSleep);
	CHECK(pacer.sleepDuration() == 0ms);
	CHECK(pacer.congested());
	CHECK(pacer.consecutiveOverruns() == 1u);

	// A second consecutive overrun keeps running immediately to catch up.
	shouldSleep = pacer.finishCycle(75ms);
	CHECK_FALSE(shouldSleep);
	CHECK(pacer.sleepDuration() == 0ms);
	CHECK(pacer.consecutiveOverruns() == 2u);

	// Sustained congestion yields briefly to avoid a tight CPU spin.
	shouldSleep = pacer.finishCycle(75ms);
	CHECK(shouldSleep);
	CHECK(pacer.sleepDuration() == 1ms);
	CHECK(pacer.consecutiveOverruns() == 3u);

	// The next healthy cycle resets congestion state and returns to normal sleeping.
	shouldSleep = pacer.finishCycle(10ms);
	CHECK(shouldSleep);
	CHECK(pacer.sleepDuration() == 40ms);
	CHECK_FALSE(pacer.congested());
	CHECK(pacer.consecutiveOverruns() == 0u);
}
