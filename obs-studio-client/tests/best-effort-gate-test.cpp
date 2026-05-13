#include "best-effort-gate.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace std::chrono_literals;

TEST_CASE("BestEffortGate backs off during congestion and resets when healthy")
{
	BestEffortGate gate(50ms, 250ms);

	// The gate starts open at its base interval.
	CHECK(gate.startCycle());
	CHECK(gate.currentInterval() == 50ms);

	// A congested best-effort run doubles the interval and schedules one skipped cycle.
	gate.finishCycle(true);
	CHECK(gate.currentInterval() == 100ms);
	CHECK_FALSE(gate.startCycle());

	// The skipped cycle is consumed without increasing the interval again.
	gate.finishCycle(true);
	CHECK(gate.startCycle());

	// The next congested best-effort run doubles the interval and creates a longer skip window.
	gate.finishCycle(true);
	CHECK(gate.currentInterval() == 200ms);
	CHECK_FALSE(gate.startCycle());
	gate.finishCycle(true);

	// The gate remains closed until the 200ms skip window is fully consumed.
	CHECK_FALSE(gate.startCycle());
	gate.finishCycle(true);
	CHECK_FALSE(gate.startCycle());
	gate.finishCycle(true);
	CHECK(gate.startCycle());

	// Further congestion caps the interval at the configured maximum.
	gate.finishCycle(true);
	CHECK(gate.currentInterval() == 250ms);

	// A healthy cycle clears backoff and reopens the gate at its base interval.
	gate.finishCycle(false);
	CHECK(gate.startCycle());
	CHECK(gate.currentInterval() == 50ms);
}
