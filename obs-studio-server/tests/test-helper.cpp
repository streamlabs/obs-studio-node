#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include "nodeobs_api.h"
#include "osn-error.hpp"
#include <obs.h>
#include "shared.hpp"
#include <string>
#include "test-helper.hpp"
#include <vector>

namespace osn::tests {

// Reads the `wd` export from tests/osn-tests/osn/index.ts.
// The line has the form: const wd: string = "...";
static std::string parseWdFromIndexTs()
{
	const std::string indexTsPath = std::string(OSN_SOURCE_DIR) + "/tests/osn-tests/osn/index.ts";

	std::ifstream file(indexTsPath);
	INFO("Could not open " + indexTsPath);
	REQUIRE(file.is_open());

	std::string line;
	while (std::getline(file, line)) {
		const std::string prefix = "const wd";
		if (line.find(prefix) == std::string::npos)
			continue;

		auto first = line.find('"');
		auto last = line.rfind('"');
		INFO("Could not parse wd value from: " + line);
		REQUIRE(first != std::string::npos);
		REQUIRE(last != first);

		return line.substr(first + 1, last - first - 1);
	}

	FAIL("wd declaration not found in " + indexTsPath);
	return {};
}

void setWorkingFolder(const std::string &wd)
{
	std::vector<ipc::value> args = {ipc::value(wd)};
	std::vector<ipc::value> response;
	OBS_API::SetWorkingDirectory(nullptr, 0, args, response);
	CHECK(response.size() >= 2);
	ErrorCode error = (ErrorCode)response[0].value_union.ui64;
	CHECK(error == ErrorCode::Ok);
}

void setupApi()
{
#if defined(__APPLE__)
	g_util_osx = new UtilInt();
	g_util_osx->init();
	// Workaround normal app startup where "browser_source" plugin is initialized
	CHECK(!g_util_osx->hasInitApi());
	g_util_osx->nextState();
	CHECK(g_util_osx->hasInitApi());
#endif
	const std::string appPath = std::string(OSN_SOURCE_DIR) + "/tests/osn-tests/osnData/slobs-client";
	// osn.NodeObs.OBS_API_initAPI(this.language, this.obsPath, this.version, this.crashServer);
	std::vector<ipc::value> args = {ipc::value(appPath), ipc::value("en-US"), ipc::value("0.00.00-preview.0"), ipc::value("")};
	std::vector<ipc::value> response;
	OBS_API::OBS_API_initAPI(nullptr, 0, args, response);
	CHECK(response.size() >= 2);
	ErrorCode error = (ErrorCode)response[0].value_union.ui64;
	CHECK(error == ErrorCode::Ok);
}

void TestHelper::initializeOBS()
{
	const std::string wd = parseWdFromIndexTs();
	setWorkingFolder(wd);
	setupApi();
}

void TestHelper::finalizeOBS()
{
	std::vector<ipc::value> args = {};
	std::vector<ipc::value> response;
	OBS_API::OBS_API_destroyOBS_API(nullptr, 0, args, response);
	CHECK(response.size() >= 1);
	ErrorCode error = (ErrorCode)response[0].value_union.ui64;
	CHECK(error == ErrorCode::Ok);
}

} // namespace osn::tests
