#include <catch2/catch_test_macros.hpp>

#include "nodeobs_api.h"
#include "nodeobs_configManager.hpp"
#include "nodeobs_settings.h"
#include "obs-setup.hpp"
#include "osn-error.hpp"

#include <cstring>
#include <filesystem>

// Exercise Advanced Settings with its native metadata, without resetting video
// or duplicating the IPC serialization in this preference regression.
class OBSSettingsTestAccess {
public:
	static void saveAdvancedMediaCaching(bool enabled)
	{
		auto settings = OBS_settings::getAdvancedSettings();
		for (auto &category : settings) {
			for (auto &parameter : category.params) {
				if (parameter.name != "fileCaching")
					continue;
				REQUIRE(parameter.currentValue.size() == sizeof(enabled));
				std::memcpy(parameter.currentValue.data(), &enabled, sizeof(enabled));
				OBS_settings::saveAdvancedSettings(settings);
				return;
			}
		}
		FAIL("Advanced Settings did not expose fileCaching");
	}
};

namespace {

// Restore the saved preference, including the absence of an explicit override.
// Resolve the configuration again because the startup test reloads it from disk.
class ScopedMediaCachingPreference {
public:
	ScopedMediaCachingPreference()
	{
		auto *config = ConfigManager::getInstance().getGlobal();
		hadUserValue = config_has_user_value(config, "General", "fileCaching");
		value = config_get_bool(config, "General", "fileCaching");
	}
	~ScopedMediaCachingPreference()
	{
		std::vector<ipc::value> response;
		OBS_API::SetMediaFileCaching(nullptr, 0, {ipc::value(static_cast<uint32_t>(value))}, response);
		if (!hadUserValue) {
			auto *config = ConfigManager::getInstance().getGlobal();
			config_remove_value(config, "General", "fileCaching");
			config_save_safe(config, "tmp", nullptr);
		}
	}

private:
	bool hadUserValue;
	bool value;
};

void checkMediaCachingPreference(bool expected)
{
	CHECK(config_get_bool(ConfigManager::getInstance().getGlobal(), "General", "fileCaching") == expected);
	CHECK(OBS_API::getMediaFileCaching() == expected);
	std::vector<ipc::value> response;
	OBS_API::GetMediaFileCaching(nullptr, 0, {}, response);
	REQUIRE(response.size() == 2);
	CHECK(static_cast<ErrorCode>(response[0].value_union.ui64) == ErrorCode::Ok);
	CHECK(static_cast<bool>(response[1].value_union.ui32) == expected);
}

} // namespace

TEST_CASE("Media caching preferences stay synchronized across settings APIs", "[media-cache][settings]")
{
	osn::tests::ObsSetup setup;
	ScopedMediaCachingPreference restorePreference;
	for (bool enabled : {false, true, false}) {
		INFO("Advanced fileCaching = " << enabled);
		// Seed the opposite value through the dedicated API. Then exercise both
		// directions through the Advanced Settings save path that missed the flag.
		std::vector<ipc::value> response;
		OBS_API::SetMediaFileCaching(nullptr, 0, {ipc::value(static_cast<uint32_t>(!enabled))}, response);
		REQUIRE(response.size() == 1);
		CHECK(static_cast<ErrorCode>(response[0].value_union.ui64) == ErrorCode::Ok);
		checkMediaCachingPreference(!enabled);
		OBSSettingsTestAccess::saveAdvancedMediaCaching(enabled);
		checkMediaCachingPreference(enabled);
	}
}

TEST_CASE("Media caching honors the saved preference on startup", "[media-cache][settings]")
{
	const std::string configPath = std::string(OSN_SOURCE_DIR) + "/tests/osn-tests/osnData/slobs-client";
	std::filesystem::create_directories(configPath);
	auto &configs = ConfigManager::getInstance();
	configs.setAppdataPath(configPath);
	ScopedMediaCachingPreference restorePreference;
	config_set_bool(configs.getGlobal(), "General", "fileCaching", false);
	REQUIRE(config_save_safe(configs.getGlobal(), "tmp", nullptr) == CONFIG_SUCCESS);
	configs.reloadConfig();

	// The default runtime flag is true. Startup must read the saved false value
	// without requiring the frontend to call SetMediaFileCaching afterward.
	osn::tests::ObsSetup setup;
	checkMediaCachingPreference(false);
}
