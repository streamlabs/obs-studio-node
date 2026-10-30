#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace osn {

namespace shutdown_budget {
constexpr std::chrono::seconds Prepare{15};
constexpr std::chrono::seconds OutputStep{30};
constexpr std::chrono::seconds OutputCap{180};
constexpr std::chrono::seconds Teardown{30};
} // namespace shutdown_budget

// Terminates the process when a shutdown stage outlives its budget. While outputs are
// finalizing (see FinalizeTracker) the deadline is extended up to the cap set by SetCap().
class ShutdownWatchdog {
public:
	using Clock = std::chrono::steady_clock;
	using NowFn = std::function<Clock::time_point()>;
	using ExpiredFn = std::function<void(const std::string &stage, std::chrono::seconds exceeded)>;

	ShutdownWatchdog(NowFn now, ExpiredFn onExpired, std::chrono::milliseconds pollInterval = std::chrono::milliseconds(100));
	~ShutdownWatchdog();

	ShutdownWatchdog(const ShutdownWatchdog &) = delete;
	ShutdownWatchdog &operator=(const ShutdownWatchdog &) = delete;

	// Process-wide instance that terminates the process on expiry (Windows only). Never destroyed.
	static ShutdownWatchdog &Instance();

	// Replaces the current stage and clears any cap.
	void Arm(const std::string &stage, std::chrono::seconds budget);
	void Kick(std::chrono::seconds budget);
	void SetCap(std::chrono::seconds cap);
	void Disarm();

	void AddFinalizing(int delta);

private:
	void Run();
	bool IsExpired(Clock::time_point now, std::chrono::seconds &exceeded) const;

	const NowFn m_now;
	const ExpiredFn m_onExpired;
	const std::chrono::milliseconds m_pollInterval;

	std::mutex m_mtx;
	std::condition_variable m_cv;
	std::thread m_thread;
	bool m_stop = false;

	bool m_armed = false;
	std::string m_stage;
	std::chrono::seconds m_budget{0};
	Clock::time_point m_deadline;

	struct Cap {
		Clock::time_point at;
		std::chrono::seconds limit;
	};
	std::optional<Cap> m_cap;

	int m_finalizing = 0;
};

// Counts one output as finalizing between Begin() and End(). End() without a Begin() is a no-op.
class FinalizeTracker {
public:
	explicit FinalizeTracker(ShutdownWatchdog &watchdog) : m_watchdog(watchdog) {}
	~FinalizeTracker() { End(); }

	void Begin()
	{
		if (!m_active.exchange(true))
			m_watchdog.AddFinalizing(1);
	}

	void End()
	{
		if (m_active.exchange(false))
			m_watchdog.AddFinalizing(-1);
	}

private:
	ShutdownWatchdog &m_watchdog;
	std::atomic<bool> m_active{false};
};

} // namespace osn
