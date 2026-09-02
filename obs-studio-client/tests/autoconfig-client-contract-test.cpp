#include "autoconfig-client-contract.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>

namespace contract = autoConfig::clientContract;

TEST_CASE("AutoConfig client accepts only ordered events for its own session")
{
	const auto valid = contract::validateEvent(R"({"schemaVersion":1,"sessionId":"run","sequence":4,"type":"progress","progress":0.5})", "run", 3);
	CHECK(valid.valid);
	CHECK_FALSE(valid.terminal);
	CHECK(valid.sequence == 4);

	CHECK_FALSE(contract::validateEvent(R"({"schemaVersion":1,"sessionId":"other","sequence":5,"type":"progress","progress":0.5})", "run", 4).valid);
	CHECK_FALSE(contract::validateEvent(R"({"schemaVersion":1,"sessionId":"run","sequence":4,"type":"progress","progress":0.5})", "run", 4).valid);
	CHECK_FALSE(contract::validateEvent(R"({"schemaVersion":1,"sessionId":"run","sequence":5,"type":"unknown","progress":0.5})", "run", 4).valid);
	CHECK_FALSE(contract::validateEvent("{", "run", 4).valid);
}

TEST_CASE("AutoConfig client treats an empty poll as no event")
{
	const auto empty = contract::decodePolledEvent(std::nullopt, "run", 7);
	CHECK_FALSE(empty);

	const auto event = contract::decodePolledEvent(R"({"schemaVersion":1,"sessionId":"run","sequence":8,"type":"progress","progress":0.5})", "run", 7);
	REQUIRE(event);
	CHECK(event->valid);
	CHECK(event->sequence == 8);
}

TEST_CASE("AutoConfig client recognizes terminal events and validates results")
{
	const auto terminal = contract::validateEvent(R"({"schemaVersion":1,"sessionId":"run","sequence":5,"type":"complete","progress":1})", "run", 4);
	CHECK(terminal.valid);
	CHECK(terminal.terminal);

	CHECK(contract::validateResult(R"({"schemaVersion":1,"sessionId":"run","status":"complete","legs":[]})", "run").empty());
	CHECK_FALSE(contract::validateResult(R"({"schemaVersion":1,"sessionId":"other","status":"complete","legs":[]})", "run").empty());
	CHECK_FALSE(contract::validateResult(R"({"schemaVersion":1,"sessionId":"run","status":"unknown","legs":[]})", "run").empty());
	CHECK_FALSE(contract::validateResult("{", "run").empty());
}

TEST_CASE("AutoConfig Close failure leaves exactly one retryable finish state")
{
	contract::RunState state;
	CHECK(state.beginFinish());
	CHECK_FALSE(state.beginFinish());
	state.finishAttempt(false);
	CHECK(state.canRetryClose());
	CHECK(state.beginFinish());
	state.finishAttempt(true);
	CHECK(state.isClosed());
	CHECK_FALSE(state.canRetryClose());
	CHECK_FALSE(state.beginFinish());
}
