#include "shutdown-watchdog.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>

namespace {

using namespace std::chrono_literals;

struct FakeClock {
	std::atomic<long long> seconds{0};

	osn::ShutdownWatchdog::Clock::time_point Now() const { return osn::ShutdownWatchdog::Clock::time_point(std::chrono::seconds(seconds.load())); }
};

struct Fixture {
	FakeClock clock;
	std::atomic<int> expiredCount{0};
	std::mutex mtx;
	std::string expiredStage;
	std::chrono::seconds expiredAfter{0};

	osn::ShutdownWatchdog watchdog{[this] { return clock.Now(); },
				       [this](const std::string &stage, std::chrono::seconds exceeded) {
					       std::lock_guard lock(mtx);
					       expiredStage = stage;
					       expiredAfter = exceeded;
					       expiredCount++;
				       },
				       1ms};

	// Advances the fake clock and gives the polling thread time to observe it.
	void Advance(long long secs)
	{
		clock.seconds += secs;
		std::this_thread::sleep_for(50ms);
	}
};

} // namespace

TEST_CASE("Watchdog fires after the budget and names the stage", "[shutdown-watchdog]")
{
	Fixture f;
	f.watchdog.Arm("Teardown", 30s);

	f.Advance(29);
	REQUIRE(f.expiredCount == 0);

	f.Advance(2);
	REQUIRE(f.expiredCount == 1);
	REQUIRE(f.expiredStage == "Teardown");
	REQUIRE(f.expiredAfter == 30s);
}

TEST_CASE("Watchdog fires once", "[shutdown-watchdog]")
{
	Fixture f;
	f.watchdog.Arm("Prepare", 15s);
	f.Advance(16);
	f.Advance(100);
	REQUIRE(f.expiredCount == 1);
}

TEST_CASE("Kick re-arms the deadline", "[shutdown-watchdog]")
{
	Fixture f;
	f.watchdog.Arm("Outputs", 30s);

	f.Advance(20);
	f.watchdog.Kick(30s);
	f.Advance(20);
	REQUIRE(f.expiredCount == 0);

	f.Advance(11);
	REQUIRE(f.expiredCount == 1);
}

TEST_CASE("Disarm and Kick on a disarmed watchdog never fire", "[shutdown-watchdog]")
{
	Fixture f;
	f.watchdog.Kick(1s);
	f.Advance(10);
	REQUIRE(f.expiredCount == 0);

	f.watchdog.Arm("Outputs", 30s);
	f.watchdog.Disarm();
	f.Advance(100);
	REQUIRE(f.expiredCount == 0);
}

TEST_CASE("Finalizing outputs extend the deadline up to the cap and no further", "[shutdown-watchdog]")
{
	Fixture f;
	f.watchdog.Arm("Outputs", 30s);
	f.watchdog.SetCap(180s);
	f.watchdog.AddFinalizing(1);

	f.Advance(100);
	REQUIRE(f.expiredCount == 0);
	f.Advance(79);
	REQUIRE(f.expiredCount == 0);

	f.Advance(2);
	REQUIRE(f.expiredCount == 1);
	REQUIRE(f.expiredStage == "Outputs");
	REQUIRE(f.expiredAfter == 180s);
}

TEST_CASE("Finishing a finalize starts a fresh step", "[shutdown-watchdog]")
{
	Fixture f;
	f.watchdog.Arm("Outputs", 30s);
	f.watchdog.SetCap(180s);
	f.watchdog.AddFinalizing(1);

	f.Advance(100);
	f.watchdog.AddFinalizing(-1);
	f.Advance(29);
	REQUIRE(f.expiredCount == 0);

	f.Advance(2);
	REQUIRE(f.expiredCount == 1);
	REQUIRE(f.expiredAfter == 30s);
}

TEST_CASE("Finalizing does not extend a stage without a cap", "[shutdown-watchdog]")
{
	Fixture f;
	f.watchdog.Arm("Teardown", 30s);
	f.watchdog.AddFinalizing(1);

	f.Advance(31);
	REQUIRE(f.expiredCount == 1);
}

TEST_CASE("Arm clears the cap of the previous stage", "[shutdown-watchdog]")
{
	Fixture f;
	f.watchdog.Arm("Outputs", 30s);
	f.watchdog.SetCap(180s);
	f.watchdog.Arm("Teardown", 30s);
	f.watchdog.AddFinalizing(1);

	f.Advance(31);
	REQUIRE(f.expiredCount == 1);
	REQUIRE(f.expiredStage == "Teardown");
}

TEST_CASE("FinalizeTracker ignores End without Begin", "[shutdown-watchdog]")
{
	Fixture f;
	f.watchdog.Arm("Outputs", 30s);
	f.watchdog.SetCap(180s);

	osn::FinalizeTracker stopWithoutStopping(f.watchdog);
	stopWithoutStopping.End();

	// A negative count would wrongly keep this finalize from extending the deadline.
	osn::FinalizeTracker tracker(f.watchdog);
	tracker.Begin();
	f.Advance(100);
	REQUIRE(f.expiredCount == 0);

	tracker.End();
	tracker.End();
	tracker.Begin();
	f.Advance(50);
	REQUIRE(f.expiredCount == 0);
}

TEST_CASE("FinalizeTracker counts Begin once and releases on destruction", "[shutdown-watchdog]")
{
	Fixture f;
	f.watchdog.Arm("Outputs", 30s);
	f.watchdog.SetCap(180s);

	{
		osn::FinalizeTracker tracker(f.watchdog);
		tracker.Begin();
		tracker.Begin();
		f.Advance(100);
		REQUIRE(f.expiredCount == 0);
	}

	f.Advance(31);
	REQUIRE(f.expiredCount == 1);
	REQUIRE(f.expiredAfter == 30s);
}
