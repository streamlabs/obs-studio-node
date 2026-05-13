#include "best-effort-gate.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace std::chrono_literals;

TEST_CASE("BestEffortGate backs off during congestion and resets when healthy")
{
	BestEffortGate gate(50ms, 250ms);
	CHECK(gate.startCycle());
	CHECK(gate.currentInterval() == 50ms);

	gate.finishCycle(true);
	CHECK(gate.currentInterval() == 100ms);
	CHECK_FALSE(gate.startCycle());

	gate.finishCycle(true);
	CHECK(gate.startCycle());

	gate.finishCycle(true);
	CHECK(gate.currentInterval() == 200ms);
	CHECK_FALSE(gate.startCycle());
	gate.finishCycle(true);
	CHECK_FALSE(gate.startCycle());
	gate.finishCycle(true);
	CHECK_FALSE(gate.startCycle());
	gate.finishCycle(true);
	CHECK(gate.startCycle());

	gate.finishCycle(true);
	CHECK(gate.currentInterval() == 250ms);

	gate.finishCycle(false);
	CHECK(gate.startCycle());
	CHECK(gate.currentInterval() == 50ms);
}
