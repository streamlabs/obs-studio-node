#include "polling-pacer.hpp"

#include <chrono>
#include <iostream>
#include <string>

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
	PollingPacer pacer(50ms);

	auto shouldSleep = pacer.finishCycle(20ms);
	expect_true("healthy cycle should sleep", shouldSleep);
	expect_equal("healthy sleep duration", pacer.sleepDuration(), 30ms);
	expect_false("healthy cycle is not congested", pacer.congested());
	expect_equal("healthy cycle resets overruns", pacer.consecutiveOverruns(), 0u);

	shouldSleep = pacer.finishCycle(50ms);
	expect_false("first overrun should not sleep", shouldSleep);
	expect_equal("first overrun has no sleep", pacer.sleepDuration(), 0ms);
	expect_true("first overrun is congested", pacer.congested());
	expect_equal("first overrun count", pacer.consecutiveOverruns(), 1u);

	shouldSleep = pacer.finishCycle(75ms);
	expect_false("second overrun should not sleep", shouldSleep);
	expect_equal("second overrun has no sleep", pacer.sleepDuration(), 0ms);
	expect_equal("second overrun count", pacer.consecutiveOverruns(), 2u);

	shouldSleep = pacer.finishCycle(75ms);
	expect_true("sustained overrun should yield briefly", shouldSleep);
	expect_equal("sustained overrun yields briefly", pacer.sleepDuration(), 1ms);
	expect_equal("third overrun count", pacer.consecutiveOverruns(), 3u);

	shouldSleep = pacer.finishCycle(10ms);
	expect_true("recovery should sleep", shouldSleep);
	expect_equal("recovery sleep duration", pacer.sleepDuration(), 40ms);
	expect_false("recovery is healthy", pacer.congested());
	expect_equal("recovery resets overrun count", pacer.consecutiveOverruns(), 0u);

	return failures == 0 ? 0 : 1;
}
