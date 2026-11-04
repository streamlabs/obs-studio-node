#pragma once

#include <chrono>
#include <optional>

namespace osn {

namespace orphan_budget {
constexpr std::chrono::seconds CrashHandlerGrace{5};
} // namespace orphan_budget

// Decides when an obs64 that lost all its clients may stop waiting for the crash-handler's exit message.
class OrphanPolicy {
public:
	using Clock = std::chrono::steady_clock;

	enum class Verdict { Wait, Acknowledged, NoLiveOutputs, CrashHandlerGone };

	struct Status {
		bool acknowledged = false;
		bool outputsLive = false;
		bool crashHandlerAlive = false;
	};

	explicit OrphanPolicy(std::chrono::seconds grace) : m_grace(grace) {}

	Verdict Evaluate(const Status &status, Clock::time_point now)
	{
		if (status.acknowledged)
			return Verdict::Acknowledged;

		if (!status.outputsLive)
			return Verdict::NoLiveOutputs;

		if (status.crashHandlerAlive) {
			m_goneSince.reset();
			return Verdict::Wait;
		}

		if (!m_goneSince)
			m_goneSince = now;

		return now - *m_goneSince >= m_grace ? Verdict::CrashHandlerGone : Verdict::Wait;
	}

private:
	const std::chrono::seconds m_grace;
	std::optional<Clock::time_point> m_goneSince;
};

} // namespace osn
