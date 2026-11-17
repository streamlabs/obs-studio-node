#include "orphan-policy.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

using namespace std::chrono_literals;
using Verdict = osn::OrphanPolicy::Verdict;
using Status = osn::OrphanPolicy::Status;

osn::OrphanPolicy::Clock::time_point At(long long seconds)
{
	return osn::OrphanPolicy::Clock::time_point(std::chrono::seconds(seconds));
}

Status Live(bool crashHandlerAlive)
{
	return {false, true, crashHandlerAlive};
}

} // namespace

TEST_CASE("Orphan with live outputs waits while the crash-handler is alive", "[orphan-policy]")
{
	osn::OrphanPolicy policy(5s);

	REQUIRE(policy.Evaluate(Live(true), At(0)) == Verdict::Wait);
	REQUIRE(policy.Evaluate(Live(true), At(3600)) == Verdict::Wait);
}

TEST_CASE("Orphan without live outputs does not wait", "[orphan-policy]")
{
	osn::OrphanPolicy policy(5s);

	REQUIRE(policy.Evaluate({false, false, true}, At(0)) == Verdict::NoLiveOutputs);
	REQUIRE(policy.Evaluate({false, false, false}, At(0)) == Verdict::NoLiveOutputs);
}

TEST_CASE("Outputs ending on their own while waiting ends the wait", "[orphan-policy]")
{
	osn::OrphanPolicy policy(5s);

	REQUIRE(policy.Evaluate(Live(true), At(0)) == Verdict::Wait);
	REQUIRE(policy.Evaluate({false, false, true}, At(10)) == Verdict::NoLiveOutputs);
}

TEST_CASE("Exit message from the crash-handler ends the wait", "[orphan-policy]")
{
	osn::OrphanPolicy policy(5s);

	REQUIRE(policy.Evaluate(Live(true), At(0)) == Verdict::Wait);
	REQUIRE(policy.Evaluate({true, true, true}, At(1)) == Verdict::Acknowledged);
}

TEST_CASE("Crash-handler death proceeds only after the grace period", "[orphan-policy]")
{
	osn::OrphanPolicy policy(5s);

	REQUIRE(policy.Evaluate(Live(true), At(0)) == Verdict::Wait);
	REQUIRE(policy.Evaluate(Live(false), At(10)) == Verdict::Wait);
	REQUIRE(policy.Evaluate(Live(false), At(14)) == Verdict::Wait);
	REQUIRE(policy.Evaluate(Live(false), At(15)) == Verdict::CrashHandlerGone);
}

TEST_CASE("No crash-handler at all counts from the first evaluation", "[orphan-policy]")
{
	osn::OrphanPolicy policy(5s);

	REQUIRE(policy.Evaluate(Live(false), At(100)) == Verdict::Wait);
	REQUIRE(policy.Evaluate(Live(false), At(104)) == Verdict::Wait);
	REQUIRE(policy.Evaluate(Live(false), At(105)) == Verdict::CrashHandlerGone);
}

TEST_CASE("Outputs ending during the grace period proceed without the grace", "[orphan-policy]")
{
	osn::OrphanPolicy policy(5s);

	REQUIRE(policy.Evaluate(Live(false), At(0)) == Verdict::Wait);
	REQUIRE(policy.Evaluate({false, false, false}, At(1)) == Verdict::NoLiveOutputs);
}

TEST_CASE("Exit message wins over a dead crash-handler", "[orphan-policy]")
{
	osn::OrphanPolicy policy(5s);

	REQUIRE(policy.Evaluate({true, true, false}, At(100)) == Verdict::Acknowledged);
}
