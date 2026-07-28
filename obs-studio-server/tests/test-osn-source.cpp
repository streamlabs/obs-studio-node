#include <catch2/catch_test_macros.hpp>
#include "nodeobs_api.h"
#include "osn-error.hpp"
#include "osn-input.hpp"
#include "osn-source.hpp"
#include <obs.h>
#include "shared.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include "obs-setup.hpp"
#include <thread>
#include <utility>
#include <vector>

// Since we do not use C++ 20 (std::jthread), defining a scoped thread.
struct joining_thread {
	std::thread t;
	explicit joining_thread(std::thread t_) : t(std::move(t_)) {}
	joining_thread(joining_thread &&) = default;
	joining_thread &operator=(joining_thread &&) = default;
	joining_thread(const joining_thread &) = delete;
	joining_thread &operator=(const joining_thread &) = delete;
	~joining_thread()
	{
		if (t.joinable())
			t.join();
	}
};

static bool wait_for_source_manager_size(std::size_t expectedSize)
{
	for (int i = 0; i < 100; i++) {
		obs_wait_for_destroy_queue();

		if (osn::Source::Manager::GetInstance().size() == expectedSize)
			return true;

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	return false;
}

TEST_CASE("Run osn::source tests")
{
	osn::tests::ObsSetup setupOBS;

	SECTION("Get properties of browser source while releasing concurrently does not crash")
	{
		auto sourceCount = osn::Source::Manager::GetInstance().size();
		const int iterations = 20;
		std::vector<joining_thread> workers;
		std::vector<uint8_t> releaseOk(iterations, 0);
		std::vector<ErrorCode> getPropertiesCode(iterations, ErrorCode::Error);

		for (int i = 0; i < iterations; i++) {
			const std::string sourceName = "test-input-" + std::to_string(i);
			std::vector<ipc::value> args = {ipc::value("browser_source"), ipc::value(sourceName)};
			std::vector<ipc::value> response;

			osn::Input::Create(nullptr, 0, args, response);
			REQUIRE(response.size() >= 2);
			ErrorCode error = (ErrorCode)response[0].value_union.ui64;
			REQUIRE(error == ErrorCode::Ok);

			uint64_t sourceId = response[1].value_union.ui64;

			workers.push_back(joining_thread(std::thread([sourceId, i, &getPropertiesCode]() {
				std::vector<ipc::value> propArgs = {ipc::value(sourceId)};
				std::vector<ipc::value> propResponse;
				osn::Source::GetProperties(nullptr, 0, propArgs, propResponse);
				if (propResponse.size() >= 1) {
					getPropertiesCode[i] = (ErrorCode)propResponse[0].value_union.ui64;
				}
			})));

			workers.push_back(joining_thread(std::thread([sourceId, i, &releaseOk]() {
				std::vector<ipc::value> propArgs = {ipc::value(sourceId)};
				std::vector<ipc::value> propResponse;
				osn::Source::Release(nullptr, 0, propArgs, propResponse);
				// Capture result for checking on the main thread after join.
				if (propResponse.size() >= 1) {
					releaseOk[i] = ((ErrorCode)propResponse[0].value_union.ui64 == ErrorCode::Ok);
				}
			})));
		}

		workers.clear();
		// Check release results on the main thread where Catch2 is safe to use.
		for (int i = 0; i < iterations; i++) {
			CHECK(releaseOk[i]);
			// ErrorCode::InvalidReference is possible if the source was deleted before we could acquire the source
			bool expectedErrorCode = getPropertiesCode[i] == ErrorCode::Ok || getPropertiesCode[i] == ErrorCode::InvalidReference;
			CHECK(expectedErrorCode);
		}

		CHECK(wait_for_source_manager_size(sourceCount)); // Check to see if all objects released.
	}
}
