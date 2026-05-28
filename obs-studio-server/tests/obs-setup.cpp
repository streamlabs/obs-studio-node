#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include "nodeobs_api.h"
#include "osn-error.hpp"
#include <obs.h>
#include "shared.hpp"
#include <string>
#include "obs-setup.hpp"
#include <vector>

namespace osn::tests {

void setWorkingFolder(const std::string &wd)
{
	std::vector<ipc::value> args = {ipc::value(wd)};
	std::vector<ipc::value> response;
	OBS_API::SetWorkingDirectory(nullptr, 0, args, response);
	REQUIRE(response.size() >= 2);
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
	REQUIRE(response.size() >= 2);
	ErrorCode error = (ErrorCode)response[0].value_union.ui64;
	CHECK(error == ErrorCode::Ok);
}

ObsSetup::ObsSetup()
{
	setWorkingFolder(OSN_TEST_WD);
	setupApi();
}

ObsSetup::~ObsSetup()
{
	std::vector<ipc::value> args = {};
	std::vector<ipc::value> response;
	OBS_API::OBS_API_destroyOBS_API(nullptr, 0, args, response);
	REQUIRE(response.size() >= 1);
	ErrorCode error = (ErrorCode)response[0].value_union.ui64;
	CHECK(error == ErrorCode::Ok);
}

} // namespace osn::tests
