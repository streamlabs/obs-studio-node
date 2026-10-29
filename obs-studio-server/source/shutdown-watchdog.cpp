#include "shutdown-watchdog.hpp"

#include <memory>

#include <util/base.h>

#ifdef _WIN32
#include <Windows.h>
#endif

osn::ShutdownWatchdog::ShutdownWatchdog(NowFn now, ExpiredFn onExpired, std::chrono::milliseconds pollInterval)
	: m_now(std::move(now)), m_onExpired(std::move(onExpired)), m_pollInterval(pollInterval)
{
}

osn::ShutdownWatchdog::~ShutdownWatchdog()
{
	{
		std::lock_guard lock(m_mtx);
		m_stop = true;
	}
	m_cv.notify_all();
	if (m_thread.joinable())
		m_thread.join();
}

#ifdef _WIN32
namespace {
constexpr UINT WatchdogExitCode = 0x0B5D0001;

void TerminateSelf(const std::string &stage, std::chrono::seconds exceeded)
{
	// The hung thread may hold the log mutex, so log from a helper and give up on it after 1 s.
	auto logged = std::make_shared<std::atomic<bool>>(false);
	std::thread([logged, stage, exceeded] {
		blog(LOG_ERROR, "Shutdown watchdog: %s exceeded %lld s, terminating", stage.c_str(), static_cast<long long>(exceeded.count()));
		*logged = true;
	}).detach();

	for (int i = 0; i < 100 && !*logged; i++)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

	TerminateProcess(GetCurrentProcess(), WatchdogExitCode);
}
} // namespace

#endif

osn::ShutdownWatchdog &osn::ShutdownWatchdog::Instance()
{
#ifdef _WIN32
	static ShutdownWatchdog *instance = new ShutdownWatchdog(&Clock::now, &TerminateSelf);
#else
	// Never armed outside Windows.
	static ShutdownWatchdog *instance = new ShutdownWatchdog(&Clock::now, [](const std::string &, std::chrono::seconds) {});
#endif
	return *instance;
}

void osn::ShutdownWatchdog::Arm(const std::string &stage, std::chrono::seconds budget)
{
	{
		std::lock_guard lock(m_mtx);
		m_armed = true;
		m_stage = stage;
		m_budget = budget;
		m_deadline = m_now() + budget;
		m_cap.reset();
		if (!m_thread.joinable())
			m_thread = std::thread(&ShutdownWatchdog::Run, this);
	}
	m_cv.notify_all();
}

void osn::ShutdownWatchdog::Kick(std::chrono::seconds budget)
{
	std::lock_guard lock(m_mtx);
	if (!m_armed)
		return;
	m_budget = budget;
	m_deadline = m_now() + budget;
}

void osn::ShutdownWatchdog::SetCap(std::chrono::seconds cap)
{
	std::lock_guard lock(m_mtx);
	m_cap = Cap{m_now() + cap, cap};
}

void osn::ShutdownWatchdog::Disarm()
{
	std::lock_guard lock(m_mtx);
	m_armed = false;
}

void osn::ShutdownWatchdog::AddFinalizing(int delta)
{
	std::lock_guard lock(m_mtx);
	m_finalizing += delta;
	// Each output gets a fresh step once it finishes finalizing.
	if (m_finalizing == 0 && delta < 0 && m_armed)
		m_deadline = m_now() + m_budget;
}

bool osn::ShutdownWatchdog::IsExpired(Clock::time_point now, std::chrono::seconds &exceeded) const
{
	if (m_cap && now >= m_cap->at) {
		exceeded = m_cap->limit;
		return true;
	}
	if (now < m_deadline)
		return false;
	if (m_cap && m_finalizing > 0)
		return false;
	exceeded = m_budget;
	return true;
}

void osn::ShutdownWatchdog::Run()
{
	std::unique_lock lock(m_mtx);
	while (!m_stop) {
		m_cv.wait_for(lock, m_pollInterval);
		if (m_stop)
			break;
		std::chrono::seconds exceeded{0};
		if (!m_armed || !IsExpired(m_now(), exceeded))
			continue;

		m_armed = false;
		const std::string stage = m_stage;
		lock.unlock();
		m_onExpired(stage, exceeded);
		lock.lock();
	}
}
