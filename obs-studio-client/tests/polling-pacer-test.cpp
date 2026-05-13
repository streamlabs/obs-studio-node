#include "polling-pacer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace std::chrono_literals;

TEST_CASE("PollingPacer tracks healthy cycles and congestion")
{
	PollingPacer pacer(50ms);

	auto shouldSleep = pacer.finishCycle(20ms);
	CHECK(shouldSleep);
	CHECK(pacer.sleepDuration() == 30ms);
	CHECK_FALSE(pacer.congested());
	CHECK(pacer.consecutiveOverruns() == 0u);

	shouldSleep = pacer.finishCycle(50ms);
	CHECK_FALSE(shouldSleep);
	CHECK(pacer.sleepDuration() == 0ms);
	CHECK(pacer.congested());
	CHECK(pacer.consecutiveOverruns() == 1u);

	shouldSleep = pacer.finishCycle(75ms);
	CHECK_FALSE(shouldSleep);
	CHECK(pacer.sleepDuration() == 0ms);
	CHECK(pacer.consecutiveOverruns() == 2u);

	shouldSleep = pacer.finishCycle(75ms);
	CHECK(shouldSleep);
	CHECK(pacer.sleepDuration() == 1ms);
	CHECK(pacer.consecutiveOverruns() == 3u);

	shouldSleep = pacer.finishCycle(10ms);
	CHECK(shouldSleep);
	CHECK(pacer.sleepDuration() == 40ms);
	CHECK_FALSE(pacer.congested());
	CHECK(pacer.consecutiveOverruns() == 0u);
}
