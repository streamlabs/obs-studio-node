#include "best-effort-gate.hpp"
#include "polling-pacer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace std::chrono_literals;

TEST_CASE("PollingPacer congestion drives BestEffortGate backoff and recovery")
{
	PollingPacer pacer(50ms);
	BestEffortGate gate(50ms, 250ms);

	// A healthy cycle leaves the best-effort gate at its base interval.
	auto shouldRunBestEffort = gate.startCycle();
	CHECK(shouldRunBestEffort);
	CHECK(pacer.finishCycle(20ms));
	CHECK_FALSE(pacer.congested());
	gate.finishCycle(pacer.congested());
	CHECK(gate.currentInterval() == 50ms);

	// The first overrun marks the pacer congested and backs the gate off.
	shouldRunBestEffort = gate.startCycle();
	CHECK(shouldRunBestEffort);
	CHECK_FALSE(pacer.finishCycle(80ms));
	CHECK(pacer.congested());
	CHECK(pacer.consecutiveOverruns() == 1u);
	gate.finishCycle(pacer.congested());
	CHECK(gate.currentInterval() == 100ms);

	// A skipped best-effort cycle consumes the gate's skip window without further backoff.
	shouldRunBestEffort = gate.startCycle();
	CHECK_FALSE(shouldRunBestEffort);
	CHECK_FALSE(pacer.finishCycle(80ms));
	CHECK(pacer.congested());
	CHECK(pacer.consecutiveOverruns() == 2u);
	gate.finishCycle(pacer.congested());
	CHECK(gate.currentInterval() == 100ms);

	// When best-effort work is allowed during sustained congestion, both helpers pace more aggressively.
	shouldRunBestEffort = gate.startCycle();
	CHECK(shouldRunBestEffort);
	CHECK(pacer.finishCycle(80ms));
	CHECK(pacer.congested());
	CHECK(pacer.sleepDuration() == 1ms);
	CHECK(pacer.consecutiveOverruns() == 3u);
	gate.finishCycle(pacer.congested());
	CHECK(gate.currentInterval() == 200ms);

	// A healthy cycle after congestion resets the pacer and reopens the gate at the base interval.
	shouldRunBestEffort = gate.startCycle();
	CHECK_FALSE(shouldRunBestEffort);
	CHECK(pacer.finishCycle(10ms));
	CHECK_FALSE(pacer.congested());
	CHECK(pacer.sleepDuration() == 40ms);
	CHECK(pacer.consecutiveOverruns() == 0u);
	gate.finishCycle(pacer.congested());

	CHECK(gate.currentInterval() == 50ms);
	CHECK(gate.startCycle());
}
