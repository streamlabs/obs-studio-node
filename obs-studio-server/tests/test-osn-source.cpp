#include <catch2/catch_test_macros.hpp>
#include <mutex>
#include "nodeobs_api.h"
#include "osn-error.hpp"
#include "osn-input.hpp"
#include "osn-source.hpp"
#include <obs.h>
#include "shared.hpp"
#include <string>
#include "obs-setup.hpp"
#include <thread>
#include <vector>

TEST_CASE("Run osn::source tests")
{
	osn::tests::ObsSetup setupOBS;

	SECTION("Get properties of browser source while releasing concurrently does not crash")
	{
		const int iterations = 20;
		std::vector<std::thread> workers;
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

			workers.push_back(std::thread([sourceId, i, &getPropertiesCode]() {
				std::vector<ipc::value> propArgs = {ipc::value(sourceId)};
				std::vector<ipc::value> propResponse;
				osn::Source::GetProperties(nullptr, 0, propArgs, propResponse);
				if (propResponse.size() >= 1) {
					getPropertiesCode[i] = (ErrorCode)propResponse[0].value_union.ui64;
				}
			}));

			workers.push_back(std::thread([sourceId, i, &releaseOk]() {
				std::vector<ipc::value> propArgs = {ipc::value(sourceId)};
				std::vector<ipc::value> propResponse;
				osn::Source::Release(nullptr, 0, propArgs, propResponse);
#if defined(TRIGGER_CRASH)
				// Also release the refcount to trigger actual private data destruction
				obs_source_t *src = osn::Source::Manager::GetInstance().find(sourceId); // may be null already
				if (src)
					obs_source_release(src);
#endif

				// Capture result for checking on the main thread after join.
				if (propResponse.size() >= 1) {
					releaseOk[i] = ((ErrorCode)propResponse[0].value_union.ui64 == ErrorCode::Ok);
				}
			}));
		}

		for (std::thread &worker : workers) {
			if (worker.joinable())
				worker.join();
		}

		// Check release results on the main thread where Catch2 is safe to use.
		for (int i = 0; i < iterations; i++) {
			CHECK(releaseOk[i]);
			// ErrorCode::InvalidReference is possible if the source was deleted before we could acquire the source
			bool expectedErrorCode = getPropertiesCode[i] == ErrorCode::Ok || getPropertiesCode[i] == ErrorCode::InvalidReference;
			CHECK(expectedErrorCode);
		}
	}
}
