#include "best-effort-gate.hpp"

#include <chrono>
#include <iostream>

using namespace std::chrono_literals;

namespace {
int failures = 0;

template<typename T, typename U> void expect_equal(const char *name, const T &actual, const U &expected)
{
	if (actual == expected)
		return;

	std::cerr << name << " failed" << std::endl;
	failures++;
}

void expect_true(const char *name, bool value)
{
	expect_equal(name, value, true);
}

void expect_false(const char *name, bool value)
{
	expect_equal(name, value, false);
}
}

int main()
{
	BestEffortGate gate(50ms, 250ms);
	expect_true("gate starts open", gate.startCycle());
	expect_equal("gate starts at base interval", gate.currentInterval(), 50ms);

	gate.finishCycle(true);
	expect_equal("first congested attempt doubles interval", gate.currentInterval(), 100ms);
	expect_false("gate skips next cycle after 100ms interval", gate.startCycle());

	gate.finishCycle(true);
	expect_true("gate reopens after one skipped cycle", gate.startCycle());

	gate.finishCycle(true);
	expect_equal("second congested attempt doubles interval", gate.currentInterval(), 200ms);
	expect_false("gate skips first 200ms cycle", gate.startCycle());
	gate.finishCycle(true);
	expect_false("gate skips second 200ms cycle", gate.startCycle());
	gate.finishCycle(true);
	expect_false("gate skips third 200ms cycle", gate.startCycle());
	gate.finishCycle(true);
	expect_true("gate reopens after 200ms skip window", gate.startCycle());

	gate.finishCycle(true);
	expect_equal("gate caps interval at max", gate.currentInterval(), 250ms);

	gate.finishCycle(false);
	expect_true("healthy cycle resets gate open", gate.startCycle());
	expect_equal("healthy cycle resets gate interval", gate.currentInterval(), 50ms);

	return failures == 0 ? 0 : 1;
}
