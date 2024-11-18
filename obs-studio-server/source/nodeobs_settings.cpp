/******************************************************************************
    Copyright (C) 2016-2019 by Streamlabs (General Workings Inc)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

******************************************************************************/

#include "nodeobs_settings.h"
#include "osn-error.hpp"
#include "nodeobs_api.h"
#include "shared.hpp"
#include "memory-manager.h"
#include "osn-video.hpp"

#ifdef WIN32
#include <windows.h>
#include "strmif.h"
#include "uuids.h"
#include "util/windows/ComPtr.hpp"
#include "util/windows/CoTaskMemPtr.hpp"
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#endif

#ifdef __APPLE__
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include <unordered_set>
#include <unordered_map>

std::vector<const char *> tabStreamTypes;
const char *currentServiceName;
std::vector<SubCategory> currentAudioSettings;

bool update_nvenc_presets(obs_data_t *data, const char *encoderId);
const char *convert_nvenc_simple_preset(const char *old_preset);

/* some nice default output resolution vals */
static const double vals[] = {1.0, 1.25, (1.0 / 0.75), 1.5, (1.0 / 0.6), 1.75, 2.0, 2.25, 2.5, 2.75, 3.0};

static const size_t numVals = sizeof(vals) / sizeof(double);

static std::string ResString(uint64_t cx, uint64_t cy)
{
	std::ostringstream res;
	res << cx << "x" << cy;
	return res.str();
}

OBS_settings::OBS_settings() {}
OBS_settings::~OBS_settings() {}

void OBS_settings::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("Settings");

	cls->register_function(
		std::make_shared<ipc::function>("OBS_settings_getSettings", std::vector<ipc::type>{ipc::type::String}, OBS_settings_getSettings));
	cls->register_function(std::make_shared<ipc::function>(
		"OBS_settings_saveSettings", std::vector<ipc::type>{ipc::type::String, ipc::type::UInt32, ipc::type::UInt32, ipc::type::Binary},
		OBS_settings_saveSettings));
	cls->register_function(
		std::make_shared<ipc::function>("OBS_settings_getInputAudioDevices", std::vector<ipc::type>{}, OBS_settings_getInputAudioDevices));
	cls->register_function(
		std::make_shared<ipc::function>("OBS_settings_getOutputAudioDevices", std::vector<ipc::type>{}, OBS_settings_getOutputAudioDevices));
	cls->register_function(std::make_shared<ipc::function>("OBS_settings_getVideoDevices", std::vector<ipc::type>{}, OBS_settings_getVideoDevices));

	srv.register_collection(cls);
}

void OBS_settings::OBS_settings_getSettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	std::string nameCategory = args[0].value_str;
	CategoryTypes type = NODEOBS_CATEGORY_LIST;
	std::vector<SubCategory> settings = getSettings(nameCategory, type);
	std::vector<char> binaryValue;

	for (int i = 0; i < settings.size(); i++) {
		std::vector<char> serializedBuf = settings.at(i).serialize();
		binaryValue.insert(binaryValue.end(), serializedBuf.begin(), serializedBuf.end());
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint64_t)settings.size()));
	rval.push_back(ipc::value((uint64_t)binaryValue.size()));
	rval.push_back(ipc::value(binaryValue));
	rval.push_back(ipc::value(type));
	AUTO_DEBUG;
}

void OBS_settings::UpdateAudioSettings(bool saveOnlyIfLimitApplied)
{
	// Do nothing if there is no info
	if (currentAudioSettings.size() == 0)
		return;

	auto currentChannelSetup = std::string(std::string(config_get_string(ConfigManager::getInstance().getBasic(), "Audio", "ChannelSetup")));
	bool isSurround = IsSurround(currentChannelSetup.c_str());

	bool limitApplied = false;
	for (auto &settings : currentAudioSettings) {
		if (settings.paramsCount != 2)
			continue;

		if (settings.params[0].name.compare("Track1Bitrate") == 0 || settings.params[0].name.compare("Track2Bitrate") == 0 ||
		    settings.params[0].name.compare("Track3Bitrate") == 0 || settings.params[0].name.compare("Track4Bitrate") == 0 ||
		    settings.params[0].name.compare("Track5Bitrate") == 0 || settings.params[0].name.compare("Track6Bitrate") == 0) {
			std::string valueStr(settings.params[0].currentValue.begin(), settings.params[0].currentValue.end());

			int value = std::atoi(valueStr.c_str());

			// Limit the value if not surround
			if (!isSurround && value > 320) {
				auto maxValue = std::to_string(320);
				std::vector<char> data(maxValue.begin(), maxValue.end());
				settings.params[0].currentValue = data;
				limitApplied = true;
			}
		}
	}

	if ((!saveOnlyIfLimitApplied || (saveOnlyIfLimitApplied && limitApplied))) {
		OBS_settings::saveGenericSettings(currentAudioSettings, "AdvOut", ConfigManager::getInstance().getBasic());
	}
}

void addEntry(std::vector<std::pair<std::string, ipc::value>> &entryVector, const std::string &name, const ipc::value &value)
{
	entryVector.push_back({name, value});
}

std::vector<std::pair<std::string, ipc::value>> createSettingEntry(const std::string &name, const std::string &type, const std::string &description,
								   const std::string &subType = "", double minVal = 0, double maxVal = 0, double stepVal = 1)
{

	std::vector<std::pair<std::string, ipc::value>> entry;
	addEntry(entry, "name", name);
	addEntry(entry, "type", type);
	addEntry(entry, "description", description);
	addEntry(entry, "subType", subType);
	addEntry(entry, "minVal", minVal);
	addEntry(entry, "maxVal", maxVal);
	addEntry(entry, "stepVal", stepVal);
	return entry;
}

std::vector<SubCategory> serializeCategory(uint32_t subCategoriesCount, uint32_t sizeStruct, std::vector<char> buffer)
{
	std::vector<SubCategory> category;

	size_t indexData = 0;
	for (uint32_t i = 0; i < subCategoriesCount; i++) {
		SubCategory sc;

		uint64_t *sizeMessage = reinterpret_cast<uint64_t *>(buffer.data() + indexData);
		indexData += sizeof(uint64_t);

		std::string name(buffer.data() + indexData, *sizeMessage);
		indexData += *sizeMessage;

		uint32_t *paramsCount = reinterpret_cast<uint32_t *>(buffer.data() + indexData);
		indexData += sizeof(uint32_t);

		Parameter param;
		for (int j = 0; j < *paramsCount; j++) {
			uint64_t *sizeName = reinterpret_cast<std::uint64_t *>(buffer.data() + indexData);
			indexData += sizeof(uint64_t);

			std::string name(buffer.data() + indexData, *sizeName);
			indexData += *sizeName;

			uint64_t *sizeDescription = reinterpret_cast<uint64_t *>(buffer.data() + indexData);
			indexData += sizeof(uint64_t);

			std::string description(buffer.data() + indexData, *sizeDescription);
			indexData += *sizeDescription;

			uint64_t *sizeType = reinterpret_cast<uint64_t *>(buffer.data() + indexData);
			indexData += sizeof(uint64_t);

			std::string type(buffer.data() + indexData, *sizeType);
			indexData += *sizeType;

			uint64_t *sizeSubType = reinterpret_cast<uint64_t *>(buffer.data() + indexData);
			indexData += sizeof(uint64_t);

			std::string subType(buffer.data() + indexData, *sizeSubType);
			indexData += *sizeSubType;

			bool *enabled = reinterpret_cast<bool *>(buffer.data() + indexData);
			indexData += sizeof(bool);

			bool *masked = reinterpret_cast<bool *>(buffer.data() + indexData);
			indexData += sizeof(bool);

			bool *visible = reinterpret_cast<bool *>(buffer.data() + indexData);
			indexData += sizeof(bool);

			double *minVal = reinterpret_cast<double *>(buffer.data() + indexData);
			indexData += sizeof(double);

			double *maxVal = reinterpret_cast<double *>(buffer.data() + indexData);
			indexData += sizeof(double);

			double *stepVal = reinterpret_cast<double *>(buffer.data() + indexData);
			indexData += sizeof(double);

			uint64_t *sizeOfCurrentValue = reinterpret_cast<uint64_t *>(buffer.data() + indexData);
			indexData += sizeof(uint64_t);

			std::vector<char> currentValue;
			currentValue.resize(*sizeOfCurrentValue);
			memcpy(currentValue.data(), buffer.data() + indexData, *sizeOfCurrentValue);
			indexData += *sizeOfCurrentValue;

			uint64_t *sizeOfValues = reinterpret_cast<uint64_t *>(buffer.data() + indexData);
			indexData += sizeof(uint64_t);

			uint64_t *countValues = reinterpret_cast<uint64_t *>(buffer.data() + indexData);
			indexData += sizeof(uint64_t);

			std::vector<char> values;
			values.resize(*sizeOfValues);
			memcpy(values.data(), buffer.data() + indexData, *sizeOfValues);
			indexData += *sizeOfValues;

			param.name = name;
			param.description = description;
			param.type = type;
			param.subType = subType;
			param.enabled = *enabled;
			param.masked = *masked;
			param.visible = *visible;
			param.minVal = *minVal;
			param.maxVal = *maxVal;
			param.stepVal = *stepVal;
			param.currentValue = currentValue;
			param.values = values;
			param.countValues = *countValues;

			sc.params.push_back(param);
		}
		sc.name = name;
		sc.paramsCount = *paramsCount;
		category.push_back(sc);
	}
	return category;
}

void OBS_settings::OBS_settings_saveSettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	std::string nameCategory = args[0].value_str;
	uint32_t subCategoriesCount = args[1].value_union.ui32;
	uint32_t sizeStruct = args[2].value_union.ui32;

	std::vector<char> buffer;
	buffer.resize(sizeStruct);
	memcpy(buffer.data(), args[3].value_bin.data(), sizeStruct);

	std::vector<SubCategory> settings = serializeCategory(subCategoriesCount, sizeStruct, buffer);

	if (saveSettings(nameCategory, settings)) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	} else {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Error));
		rval.push_back(ipc::value("Failed to save settings"));
	}

	AUTO_DEBUG;
}

SubCategory OBS_settings::serializeSettingsData(const std::string &nameSubCategory, std::vector<std::vector<std::pair<std::string, ipc::value>>> &entries,
						config_t *config, const std::string &section, bool isVisible, bool isEnabled)
{
	SubCategory sc;

	for (int i = 0; i < entries.size(); i++) {
		Parameter param;

		param.name = entries.at(i).at(0).second.value_str;
		param.type = entries.at(i).at(1).second.value_str;
		param.description = entries.at(i).at(2).second.value_str;
		param.subType = entries.at(i).at(3).second.value_str;
		param.minVal = entries.at(i).at(4).second.value_union.fp64;
		param.maxVal = entries.at(i).at(5).second.value_union.fp64;
		param.stepVal = entries.at(i).at(6).second.value_union.fp64;

		std::string currentValueParam;
		if (entries.at(i).size() > 7) {
			currentValueParam = entries.at(i).at(7).first.c_str();
		}

		// Current value
		if (!currentValueParam.empty() && currentValueParam.compare("currentValue") == 0) {
			const char *currentValue = entries.at(i).at(7).second.value_str.c_str();
			param.currentValue.resize(strlen(currentValue));
			std::memcpy(param.currentValue.data(), currentValue, strlen(currentValue));
			param.sizeOfCurrentValue = strlen(currentValue);
			entries.at(i).erase(entries.at(i).begin() + 7);
		} else {
			if (param.type.compare("OBS_PROPERTY_LIST") == 0 || param.type.compare("OBS_PROPERTY_PATH") == 0 ||
			    param.type.compare("OBS_PROPERTY_EDIT_PATH") == 0 || param.type.compare("OBS_PROPERTY_EDIT_TEXT") == 0) {
				const char *currentValue = NULL;
				currentValue = config_get_string(config, section.c_str(), param.name.c_str());
				if (section.compare("Video") == 0) {
					if (param.name.compare("ColorSpace") == 0 || param.name.compare("ColorFormat") == 0 ||
					    param.name.compare("ColorRange") == 0) {
						currentValue = config_get_string(config, "AdvVideo", param.name.c_str());
					}
				} else if (section.compare("SimpleOutput") == 0) {
					if (param.name.compare("NVENCPreset2") == 0) {
						currentValue = config_get_string(config, "SimpleOutput", param.name.c_str());
						if (currentValue == NULL) {
							const char *oldParamName = "NVENCPreset";
							const char *oldValue = config_get_string(config, "SimpleOutput", oldParamName);
							if (oldValue != NULL) {
								currentValue = convert_nvenc_simple_preset(oldValue);
								blog(LOG_INFO, "NVENC presets converted from %s to %s", oldValue, currentValue);
							}
						}
					}
				}

				if (currentValue != NULL) {
					param.currentValue.resize(strlen(currentValue));
					std::memcpy(param.currentValue.data(), currentValue, strlen(currentValue));
					param.sizeOfCurrentValue = strlen(currentValue);

				} else {
					param.sizeOfCurrentValue = 0;
				}
			} else if (param.type.compare("OBS_PROPERTY_INT") == 0) {
				int64_t val = config_get_int(config, section.c_str(), param.name.c_str());

				param.currentValue.resize(sizeof(val));
				memcpy(param.currentValue.data(), &val, sizeof(val));
				param.sizeOfCurrentValue = sizeof(val);
			} else if (param.type.compare("OBS_PROPERTY_UINT") == 0) {
				uint64_t val = config_get_uint(config, section.c_str(), param.name.c_str());

				param.currentValue.resize(sizeof(val));
				memcpy(param.currentValue.data(), &val, sizeof(val));
				param.sizeOfCurrentValue = sizeof(val);
			} else if (param.type.compare("OBS_PROPERTY_BOOL") == 0) {
				bool val = config_get_bool(config, section.c_str(), param.name.c_str());

				param.currentValue.resize(sizeof(val));
				memcpy(param.currentValue.data(), &val, sizeof(val));
				param.sizeOfCurrentValue = sizeof(val);
			} else if (param.type.compare("OBS_PROPERTY_DOUBLE") == 0) {
				double val = config_get_double(config, section.c_str(), param.name.c_str());

				param.currentValue.resize(sizeof(val));
				memcpy(param.currentValue.data(), &val, sizeof(val));
				param.sizeOfCurrentValue = sizeof(val);
			}
		}

		// Values
		if (entries.at(i).size() > 7) {
			for (int j = 7; j < entries.at(i).size(); j++) {
				std::string name = entries.at(i).at(j).first;

				uint64_t sizeName = name.length();
				std::vector<char> sizeNameBuffer;
				sizeNameBuffer.resize(sizeof(sizeName));
				memcpy(sizeNameBuffer.data(), &sizeName, sizeof(sizeName));

				param.values.insert(param.values.end(), sizeNameBuffer.begin(), sizeNameBuffer.end());
				param.values.insert(param.values.end(), name.begin(), name.end());

				std::string value = entries.at(i).at(j).second.value_str;

				uint64_t sizeValue = value.length();
				std::vector<char> sizeValueBuffer;
				sizeValueBuffer.resize(sizeof(sizeValue));
				memcpy(sizeValueBuffer.data(), &sizeValue, sizeof(sizeValue));

				param.values.insert(param.values.end(), sizeValueBuffer.begin(), sizeValueBuffer.end());
				param.values.insert(param.values.end(), value.begin(), value.end());
			}

			param.sizeOfValues = param.values.size();
			param.countValues = entries.at(i).size() - 7;
		}

		if (param.name.compare("RecFormat") == 0 && section.compare("SimpleOutput") == 0) {
			const char *quality = config_get_string(config, "SimpleOutput", "RecQuality");

			if (quality && strcmp(quality, "Lossless") == 0)
				param.visible = false;
			else
				param.visible = isVisible;
		} else {
			param.visible = isVisible;
		}

		param.enabled = isEnabled;
		param.masked = false;

		sc.params.push_back(param);
	}

	sc.paramsCount = sc.params.size();
	sc.name = nameSubCategory;
	return sc;
}

std::vector<SubCategory> OBS_settings::getGeneralSettings()
{
	std::vector<SubCategory> generalSettings;

	std::vector<std::vector<std::pair<std::string, ipc::value>>> entries;

	// Output
	entries.push_back(createSettingEntry("WarnBeforeStartingStream", "OBS_PROPERTY_BOOL", "Show confirmation dialog when starting streams"));
	entries.push_back(createSettingEntry("WarnBeforeStoppingStream", "OBS_PROPERTY_BOOL", "Show confirmation dialog when stopping streams"));
	entries.push_back(createSettingEntry("RecordWhenStreaming", "OBS_PROPERTY_BOOL", "Automatically record when streaming"));
	entries.push_back(createSettingEntry("KeepRecordingWhenStreamStops", "OBS_PROPERTY_BOOL", "Keep recording when stream stops"));
	entries.push_back(createSettingEntry("ReplayBufferWhileStreaming", "OBS_PROPERTY_BOOL", "Automatically start replay buffer when streaming"));
	entries.push_back(createSettingEntry("KeepReplayBufferStreamStops", "OBS_PROPERTY_BOOL", "Keep replay buffer active when stream stops"));

	generalSettings.push_back(serializeSettingsData("Output", entries, ConfigManager::getInstance().getGlobal(), "BasicWindow", true, true));
	entries.clear();

	// Source Alignement Snapping
	entries.push_back(createSettingEntry("SnappingEnabled", "OBS_PROPERTY_BOOL", "Enable"));
	entries.push_back(createSettingEntry("SnapDistance", "OBS_PROPERTY_DOUBLE", "Snap Sensitivity", "", 0, 100, 0.5));
	entries.push_back(createSettingEntry("ScreenSnapping", "OBS_PROPERTY_BOOL", "Snap Sources to edge of screen"));
	entries.push_back(createSettingEntry("SourceSnapping", "OBS_PROPERTY_BOOL", "Snap Sources to other sources"));
	entries.push_back(createSettingEntry("CenterSnapping", "OBS_PROPERTY_BOOL", "Snap Sources to horizontal and vertical center"));

	generalSettings.push_back(
		serializeSettingsData("Source Alignement Snapping", entries, ConfigManager::getInstance().getGlobal(), "BasicWindow", true, true));
	entries.clear();

	// Projectors
	entries.push_back(createSettingEntry("HideProjectorCursor", "OBS_PROPERTY_BOOL", "Hide cursor over projectors"));
	entries.push_back(createSettingEntry("ProjectorAlwaysOnTop", "OBS_PROPERTY_BOOL", "Make projectors always on top"));
	entries.push_back(createSettingEntry("SaveProjectors", "OBS_PROPERTY_BOOL", "Save projectors on exit"));

	generalSettings.push_back(serializeSettingsData("Projectors", entries, ConfigManager::getInstance().getGlobal(), "BasicWindow", true, true));
	entries.clear();

	// System Tray
	entries.push_back(createSettingEntry("SysTrayEnabled", "OBS_PROPERTY_BOOL", "Enable"));
	entries.push_back(createSettingEntry("SysTrayWhenStarted", "OBS_PROPERTY_BOOL", "Minimize to system tray when started"));
	entries.push_back(createSettingEntry("SysTrayMinimizeToTray", "OBS_PROPERTY_BOOL", "Always minimize to system tray instead of task bar"));

	generalSettings.push_back(serializeSettingsData("System Tray", entries, ConfigManager::getInstance().getGlobal(), "BasicWindow", true, true));
	entries.clear();

	return generalSettings;
}

void OBS_settings::saveGeneralSettings(std::vector<SubCategory> generalSettings, std::string pathConfigDirectory)
{
	config_t *config;
	pathConfigDirectory += "global.ini";

	int result = config_open(&config, pathConfigDirectory.c_str(), CONFIG_OPEN_EXISTING);

	if (result != CONFIG_SUCCESS) {
		config = config_create(pathConfigDirectory.c_str());
	}

	if (config == NULL) {
		throw "Invalid configuration file";
	}

	SubCategory sc;

	for (int i = 0; i < generalSettings.size(); i++) {
		sc = generalSettings.at(i);

		std::string nameSubcategory = sc.name;

		Parameter param;
		for (int j = 0; j < sc.params.size(); j++) {
			param = sc.params.at(i);

			std::string name, type;

			name = param.name;
			type = param.type;

			if (type.compare("OBS_PROPERTY_LIST") == 0) {
				config_set_string(config, "BasicWindow", name.c_str(), param.currentValue.data());
			} else if (type.compare("OBS_PROPERTY_INT") == 0) {
				int64_t *value = reinterpret_cast<int64_t *>(param.currentValue.data());
				config_set_int(config, "BasicWindow", name.c_str(), *value);
			} else if (type.compare("OBS_PROPERTY_UINT") == 0) {
				uint64_t *value = reinterpret_cast<uint64_t *>(param.currentValue.data());
				config_set_uint(config, "BasicWindow", name.c_str(), *value);
			} else if (type.compare("OBS_PROPERTY_BOOL") == 0) {
				bool *value = reinterpret_cast<bool *>(param.currentValue.data());
				config_set_bool(config, "BasicWindow", name.c_str(), *value);
			} else if (type.compare("OBS_PROPERTY_DOUBLE") == 0) {
				double *value = reinterpret_cast<double *>(param.currentValue.data());
				config_set_double(config, "BasicWindow", name.c_str(), *value);
			}
		}
	}
	config_save_safe(config, "tmp", nullptr);
	config_close(config);
}

std::vector<SubCategory> OBS_settings::getStreamSettings(StreamServiceId serviceId)
{
	bool isCategoryEnabled = !OBS_service::isStreamingOutputActive(serviceId);

	obs_service_t *currentService = OBS_service::getService(serviceId);
	obs_data_t *settings = obs_service_get_settings(currentService);

	std::vector<SubCategory> streamSettings;
	SubCategory service;

	service.name = "Untitled";

	Parameter streamType;
	streamType.name = "streamType";
	streamType.type = "OBS_PROPERTY_LIST";
	streamType.subType = "OBS_COMBO_FORMAT_STRING";

	int index = 0;
	const char *type;

	uint32_t indexData = 0;
	while (obs_enum_service_types(index++, &type)) {
		std::string name = obs_service_get_display_name(type);

		uint64_t sizeName = name.length();
		std::vector<char> sizeNameBuffer;
		sizeNameBuffer.resize(sizeof(sizeName));
		memcpy(sizeNameBuffer.data(), &sizeName, sizeof(sizeName));

		streamType.values.insert(streamType.values.end(), sizeNameBuffer.begin(), sizeNameBuffer.end());
		streamType.values.insert(streamType.values.end(), name.begin(), name.end());

		std::string value = type;

		uint64_t sizeValue = value.length();
		std::vector<char> sizeValueBuffer;
		sizeValueBuffer.resize(sizeof(sizeValue));
		memcpy(sizeValueBuffer.data(), &sizeValue, sizeof(sizeValue));

		streamType.values.insert(streamType.values.end(), sizeValueBuffer.begin(), sizeValueBuffer.end());
		streamType.values.insert(streamType.values.end(), value.begin(), value.end());
	}

	streamType.sizeOfValues = streamType.values.size();
	streamType.countValues = index - 1;

	streamType.description = "Stream Type";

	const char *servType = obs_service_get_type(currentService);
	streamType.currentValue.resize(strlen(servType));
	memcpy(streamType.currentValue.data(), servType, strlen(servType));
	streamType.sizeOfCurrentValue = strlen(servType);

	streamType.visible = true;
	streamType.enabled = isCategoryEnabled;
	streamType.masked = false;

	service.params.push_back(streamType);
	service.paramsCount = service.params.size();

	streamSettings.push_back(service);

	SubCategory serviceConfiguration;

	obs_properties_t *properties = obs_service_properties(currentService);
	obs_property_t *property = obs_properties_first(properties);
	obs_combo_format format;
	std::string formatString;

	index = 0;
	uint32_t indexDataServiceConfiguration = 0;

	while (property) {
		Parameter param;

		param.name = obs_property_name(property);

		std::vector<std::pair<std::string, void *>> values;

		int count = (int)obs_property_list_item_count(property);
		format = obs_property_list_format(property);

		for (int i = 0; i < count; i++) {
			//Value
			if (format == OBS_COMBO_FORMAT_INT) {
				std::string name = obs_property_list_item_name(property, i);

				uint64_t sizeName = name.length();
				std::vector<char> sizeNameBuffer;
				sizeNameBuffer.resize(sizeof(sizeName));
				memcpy(sizeNameBuffer.data(), &sizeName, sizeof(sizeName));

				param.values.insert(param.values.end(), sizeNameBuffer.begin(), sizeNameBuffer.end());
				param.values.insert(param.values.end(), name.begin(), name.end());

				int64_t value = obs_property_list_item_int(property, i);

				std::vector<char> valueBuffer;
				valueBuffer.resize(sizeof(value));
				memcpy(valueBuffer.data(), &value, sizeof(value));

				param.values.insert(param.values.end(), valueBuffer.begin(), valueBuffer.end());

				formatString = "OBS_PROPERTY_INT";
				param.subType = "OBS_COMBO_FORMAT_INT";
			} else if (format == OBS_COMBO_FORMAT_FLOAT) {
				std::string name = obs_property_list_item_name(property, i);

				uint64_t sizeName = name.length();
				std::vector<char> sizeNameBuffer;
				sizeNameBuffer.resize(sizeof(sizeName));
				memcpy(sizeNameBuffer.data(), &sizeName, sizeof(sizeName));

				param.values.insert(param.values.end(), sizeNameBuffer.begin(), sizeNameBuffer.end());
				param.values.insert(param.values.end(), name.begin(), name.end());

				double value = obs_property_list_item_float(property, i);

				std::vector<char> valueBuffer;
				valueBuffer.resize(sizeof(value));
				memcpy(valueBuffer.data(), &value, sizeof(value));

				param.values.insert(param.values.end(), valueBuffer.begin(), valueBuffer.end());

				formatString = "OBS_PROPERTY_DOUBLE";
				param.subType = "OBS_COMBO_FORMAT_FLOAT";
			} else if (format == OBS_COMBO_FORMAT_STRING) {
				std::string name = obs_property_list_item_name(property, i);

				uint64_t sizeName = name.length();
				std::vector<char> sizeNameBuffer;
				sizeNameBuffer.resize(sizeof(sizeName));
				memcpy(sizeNameBuffer.data(), &sizeName, sizeof(sizeName));

				param.values.insert(param.values.end(), sizeNameBuffer.begin(), sizeNameBuffer.end());
				param.values.insert(param.values.end(), name.begin(), name.end());

				std::string value = obs_property_list_item_string(property, i);
				uint64_t sizeValue = value.length();

				if (value[sizeValue - 1] == '/') {
					sizeValue--;
					value.resize(sizeValue);
				}

				std::vector<char> sizeValueBuffer;
				sizeValueBuffer.resize(sizeof(sizeValue));
				memcpy(sizeValueBuffer.data(), &sizeValue, sizeof(sizeValue));

				param.values.insert(param.values.end(), sizeValueBuffer.begin(), sizeValueBuffer.end());
				param.values.insert(param.values.end(), value.begin(), value.end());

				formatString = "OBS_PROPERTY_LIST";
				param.subType = "OBS_COMBO_FORMAT_STRING";
			} else {
				std::cout << "INVALID FORMAT" << std::endl;
			}
		}

		param.sizeOfValues = param.values.size();
		param.countValues = count;

		if (count == 0) {
			if (strcmp(obs_property_name(property), "bearer_token") == 0) {
				const char *bearer_token = obs_service_get_connect_info(currentService, OBS_SERVICE_CONNECT_INFO_BEARER_TOKEN);
				formatString = "OBS_PROPERTY_EDIT_TEXT";

				if (bearer_token == NULL)
					bearer_token = "";

				param.currentValue.resize(strlen(bearer_token));
				memcpy(param.currentValue.data(), bearer_token, strlen(bearer_token));
				param.sizeOfCurrentValue = strlen(bearer_token);
			}
			if (strcmp(obs_property_name(property), "key") == 0) {
				const char *stream_key = obs_service_get_key(currentService);
				formatString = "OBS_PROPERTY_EDIT_TEXT";

				if (stream_key == NULL)
					stream_key = "";

				param.currentValue.resize(strlen(stream_key));
				memcpy(param.currentValue.data(), stream_key, strlen(stream_key));
				param.sizeOfCurrentValue = strlen(stream_key);
			}
			if (strcmp(obs_property_name(property), "show_all") == 0) {
				bool show_all = obs_data_get_bool(settings, "show_all");
				formatString = "OBS_PROPERTY_BOOL";

				param.currentValue.resize(sizeof(show_all));
				memcpy(param.currentValue.data(), &show_all, sizeof(show_all));
				param.sizeOfCurrentValue = sizeof(show_all);
			}
			if (strcmp(obs_property_name(property), "server") == 0) {
				const char *server = obs_service_get_url(currentService);
				if (strcmp(obs_service_get_type(currentService), "rtmp_common") == 0) {
					formatString = "OBS_PROPERTY_LIST";
				} else {
					formatString = "OBS_PROPERTY_EDIT_TEXT";
				}

				if (server == NULL)
					server = "";

				param.currentValue.resize(strlen(server));
				memcpy(param.currentValue.data(), server, strlen(server));
				param.sizeOfCurrentValue = strlen(server);
			}
			if (strcmp(obs_property_name(property), "username") == 0) {
				const char *username = obs_service_get_username(currentService);
				formatString = "OBS_PROPERTY_EDIT_TEXT";

				if (username == NULL)
					username = "";

				param.currentValue.resize(strlen(username));
				memcpy(param.currentValue.data(), username, strlen(username));
				param.sizeOfCurrentValue = strlen(username);
			}
			if (strcmp(obs_property_name(property), "password") == 0) {
				const char *password = obs_service_get_password(currentService);
				formatString = "OBS_PROPERTY_EDIT_TEXT";

				if (password == NULL)
					password = "";

				param.currentValue.resize(strlen(password));
				memcpy(param.currentValue.data(), password, strlen(password));
				param.sizeOfCurrentValue = strlen(password);
			}
			if (strcmp(obs_property_name(property), "use_auth") == 0) {
				bool use_auth = obs_data_get_bool(settings, "use_auth");
				formatString = "OBS_PROPERTY_BOOL";

				param.currentValue.resize(sizeof(use_auth));
				memcpy(param.currentValue.data(), &use_auth, sizeof(use_auth));
				param.sizeOfCurrentValue = sizeof(use_auth);
			}
		} else {
			if (format == OBS_COMBO_FORMAT_INT) {
				int64_t value = obs_data_get_int(settings, obs_property_name(property));

				param.currentValue.resize(sizeof(value));
				memcpy(param.currentValue.data(), &value, sizeof(value));
				param.sizeOfCurrentValue = sizeof(value);
			} else if (format == OBS_COMBO_FORMAT_FLOAT) {
				double value = obs_data_get_double(settings, obs_property_name(property));

				param.currentValue.resize(sizeof(value));
				memcpy(param.currentValue.data(), &value, sizeof(value));
				param.sizeOfCurrentValue = sizeof(value);
			} else if (format == OBS_COMBO_FORMAT_STRING) {
				currentServiceName = obs_data_get_string(settings, obs_property_name(property));

				param.currentValue.resize(strlen(currentServiceName));
				memcpy(param.currentValue.data(), currentServiceName, strlen(currentServiceName));
				param.sizeOfCurrentValue = strlen(currentServiceName);
			}
		}

		param.type = formatString;
		param.description = obs_property_description(property);
		param.visible = obs_property_visible(property);
		param.enabled = isCategoryEnabled;

		param.masked = formatString.compare("OBS_PROPERTY_EDIT_TEXT") == 0 && obs_proprety_text_type(property) == OBS_TEXT_PASSWORD;

		serviceConfiguration.params.push_back(param);

		index++;
		obs_property_next(&property);
	}

	serviceConfiguration.name = "Untitled";
	serviceConfiguration.paramsCount = serviceConfiguration.params.size();
	streamSettings.push_back(serviceConfiguration);

	obs_properties_destroy(properties);

	return streamSettings;
}

bool OBS_settings::saveStreamSettings(std::vector<SubCategory> streamSettings, StreamServiceId serviceId)
{
	obs_service_t *currentService = OBS_service::getService(serviceId);
	if (!obs_service_is_ready_to_update(currentService))
		return false;

	obs_data_t *settings = nullptr;

	std::string currentStreamType = obs_service_get_type(currentService);
	std::string newserviceTypeValue;

	std::string currentServiceName = obs_data_get_string(obs_service_get_settings(currentService), "service");
	std::string newServiceValue;

	SubCategory sc;
	bool serviceChanged = false;
	bool serviceSettingsInvalid = false;

	for (int i = 0; i < streamSettings.size(); i++) {
		sc = streamSettings.at(i);

		std::string nameSubcategory = sc.name;
		Parameter param;
		for (int j = 0; j < sc.params.size(); j++) {
			param = sc.params.at(j);

			std::string name = param.name;
			std::string type = param.type;

			if (type.compare("OBS_PROPERTY_LIST") == 0 || type.compare("OBS_PROPERTY_EDIT_TEXT") == 0) {
				std::string value(param.currentValue.data(), param.currentValue.size());

				if (name.compare("streamType") == 0) {
					if (value.size() == 0) {
						serviceSettingsInvalid = true;
						break;
					}
					newserviceTypeValue = value;
					settings = obs_service_defaults(newserviceTypeValue.c_str());
					if (currentStreamType.compare(newserviceTypeValue) != 0) {

						if (newserviceTypeValue.compare("rtmp_common") == 0) {
							obs_data_set_string(settings, "streamType", "rtmp_common");
							obs_data_set_string(settings, "service", "Twitch");
							obs_data_set_bool(settings, "show_all", 0);
							obs_data_set_string(settings, "server", "auto");
							obs_data_set_string(settings, "key", "");
						}
					}
				}

				if (name.compare("service") == 0) {
					if (value.size() == 0) {
						serviceSettingsInvalid = true;
						break;
					}
					newServiceValue = value;
					if (currentServiceName.compare(newServiceValue) != 0) {
						serviceChanged = true;
					}
				}
				obs_data_set_string(settings, name.c_str(), value.c_str());
			} else if (type.compare("OBS_PROPERTY_INT") == 0 || type.compare("OBS_PROPERTY_UINT") == 0) {
				int64_t *value = reinterpret_cast<int64_t *>(param.currentValue.data());
				obs_data_set_int(settings, name.c_str(), *value);
			} else if (type.compare("OBS_PROPERTY_BOOL") == 0) {
				bool *value = reinterpret_cast<bool *>(param.currentValue.data());
				obs_data_set_bool(settings, name.c_str(), *value);
			} else if (type.compare("OBS_PROPERTY_DOUBLE") == 0) {
				double *value = reinterpret_cast<double *>(param.currentValue.data());
				obs_data_set_double(settings, name.c_str(), *value);
			}
		}
	}

	if (serviceSettingsInvalid) {
		if (settings)
			obs_data_release(settings);
		return false;
	}

	obs_data_t *hotkeyData = obs_hotkeys_save_service(currentService);

	obs_service_t *newService = obs_service_create(newserviceTypeValue.c_str(), "default_service", settings, hotkeyData);

	if (serviceChanged) {
		std::string server = obs_data_get_string(settings, "server");
		bool serverFound = false;
		std::string defaultServer;

		// Check if server is valid
		obs_properties_t *properties = obs_service_properties(newService);
		obs_property_t *property = obs_properties_first(properties);

		while (property) {
			std::string name = obs_property_name(property);

			if (name.compare("server") == 0) {
				int count = (int)obs_property_list_item_count(property);
				int i = 0;

				while (i < count && !serverFound) {
					std::string value = obs_property_list_item_string(property, i);

					if (i == 0)
						defaultServer = value;

					if (value.compare(server) == 0)
						serverFound = true;

					i++;
				}
			}
			obs_property_next(&property);
		}

		if (!serverFound && defaultServer.compare("") != 0) {
			// Server not found, we set the default server
			obs_data_set_string(settings, "server", defaultServer.c_str());
			obs_service_update(newService, settings);
		}
	}

	obs_data_release(hotkeyData);

	OBS_service::setService(newService, serviceId);

	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "type", obs_service_get_type(newService));
	obs_data_set_obj(data, "settings", settings);

	if (!obs_data_save_json_safe(data, ConfigManager::getInstance().getService(serviceId).c_str(), "tmp", "bak")) {
		blog(LOG_WARNING, "Failed to save service");
	}

	obs_data_release(hotkeyData);
	obs_data_release(data);
	return true;
}

bool EncoderAvailable(const std::string &encoder)
{
	const char *val;
	int i = 0;

	while (obs_enum_encoder_types(i++, &val)) {
		if (val == nullptr)
			continue;
		if (std::string(val) == encoder)
			return true;
	}

	return false;
}

static bool isEncoderAvailableForStreaming(const char *encoder, obs_service_t *service)
{
	if (!encoder || !service)
		return false;

	auto supportedCodecs = obs_service_get_supported_video_codecs(service);
	auto encoderCodec = obs_get_encoder_codec(encoder);

	if (!supportedCodecs || !encoderCodec)
		return false;

	while (*supportedCodecs) {
		if (strcmp(*supportedCodecs, encoderCodec) == 0)
			return true;
		supportedCodecs++;
	}

	return false;
}

// Codect/Container support check.
// from OBS code UI\window-basic-settings.cpp
static const std::unordered_map<std::string, std::unordered_set<std::string>> codec_compat = {
	// Technically our muxer supports HEVC and AV1 as well, but nothing else does
	{"flv", {"h264", "aac"}},
	{"mpegts", {"h264", "hevc", "aac", "opus"}},
	{"hls", {"h264", "hevc", "aac"}}, // Also using MPEG-TS, but no Opus support
	{"mov", {"h264", "hevc", "prores", "aac", "alac", "pcm_s16le", "pcm_s24le", "pcm_f32le"}},
	{"mp4", {"h264", "hevc", "av1", "aac", "opus", "alac", "flac"}},
	{"fragmented_mov", {"h264", "hevc", "prores", "aac", "alac", "pcm_s16le", "pcm_s24le", "pcm_f32le"}},
	{"fragmented_mp4", {"h264", "hevc", "av1", "aac", "opus", "alac", "flac"}},
	// MKV supports everything
	{"mkv", {}},
};

static bool ContainerSupportsCodec(const std::string &container, const std::string &codec)
{
	auto iter = codec_compat.find(container);
	if (iter == codec_compat.end())
		return false;

	auto codecs = iter->second;
	// Assume everything is supported
	if (codecs.empty())
		return true;
	return codecs.count(codec) > 0;
}

static bool isNvencAvailableForSimpleMode()
{
	// Only available if config already uses it
	const char *current_stream_encoder = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "StreamEncoder");
	const char *current_rec_encoder = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecEncoder");
	bool nvenc_used_streaming = (current_stream_encoder && strcmp(current_stream_encoder, "nvenc") == 0);
	bool nvenc_used_recording = (current_rec_encoder && strcmp(current_rec_encoder, "nvenc") == 0);

	return (nvenc_used_streaming || nvenc_used_recording) && EncoderAvailable("ffmpeg_nvenc");
}

void OBS_settings::getAvailableAudioEncoders(std::vector<std::pair<std::string, ipc::value>> *encoders, bool simple, bool recording,
					     const std::string &container)
{
	if (EncoderAvailable(SIMPLE_AUDIO_ENCODER_AAC))
		encoders->push_back(std::make_pair("AAC (Default)", ipc::value(SIMPLE_AUDIO_ENCODER_AAC)));
	if (recording && EncoderAvailable(SIMPLE_AUDIO_ENCODER_OPUS))
		encoders->push_back(std::make_pair("Opus", ipc::value(SIMPLE_AUDIO_ENCODER_OPUS)));
}

class EncoderSettings {
public:
	std::string advanced_title;
	std::string advanced_name;
	std::string simple_title;
	std::string simple_name;
	std::string simple_intenal_name;
	bool recording;
	bool streaming;
	bool check_availability;
	bool check_availability_streaming;
	bool check_availability_format;
	bool only_for_reuse_simple;
	const std::string getSimpleName() const { return simple_intenal_name.empty() ? simple_name : simple_intenal_name; }
};

std::vector<EncoderSettings> encoders_set = {
	// Software x264
	{"Software (x264)", "obs_x264", "Software (x264)", "x264", "obs_x264", true, true, false, false, true, false},
	// Software x264 low CPU (only for recording)
	{"", "", "Software (x264 low CPU usage preset, increases file size)", "x264_lowcpu", "obs_x264", true, false, false, false, true, false},
	// QuickSync H.264 (v1, deprecated)
	// This line left here for reference
	// {"QuickSync H.264 (v1 deprecated)", "obs_qsv11", "(Deprecated v1) Hardware (QSV, H.264)", "qsv", "obs_qsv11", true, true, true, false, true, false},
	// QuickSync H.264 (v2, new)
	{"QuickSync H.264", "obs_qsv11_v2", "Hardware (QSV, H.264)", "qsv", "obs_qsv11_v2", true, true, true, false, true, false},
	// QuickSync AV1
	{"QuickSync AV1", "obs_qsv11_av1", "Hardware (QSV, AV1)", "obs_qsv11_av1", "", true, true, true, false, true, false},
	// QuickSync HEVC
	{"QuickSync HEVC", "obs_qsv11_hevc", "Hardware (QSV, HEVC)", "obs_qsv11_hevc", "", true, true, true, false, true, false},
	// NVIDIA NVENC H.264
	{"NVIDIA NVENC H.264", "ffmpeg_nvenc", "NVIDIA NVENC H.264", "nvenc", "ffmpeg_nvenc", true, true, true, false, true, true},
	// NVIDIA NVENC H.264 (new)
	{"NVIDIA NVENC H.264 (new)", "jim_nvenc", "NVIDIA NVENC H.264 (new)", "jim_nvenc", "", true, true, true, false, true, false},
	// NVIDIA NVENC HEVC
	{"NVIDIA NVENC HEVC", "jim_hevc_nvenc", "Hardware (NVENC, HEVC)", "nvenc_hevc", "jim_hevc_nvenc", true, true, true, true, true, false},
	// NVIDIA NVENC AV1
	{"NVIDIA NVENC AV1", "jim_av1_nvenc", "Hardware (NVENC, AV1)", "jim_av1_nvenc", "", true, true, true, true, true, false},
	// Apple VT H264 Software Encoder
	{"Apple VT H264 Software Encoder", "com.apple.videotoolbox.videoencoder.h264", "Software (Apple, H.264)", "com.apple.videotoolbox.videoencoder.h264",
	 "", true, true, true, false, true, false},
	// Apple VT H264 Hardware Encoder
	{"Apple VT H264 Hardware Encoder", "com.apple.videotoolbox.videoencoder.h264.gva", "Hardware (Apple, H.264)",
	 "com.apple.videotoolbox.videoencoder.h264.gva", "", true, true, true, false, true, false},
	// Apple VT H264 Hardware Encoder
	{"Apple VT H264 Hardware Encoder", "com.apple.videotoolbox.videoencoder.ave.avc", "Hardware (Apple, H.264)",
	 "com.apple.videotoolbox.videoencoder.ave.avc", "", true, true, true, false, true, false},
	// AMD HW H.264
	{"AMD HW H.264", "h264_texture_amf", "Hardware (AMD, H.264)", "amd", "h264_texture_amf", true, true, true, false, true, false},
	// AMD HW H.265 (HEVC)
	{"AMD HW H.265 (HEVC)", "h265_texture_amf", "Hardware (AMD, HEVC)", "amd_hevc", "h265_texture_amf", true, true, true, true, true, false},
	// AMD HW AV1
	{"AMD HW AV1", "amd_av1", "Hardware (AMD, AV1)", "av1", "amd_av1", true, true, true, true, true, false},
	// AOM AV1
	{"AOM AV1", "ffmpeg_aom_av1", "AOM AV1", "ffmpeg_aom_av1", "", true, true, true, false, true, false},
	// SVT-AV1
	{"SVT-AV1", "ffmpeg_svt_av1", "SVT-AV1", "ffmpeg_svt_av1", "", true, true, true, false, true, false}};

void OBS_settings::getSimpleAvailableEncoders(std::vector<std::pair<std::string, ipc::value>> *list, bool recording, const std::string &container)
{
	for (const auto &encoderSetting : encoders_set) {
		if (encoderSetting.simple_name.empty())
			continue;

		if (!recording && !encoderSetting.streaming)
			continue;

		if (recording && !encoderSetting.recording)
			continue;

		if (encoderSetting.check_availability && !EncoderAvailable(encoderSetting.getSimpleName()))
			continue;

		if (!recording && encoderSetting.check_availability_streaming &&
		    !isEncoderAvailableForStreaming(encoderSetting.getSimpleName().c_str(), OBS_service::getService(StreamServiceId::Main)))
			continue;

		if (encoderSetting.only_for_reuse_simple && !isNvencAvailableForSimpleMode())
			continue;

		if (recording && encoderSetting.check_availability_format) {
			const char *codec = obs_get_encoder_codec(encoderSetting.getSimpleName().c_str());
			if (!codec) {
				blog(LOG_DEBUG, "[ENCODER_SKIPPED] codec is null");
				continue;
			}
			if (!ContainerSupportsCodec(container, codec))
				continue;
		}

		list->push_back(std::make_pair(encoderSetting.simple_title, ipc::value(encoderSetting.simple_name)));
	}
}

void OBS_settings::getAdvancedAvailableEncoders(std::vector<std::pair<std::string, ipc::value>> *list, bool recording, const std::string &container)
{
	for (const auto &encoderSetting : encoders_set) {
		if (encoderSetting.advanced_name.empty())
			continue;

		if (!recording && !encoderSetting.streaming)
			continue;

		if (recording && !encoderSetting.recording)
			continue;

		if (encoderSetting.check_availability && !EncoderAvailable(encoderSetting.advanced_name))
			continue;

		if (!recording && encoderSetting.check_availability_streaming &&
		    !isEncoderAvailableForStreaming(encoderSetting.advanced_name.c_str(), OBS_service::getService(StreamServiceId::Main)))
			continue;

		if (recording && encoderSetting.check_availability_format) {
			const char *codec = obs_get_encoder_codec(encoderSetting.advanced_name.c_str());
			if (!codec) {
				blog(LOG_WARNING, "[SUPPORTED_CODECS] codec is null for %s", encoderSetting.advanced_name.c_str());
				continue;
			}
			if (!ContainerSupportsCodec(container, codec))
				continue;
		}

		list->push_back(std::make_pair(encoderSetting.advanced_title, ipc::value(encoderSetting.advanced_name)));
	}
}

#ifdef __APPLE__
std::string newValue;
static const char *translate_macvth264_encoder(std::string encoder)
{
	if (strcmp(encoder.c_str(), "vt_h264_hw") == 0) {
		newValue = "com.apple.videotoolbox.videoencoder.h264.gva";
	} else if (strcmp(encoder.c_str(), "vt_h264_sw") == 0) {
		newValue = "com.apple.videotoolbox.videoencoder.h264";
	} else {
		newValue = std::string(encoder);
	}

	return newValue.c_str();
}
#endif

void OBS_settings::getSimpleOutputSettings(std::vector<SubCategory> *outputSettings, config_t *config, bool isCategoryEnabled)
{
	std::vector<std::vector<std::pair<std::string, ipc::value>>> entries;

	//Streaming

	//Video Bitrate
	entries.push_back(createSettingEntry("VBitrate", "OBS_PROPERTY_INT", "Video Bitrate", "", 0, 1000000, 1));

	// Stream Encoder
	auto streamEncoder = createSettingEntry("StreamEncoder", "OBS_PROPERTY_LIST", "Encoder", "OBS_COMBO_FORMAT_STRING");

	getSimpleAvailableEncoders(&streamEncoder, false, config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecFormat"));

#ifdef __APPLE__
	const char *sEncoder = config_get_string(config, "SimpleOutput", "StreamEncoder");
	config_set_string(config, "SimpleOutput", "StreamEncoder", translate_macvth264_encoder(std::string(sEncoder)));
	const char *rEncoder = config_get_string(config, "SimpleOutput", "RecEncoder");
	config_set_string(config, "SimpleOutput", "RecEncoder", translate_macvth264_encoder(std::string(rEncoder)));
#endif

	entries.push_back(streamEncoder);

	// Audio Bitrate
	auto aBitrate = createSettingEntry("ABitrate", "OBS_PROPERTY_LIST", "Audio Bitrate", "OBS_COMBO_FORMAT_STRING");

	auto &bitrateMap = GetAACEncoderBitrateMap();
	for (auto &entry : bitrateMap)
		aBitrate.push_back(std::make_pair(std::to_string(entry.first), std::to_string(entry.first)));
	entries.push_back(aBitrate);

	//Enable Advanced Encoder Settings
	entries.push_back(createSettingEntry("UseAdvanced", "OBS_PROPERTY_BOOL", "Enable Advanced Encoder Settings"));

	if (config_get_bool(config, "SimpleOutput", "UseAdvanced")) {
		//Enforce streaming service bitrate limits
		entries.push_back(createSettingEntry("EnforceBitrate", "OBS_PROPERTY_BOOL", "Enforce streaming service bitrate limits"));

		obs_data_t *settings = obs_service_get_settings(OBS_service::getService(StreamServiceId::Main)); //todo DUALOUTPUT
		const char *serviceName = obs_data_get_string(settings, "service");
		obs_data_release(settings);

		if (serviceName && strcmp(serviceName, "Twitch") == 0) {
			bool soundtrackSourceExists = false;
			obs_enum_sources(
				[](void *param, obs_source_t *source) {
					auto id = obs_source_get_id(source);
					if (strcmp(id, "soundtrack_source") == 0) {
						*reinterpret_cast<bool *>(param) = true;
						return false;
					}
					return true;
				},
				&soundtrackSourceExists);
			std::string twitchVODDesc = "Twitch VOD Track (Uses Track 2).";
			if (soundtrackSourceExists)
				twitchVODDesc += " Remove Twitch Soundtrack in order to enable this.";

			//Twitch VOD
			auto twitchVOD = createSettingEntry("VodTrackEnabled", "OBS_PROPERTY_BOOL", twitchVODDesc);
			entries.push_back(twitchVOD);
		}

		//Encoder Preset
		const char *defaultPreset;
		const char *encoder = config_get_string(config, "SimpleOutput", "StreamEncoder");

		std::vector<std::pair<std::string, ipc::value>> preset;

		if (strcmp(encoder, SIMPLE_ENCODER_QSV) == 0 || strcmp(encoder, ADVANCED_ENCODER_QSV) == 0) {
			preset = createSettingEntry("QSVPreset", "OBS_PROPERTY_LIST", "Encoder Preset (higher = less CPU)", "OBS_COMBO_FORMAT_STRING");
			preset.push_back({"Speed", ipc::value("speed")});
			preset.push_back({"Balanced", ipc::value("balanced")});
			preset.push_back({"Quality", ipc::value("quality")});
			entries.push_back(preset);
			defaultPreset = "balanced";

		} else if (strcmp(encoder, SIMPLE_ENCODER_NVENC) == 0 || strcmp(encoder, ADVANCED_ENCODER_NVENC) == 0 ||
			   strcmp(encoder, ENCODER_NEW_NVENC) == 0 || strcmp(encoder, ENCODER_NEW_HEVC_NVENC) == 0) {
			preset = createSettingEntry("NVENCPreset2", "OBS_PROPERTY_LIST", "Encoder Preset (higher = less CPU)", "OBS_COMBO_FORMAT_STRING");

			obs_properties_t *props = obs_get_encoder_properties("ffmpeg_nvenc");

			obs_property_t *p = obs_properties_get(props, "preset2");
			size_t num = obs_property_list_item_count(p);
			for (size_t i = 0; i < num; i++) {
				const char *name = obs_property_list_item_name(p, i);
				const char *val = obs_property_list_item_string(p, i);

				preset.push_back(std::make_pair(name, ipc::value(val)));
			}

			obs_properties_destroy(props);

			defaultPreset = "p5";
			entries.push_back(preset);
		} else if (strcmp(encoder, SIMPLE_ENCODER_AMD) == 0 || strcmp(encoder, ADVANCED_ENCODER_AMD) == 0) {
			preset = createSettingEntry("AMDPreset", "OBS_PROPERTY_LIST", "Encoder Preset (higher = less CPU)", "OBS_COMBO_FORMAT_STRING");
			preset.push_back({"Speed", ipc::value("speed")});
			preset.push_back({"Balanced", ipc::value("balanced")});
			preset.push_back({"Quality", ipc::value("quality")});
			entries.push_back(preset);
			defaultPreset = "balanced";
		} else if (strcmp(encoder, APPLE_SOFTWARE_VIDEO_ENCODER) == 0 || strcmp(encoder, APPLE_HARDWARE_VIDEO_ENCODER) == 0 ||
			   strcmp(encoder, APPLE_HARDWARE_VIDEO_ENCODER_M1) == 0) {
			preset = createSettingEntry("Profile", "OBS_PROPERTY_LIST", "", "OBS_COMBO_FORMAT_STRING");
			preset.push_back({"(None)", ipc::value("")});
			preset.push_back({"baseline", ipc::value("baseline")});
			preset.push_back({"main", ipc::value("main")});
			preset.push_back({"high", ipc::value("high")});
			entries.push_back(preset);
		} else {
			preset = createSettingEntry("Preset", "OBS_PROPERTY_LIST", "Encoder Preset (higher = less CPU)", "OBS_COMBO_FORMAT_STRING");
			preset.push_back({"ultrafast", ipc::value("ultrafast")});
			preset.push_back({"superfast", ipc::value("superfast")});
			preset.push_back({"veryfast", ipc::value("veryfast")});
			preset.push_back({"faster", ipc::value("faster")});
			preset.push_back({"fast", ipc::value("fast")});
			preset.push_back({"medium", ipc::value("medium")});
			preset.push_back({"slow", ipc::value("slow")});
			preset.push_back({"slower", ipc::value("slower")});
			entries.push_back(preset);

			defaultPreset = "veryfast";

			//Custom Encoder Settings
			entries.push_back(createSettingEntry("x264Settings", "OBS_PROPERTY_EDIT_TEXT", "Custom Encoder Settings"));
		}
	}

	outputSettings->push_back(serializeSettingsData("Streaming", entries, config, "SimpleOutput", true, isCategoryEnabled));
	entries.clear();

	//Recording

	//Recording Path
	entries.push_back(createSettingEntry("FilePath", "OBS_PROPERTY_PATH", "Recording Path"));

	//Generate File Name without Space
	entries.push_back(createSettingEntry("FileNameWithoutSpace", "OBS_PROPERTY_BOOL", "Generate File Name without Space"));

	//Recording Quality
	auto recQuality = createSettingEntry("RecQuality", "OBS_PROPERTY_LIST", "Recording Quality", "OBS_COMBO_FORMAT_STRING");
	recQuality.push_back({"Same as stream", ipc::value("Stream")});
	recQuality.push_back({"High Quality, Medium File Size", ipc::value("Small")});
	recQuality.push_back({"Indistinguishable Quality, Large File Size", ipc::value("HQ")});
	recQuality.push_back({"Lossless Quality, Tremendously Large File Size", ipc::value("Lossless")});
	entries.push_back(recQuality);

	//Recording Format
	auto recFormat = createSettingEntry("RecFormat", "OBS_PROPERTY_LIST", "Recording Format", "OBS_COMBO_FORMAT_STRING");
	recFormat.push_back({"mp4", ipc::value("mp4")});
	recFormat.push_back({"flv", ipc::value("flv")});
	recFormat.push_back({"mov", ipc::value("mov")});
	recFormat.push_back({"mkv", ipc::value("mkv")});
	recFormat.push_back({"mpegts", ipc::value("mpegts")});
	entries.push_back(recFormat);

	//Rec Encoder
	std::string currentRecQuality = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecQuality");

	if (currentRecQuality.compare("Small") == 0 || currentRecQuality.compare("HQ") == 0) {
		auto recEncoder = createSettingEntry("RecEncoder", "OBS_PROPERTY_LIST", "Encoder", "OBS_COMBO_FORMAT_STRING");

		getSimpleAvailableEncoders(&recEncoder, true, config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecFormat"));

		entries.push_back(recEncoder);
	}

	auto recAEncoder = createSettingEntry("RecAEncoder", "OBS_PROPERTY_LIST", "Audio Encoder", "OBS_COMBO_FORMAT_STRING");

	getAvailableAudioEncoders(&recAEncoder, true, true, config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecFormat"));

	entries.push_back(recAEncoder);

	// Custom Muxer Settings
	auto muxerCustom = createSettingEntry("MuxerCustom", "OBS_PROPERTY_EDIT_TEXT", "Custom Muxer Settings");
	entries.push_back(muxerCustom);

	outputSettings->push_back(serializeSettingsData("Recording", entries, config, "SimpleOutput", true, isCategoryEnabled));

	getReplayBufferSettings(outputSettings, config, false, isCategoryEnabled);
}

void OBS_settings::getEncoderSettings(const obs_encoder_t *encoder, obs_data_t *settings, std::vector<Parameter> *subCategoryParameters, int index,
				      bool isCategoryEnabled, bool applyServiceSettings, bool recordEncoder)
{
	obs_properties_t *encoderProperties = obs_encoder_properties(encoder);
	obs_property_t *property = obs_properties_first(encoderProperties);

	OBSData service_default_settings;
	if (applyServiceSettings && obs_encoder_get_type(encoder) == OBS_ENCODER_VIDEO) {
		service_default_settings = obs_data_create();
		// INT_MAX value is needed to get actual upper bound of service-default bitrate
		obs_data_set_int(service_default_settings, "bitrate", INT_MAX);
		obs_service_apply_encoder_settings(OBS_service::getService(StreamServiceId::Main), service_default_settings, nullptr);
	}

	Parameter param;
	while (property) {
		param.name = obs_property_name(property);
		obs_property_type typeProperty = obs_property_get_type(property);

		switch (typeProperty) {
		case OBS_PROPERTY_BOOL: {
			param.type = "OBS_PROPERTY_BOOL";
			param.description = obs_property_description(property);

			bool value = obs_data_get_bool(settings, param.name.c_str());

			param.currentValue.resize(sizeof(value));
			memcpy(param.currentValue.data(), &value, sizeof(value));
			param.sizeOfCurrentValue = sizeof(value);

			break;
		}
		case OBS_PROPERTY_INT: {
			param.type = "OBS_PROPERTY_INT";
			param.description = obs_property_description(property);

			int64_t value = obs_data_get_int(settings, param.name.c_str());

			param.currentValue.resize(sizeof(value));
			memcpy(param.currentValue.data(), &value, sizeof(value));
			param.sizeOfCurrentValue = sizeof(value);

			param.minVal = obs_property_int_min(property);
			param.maxVal = obs_property_int_max(property);
			param.stepVal = obs_property_int_step(property);
			break;
		}
		case OBS_PROPERTY_FLOAT: {
			param.type = "OBS_PROPERTY_DOUBLE";
			param.description = obs_property_description(property);

			double value = obs_data_get_double(settings, param.name.c_str());

			param.currentValue.resize(sizeof(value));
			memcpy(param.currentValue.data(), &value, sizeof(value));
			param.sizeOfCurrentValue = sizeof(value);

			param.minVal = obs_property_float_min(property);
			param.maxVal = obs_property_float_max(property);
			param.stepVal = obs_property_float_step(property);
			break;
		}
		case OBS_PROPERTY_TEXT: {
			param.type = "OBS_PROPERTY_TEXT";
			param.description = obs_property_description(property);

			const char *currentValue = obs_data_get_string(settings, param.name.c_str());

			if (currentValue == NULL) {
				currentValue = "";
			}

			param.currentValue.resize(strlen(currentValue));
			memcpy(param.currentValue.data(), currentValue, strlen(currentValue));
			param.sizeOfCurrentValue = strlen(currentValue);
			break;
		}
		case OBS_PROPERTY_PATH: {
			param.type = "OBS_PROPERTY_PATH";
			param.description = obs_property_description(property);

			const char *currentValue = obs_data_get_string(settings, param.name.c_str());

			if (currentValue == NULL) {
				currentValue = "";
			}

			param.currentValue.resize(strlen(currentValue));
			memcpy(param.currentValue.data(), currentValue, strlen(currentValue));
			param.sizeOfCurrentValue = strlen(currentValue);

			break;
		}
		case OBS_PROPERTY_LIST: {
			param.type = "OBS_PROPERTY_LIST";
			param.description = obs_property_description(property);

			obs_combo_format format = obs_property_list_format(property);

			if (format == OBS_COMBO_FORMAT_INT) {
				int64_t value = obs_data_get_int(settings, param.name.c_str());
				param.currentValue.resize(sizeof(value));
				memcpy(param.currentValue.data(), &value, sizeof(value));
				param.sizeOfCurrentValue = sizeof(value);

				param.minVal = obs_property_int_min(property);
				param.maxVal = obs_property_int_max(property);
				param.stepVal = obs_property_int_step(property);
			} else if (format == OBS_COMBO_FORMAT_FLOAT) {
				double value = obs_data_get_double(settings, param.name.c_str());
				param.currentValue.resize(sizeof(value));
				memcpy(param.currentValue.data(), &value, sizeof(value));
				param.sizeOfCurrentValue = sizeof(value);

				param.minVal = obs_property_float_min(property);
				param.maxVal = obs_property_float_max(property);
				param.stepVal = obs_property_float_step(property);
			} else if (format == OBS_COMBO_FORMAT_STRING) {
				const char *currentValue = obs_data_get_string(settings, param.name.c_str());

				if (currentValue == NULL) {
					currentValue = "";
				}

				param.currentValue.resize(strlen(currentValue));
				memcpy(param.currentValue.data(), currentValue, strlen(currentValue));
				param.sizeOfCurrentValue = strlen(currentValue);
			}

			int count = (int)obs_property_list_item_count(property);

			param.values.clear();

			for (int i = 0; i < count; i++) {
				// Name
				std::string itemName = obs_property_list_item_name(property, i);

				if (format == OBS_COMBO_FORMAT_INT) {
					param.subType = "OBS_COMBO_FORMAT_INT";

					uint64_t sizeName = itemName.length();
					std::vector<char> sizeNameBuffer;
					sizeNameBuffer.resize(sizeof(sizeName));
					memcpy(sizeNameBuffer.data(), &sizeName, sizeof(sizeName));

					param.values.insert(param.values.end(), sizeNameBuffer.begin(), sizeNameBuffer.end());
					param.values.insert(param.values.end(), itemName.begin(), itemName.end());

					int64_t value = obs_property_list_item_int(property, i);

					std::vector<char> valueBuffer;
					valueBuffer.resize(sizeof(uint64_t));
					memcpy(valueBuffer.data(), &value, sizeof(value));

					param.values.insert(param.values.end(), valueBuffer.begin(), valueBuffer.end());
				} else if (format == OBS_COMBO_FORMAT_FLOAT) {
					param.subType = "OBS_COMBO_FORMAT_FLOAT";

					uint64_t sizeName = itemName.length();
					std::vector<char> sizeNameBuffer;
					sizeNameBuffer.resize(sizeof(sizeName));
					memcpy(sizeNameBuffer.data(), &sizeName, sizeof(sizeName));

					param.values.insert(param.values.end(), sizeNameBuffer.begin(), sizeNameBuffer.end());
					param.values.insert(param.values.end(), itemName.begin(), itemName.end());

					double value = obs_property_list_item_float(property, i);

					std::vector<char> valueBuffer;
					valueBuffer.resize(sizeof(value));
					memcpy(valueBuffer.data(), &value, sizeof(value));

					param.values.insert(param.values.end(), valueBuffer.begin(), valueBuffer.end());
				} else if (format == OBS_COMBO_FORMAT_STRING) {
					param.subType = "OBS_COMBO_FORMAT_STRING";

					uint64_t sizeName = itemName.length();
					std::vector<char> sizeNameBuffer;
					sizeNameBuffer.resize(sizeof(sizeName));
					memcpy(sizeNameBuffer.data(), &sizeName, sizeof(sizeName));

					param.values.insert(param.values.end(), sizeNameBuffer.begin(), sizeNameBuffer.end());
					param.values.insert(param.values.end(), itemName.begin(), itemName.end());

					std::string value = obs_property_list_item_string(property, i);

					uint64_t sizeValue = value.length();
					std::vector<char> sizeValueBuffer;
					sizeValueBuffer.resize(sizeof(sizeValue));
					memcpy(sizeValueBuffer.data(), &sizeValue, sizeof(sizeValue));

					param.values.insert(param.values.end(), sizeValueBuffer.begin(), sizeValueBuffer.end());
					param.values.insert(param.values.end(), value.begin(), value.end());
				}
			}

			param.sizeOfValues = param.values.size();
			param.countValues = count;

			break;
		}
		case OBS_PROPERTY_EDITABLE_LIST: {
			param.type = "OBS_PROPERTY_EDITABLE_LIST";
			param.description = obs_property_description(property);

			const char *currentValue = obs_data_get_string(settings, param.name.c_str());

			if (currentValue == NULL)
				currentValue = "";

			param.currentValue.resize(strlen(currentValue));
			memcpy(param.currentValue.data(), currentValue, strlen(currentValue));
			param.sizeOfCurrentValue = strlen(currentValue);

			break;
		}
		}
		param.visible = obs_property_visible(property);

		bool isEnabled = obs_property_enabled(property);
		if (!isCategoryEnabled)
			isEnabled = isCategoryEnabled;

		if (applyServiceSettings) {
			auto data_item = obs_data_first(service_default_settings);
			while (data_item) {
				if (param.name == obs_data_item_get_name(data_item)) {
					isEnabled = false;

					const auto obsType = obs_data_item_gettype(data_item);
					if (obsType == OBS_DATA_STRING) {
						const auto obs_val = obs_data_item_get_string(data_item);
						const auto val_len = strlen(obs_val);
						param.currentValue.resize(val_len);
						memcpy(param.currentValue.data(), obs_val, val_len);
						param.sizeOfCurrentValue = val_len;
					} else if (obsType == OBS_DATA_NUMBER) {
						const auto obs_val = obs_data_item_get_int(data_item);
						if (param.name == "bitrate") {
							param.visible = true;
							int64_t cur_settings_value = obs_data_get_int(settings, param.name.c_str());

							const auto vbitrate_abs_max = obs_data_get_int(settings, "vbitrate_abs_max");
							if (!vbitrate_abs_max) {
								// Contains value of '10000000' deeply hardcoded inside OBS
								obs_data_set_int(settings, "vbitrate_abs_max", param.maxVal);
							}

							param.maxVal = obs_val;
							if (cur_settings_value > obs_val) {
								cur_settings_value = obs_val;
								param.currentValue.resize(sizeof(cur_settings_value));
								memcpy(param.currentValue.data(), &cur_settings_value, sizeof(cur_settings_value));
								param.sizeOfCurrentValue = sizeof(cur_settings_value);
							}
							isEnabled = true;
						} else {
							param.currentValue.resize(sizeof(obs_val));
							memcpy(param.currentValue.data(), &obs_val, sizeof(obs_val));
							param.sizeOfCurrentValue = sizeof(obs_val);
						}
					}
				} else if (param.name == "crf") {
					param.visible = false;
				}
				obs_data_item_next(&data_item);
			}

		} else {
			// Restoring back prev value if any
			const auto vbitrate_abs_max = obs_data_get_int(settings, "vbitrate_abs_max");
			obs_data_erase(settings, "vbitrate_abs_max");
			if (param.name == "bitrate" && vbitrate_abs_max) {
				param.maxVal = vbitrate_abs_max;
			}
		}

		param.enabled = isEnabled;
		param.masked = false;

		if (recordEncoder) {
			param.name.insert(0, "Rec");
		}

		subCategoryParameters->push_back(param);

		obs_property_next(&property);
	}

	obs_properties_destroy(encoderProperties);
}

SubCategory OBS_settings::getAdvancedOutputStreamingSettings(config_t *config, bool isCategoryEnabled)
{
	int index = 0;

	SubCategory streamingSettings;
	std::vector<std::vector<std::pair<std::string, std::string>>> entries;

	streamingSettings.name = "Streaming";

	// Audio Track : list
	Parameter trackIndex;
	trackIndex.name = "TrackIndex";
	trackIndex.type = "OBS_PROPERTY_LIST";
	trackIndex.subType = "OBS_COMBO_FORMAT_STRING";
	trackIndex.description = "Audio Track";

	std::vector<std::pair<std::string, std::string>> trackIndexValues;
	trackIndexValues.push_back(std::make_pair("1", "1"));
	trackIndexValues.push_back(std::make_pair("2", "2"));
	trackIndexValues.push_back(std::make_pair("3", "3"));
	trackIndexValues.push_back(std::make_pair("4", "4"));
	trackIndexValues.push_back(std::make_pair("5", "5"));
	trackIndexValues.push_back(std::make_pair("6", "6"));

	for (int i = 0; i < trackIndexValues.size(); i++) {
		std::string name = trackIndexValues.at(i).first;

		uint64_t sizeName = name.length();
		std::vector<char> sizeNameBuffer;
		sizeNameBuffer.resize(sizeof(sizeName));
		memcpy(sizeNameBuffer.data(), &sizeName, sizeof(sizeName));

		trackIndex.values.insert(trackIndex.values.end(), sizeNameBuffer.begin(), sizeNameBuffer.end());
		trackIndex.values.insert(trackIndex.values.end(), name.begin(), name.end());

		std::string value = trackIndexValues.at(i).second;

		uint64_t sizeValue = value.length();
		std::vector<char> sizeValueBuffer;
		sizeValueBuffer.resize(sizeof(sizeValue));
		memcpy(sizeValueBuffer.data(), &sizeValue, sizeof(sizeValue));

		trackIndex.values.insert(trackIndex.values.end(), sizeValueBuffer.begin(), sizeValueBuffer.end());
		trackIndex.values.insert(trackIndex.values.end(), value.begin(), value.end());
	}

	trackIndex.sizeOfValues = trackIndex.values.size();
	trackIndex.countValues = trackIndexValues.size();

	const char *trackIndexCurrentValue = config_get_string(config, "AdvOut", "TrackIndex");
	if (trackIndexCurrentValue == NULL)
		trackIndexCurrentValue = "";

	trackIndex.currentValue.resize(strlen(trackIndexCurrentValue));
	memcpy(trackIndex.currentValue.data(), trackIndexCurrentValue, strlen(trackIndexCurrentValue));
	trackIndex.sizeOfCurrentValue = strlen(trackIndexCurrentValue);

	trackIndex.visible = true;
	trackIndex.enabled = isCategoryEnabled;
	trackIndex.masked = false;

	streamingSettings.params.push_back(trackIndex);

	obs_data_t *serviceSettings = obs_service_get_settings(OBS_service::getService(StreamServiceId::Main)); //todo DUALOUTPUT
	const char *serviceName = obs_data_get_string(serviceSettings, "service");
	obs_data_release(serviceSettings);

	if (serviceName && strcmp(serviceName, "Twitch") == 0) {
		bool soundtrackSourceExists = false;
		obs_enum_sources(
			[](void *param, obs_source_t *source) {
				auto id = obs_source_get_id(source);
				if (strcmp(id, "soundtrack_source") == 0) {
					*reinterpret_cast<bool *>(param) = true;
					return false;
				}
				return true;
			},
			&soundtrackSourceExists);
		std::string twitchVODDesc = "Twitch VOD";
		if (soundtrackSourceExists)
			twitchVODDesc += ". Remove Twitch Soundtrack in order to enable this.";

		// Twitch VOD : boolean
		Parameter twiwchVOD;
		twiwchVOD.name = "VodTrackEnabled";
		twiwchVOD.type = "OBS_PROPERTY_BOOL";
		twiwchVOD.description = twitchVODDesc;

		bool doTwiwchVOD = config_get_bool(config, "AdvOut", "VodTrackEnabled");

		twiwchVOD.currentValue.resize(sizeof(doTwiwchVOD));
		memcpy(twiwchVOD.currentValue.data(), &doTwiwchVOD, sizeof(doTwiwchVOD));
		twiwchVOD.sizeOfCurrentValue = sizeof(doTwiwchVOD);

		twiwchVOD.visible = true;
		twiwchVOD.enabled = isCategoryEnabled;
		twiwchVOD.masked = false;

		streamingSettings.params.push_back(twiwchVOD);

		if (doTwiwchVOD) {
			// Twitch Audio track: list
			Parameter trackVODIndex;
			trackVODIndex.name = "VodTrackIndex";
			trackVODIndex.type = "OBS_PROPERTY_LIST";
			trackVODIndex.subType = "OBS_COMBO_FORMAT_STRING";
			trackVODIndex.description = "Twitch VOD Track";

			std::vector<std::pair<std::string, std::string>> trackVODIndexValues;
			trackVODIndexValues.push_back(std::make_pair("1", "1"));
			trackVODIndexValues.push_back(std::make_pair("2", "2"));
			trackVODIndexValues.push_back(std::make_pair("3", "3"));
			trackVODIndexValues.push_back(std::make_pair("4", "4"));
			trackVODIndexValues.push_back(std::make_pair("5", "5"));
			trackVODIndexValues.push_back(std::make_pair("6", "6"));

			for (int i = 0; i < trackVODIndexValues.size(); i++) {
				std::string name = trackVODIndexValues.at(i).first;

				uint64_t sizeName = name.length();
				std::vector<char> sizeNameBuffer;
				sizeNameBuffer.resize(sizeof(sizeName));
				memcpy(sizeNameBuffer.data(), &sizeName, sizeof(sizeName));

				trackVODIndex.values.insert(trackVODIndex.values.end(), sizeNameBuffer.begin(), sizeNameBuffer.end());
				trackVODIndex.values.insert(trackVODIndex.values.end(), name.begin(), name.end());

				std::string value = trackVODIndexValues.at(i).second;

				uint64_t sizeValue = value.length();
				std::vector<char> sizeValueBuffer;
				sizeValueBuffer.resize(sizeof(sizeValue));
				memcpy(sizeValueBuffer.data(), &sizeValue, sizeof(sizeValue));

				trackVODIndex.values.insert(trackVODIndex.values.end(), sizeValueBuffer.begin(), sizeValueBuffer.end());
				trackVODIndex.values.insert(trackVODIndex.values.end(), value.begin(), value.end());
			}

			trackVODIndex.sizeOfValues = trackVODIndex.values.size();
			trackVODIndex.countValues = trackVODIndexValues.size();

			const char *trackVODIndexCurrentValue = config_get_string(config, "AdvOut", "VodTrackIndex");
			if (trackVODIndexCurrentValue == NULL)
				trackVODIndexCurrentValue = "";

			trackVODIndex.currentValue.resize(strlen(trackVODIndexCurrentValue));
			memcpy(trackVODIndex.currentValue.data(), trackVODIndexCurrentValue, strlen(trackVODIndexCurrentValue));
			trackVODIndex.sizeOfCurrentValue = strlen(trackVODIndexCurrentValue);

			trackVODIndex.visible = true;
			trackVODIndex.enabled = isCategoryEnabled;
			trackVODIndex.masked = false;

			streamingSettings.params.push_back(trackVODIndex);
		}
	}

	// Encoder : list
	Parameter videoEncoders;
	videoEncoders.name = "Encoder";
	videoEncoders.type = "OBS_PROPERTY_LIST";
	videoEncoders.description = "Encoder";
	videoEncoders.subType = "OBS_COMBO_FORMAT_STRING";

	const char *encoderCurrentValue = config_get_string(config, "AdvOut", "Encoder");
	if (encoderCurrentValue == NULL) {
		encoderCurrentValue = "";
	}

#ifdef __APPLE__
	encoderCurrentValue = translate_macvth264_encoder(std::string(encoderCurrentValue));
	config_set_string(config, "AdvOut", "Encoder", encoderCurrentValue);

#endif

	videoEncoders.currentValue.resize(strlen(encoderCurrentValue));
	memcpy(videoEncoders.currentValue.data(), encoderCurrentValue, strlen(encoderCurrentValue));
	videoEncoders.sizeOfCurrentValue = strlen(encoderCurrentValue);

	std::vector<std::pair<std::string, ipc::value>> encoderValues;
	getAdvancedAvailableEncoders(&encoderValues, false, config_get_string(config, "AdvOut", "RecFormat"));

	for (int i = 0; i < encoderValues.size(); i++) {
		std::string name = encoderValues.at(i).first;

		uint64_t sizeName = name.length();
		std::vector<char> sizeNameBuffer;
		sizeNameBuffer.resize(sizeof(sizeName));
		memcpy(sizeNameBuffer.data(), &sizeName, sizeof(sizeName));

		videoEncoders.values.insert(videoEncoders.values.end(), sizeNameBuffer.begin(), sizeNameBuffer.end());
		videoEncoders.values.insert(videoEncoders.values.end(), name.begin(), name.end());

		std::string value = encoderValues.at(i).second.value_str;

		uint64_t sizeValue = value.length();
		std::vector<char> sizeValueBuffer;
		sizeValueBuffer.resize(sizeof(sizeValue));
		memcpy(sizeValueBuffer.data(), &sizeValue, sizeof(sizeValue));

		videoEncoders.values.insert(videoEncoders.values.end(), sizeValueBuffer.begin(), sizeValueBuffer.end());
		videoEncoders.values.insert(videoEncoders.values.end(), value.begin(), value.end());
	}

	videoEncoders.sizeOfValues = videoEncoders.values.size();
	videoEncoders.countValues = encoderValues.size();

	videoEncoders.visible = true;
	videoEncoders.enabled = isCategoryEnabled;
	videoEncoders.masked = false;

	streamingSettings.params.push_back(videoEncoders);

	// Enforce streaming service encoder settings : boolean
	Parameter applyServiceSettings;
	applyServiceSettings.name = "ApplyServiceSettings";
	applyServiceSettings.type = "OBS_PROPERTY_BOOL";
	applyServiceSettings.description = "Enforce streaming service encoder settings";

	bool applyServiceSettingsValue = config_get_bool(config, "AdvOut", "ApplyServiceSettings");

	applyServiceSettings.currentValue.resize(sizeof(applyServiceSettingsValue));
	memcpy(applyServiceSettings.currentValue.data(), &applyServiceSettingsValue, sizeof(applyServiceSettingsValue));
	applyServiceSettings.sizeOfCurrentValue = sizeof(applyServiceSettingsValue);

	applyServiceSettings.visible = true;
	applyServiceSettings.enabled = isCategoryEnabled;
	applyServiceSettings.masked = false;

	streamingSettings.params.push_back(applyServiceSettings);

	// Rescale Output : boolean
	Parameter rescale;
	rescale.name = "Rescale";
	rescale.type = "OBS_PROPERTY_BOOL";
	rescale.description = "Rescale Output";

	bool doRescale = config_get_bool(config, "AdvOut", "Rescale");

	rescale.currentValue.resize(sizeof(doRescale));
	memcpy(rescale.currentValue.data(), &doRescale, sizeof(doRescale));
	rescale.sizeOfCurrentValue = sizeof(doRescale);

	rescale.visible = strcmp(encoderCurrentValue, ENCODER_NEW_NVENC) != 0;
	rescale.enabled = isCategoryEnabled;
	rescale.masked = false;

	streamingSettings.params.push_back(rescale);

	if (doRescale) {
		// Output Resolution : list
		Parameter rescaleRes;
		rescaleRes.name = "RescaleRes";
		rescaleRes.type = "OBS_INPUT_RESOLUTION_LIST";
		rescaleRes.description = "Output Resolution";
		rescaleRes.subType = "OBS_COMBO_FORMAT_STRING";

		uint64_t base_cx = config_get_uint(config, "Video", "BaseCX");
		uint64_t base_cy = config_get_uint(config, "Video", "BaseCY");

		const char *outputResString = config_get_string(config, "AdvOut", "RescaleRes");

		if (outputResString == NULL) {
			outputResString = "1280x720";
			config_set_string(config, "AdvOut", "RescaleRes", outputResString);
			config_save_safe(config, "tmp", nullptr);
		}

		rescaleRes.currentValue.resize(strlen(outputResString));
		memcpy(rescaleRes.currentValue.data(), outputResString, strlen(outputResString));
		rescaleRes.sizeOfCurrentValue = strlen(outputResString);

		std::vector<std::pair<uint64_t, uint64_t>> outputResolutions = getOutputResolutions(base_cx, base_cy);

		uint32_t indexDataRescaleRes = 0;

		for (int i = 0; i < outputResolutions.size(); i++) {
			std::string outRes = ResString(outputResolutions.at(i).first, outputResolutions.at(i).second);

			for (int j = 0; j < 2; j++) {
				uint64_t sizeRes = outRes.length();
				std::vector<char> sizeNameBuffer;
				sizeNameBuffer.resize(sizeof(sizeRes));
				memcpy(sizeNameBuffer.data(), &sizeRes, sizeof(sizeRes));

				rescaleRes.values.insert(rescaleRes.values.end(), sizeNameBuffer.begin(), sizeNameBuffer.end());
				rescaleRes.values.insert(rescaleRes.values.end(), outRes.begin(), outRes.end());
			}
		}

		rescaleRes.sizeOfValues = rescaleRes.values.size();
		rescaleRes.countValues = outputResolutions.size();

		rescaleRes.visible = strcmp(encoderCurrentValue, ENCODER_NEW_NVENC) != 0;
		rescaleRes.enabled = isCategoryEnabled;
		rescaleRes.masked = false;

		streamingSettings.params.push_back(rescaleRes);
	}

	// Encoder settings
	const char *encoderID = config_get_string(config, "AdvOut", "Encoder");
	if (encoderID == NULL) {
		encoderID = "obs_x264";
		config_set_string(config, "AdvOut", "Encoder", encoderID);
		config_save_safe(config, "tmp", nullptr);
	}

	obs_data_t *settings = obs_encoder_defaults(encoderID);
	obs_encoder_t *streamingEncoder = OBS_service::getStreamingEncoder(StreamServiceId::Main);
	obs_encoder_t *recordEncoder = obs_output_get_video_encoder(OBS_service::getRecordingOutput());
	obs_output_t *streamOutputMain = OBS_service::getStreamingOutput(StreamServiceId::Main);
	obs_output_t *streamOutputSecond = OBS_service::getStreamingOutput(StreamServiceId::Second);
	obs_output_t *recordOutput = OBS_service::getRecordingOutput();

	/*
		If the stream and recording outputs uses the same encoders, we need to check if both are ready
		to update before recreating the stream encoder to prevent releasing it when it's still being used.
		If they use differente encoders, just check for the stream output.
	*/
	bool streamOutputIsReadyToUpdate = true;
	streamOutputIsReadyToUpdate &= streamOutputMain ? obs_output_is_ready_to_update(streamOutputMain) : false;
	streamOutputIsReadyToUpdate &= streamOutputSecond ? obs_output_is_ready_to_update(streamOutputSecond) : false;

	bool recOutputIsReadyToUpdate = recordOutput ? obs_output_is_ready_to_update(recordOutput) : false;

	bool recOutputBlockStreamOutput = false;
	if (streamingEncoder == recordEncoder) {
		recOutputBlockStreamOutput = !recOutputIsReadyToUpdate;
	}

	if ((streamOutputIsReadyToUpdate && !recOutputBlockStreamOutput) || streamingEncoder == nullptr) {
		struct stat buffer;
		std::string streamConfigFile = ConfigManager::getInstance().getStream();
		bool fileExist = (os_stat(streamConfigFile.c_str(), &buffer) == 0);

		std::string encoder_name = OBS_service::GetVideoEncoderName(StreamServiceId::Main, false, false, encoderID);
		if (!fileExist) {
			streamingEncoder = obs_video_encoder_create(encoderID, encoder_name.c_str(), nullptr, nullptr);
			OBS_service::setStreamingEncoder(streamingEncoder, StreamServiceId::Main);

			if (!obs_data_save_json_safe(settings, streamConfigFile.c_str(), "tmp", "bak")) {
				blog(LOG_WARNING, "Failed to save encoder %s", streamConfigFile.c_str());
			}
		} else {
			obs_data_t *data = obs_data_create_from_json_file_safe(streamConfigFile.c_str(), "bak");

			update_nvenc_presets(data, encoderID);

			obs_data_apply(settings, data);
			streamingEncoder = obs_video_encoder_create(encoderID, encoder_name.c_str(), settings, nullptr);
			OBS_service::setStreamingEncoder(streamingEncoder, StreamServiceId::Main);
		}

	} else {
		settings = obs_encoder_get_settings(streamingEncoder);
	}

	getEncoderSettings(streamingEncoder, settings, &(streamingSettings.params), index, isCategoryEnabled, applyServiceSettingsValue, false);
	streamingSettings.paramsCount = streamingSettings.params.size();
	return streamingSettings;
}

void OBS_settings::getStandardRecordingSettings(SubCategory *subCategoryParameters, config_t *config, bool isCategoryEnabled)
{
	int index = 1;

	// Recording Path : file
	Parameter recFilePath;
	recFilePath.name = "RecFilePath";
	recFilePath.type = "OBS_PROPERTY_PATH";
	recFilePath.description = "Recording Path";

	const char *RecFilePathCurrentValue = config_get_string(config, "AdvOut", "RecFilePath");

	if (RecFilePathCurrentValue == NULL) {
		std::string RecFilePathTextString = OBS_service::GetDefaultVideoSavePath();
		const char *RecFilePathText = RecFilePathTextString.c_str();

		recFilePath.currentValue.resize(strlen(RecFilePathText));
		memcpy(recFilePath.currentValue.data(), RecFilePathText, strlen(RecFilePathText));
		recFilePath.sizeOfCurrentValue = strlen(RecFilePathText);
	} else {
		recFilePath.currentValue.resize(strlen(RecFilePathCurrentValue));
		memcpy(recFilePath.currentValue.data(), RecFilePathCurrentValue, strlen(RecFilePathCurrentValue));
		recFilePath.sizeOfCurrentValue = strlen(RecFilePathCurrentValue);
	}

	recFilePath.visible = true;
	recFilePath.enabled = isCategoryEnabled;
	recFilePath.masked = false;

	subCategoryParameters->params.push_back(recFilePath);

	// Generate File Name without Space : boolean
	Parameter recFileNameWithoutSpace;
	recFileNameWithoutSpace.name = "RecFileNameWithoutSpace";
	recFileNameWithoutSpace.type = "OBS_PROPERTY_BOOL";
	recFileNameWithoutSpace.description = "Generate File Name without Space";

	bool noSpace = config_get_bool(config, "AdvOut", "RecFileNameWithoutSpace");
	recFileNameWithoutSpace.currentValue.resize(sizeof(noSpace));
	memcpy(recFileNameWithoutSpace.currentValue.data(), &noSpace, sizeof(noSpace));
	recFileNameWithoutSpace.sizeOfCurrentValue = sizeof(noSpace);

	recFileNameWithoutSpace.visible = true;
	recFileNameWithoutSpace.enabled = isCategoryEnabled;
	recFileNameWithoutSpace.masked = false;

	subCategoryParameters->params.push_back(recFileNameWithoutSpace);

	// Recording Format : list
	Parameter recFormat;
	recFormat.name = "RecFormat";
	recFormat.type = "OBS_PROPERTY_LIST";
	recFormat.description = "Recording Format";
	recFormat.subType = "OBS_COMBO_FORMAT_STRING";

	const char *recFormatCurrentValue = config_get_string(config, "AdvOut", "RecFormat");
	if (recFormatCurrentValue == NULL)
		recFormatCurrentValue = "";

	recFormat.currentValue.resize(strlen(recFormatCurrentValue));
	memcpy(recFormat.currentValue.data(), recFormatCurrentValue, strlen(recFormatCurrentValue));
	recFormat.sizeOfCurrentValue = strlen(recFormatCurrentValue);

	std::vector<std::pair<std::string, std::string>> recFormatValues;
	recFormatValues.push_back(std::make_pair("mp4", "mp4"));
	recFormatValues.push_back(std::make_pair("flv", "flv"));
	recFormatValues.push_back(std::make_pair("mov", "mov"));
	recFormatValues.push_back(std::make_pair("mkv", "mkv"));
	recFormatValues.push_back(std::make_pair("mpegts", "mpegts"));
	recFormatValues.push_back(std::make_pair("hls", "hls"));

	uint32_t indexDataRecFormat = 0;

	for (int i = 0; i < recFormatValues.size(); i++) {
		std::string name = recFormatValues.at(i).first;

		uint64_t sizeName = name.length();
		std::vector<char> sizeNameBuffer;
		sizeNameBuffer.resize(sizeof(sizeName));
		memcpy(sizeNameBuffer.data(), &sizeName, sizeof(sizeName));

		recFormat.values.insert(recFormat.values.end(), sizeNameBuffer.begin(), sizeNameBuffer.end());
		recFormat.values.insert(recFormat.values.end(), name.begin(), name.end());

		std::string value = recFormatValues.at(i).second;

		uint64_t sizeValue = value.length();
		std::vector<char> sizeValueBuffer;
		sizeValueBuffer.resize(sizeof(sizeValue));
		memcpy(sizeValueBuffer.data(), &sizeValue, sizeof(sizeValue));

		recFormat.values.insert(recFormat.values.end(), sizeValueBuffer.begin(), sizeValueBuffer.end());
		recFormat.values.insert(recFormat.values.end(), value.begin(), value.end());
	}

	recFormat.sizeOfValues = recFormat.values.size();
	recFormat.countValues = recFormatValues.size();

	recFormat.visible = true;
	recFormat.enabled = isCategoryEnabled;
	recFormat.masked = false;

	subCategoryParameters->params.push_back(recFormat);

	// Audio Track : list

	std::string recTracksDesc =
		std::string("Audio Track") +
		(IsMultitrackAudioSupported(recFormatCurrentValue) ? "" : " (Format FLV does not support multiple audio tracks per recording)");

	Parameter recTracks;
	recTracks.name = "RecTracks";
	recTracks.type = "OBS_PROPERTY_BITMASK";
	recTracks.description = recTracksDesc;
	recTracks.subType = "";

	uint64_t recTracksCurrentValue = config_get_uint(config, "AdvOut", "RecTracks");

	recTracks.currentValue.resize(sizeof(recTracksCurrentValue));
	memcpy(recTracks.currentValue.data(), &recTracksCurrentValue, sizeof(recTracksCurrentValue));
	recTracks.sizeOfCurrentValue = sizeof(recTracksCurrentValue);

	recTracks.visible = true;
	recTracks.enabled = IsMultitrackAudioSupported(recFormatCurrentValue);
	recTracks.masked = false;

	subCategoryParameters->params.push_back(recTracks);

	// Encoder : list
	Parameter recEncoder;
	recEncoder.name = "RecEncoder";
	recEncoder.type = "OBS_PROPERTY_LIST";
	recEncoder.description = "Recording";
	recEncoder.subType = "OBS_COMBO_FORMAT_STRING";

	const char *recEncoderCurrentValue = config_get_string(config, "AdvOut", "RecEncoder");
	if (!recEncoderCurrentValue)
		recEncoderCurrentValue = "none";

#ifdef __APPLE__
	recEncoderCurrentValue = translate_macvth264_encoder(std::string(recEncoderCurrentValue));
	config_set_string(config, "AdvOut", "RecEncoder", recEncoderCurrentValue);
#endif

	recEncoder.currentValue.resize(strlen(recEncoderCurrentValue));
	memcpy(recEncoder.currentValue.data(), recEncoderCurrentValue, strlen(recEncoderCurrentValue));
	recEncoder.sizeOfCurrentValue = strlen(recEncoderCurrentValue);

	std::vector<std::pair<std::string, ipc::value>> Encoder;
	Encoder.push_back(std::make_pair("Use stream encoder", ipc::value("none")));
	getAdvancedAvailableEncoders(&Encoder, true, recFormatCurrentValue);

	uint32_t indexDataRecEncoder = 0;

	for (int i = 0; i < Encoder.size(); i++) {
		std::string name = Encoder.at(i).first;

		uint64_t sizeName = name.length();
		std::vector<char> sizeNameBuffer;
		sizeNameBuffer.resize(sizeof(sizeName));
		memcpy(sizeNameBuffer.data(), &sizeName, sizeof(sizeName));

		recEncoder.values.insert(recEncoder.values.end(), sizeNameBuffer.begin(), sizeNameBuffer.end());
		recEncoder.values.insert(recEncoder.values.end(), name.begin(), name.end());

		std::string value = Encoder.at(i).second.value_str;

		uint64_t sizeValue = value.length();
		std::vector<char> sizeValueBuffer;
		sizeValueBuffer.resize(sizeof(sizeValue));
		memcpy(sizeValueBuffer.data(), &sizeValue, sizeof(sizeValue));

		recEncoder.values.insert(recEncoder.values.end(), sizeValueBuffer.begin(), sizeValueBuffer.end());
		recEncoder.values.insert(recEncoder.values.end(), value.begin(), value.end());
	}

	recEncoder.sizeOfValues = recEncoder.values.size();
	recEncoder.countValues = Encoder.size();

	recEncoder.visible = true;
	recEncoder.enabled = isCategoryEnabled;
	recEncoder.masked = false;

	subCategoryParameters->params.push_back(recEncoder);

	// Audio Encoder : list
	Parameter recAEncoder;
	recAEncoder.name = "RecAEncoder";
	recAEncoder.type = "OBS_PROPERTY_LIST";
	recAEncoder.description = "Recording Audio Encoder";
	recAEncoder.subType = "OBS_COMBO_FORMAT_STRING";

	const char *recAEncoderCurrentValue = config_get_string(config, "AdvOut", "RecAEncoder");
	if (!recAEncoderCurrentValue)
		recAEncoderCurrentValue = "none";

	recAEncoder.currentValue.resize(strlen(recAEncoderCurrentValue));
	memcpy(recAEncoder.currentValue.data(), recAEncoderCurrentValue, strlen(recAEncoderCurrentValue));
	recAEncoder.sizeOfCurrentValue = strlen(recAEncoderCurrentValue);

	std::vector<std::pair<std::string, ipc::value>> AEncoder;

	getAvailableAudioEncoders(&AEncoder, false, true, config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecFormat"));

	uint32_t indexDatarecAEncoder = 0;

	for (int i = 0; i < AEncoder.size(); i++) {
		std::string name = AEncoder.at(i).first;

		uint64_t sizeName = name.length();
		std::vector<char> sizeNameBuffer;
		sizeNameBuffer.resize(sizeof(sizeName));
		memcpy(sizeNameBuffer.data(), &sizeName, sizeof(sizeName));

		recAEncoder.values.insert(recAEncoder.values.end(), sizeNameBuffer.begin(), sizeNameBuffer.end());
		recAEncoder.values.insert(recAEncoder.values.end(), name.begin(), name.end());

		std::string value = AEncoder.at(i).second.value_str;

		uint64_t sizeValue = value.length();
		std::vector<char> sizeValueBuffer;
		sizeValueBuffer.resize(sizeof(sizeValue));
		memcpy(sizeValueBuffer.data(), &sizeValue, sizeof(sizeValue));

		recAEncoder.values.insert(recAEncoder.values.end(), sizeValueBuffer.begin(), sizeValueBuffer.end());
		recAEncoder.values.insert(recAEncoder.values.end(), value.begin(), value.end());
	}

	recAEncoder.sizeOfValues = recAEncoder.values.size();
	recAEncoder.countValues = AEncoder.size();

	recAEncoder.visible = true;
	recAEncoder.enabled = isCategoryEnabled;
	recAEncoder.masked = false;

	subCategoryParameters->params.push_back(recAEncoder);

	const char *streamEncoderCurrentValue = config_get_string(config, "AdvOut", "Encoder");
	bool streamScaleAvailable = strcmp(recEncoderCurrentValue, "none") != 0;

	// Rescale Output : boolean
	Parameter recRescale;
	recRescale.name = "RecRescale";
	recRescale.type = "OBS_PROPERTY_BOOL";
	recRescale.description = "Rescale Output";

	bool doRescale = config_get_bool(config, "AdvOut", "RecRescale");

	recRescale.currentValue.resize(sizeof(doRescale));
	memcpy(recRescale.currentValue.data(), &doRescale, sizeof(doRescale));
	recRescale.sizeOfCurrentValue = sizeof(doRescale);

	recRescale.visible = strcmp(recEncoderCurrentValue, ENCODER_NEW_NVENC) != 0 && streamScaleAvailable;
	recRescale.enabled = isCategoryEnabled;
	recRescale.masked = false;

	subCategoryParameters->params.push_back(recRescale);

	// Output Resolution : list
	if (doRescale) {
		// Output Resolution : list
		Parameter recRescaleRes;
		recRescaleRes.name = "RecRescaleRes";
		recRescaleRes.type = "OBS_INPUT_RESOLUTION_LIST";
		recRescaleRes.description = "Output Resolution";
		recRescaleRes.subType = "OBS_COMBO_FORMAT_STRING";

		uint64_t base_cx = config_get_uint(config, "Video", "BaseCX");
		uint64_t base_cy = config_get_uint(config, "Video", "BaseCY");

		const char *outputResString = config_get_string(config, "AdvOut", "RecRescaleRes");

		if (outputResString == NULL) {
			outputResString = "1280x720";
			config_set_string(config, "AdvOut", "RecRescaleRes", outputResString);
			config_save_safe(config, "tmp", nullptr);
		}

		recRescaleRes.currentValue.resize(strlen(outputResString));
		memcpy(recRescaleRes.currentValue.data(), outputResString, strlen(outputResString));
		recRescaleRes.sizeOfCurrentValue = strlen(outputResString);

		std::vector<std::pair<uint64_t, uint64_t>> outputResolutions = getOutputResolutions(base_cx, base_cy);

		uint32_t indexDataRecRescaleRes = 0;

		for (int i = 0; i < outputResolutions.size(); i++) {
			std::string outRes = ResString(outputResolutions.at(i).first, outputResolutions.at(i).second);

			for (int j = 0; j < 2; j++) {
				uint64_t sizeRes = outRes.length();
				std::vector<char> sizeNameBuffer;
				sizeNameBuffer.resize(sizeof(sizeRes));
				memcpy(sizeNameBuffer.data(), &sizeRes, sizeof(sizeRes));

				recRescaleRes.values.insert(recRescaleRes.values.end(), sizeNameBuffer.begin(), sizeNameBuffer.end());
				recRescaleRes.values.insert(recRescaleRes.values.end(), outRes.begin(), outRes.end());
			}
		}

		recRescaleRes.sizeOfValues = recRescaleRes.values.size();
		recRescaleRes.countValues = outputResolutions.size();

		recRescaleRes.visible = strcmp(recEncoderCurrentValue, ENCODER_NEW_NVENC) != 0 && streamScaleAvailable;
		recRescaleRes.enabled = isCategoryEnabled;
		recRescaleRes.masked = false;

		subCategoryParameters->params.push_back(recRescaleRes);
	}

	// Custom Muxer Settings : edit_text
	Parameter recMuxerCustom;
	recMuxerCustom.name = "RecMuxerCustom";
	recMuxerCustom.type = "OBS_PROPERTY_EDIT_TEXT";
	recMuxerCustom.description = "Custom Muxer Settings";

	const char *RecMuxerCustomCurrentValue = config_get_string(config, "AdvOut", "RecMuxerCustom");
	if (RecMuxerCustomCurrentValue == NULL)
		RecMuxerCustomCurrentValue = "";

	recMuxerCustom.currentValue.resize(strlen(RecMuxerCustomCurrentValue));
	memcpy(recMuxerCustom.currentValue.data(), RecMuxerCustomCurrentValue, strlen(RecMuxerCustomCurrentValue));
	recMuxerCustom.sizeOfCurrentValue = strlen(RecMuxerCustomCurrentValue);

	recMuxerCustom.visible = true;
	recMuxerCustom.enabled = isCategoryEnabled;
	recMuxerCustom.masked = false;

	subCategoryParameters->params.push_back(recMuxerCustom);

	// Automatic File Splitting
	Parameter recSplitFile;
	recSplitFile.name = "RecSplitFile";
	recSplitFile.type = "OBS_PROPERTY_BOOL";
	recSplitFile.description = "Automatic File Splitting";

	bool recSplitFileVal = config_get_bool(config, "AdvOut", "RecSplitFile");

	recSplitFile.currentValue.resize(sizeof(recSplitFileVal));
	memcpy(recSplitFile.currentValue.data(), &recSplitFileVal, sizeof(recSplitFileVal));
	recSplitFile.sizeOfCurrentValue = sizeof(recSplitFileVal);

	recSplitFile.visible = true;
	recSplitFile.enabled = isCategoryEnabled;
	recSplitFile.masked = false;

	subCategoryParameters->params.push_back(recSplitFile);

	// Automatic File Splitting
	Parameter recSplitFileType;
	recSplitFileType.name = "RecSplitFileType";
	recSplitFileType.type = "OBS_PROPERTY_LIST";
	recSplitFileType.description = "";
	recSplitFileType.subType = "OBS_COMBO_FORMAT_STRING";

	const char *recSplitFileTypeVal = config_get_string(config, "AdvOut", "RecSplitFileType");
	if (recSplitFileTypeVal == NULL)
		recSplitFileTypeVal = "Time";

	recSplitFileType.currentValue.resize(strlen(recSplitFileTypeVal));
	memcpy(recSplitFileType.currentValue.data(), recSplitFileTypeVal, strlen(recSplitFileTypeVal));
	recSplitFileType.sizeOfCurrentValue = strlen(recSplitFileTypeVal);

	std::vector<std::pair<std::string, std::string>> recSplitFileTypeValues;
	recSplitFileTypeValues.push_back(std::make_pair("Split by Time", "Time"));
	recSplitFileTypeValues.push_back(std::make_pair("Split by Size", "Size"));
	recSplitFileTypeValues.push_back(std::make_pair("Only split manually", "Manual"));

	for (int i = 0; i < recSplitFileTypeValues.size(); i++) {
		std::string name = recSplitFileTypeValues.at(i).first;

		uint64_t sizeName = name.length();
		std::vector<char> sizeNameBuffer;
		sizeNameBuffer.resize(sizeof(sizeName));
		memcpy(sizeNameBuffer.data(), &sizeName, sizeof(sizeName));

		recSplitFileType.values.insert(recSplitFileType.values.end(), sizeNameBuffer.begin(), sizeNameBuffer.end());
		recSplitFileType.values.insert(recSplitFileType.values.end(), name.begin(), name.end());

		std::string value = recSplitFileTypeValues.at(i).second;

		uint64_t sizeValue = value.length();
		std::vector<char> sizeValueBuffer;
		sizeValueBuffer.resize(sizeof(sizeValue));
		memcpy(sizeValueBuffer.data(), &sizeValue, sizeof(sizeValue));

		recSplitFileType.values.insert(recSplitFileType.values.end(), sizeValueBuffer.begin(), sizeValueBuffer.end());
		recSplitFileType.values.insert(recSplitFileType.values.end(), value.begin(), value.end());
	}

	recSplitFileType.sizeOfValues = recSplitFileType.values.size();
	recSplitFileType.countValues = recSplitFileTypeValues.size();
	recSplitFileType.visible = recSplitFileVal;
	recSplitFileType.enabled = isCategoryEnabled;
	recSplitFileType.masked = false;

	subCategoryParameters->params.push_back(recSplitFileType);

	if (strcmp(recSplitFileTypeVal, "Time") == 0) {
		Parameter recSplitFileTime;
		recSplitFileTime.name = "RecSplitFileTime";
		recSplitFileTime.type = "OBS_PROPERTY_UINT";
		recSplitFileTime.description = "Split Time (min)";

		uint64_t recSplitFileTimeVal = config_get_uint(config, "AdvOut", "RecSplitFileTime");

		recSplitFileTime.currentValue.resize(sizeof(recSplitFileTimeVal));
		memcpy(recSplitFileTime.currentValue.data(), &recSplitFileTimeVal, sizeof(recSplitFileTimeVal));
		recSplitFileTime.sizeOfCurrentValue = sizeof(recSplitFileTimeVal);

		recSplitFileTime.visible = recSplitFileVal;
		recSplitFileTime.enabled = isCategoryEnabled;
		recSplitFileTime.masked = false;
		recSplitFileTime.minVal = 1;
		recSplitFileTime.maxVal = 525600;

		subCategoryParameters->params.push_back(recSplitFileTime);
	} else if (strcmp(recSplitFileTypeVal, "Size") == 0) {
		Parameter recSplitFileSize;
		recSplitFileSize.name = "RecSplitFileSize";
		recSplitFileSize.type = "OBS_PROPERTY_UINT";
		recSplitFileSize.description = "Split Size (MB)";

		uint64_t recSplitFileSizeVal = config_get_uint(config, "AdvOut", "RecSplitFileSize");

		recSplitFileSize.currentValue.resize(sizeof(recSplitFileSizeVal));
		memcpy(recSplitFileSize.currentValue.data(), &recSplitFileSizeVal, sizeof(recSplitFileSizeVal));
		recSplitFileSize.sizeOfCurrentValue = sizeof(recSplitFileSizeVal);

		recSplitFileSize.visible = recSplitFileVal;
		recSplitFileSize.enabled = isCategoryEnabled;
		recSplitFileSize.masked = false;
		recSplitFileSize.minVal = 20;
		recSplitFileSize.maxVal = 1073741824;

		subCategoryParameters->params.push_back(recSplitFileSize);
	}

	Parameter recSplitFileResetTimestamps;
	recSplitFileResetTimestamps.name = "RecSplitFileResetTimestamps";
	recSplitFileResetTimestamps.type = "OBS_PROPERTY_BOOL";
	recSplitFileResetTimestamps.description = "Reset timestamps at the beginning of each split file";

	bool recSplitFileResetTimestampsVal = config_get_bool(config, "AdvOut", "RecSplitFileResetTimestamps");

	recSplitFileResetTimestamps.currentValue.resize(sizeof(recSplitFileResetTimestampsVal));
	memcpy(recSplitFileResetTimestamps.currentValue.data(), &recSplitFileResetTimestampsVal, sizeof(recSplitFileResetTimestampsVal));
	recSplitFileResetTimestamps.sizeOfCurrentValue = sizeof(recSplitFileResetTimestampsVal);

	recSplitFileResetTimestamps.visible = recSplitFileVal;
	recSplitFileResetTimestamps.enabled = isCategoryEnabled;
	recSplitFileResetTimestamps.masked = false;

	subCategoryParameters->params.push_back(recSplitFileResetTimestamps);

	// Encoder settings
	struct stat buffer;

	bool fileExist = (os_stat(ConfigManager::getInstance().getRecord().c_str(), &buffer) == 0);

	obs_data_t *settings = obs_encoder_defaults(recEncoderCurrentValue);
	obs_encoder_t *recordingEncoder;

	recordingEncoder = OBS_service::getRecordingEncoder();
	obs_output_t *recordOutput = OBS_service::getRecordingOutput();

	if (recordOutput == NULL)
		return;

	if (obs_output_active(recordOutput)) {
		settings = obs_encoder_get_settings(recordingEncoder);
	} else if (!recordingEncoder || (recordingEncoder && !obs_encoder_active(recordingEncoder))) {
		std::string recEncoderName = OBS_service::GetVideoEncoderName(StreamServiceId::Main, true, true, recEncoderCurrentValue);
		if (!fileExist) {
			recordingEncoder = obs_video_encoder_create(recEncoderCurrentValue, recEncoderName.c_str(), nullptr, nullptr);
			OBS_service::setRecordingEncoder(recordingEncoder);

			if (!obs_data_save_json_safe(settings, ConfigManager::getInstance().getRecord().c_str(), "tmp", "bak")) {
				blog(LOG_WARNING, "Failed to save encoder %s", ConfigManager::getInstance().getRecord().c_str());
			}
		} else if (strcmp(recEncoderCurrentValue, "none") != 0) {
			obs_data_t *data = obs_data_create_from_json_file_safe(ConfigManager::getInstance().getRecord().c_str(), "bak");
			update_nvenc_presets(data, recEncoderCurrentValue);
			obs_data_apply(settings, data);
			recordingEncoder = obs_video_encoder_create(recEncoderCurrentValue, recEncoderName.c_str(), settings, nullptr);
			OBS_service::setRecordingEncoder(recordingEncoder);
		}
	} else {
		settings = obs_encoder_get_settings(recordingEncoder);
	}

	if (strcmp(recEncoderCurrentValue, "none")) {
		getEncoderSettings(recordingEncoder, settings, &(subCategoryParameters->params), index, isCategoryEnabled, false, true);
	}

	subCategoryParameters->paramsCount = subCategoryParameters->params.size();
}

SubCategory OBS_settings::getAdvancedOutputRecordingSettings(config_t *config, bool isCategoryEnabled)
{
	SubCategory recordingSettings;

	int index = 0;

	std::vector<std::vector<std::pair<std::string, std::string>>> entries;

	// Type : list
	Parameter recType;
	recType.name = "RecType";
	recType.type = "OBS_PROPERTY_LIST";
	recType.subType = "OBS_COMBO_FORMAT_STRING";
	recType.description = "Type";

	std::vector<std::pair<std::string, std::string>> recTypeValues;
	recTypeValues.push_back(std::make_pair("Standard", "Standard"));

	uint32_t indexDataRecType = 0;

	for (int i = 0; i < recTypeValues.size(); i++) {
		std::string name = recTypeValues.at(i).first;

		uint64_t sizeName = name.length();
		std::vector<char> sizeNameBuffer;
		sizeNameBuffer.resize(sizeof(sizeName));
		memcpy(sizeNameBuffer.data(), &sizeName, sizeof(sizeName));

		recType.values.insert(recType.values.end(), sizeNameBuffer.begin(), sizeNameBuffer.end());
		recType.values.insert(recType.values.end(), name.begin(), name.end());

		std::string value = recTypeValues.at(i).second;

		uint64_t sizeValue = value.length();
		std::vector<char> sizeValueBuffer;
		sizeValueBuffer.resize(sizeof(sizeValue));
		memcpy(sizeValueBuffer.data(), &sizeValue, sizeof(sizeValue));

		recType.values.insert(recType.values.end(), sizeValueBuffer.begin(), sizeValueBuffer.end());
		recType.values.insert(recType.values.end(), value.begin(), value.end());
	}

	recType.sizeOfValues = recType.values.size();
	recType.countValues = recTypeValues.size();

	const char *RecTypeCurrentValue = config_get_string(config, "AdvOut", "RecType");
	if (RecTypeCurrentValue == NULL)
		RecTypeCurrentValue = "";

	recType.currentValue.resize(strlen(RecTypeCurrentValue));
	memcpy(recType.currentValue.data(), RecTypeCurrentValue, strlen(RecTypeCurrentValue));
	recType.sizeOfCurrentValue = strlen(RecTypeCurrentValue);

	recType.visible = true;
	recType.enabled = isCategoryEnabled;
	recType.masked = false;

	recordingSettings.params.push_back(recType);

	getStandardRecordingSettings(&recordingSettings, config, isCategoryEnabled);

	recordingSettings.name = "Recording";
	recordingSettings.paramsCount = recordingSettings.params.size();

	return recordingSettings;
}

void OBS_settings::getAdvancedOutputAudioSettings(std::vector<SubCategory> *outputSettings, config_t *config, bool isCategoryEnabled)
{
	std::vector<std::vector<std::pair<std::string, ipc::value>>> entries;
	uint32_t initialSettingsIndex = outputSettings->size();

	auto &bitrateMap = GetAACEncoderBitrateMap();
	UpdateAudioSettings(true);

	const int numberOfTracks = 6;

	for (int track = 1; track <= numberOfTracks; ++track) {
		auto trackBitrate =
			createSettingEntry("Track" + std::to_string(track) + "Bitrate", "OBS_PROPERTY_LIST", "Audio Bitrate", "OBS_COMBO_FORMAT_STRING");
		for (auto &entry : bitrateMap) {
			trackBitrate.push_back(std::make_pair(std::to_string(entry.first), ipc::value(std::to_string(entry.first))));
		}
		entries.push_back(trackBitrate);

		auto trackName = createSettingEntry("Track" + std::to_string(track) + "Name", "OBS_PROPERTY_EDIT_TEXT", "Name");
		entries.push_back(trackName);

		outputSettings->push_back(serializeSettingsData("Audio - Track " + std::to_string(track), entries, config, "AdvOut", true, isCategoryEnabled));
		entries.clear();
	}

	// Save the audio config
	currentAudioSettings = std::vector<SubCategory>(outputSettings->begin() + initialSettingsIndex, outputSettings->end());
}

void OBS_settings::getReplayBufferSettings(std::vector<SubCategory> *outputSettings, config_t *config, bool advanced, bool isCategoryEnabled)
{
	std::vector<std::vector<std::pair<std::string, ipc::value>>> entries;

	entries.push_back(createSettingEntry("RecRB", "OBS_PROPERTY_BOOL", "Enable Replay Buffer"));

	bool currentRecRb = config_get_bool(config, advanced ? "AdvOut" : "SimpleOutput", "RecRB");

	if (currentRecRb) {
		auto recRBTime = createSettingEntry("RecRBTime", "OBS_PROPERTY_INT", "Maximum Replay Time (Seconds)", "", 0, 21599, 1);
		entries.push_back(recRBTime);
	}

	if (obs_get_multiple_rendering()) {
		entries.push_back(createSettingEntry("replayBufferUseStreamOutput", "OBS_PROPERTY_BOOL", "Use stream output"));
	}

	outputSettings->push_back(serializeSettingsData("Replay Buffer", entries, config, advanced ? "AdvOut" : "SimpleOutput", true, isCategoryEnabled));
	entries.clear();
}

void OBS_settings::getAdvancedOutputSettings(std::vector<SubCategory> *outputSettings, config_t *config, bool isCategoryEnabled)
{
	std::vector<std::vector<std::pair<std::string, std::string>>> entries;

	// Streaming
	outputSettings->push_back(getAdvancedOutputStreamingSettings(config, isCategoryEnabled));

	// Recording
	outputSettings->push_back(getAdvancedOutputRecordingSettings(config, isCategoryEnabled));

	// Audio
	getAdvancedOutputAudioSettings(outputSettings, config, isCategoryEnabled);

	// Replay buffer
	getReplayBufferSettings(outputSettings, config, true, isCategoryEnabled);
}

std::vector<SubCategory> OBS_settings::getOutputSettings(CategoryTypes &type)
{
	std::vector<SubCategory> outputSettings;

	bool isCategoryEnabled = !OBS_service::isStreamingOutputActive(StreamServiceId::Main) &&
				 !OBS_service::isStreamingOutputActive(StreamServiceId::Second) && !OBS_service::isRecordingOutputActive() &&
				 !OBS_service::isReplayBufferOutputActive();

	std::vector<std::vector<std::pair<std::string, ipc::value>>> entries;

	auto outputMode = createSettingEntry("Mode", "OBS_PROPERTY_LIST", "Output Mode", "OBS_COMBO_FORMAT_STRING");
	outputMode.push_back({"Simple", ipc::value("Simple")});
	outputMode.push_back({"Advanced", ipc::value("Advanced")});
	entries.push_back(outputMode);

	outputSettings.push_back(serializeSettingsData("Untitled", entries, ConfigManager::getInstance().getBasic(), "Output", true, isCategoryEnabled));
	entries.clear();

	const char *currentOutputMode = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "Mode");

	if (currentOutputMode == NULL) {
		currentOutputMode = "Simple";
	}

	if (strcmp(currentOutputMode, "Advanced") == 0) {
		getAdvancedOutputSettings(&outputSettings, ConfigManager::getInstance().getBasic(), isCategoryEnabled);
		type = NODEOBS_CATEGORY_TAB;
	} else {
		getSimpleOutputSettings(&outputSettings, ConfigManager::getInstance().getBasic(), isCategoryEnabled);
	}

	return outputSettings;
}

void OBS_settings::saveSimpleOutputSettings(std::vector<SubCategory> settings)
{
	saveGenericSettings(settings, "SimpleOutput", ConfigManager::getInstance().getBasic());
}

void OBS_settings::saveAdvancedOutputStreamingSettings(std::vector<SubCategory> settings)
{
	int indexStreamingCategory = 1;

	std::string section = "AdvOut";

	obs_encoder_t *encoder = OBS_service::getStreamingEncoder(StreamServiceId::Main);
	obs_data_t *encoderSettings = obs_encoder_get_settings(encoder);
	int indexEncoderSettings = 4;

	obs_data_t *service_settings = obs_service_get_settings(OBS_service::getService(StreamServiceId::Main));
	const char *serviceName = obs_data_get_string(service_settings, "service");
	obs_data_release(service_settings);

	if (serviceName && strcmp(serviceName, "Twitch") == 0)
		indexEncoderSettings++;

	bool newEncoderType = false;

	Parameter param;

	for (int i = 0; i < settings.at(indexStreamingCategory).params.size(); i++) {
		param = settings.at(indexStreamingCategory).params.at(i);

		std::string name = param.name;
		std::string type = param.type;

		if (type.compare("OBS_PROPERTY_EDIT_TEXT") == 0 || type.compare("OBS_PROPERTY_PATH") == 0 || type.compare("OBS_PROPERTY_TEXT") == 0 ||
		    type.compare("OBS_INPUT_RESOLUTION_LIST") == 0) {
			std::string value(param.currentValue.data(), param.currentValue.size());
			if (i < indexEncoderSettings) {
				config_set_string(ConfigManager::getInstance().getBasic(), section.c_str(), name.c_str(), value.c_str());
			} else {
				obs_data_set_string(encoderSettings, name.c_str(), value.c_str());
			}
		} else if (type.compare("OBS_PROPERTY_INT") == 0) {
			int64_t *value = reinterpret_cast<int64_t *>(param.currentValue.data());
			if (i < indexEncoderSettings) {
				config_set_int(ConfigManager::getInstance().getBasic(), section.c_str(), name.c_str(), *value);
			} else {
				obs_data_set_int(encoderSettings, name.c_str(), *value);
			}
		} else if (type.compare("OBS_PROPERTY_UINT") == 0) {
			uint64_t *value = reinterpret_cast<uint64_t *>(param.currentValue.data());
			if (i < indexEncoderSettings) {
				config_set_uint(ConfigManager::getInstance().getBasic(), section.c_str(), name.c_str(), *value);
			} else {
				obs_data_set_int(encoderSettings, name.c_str(), *value);
			}
		} else if (type.compare("OBS_PROPERTY_BOOL") == 0) {
			bool *value = reinterpret_cast<bool *>(param.currentValue.data());
			if (i < indexEncoderSettings) {
				if (name.compare("Rescale") == 0 && *value || name.compare("VodTrackEnabled") == 0 && *value) {
					indexEncoderSettings++;
				}
				config_set_bool(ConfigManager::getInstance().getBasic(), section.c_str(), name.c_str(), *value);
			} else {
				obs_data_set_bool(encoderSettings, name.c_str(), *value);
			}
		} else if (type.compare("OBS_PROPERTY_DOUBLE") == 0 || type.compare("OBS_PROPERTY_FLOAT") == 0) {
			double *value = reinterpret_cast<double *>(param.currentValue.data());
			if (i < indexEncoderSettings) {
				config_set_double(ConfigManager::getInstance().getBasic(), section.c_str(), name.c_str(), *value);
			} else {
				obs_data_set_double(encoderSettings, name.c_str(), *value);
			}
		} else if (type.compare("OBS_PROPERTY_LIST") == 0) {
			std::string subType(param.subType.data(), param.subType.size());

			if (subType.compare("OBS_COMBO_FORMAT_INT") == 0) {
				int64_t *value = reinterpret_cast<int64_t *>(param.currentValue.data());
				if (i < indexEncoderSettings) {
					config_set_int(ConfigManager::getInstance().getBasic(), section.c_str(), name.c_str(), *value);
				} else {
					obs_data_set_int(encoderSettings, name.c_str(), *value);
				}
			} else if (subType.compare("OBS_COMBO_FORMAT_FLOAT") == 0) {
				double *value = reinterpret_cast<double *>(param.currentValue.data());
				if (i < indexEncoderSettings) {
					config_set_double(ConfigManager::getInstance().getBasic(), section.c_str(), name.c_str(), *value);
				} else {
					obs_data_set_double(encoderSettings, name.c_str(), *value);
				}
			} else {
				std::string value(param.currentValue.data(), param.currentValue.size());
				if (i < indexEncoderSettings) {
					if (name.compare("Encoder") == 0) {
						const char *currentEncoder =
							config_get_string(ConfigManager::getInstance().getBasic(), section.c_str(), name.c_str());
						if (currentEncoder != NULL)
							newEncoderType = value.compare(currentEncoder) != 0;
					}
					config_set_string(ConfigManager::getInstance().getBasic(), section.c_str(), name.c_str(), value.c_str());
				} else {
					obs_data_set_string(encoderSettings, name.c_str(), value.c_str());
				}
			}
		} else {
			std::cout << "type not found ! " << type.c_str() << std::endl;
		}
	}

#ifdef WIN32
	bool dynamicBitrate = config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "DynamicBitrate");
	std::string encoderID = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "Encoder");

	if (dynamicBitrate && encoderID.compare(ENCODER_NEW_NVENC) == 0)
		obs_data_set_bool(encoderSettings, "lookahead", false);
#elif __APPLE__
	bool applyServiceSettings = config_get_bool(ConfigManager::getInstance().getBasic(), "Output", "ApplyServiceSettings");
	std::string encoderID = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "Encoder");

	if (!applyServiceSettings && (encoderID.compare(APPLE_HARDWARE_VIDEO_ENCODER) == 0 || encoderID.compare(APPLE_HARDWARE_VIDEO_ENCODER_M1) == 0))
		config_set_bool(ConfigManager::getInstance().getBasic(), "AdvOut", "ApplyServiceSettings", true);
#endif

	config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);

	if (newEncoderType) {
		encoderSettings = obs_encoder_defaults(config_get_string(ConfigManager::getInstance().getBasic(), section.c_str(), "Encoder"));

		OBS_service::createVideoStreamingEncoder(StreamServiceId::Main);
		OBS_service::createVideoStreamingEncoder(StreamServiceId::Second);
	} else {
		obs_encoder_update(encoder, encoderSettings);

		obs_encoder_t *second_encoder = OBS_service::getStreamingEncoder(StreamServiceId::Second);
		obs_encoder_update(second_encoder, encoderSettings);
	}

	if (!obs_data_save_json_safe(encoderSettings, ConfigManager::getInstance().getStream().c_str(), "tmp", "bak")) {
		blog(LOG_WARNING, "Failed to save encoder %s", ConfigManager::getInstance().getStream().c_str());
	}
}

void OBS_settings::saveAdvancedOutputRecordingSettings(std::vector<SubCategory> settings)
{
	int indexRecordingCategory = 2;
	std::string section = "AdvOut";
	bool resetAudioTracks = false;

	obs_encoder_t *encoder = OBS_service::getRecordingEncoder();
	obs_data_t *encoderSettings = obs_encoder_get_settings(encoder);

	bool recSplitFileVal = config_get_bool(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFile");
	const char *recSplitFileTypeVal = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecSplitFileType");
	size_t indexEncoderSettings = 10;

	if (recSplitFileVal) {
		indexEncoderSettings += 4;
		if (strcmp(recSplitFileTypeVal, "Manual") == 0)
			indexEncoderSettings--;
	}

	bool newEncoderType = false;
	std::string currentFormat;

	Parameter param;

	for (int i = 0; i < settings.at(indexRecordingCategory).params.size(); i++) {
		param = settings.at(indexRecordingCategory).params.at(i);

		std::string name = param.name;
		std::string type = param.type;

		if (i >= indexEncoderSettings) {
			name.erase(0, strlen("Rec"));
		}

		if (name.compare("RecType") == 0) {
			std::string value(param.currentValue.data(), param.currentValue.size());
			if (value.compare("Custom Output (FFmpeg)") == 0) {
				indexEncoderSettings = settings.at(indexRecordingCategory).params.size();
			}
		}

		if (type.compare("OBS_PROPERTY_EDIT_TEXT") == 0 || type.compare("OBS_PROPERTY_PATH") == 0 || type.compare("OBS_PROPERTY_TEXT") == 0 ||
		    type.compare("OBS_INPUT_RESOLUTION_LIST") == 0) {
			std::string value(param.currentValue.data(), param.currentValue.size());
			if (i < indexEncoderSettings) {
				config_set_string(ConfigManager::getInstance().getBasic(), section.c_str(), name.c_str(), value.c_str());
			} else {
				obs_data_set_string(encoderSettings, name.c_str(), value.c_str());
			}
		} else if (type.compare("OBS_PROPERTY_INT") == 0) {
			int64_t *value = reinterpret_cast<int64_t *>(param.currentValue.data());
			if (i < indexEncoderSettings) {
				config_set_int(ConfigManager::getInstance().getBasic(), section.c_str(), name.c_str(), *value);
			} else {
				obs_data_set_int(encoderSettings, name.c_str(), *value);
			}
		} else if (type.compare("OBS_PROPERTY_UINT") == 0 || type.compare("OBS_PROPERTY_BITMASK") == 0) {
			uint64_t value = *reinterpret_cast<uint64_t *>(param.currentValue.data());

			// Use the first audio track if multitrack isnt supported
			if (name.compare("RecTracks") == 0 && !IsMultitrackAudioSupported(currentFormat.c_str())) {
				value = 1;
			}

			if (i < indexEncoderSettings) {
				config_set_uint(ConfigManager::getInstance().getBasic(), section.c_str(), name.c_str(), value);
			} else {
				obs_data_set_int(encoderSettings, name.c_str(), value);
			}
		} else if (type.compare("OBS_PROPERTY_BOOL") == 0) {
			bool *value = reinterpret_cast<bool *>(param.currentValue.data());
			if (i < indexEncoderSettings) {
				if (name.compare("RecRescale") == 0 && *value) {
					indexEncoderSettings++;
				}
				config_set_bool(ConfigManager::getInstance().getBasic(), section.c_str(), name.c_str(), *value);
			} else {
				obs_data_set_bool(encoderSettings, name.c_str(), *value);
			}
		} else if (type.compare("OBS_PROPERTY_DOUBLE") == 0 || type.compare("OBS_PROPERTY_FLOAT") == 0) {
			double *value = reinterpret_cast<double *>(param.currentValue.data());
			if (i < indexEncoderSettings) {
				config_set_double(ConfigManager::getInstance().getBasic(), section.c_str(), name.c_str(), *value);
			} else {
				obs_data_set_double(encoderSettings, name.c_str(), *value);
			}
		} else if (type.compare("OBS_PROPERTY_LIST") == 0) {
			std::string subType(param.subType.data(), param.subType.size());

			if (subType.compare("OBS_COMBO_FORMAT_INT") == 0) {
				int64_t *value = reinterpret_cast<int64_t *>(param.currentValue.data());
				if (i < indexEncoderSettings) {
					config_set_int(ConfigManager::getInstance().getBasic(), section.c_str(), name.c_str(), *value);
				} else {
					obs_data_set_int(encoderSettings, name.c_str(), *value);
				}
			} else if (subType.compare("OBS_COMBO_FORMAT_FLOAT") == 0) {
				double *value = reinterpret_cast<double *>(param.currentValue.data());
				if (i < indexEncoderSettings) {
					config_set_double(ConfigManager::getInstance().getBasic(), section.c_str(), name.c_str(), *value);
				} else {
					obs_data_set_double(encoderSettings, name.c_str(), *value);
				}
			} else {
				std::string value(param.currentValue.data(), param.currentValue.size());
				if (i < indexEncoderSettings) {
					if (name.compare("RecEncoder") == 0) {
						const char *currentEncoder =
							config_get_string(ConfigManager::getInstance().getBasic(), section.c_str(), name.c_str());

						if (currentEncoder != NULL)
							newEncoderType = value.compare(currentEncoder) != 0;
					}
					if (name.compare("RecFormat") == 0) {
						currentFormat = value;
					}
					if (name.compare("RecAEncoder") == 0) {
						const char *currentEncoder =
							config_get_string(ConfigManager::getInstance().getBasic(), section.c_str(), name.c_str());
						if (currentEncoder == NULL || value.compare(currentEncoder) != 0) {
							resetAudioTracks = true;
						}
					}
					config_set_string(ConfigManager::getInstance().getBasic(), section.c_str(), name.c_str(), value.c_str());
				} else {
					obs_data_set_string(encoderSettings, name.c_str(), value.c_str());
				}
			}
		} else {
			std::cout << "type not found ! " << type.c_str() << std::endl;
		}
	}

	int ret = config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);

	if (newEncoderType) {
		encoderSettings = obs_encoder_defaults(config_get_string(ConfigManager::getInstance().getBasic(), section.c_str(), "RecEncoder"));
		OBS_service::createVideoRecordingEncoder();
	} else {
		obs_encoder_update(encoder, encoderSettings);
	}

	if (resetAudioTracks) {
		OBS_service::clearRecordingAudioEncoder();
		OBS_service::setupRecordingAudioEncoder();
	}

	if (!obs_data_save_json_safe(encoderSettings, ConfigManager::getInstance().getRecord().c_str(), "tmp", "bak")) {
		blog(LOG_WARNING, "Failed to save encoder %s", ConfigManager::getInstance().getRecord().c_str());
	}
}

void OBS_settings::saveAdvancedOutputSettings(std::vector<SubCategory> settings)
{
	// Streaming
	if (!OBS_service::isStreamingOutputActive(StreamServiceId::Main) && !OBS_service::isStreamingOutputActive(StreamServiceId::Second))
		saveAdvancedOutputStreamingSettings(settings);

	// Recording
	if (!obs_output_active(OBS_service::getRecordingOutput()))
		saveAdvancedOutputRecordingSettings(settings);

	// Audio
	if (settings.size() > 3) {
		std::vector<SubCategory> audioSettings;
		int indexTrack = 3;

		for (int i = 0; i < 6; i++) {
			audioSettings.push_back(settings.at(i + indexTrack));
		}

		// Update the current audio settings, limiting them if necessary
		currentAudioSettings = audioSettings;
		UpdateAudioSettings(false);
	}

	// Replay buffer
	std::vector<SubCategory> replaySettings;
	replaySettings.push_back(settings.at(9));
	saveGenericSettings(replaySettings, "AdvOut", ConfigManager::getInstance().getBasic());
}

void OBS_settings::saveOutputSettings(std::vector<SubCategory> settings)
{
	Parameter outputMode = settings.at(0).params.at(0);
	std::string value_outputMode(outputMode.currentValue.data(), outputMode.currentValue.size());
	std::string current_outputMode = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "Mode");

	if (value_outputMode.compare(current_outputMode) != 0) {
		config_set_string(ConfigManager::getInstance().getBasic(), "Output", "Mode", value_outputMode.c_str());
		config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);
		return;
	}

	if (value_outputMode.compare("Advanced") == 0)
		saveAdvancedOutputSettings(settings);
	else
		saveSimpleOutputSettings(settings);
}

std::vector<SubCategory> OBS_settings::getAudioSettings()
{
	std::vector<SubCategory> audioSettings;
	SubCategory sc;
	sc.name = "Untitled";
	sc.paramsCount = 2;

	// Sample rate
	Parameter sampleRate;
	sampleRate.name = "SampleRate";
	sampleRate.type = "OBS_PROPERTY_LIST";
	sampleRate.description = "Sample Rate (requires a restart)";
	sampleRate.subType = "OBS_COMBO_FORMAT_INT";
	sampleRate.enabled = true;
	sampleRate.masked = false;
	sampleRate.visible = true;

	uint64_t sr = config_get_uint(ConfigManager::getInstance().getBasic(), "Audio", "SampleRate");

	sampleRate.currentValue.resize(sizeof(sr));
	memcpy(sampleRate.currentValue.data(), &sr, sizeof(sr));
	sampleRate.sizeOfCurrentValue = sizeof(sr);

	std::vector<std::pair<std::string, int64_t>> values;
	values.push_back(std::make_pair("44.1khz", 44100));
	values.push_back(std::make_pair("48khz", 48000));

	for (auto value : values) {
		uint64_t sizeName = value.first.length();
		std::vector<char> sizeNameBuffer;
		sizeNameBuffer.resize(sizeof(sizeName));
		memcpy(sizeNameBuffer.data(), &sizeName, sizeof(sizeName));

		sampleRate.values.insert(sampleRate.values.end(), sizeNameBuffer.begin(), sizeNameBuffer.end());
		sampleRate.values.insert(sampleRate.values.end(), value.first.begin(), value.first.end());

		std::vector<char> valueBuffer;
		valueBuffer.resize(sizeof(uint64_t));
		memcpy(valueBuffer.data(), &value.second, sizeof(value.second));

		sampleRate.values.insert(sampleRate.values.end(), valueBuffer.begin(), valueBuffer.end());
	}

	sampleRate.sizeOfValues = sampleRate.values.size();
	sampleRate.countValues = values.size();
	sc.params.push_back(sampleRate);

	// Channels
	Parameter channels;
	channels.name = "ChannelSetup";
	channels.type = "OBS_PROPERTY_LIST";
	channels.description = "Channels (requires a restart)";
	channels.subType = "OBS_COMBO_FORMAT_STRING";
	channels.enabled = true;
	channels.masked = false;
	channels.visible = true;

	const char *c = config_get_string(ConfigManager::getInstance().getBasic(), "Audio", "ChannelSetup");

	channels.currentValue.resize(strlen(c));
	memcpy(channels.currentValue.data(), c, strlen(c));
	channels.sizeOfCurrentValue = strlen(c);

	std::vector<std::pair<std::string, std::string>> cv;
	cv.push_back(std::make_pair("Mono", "Mono"));
	cv.push_back(std::make_pair("Stereo", "Stereo"));
	cv.push_back(std::make_pair("2.1", "2.1"));
	cv.push_back(std::make_pair("4.0", "4.0"));
	cv.push_back(std::make_pair("4.1", "4.1"));
	cv.push_back(std::make_pair("5.1", "5.1"));
	cv.push_back(std::make_pair("7.1", "7.1"));

	for (auto channel : cv) {
		uint64_t sizeName = channel.first.length();
		std::vector<char> sizeNameBuffer;
		sizeNameBuffer.resize(sizeof(sizeName));
		memcpy(sizeNameBuffer.data(), &sizeName, sizeof(sizeName));

		channels.values.insert(channels.values.end(), sizeNameBuffer.begin(), sizeNameBuffer.end());
		channels.values.insert(channels.values.end(), channel.first.begin(), channel.first.end());

		uint64_t sizeValue = channel.second.length();
		std::vector<char> sizeValueBuffer;
		sizeValueBuffer.resize(sizeof(sizeValue));
		memcpy(sizeValueBuffer.data(), &sizeValue, sizeof(sizeValue));

		channels.values.insert(channels.values.end(), sizeValueBuffer.begin(), sizeValueBuffer.end());
		channels.values.insert(channels.values.end(), channel.second.begin(), channel.second.end());
	}
	channels.sizeOfValues = channels.values.size();
	channels.countValues = cv.size();
	sc.params.push_back(channels);

	audioSettings.push_back(sc);

	return audioSettings;
}

void OBS_settings::saveAudioSettings(std::vector<SubCategory> audioSettings)
{
	SubCategory sc = audioSettings.at(0);

	Parameter sampleRate = sc.params.at(0);
	uint64_t *sr_value = reinterpret_cast<uint64_t *>(sampleRate.currentValue.data());
	if (*sr_value != 0) {
		config_set_uint(ConfigManager::getInstance().getBasic(), "Audio", "SampleRate", *sr_value);
	}

	Parameter channels = sc.params.at(1);
	std::string cv(channels.currentValue.data(), channels.currentValue.size());
	config_set_string(ConfigManager::getInstance().getBasic(), "Audio", "ChannelSetup", cv.c_str());

	config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);
}

std::vector<std::pair<uint64_t, uint64_t>> OBS_settings::getOutputResolutions(uint64_t base_cx, uint64_t base_cy)
{
	std::vector<std::pair<uint64_t, uint64_t>> outputResolutions;
	for (size_t idx = 0; idx < numVals; idx++) {
		uint64_t outDownscaleCX = uint64_t(double(base_cx) / vals[idx]);
		uint64_t outDownscaleCY = uint64_t(double(base_cy) / vals[idx]);

		outDownscaleCX &= 0xFFFFFFFE;
		outDownscaleCY &= 0xFFFFFFFE;

		outputResolutions.push_back(std::make_pair(outDownscaleCX, outDownscaleCY));
	}
	return outputResolutions;
}

std::vector<SubCategory> OBS_settings::getVideoSettings()
{
	std::vector<SubCategory> videoSettings;

	bool isCategoryEnabled = !OBS_service::isStreamingOutputActive(StreamServiceId::Main) &&
				 !OBS_service::isStreamingOutputActive(StreamServiceId::Second) && !OBS_service::isRecordingOutputActive() &&
				 !OBS_service::isReplayBufferOutputActive();

	std::vector<std::vector<std::pair<std::string, ipc::value>>> entries;

	//Base (Canvas) Resolution
	std::vector<std::pair<std::string, ipc::value>> baseResolution;
	baseResolution.push_back(std::make_pair("name", ipc::value("Base")));
	baseResolution.push_back(std::make_pair("type", ipc::value("OBS_INPUT_RESOLUTION_LIST")));
	baseResolution.push_back(std::make_pair("description", ipc::value("Base (Canvas) Resolution")));
	baseResolution.push_back(std::make_pair("subType", ipc::value("OBS_COMBO_FORMAT_STRING")));
	baseResolution.push_back(std::make_pair("minVal", ipc::value((double)0)));
	baseResolution.push_back(std::make_pair("maxVal", ipc::value((double)0)));
	baseResolution.push_back(std::make_pair("stepVal", ipc::value((double)0)));

	uint64_t base_cx = config_get_uint(ConfigManager::getInstance().getBasic(), "Video", "BaseCX");
	uint64_t base_cy = config_get_uint(ConfigManager::getInstance().getBasic(), "Video", "BaseCY");

	std::string baseResolutionString = ResString(base_cx, base_cy);

	baseResolution.push_back(std::make_pair("1920x1080", ipc::value("1920x1080")));
	baseResolution.push_back(std::make_pair("1280x720", ipc::value("1280x720")));

	std::vector<std::pair<uint32_t, uint32_t>> resolutions = OBS_API::availableResolutions();

	// Fill available display resolutions
	for (int i = 0; i < resolutions.size(); i++) {
		std::string baseResolutionString;
		baseResolutionString = std::to_string(resolutions.at(i).first);
		baseResolutionString += "x";
		baseResolutionString += std::to_string(resolutions.at(i).second);

		std::pair<std::string, std::string> newBaseResolution = std::make_pair(baseResolutionString.c_str(), baseResolutionString.c_str());

		std::vector<std::pair<std::string, ipc::value>>::iterator it =
			std::find_if(baseResolution.begin(), baseResolution.end(), [&baseResolutionString](const std::pair<std::string, ipc::value> value) {
				return (value.second.value_str.compare(baseResolutionString) == 0);
			});

		if (baseResolution.size() == 7 || it == baseResolution.end()) {
			baseResolution.push_back(newBaseResolution);
		}
	}

	// Set the current base resolution selected by the user
	std::pair<std::string, std::string> newBaseResolution = std::make_pair("currentValue", baseResolutionString);

	//Check if the current resolution is in the available ones
	std::vector<std::pair<std::string, ipc::value>>::iterator it =
		std::find_if(baseResolution.begin(), baseResolution.end(), [&baseResolutionString](const std::pair<std::string, ipc::value> value) {
			return (value.second.value_str.compare(baseResolutionString) == 0);
		});

	if (it == baseResolution.end()) {
		baseResolution.push_back(std::make_pair(baseResolutionString, ipc::value(baseResolutionString)));
	}

	int indexFirstValue = 7;
	baseResolution.insert(baseResolution.begin() + indexFirstValue, newBaseResolution);

	entries.push_back(baseResolution);

	std::vector<std::pair<std::string, ipc::value>> outputResolution;
	outputResolution.push_back(std::make_pair("name", ipc::value("Output")));
	outputResolution.push_back(std::make_pair("type", ipc::value("OBS_INPUT_RESOLUTION_LIST")));
	outputResolution.push_back(std::make_pair("description", ipc::value("Output (Scaled) Resolution")));
	outputResolution.push_back(std::make_pair("subType", ipc::value("OBS_COMBO_FORMAT_STRING")));
	outputResolution.push_back(std::make_pair("minVal", ipc::value((double)0)));
	outputResolution.push_back(std::make_pair("maxVal", ipc::value((double)0)));
	outputResolution.push_back(std::make_pair("stepVal", ipc::value((double)0)));

	uint64_t out_cx = config_get_uint(ConfigManager::getInstance().getBasic(), "Video", "OutputCX");
	uint64_t out_cy = config_get_uint(ConfigManager::getInstance().getBasic(), "Video", "OutputCY");

	std::string outputResString = ResString(out_cx, out_cy);

	outputResolution.push_back(std::make_pair("currentValue", ipc::value(outputResString)));

	std::vector<std::pair<uint64_t, uint64_t>> outputResolutions = getOutputResolutions(base_cx, base_cy);

	for (int i = 0; i < outputResolutions.size(); i++) {
		std::string outRes = ResString(outputResolutions.at(i).first, outputResolutions.at(i).second);
		outputResolution.push_back(std::make_pair(outRes, outRes));
	}

	entries.push_back(outputResolution);

	//Downscale Filter
	auto scaleType = createSettingEntry("ScaleType", "OBS_PROPERTY_LIST", "Downscale Filter", "OBS_COMBO_FORMAT_STRING");
	scaleType.push_back({"Bilinear (Fastest, but blurry if scaling)", ipc::value("bilinear")});
	scaleType.push_back({"Bicubic (Sharpened scaling, 16 samples)", ipc::value("bicubic")});
	scaleType.push_back({"Lanczos (Sharpened scaling, 32 samples)", ipc::value("lanczos")});
	entries.push_back(scaleType);

	// FPS Type
	uint64_t fpsTypeValue = config_get_uint(ConfigManager::getInstance().getBasic(), "Video", "FPSType");
	auto fpsType = createSettingEntry("FPSType", "OBS_PROPERTY_LIST", "FPS Type", "OBS_COMBO_FORMAT_STRING");
	// Dynamic FPS Type Options
	if (fpsTypeValue == 0) {
		fpsType.push_back({"currentValue", ipc::value("Common FPS Values")});
	} else if (fpsTypeValue == 1) {
		fpsType.push_back({"currentValue", ipc::value("Integer FPS Value")});
	} else if (fpsTypeValue == 2) {
		fpsType.push_back({"currentValue", ipc::value("Fractional FPS Value")});
	}
	fpsType.push_back({"Common FPS Values", ipc::value("Common FPS Values")});
	fpsType.push_back({"Integer FPS Value", ipc::value("Integer FPS Value")});
	fpsType.push_back({"Fractional FPS Value", ipc::value("Fractional FPS Value")});
	entries.push_back(fpsType);

	if (fpsTypeValue == 1) {
		// Integer FPS Value
		entries.push_back(createSettingEntry("FPSInt", "OBS_PROPERTY_UINT", "Integer FPS Value", "", 0, 120, 1));
	} else if (fpsTypeValue == 2) {
		// Fractional FPS Values
		entries.push_back(createSettingEntry("FPSNum", "OBS_PROPERTY_UINT", "FPS Numerator", "", 0, 1000000, 1));
		entries.push_back(createSettingEntry("FPSDen", "OBS_PROPERTY_UINT", "FPS Denominator", "", 0, 1000000, 1));
	} else {
		// Reset to default if FPSTypeValue is out of expected range
		if (fpsTypeValue > 2) {
			config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "FPSType",
					config_get_default_uint(ConfigManager::getInstance().getBasic(), "Video", "FPSType"));
			config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);
		}
		// Common FPS Values
		auto fpsCommon = createSettingEntry("FPSCommon", "OBS_PROPERTY_LIST", "Common FPS Values", "OBS_COMBO_FORMAT_STRING");
		std::vector<std::pair<std::string, std::string>> commonFPSOptions = {{"10", "10"}, {"20", "20"}, {"24 NTSC", "24 NTSC"}, {"29.97", "29.97"},
										     {"30", "30"}, {"48", "48"}, {"59.94", "59.94"},     {"60", "60"}};
		for (const auto &option : commonFPSOptions) {
			fpsCommon.push_back({option.first, ipc::value(option.second)});
		}
		entries.push_back(fpsCommon);
	}

	videoSettings.push_back(serializeSettingsData("Untitled", entries, ConfigManager::getInstance().getBasic(), "Video", true, isCategoryEnabled));
	entries.clear();

	return videoSettings;
}

struct BaseLexer {
	lexer lex;

public:
	inline BaseLexer() { lexer_init(&lex); }
	inline ~BaseLexer() { lexer_free(&lex); }
	operator lexer *() { return &lex; }
};

// parses "[width]x[height]", string, i.e. 1024x768

static bool ConvertResText(const char *res, uint32_t &cx, uint32_t &cy)
{
	BaseLexer lex;
	base_token token;

	lexer_start(lex, res);

	// parse width
	if (!lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
		return false;
	if (token.type != BASETOKEN_DIGIT)
		return false;

	try {
		cx = std::stoul(token.text.array);
	} catch (const std::exception &) {
		return false;
	}

	// parse 'x'
	if (!lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
		return false;
	if (strref_cmpi(&token.text, "x") != 0)
		return false;

	// parse height
	if (!lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
		return false;
	if (token.type != BASETOKEN_DIGIT)
		return false;

	try {
		cy = std::stoul(token.text.array);
	} catch (const std::exception &) {
		return false;
	}

	// shouldn't be any more tokens after this
	if (lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
		return false;

	return true;
}

void OBS_settings::saveVideoSettings(std::vector<SubCategory> videoSettings)
{
	SubCategory sc = videoSettings.at(0);

	//Base resolution
	Parameter baseRes = sc.params.at(0);

	std::string baseResString(baseRes.currentValue.data(), baseRes.currentValue.size());

	uint32_t baseWidth = 0, baseHeight = 0;

	if (ConvertResText(baseResString.c_str(), baseWidth, baseHeight)) {
		config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "BaseCX", baseWidth);
		config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "BaseCY", baseHeight);
	}

	//Output resolution
	Parameter outputRes = sc.params.at(1);

	std::string outputResString(outputRes.currentValue.data(), outputRes.currentValue.size());

	uint32_t outputWidth = 0, outputHeight = 0;

	if (ConvertResText(outputResString.c_str(), outputWidth, outputHeight)) {
		config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "OutputCX", outputWidth);
		config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "OutputCY", outputHeight);
	}

	Parameter scaleParameter = sc.params.at(2);

	std::string scaleString(scaleParameter.currentValue.data(), scaleParameter.currentValue.size());

	config_set_string(ConfigManager::getInstance().getBasic(), "Video", "ScaleType", scaleString.c_str());

	Parameter fpsType = sc.params.at(3);

	std::string fpsTypeString(fpsType.currentValue.data(), fpsType.currentValue.size());

	if (fpsTypeString.compare("Common FPS Values") == 0) {
		if (config_get_uint(ConfigManager::getInstance().getBasic(), "Video", "FPSType") != 0) {
			config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "FPSType", 0);
		} else {
			Parameter fpsCommon = sc.params.at(4);
			std::string fpsCommonString(fpsCommon.currentValue.data(), fpsCommon.currentValue.size());
			config_set_string(ConfigManager::getInstance().getBasic(), "Video", "FPSCommon", fpsCommonString.c_str());
		}
	} else if (fpsTypeString.compare("Integer FPS Value") == 0) {
		if (config_get_uint(ConfigManager::getInstance().getBasic(), "Video", "FPSType") != 1) {
			config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "FPSType", 1);
		} else {
			Parameter fpsInt = sc.params.at(4);
			uint64_t *fpsIntValue = reinterpret_cast<uint64_t *>(fpsInt.currentValue.data());
			if (*fpsIntValue > 0 && *fpsIntValue < 500) {
				config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "FPSInt", *fpsIntValue);
			}
		}
	} else if (fpsTypeString.compare("Fractional FPS Value") == 0) {
		if (config_get_uint(ConfigManager::getInstance().getBasic(), "Video", "FPSType") != 2) {
			config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "FPSType", 2);
		} else {
			Parameter fpsNum = sc.params.at(4);
			uint32_t *fpsNumValue = reinterpret_cast<uint32_t *>(fpsNum.currentValue.data());

			if (*fpsNumValue > 0 && *fpsNumValue < 500) {
				config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "FPSNum", *fpsNumValue);
			}

			if (sc.params.size() > 5) {
				Parameter fpsDen = sc.params.at(5);
				uint32_t *fpsDenValue = reinterpret_cast<uint32_t *>(fpsDen.currentValue.data());
				if (*fpsDenValue > 0)
					config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "FPSDen", *fpsDenValue);
			}
		}
	}

	config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);
}

std::vector<SubCategory> OBS_settings::getAdvancedSettings()
{
	std::vector<SubCategory> advancedSettings;

	std::vector<std::vector<std::pair<std::string, ipc::value>>> entries;

#if _WIN32
	//General

	//Process Priority
	std::vector<std::pair<std::string, ipc::value>> processPriority;
	processPriority.push_back(std::make_pair("name", ipc::value("ProcessPriority")));
	processPriority.push_back(std::make_pair("type", ipc::value("OBS_PROPERTY_LIST")));
	processPriority.push_back(std::make_pair("description", ipc::value("Process Priority")));
	processPriority.push_back(std::make_pair("subType", ipc::value("OBS_COMBO_FORMAT_STRING")));
	processPriority.push_back(std::make_pair("minVal", ipc::value((double)0)));
	processPriority.push_back(std::make_pair("maxVal", ipc::value((double)0)));
	processPriority.push_back(std::make_pair("stepVal", ipc::value((double)0)));
	processPriority.push_back(std::make_pair("High", ipc::value("High")));
	processPriority.push_back(std::make_pair("Above Normal", ipc::value("AboveNormal")));
	processPriority.push_back(std::make_pair("Normal", ipc::value("Normal")));
	processPriority.push_back(std::make_pair("Below Normal", ipc::value("BelowNormal")));
	processPriority.push_back(std::make_pair("Idle", ipc::value("Idle")));

	const char *processPriorityCurrentValue = config_get_string(ConfigManager::getInstance().getGlobal(), "General", "ProcessPriority");

	if (processPriorityCurrentValue == NULL) {
		processPriorityCurrentValue = "Normal";
		config_set_string(ConfigManager::getInstance().getGlobal(), "General", "ProcessPriority", processPriorityCurrentValue);
	}

	OBS_API::SetProcessPriorityOld(processPriorityCurrentValue);

	entries.push_back(processPriority);

	advancedSettings.push_back(serializeSettingsData("General", entries, ConfigManager::getInstance().getGlobal(), "General", true, true));
	entries.clear();

#endif
	//Video

	//Color Format
	auto colorFormat = createSettingEntry("ColorFormat", "OBS_PROPERTY_LIST", "Color Format", "OBS_COMBO_FORMAT_STRING");
	std::vector<std::pair<std::string, std::string>> colorFormatOptions = {
		{"NV12 (8-bit, 4:2:0, 2 planes)", "NV12"},  {"I420 (8-bit, 4:2:0, 3 planes)", "I420"},  {"I444 (8-bit, 4:4:4, 3 planes)", "I444"},
		{"P010 (10-bit, 4:2:0, 2 planes)", "P010"}, {"I010 (10-bit, 4:2:0, 3 planes)", "I010"}, {"RGB (8-bit)", "RGB"}};
	for (const auto &option : colorFormatOptions) {
		colorFormat.push_back({option.first, ipc::value(option.second)});
	}
	entries.push_back(colorFormat);

	// YUV Color Space
	auto colorSpace = createSettingEntry("ColorSpace", "OBS_PROPERTY_LIST", "YUV Color Space", "OBS_COMBO_FORMAT_STRING");
	std::vector<std::pair<std::string, std::string>> colorSpaceOptions = {
		{"sRGB", "sRGB"}, {"Rec. 709", "709"}, {"Rec. 601", "601"}, {"Rec. 2100 (PQ)", "2100PQ"}, {"Rec. 2100 (HLG)", "2100HLG"}};
	for (const auto &option : colorSpaceOptions) {
		colorSpace.push_back({option.first, ipc::value(option.second)});
	}
	entries.push_back(colorSpace);

	// YUV Color Range
	auto colorRange = createSettingEntry("ColorRange", "OBS_PROPERTY_LIST", "YUV Color Range", "OBS_COMBO_FORMAT_STRING");
	colorRange.push_back({"Limited", ipc::value("Partial")});
	colorRange.push_back({"Full", ipc::value("Full")});
	entries.push_back(colorRange);

	// GPU Render
	auto forceGPUAsRenderDevice = createSettingEntry("ForceGPUAsRenderDevice", "OBS_PROPERTY_BOOL", "Force GPU as render device");
	entries.push_back(forceGPUAsRenderDevice);

	// SDR White Level
	auto sdrWhiteLevel = createSettingEntry("SdrWhiteLevel", "OBS_PROPERTY_INT", "SDR White Level", "", 80, 480, 1);
	entries.push_back(sdrWhiteLevel);

	// HDR Nominal Peak Level
	auto hdrNominalPeakLevel = createSettingEntry("HdrNominalPeakLevel", "OBS_PROPERTY_INT", "HDR Nominal Peak Level", "", 400, 10000, 1);
	entries.push_back(hdrNominalPeakLevel);

	advancedSettings.push_back(serializeSettingsData("Video", entries, ConfigManager::getInstance().getBasic(), "Video", true,
							 !OBS_service::isStreamingOutputActive(StreamServiceId::Main) &&
								 !OBS_service::isStreamingOutputActive(StreamServiceId::Second)));
	entries.clear();

	//Audio
	const char *monDevName = config_get_string(ConfigManager::getInstance().getBasic(), "Audio", "MonitoringDeviceName");
	const char *monDevId = config_get_string(ConfigManager::getInstance().getBasic(), "Audio", "MonitoringDeviceId");

	// Audio Monitoring Device
	auto monitoringDevice = createSettingEntry("MonitoringDeviceName", "OBS_PROPERTY_LIST", "Audio Monitoring Device", "OBS_COMBO_FORMAT_STRING");
	monitoringDevice.push_back({"currentValue", ipc::value(monDevName)}); // Assuming monDevName is defined
	monitoringDevice.push_back({"Default", ipc::value("Default")});

	auto enum_devices = [](void *param, const char *name, const char *id) {
		auto *deviceList = static_cast<std::vector<std::pair<std::string, ipc::value>> *>(param);
		deviceList->push_back({name, ipc::value(name)});
		return true;
	};
	obs_enum_audio_monitoring_devices(enum_devices, &monitoringDevice);
	entries.push_back(monitoringDevice);

#if defined(_WIN32)
	entries.push_back(createSettingEntry("DisableAudioDucking", "OBS_PROPERTY_BOOL", "Disable Windows audio ducking"));
#endif

	// Serializing Audio Settings
	advancedSettings.push_back(serializeSettingsData("Audio", entries, ConfigManager::getInstance().getBasic(), "Audio", true, true));
	entries.clear();

	// Low Latency Audio Buffering
	auto lowLatencyAudioBuffering = createSettingEntry("LowLatencyAudioBuffering", "OBS_PROPERTY_BOOL",
							   "Low Latency Audio Buffering Mode (For Decklink/NDI outputs), requires a restart");
	entries.push_back(lowLatencyAudioBuffering);

	advancedSettings.push_back(serializeSettingsData("Audio", entries, ConfigManager::getInstance().getGlobal(), "Audio", true, true));
	entries.clear();

	//Recording
	//Filename Formatting
	entries.push_back(createSettingEntry("FilenameFormatting", "OBS_PROPERTY_EDIT_TEXT", "Filename Formatting"));

	//Overwrite if file exists
	auto overwriteIfExists = createSettingEntry("OverwriteIfExists", "OBS_PROPERTY_BOOL", "Overwrite if file exists");
	entries.push_back(overwriteIfExists);

	advancedSettings.push_back(serializeSettingsData("Recording", entries, ConfigManager::getInstance().getBasic(), "Output", true, true));
	entries.clear();

	//Replay Buffer Filename Prefix
	entries.push_back(createSettingEntry("RecRBPrefix", "OBS_PROPERTY_EDIT_TEXT", "Replay Buffer Filename Prefix"));

	//Replay Buffer Filename Suffix
	entries.push_back(createSettingEntry("RecRBSuffix", "OBS_PROPERTY_EDIT_TEXT", "Replay Buffer Filename Suffix"));

	advancedSettings.push_back(serializeSettingsData("Replay Buffer", entries, ConfigManager::getInstance().getBasic(), "SimpleOutput", true, true));
	entries.clear();

	//Stream Delay
	//Enable
	entries.push_back(createSettingEntry("DelayEnable", "OBS_PROPERTY_BOOL", "Enable"));

	//Duration (seconds)
	entries.push_back(createSettingEntry("DelaySec", "OBS_PROPERTY_INT", "Duration (seconds)", "", 0, 1800, 1));

	//Preserved cutoff point (increase delay) when reconnecting
	entries.push_back(createSettingEntry("DelayPreserve", "OBS_PROPERTY_BOOL", "Preserved cutoff point (increase delay) when reconnecting"));

	advancedSettings.push_back(serializeSettingsData("Stream Delay", entries, ConfigManager::getInstance().getBasic(), "Output", true, true));
	entries.clear();

	//Automatically Reconnect
	//Enable Reconnect
	entries.push_back(createSettingEntry("Reconnect", "OBS_PROPERTY_BOOL", "Enable"));

	//Retry Delay (seconds)
	entries.push_back(createSettingEntry("RetryDelay", "OBS_PROPERTY_INT", "Retry Delay (seconds)", "", 0, 30, 1));

	//Maximum Retries
	entries.push_back(createSettingEntry("MaxRetries", "OBS_PROPERTY_INT", "Maximum Retries", "", 0, 10000, 1));

	advancedSettings.push_back(serializeSettingsData("Automatically Reconnect", entries, ConfigManager::getInstance().getBasic(), "Output", true, true));
	entries.clear();

	//Network

	//Bind to IP
	auto bindIP = createSettingEntry("BindIP", "OBS_PROPERTY_LIST", "Bind to IP", "OBS_COMBO_FORMAT_STRING");
	obs_properties_t *ppts = obs_get_output_properties("rtmp_output");
	obs_property_t *p = obs_properties_get(ppts, "bind_ip");

	size_t count = obs_property_list_item_count(p);
	for (size_t i = 0; i < count; i++) {
		const char *name = obs_property_list_item_name(p, i);
		const char *val = obs_property_list_item_string(p, i);

		bindIP.push_back({name, ipc::value(val)});
	}

	entries.push_back(bindIP);

	//Enable dynamic bitrate
	entries.push_back(createSettingEntry("DynamicBitrate", "OBS_PROPERTY_BOOL", "Dynamically change bitrate when dropping frames while streaming"));

#ifdef WIN32
	//Enable new networking code
	entries.push_back(createSettingEntry("NewSocketLoopEnable", "OBS_PROPERTY_BOOL", "Enable new networking code"));

	//Low latency mode
	entries.push_back(createSettingEntry("LowLatencyEnable", "OBS_PROPERTY_BOOL", "Low latency mode"));
#endif

	advancedSettings.push_back(serializeSettingsData("Network", entries, ConfigManager::getInstance().getBasic(), "Output", true, true));
	entries.clear();

	obs_properties_destroy(ppts);

	//Browser Source Hardware Acceleration

	entries.push_back(createSettingEntry("BrowserHWAccel", "OBS_PROPERTY_BOOL", "Enable Browser Source Hardware Acceleration (requires a restart)"));

	advancedSettings.push_back(serializeSettingsData("Sources", entries, ConfigManager::getInstance().getGlobal(), "General", true, true));
	entries.clear();

	//Enable Media File Caching
	entries.push_back(createSettingEntry("fileCaching", "OBS_PROPERTY_BOOL", "Enable media file caching"));

	advancedSettings.push_back(serializeSettingsData("Media Files", entries, ConfigManager::getInstance().getGlobal(), "General", true, true));
	entries.clear();

	return advancedSettings;
}

std::vector<SubCategory> OBS_settings::getVirtualCamSettings()
{
	std::vector<SubCategory> settings;
	std::vector<std::vector<std::pair<std::string, ipc::value>>> entries;

	entries.push_back(createSettingEntry("OutputType", "OBS_PROPERTY_INT", "Virtual camera output type", "", 0, 3, 1));
	settings.push_back(serializeSettingsData("OutputType", entries, ConfigManager::getInstance().getBasic(), "Virtual Webcam", true, true));
	entries.clear();

	entries.push_back(createSettingEntry("OutputSelection", "OBS_PROPERTY_EDIT_TEXT", "Virtual camera output selection"));
	settings.push_back(serializeSettingsData("OutputSelection", entries, ConfigManager::getInstance().getBasic(), "Virtual Webcam", true, true));
	entries.clear();

	return settings;
}

void OBS_settings::saveAdvancedSettings(std::vector<SubCategory> advancedSettings)
{
	uint32_t index = 0;
#ifdef WIN32
	//General
	std::vector<SubCategory> generalAdvancedSettings;

	generalAdvancedSettings.push_back(advancedSettings.at(index++));
	saveGenericSettings(generalAdvancedSettings, "General", ConfigManager::getInstance().getGlobal());
#endif

	//Video
	std::vector<SubCategory> videoAdvancedSettings;

	videoAdvancedSettings.push_back(advancedSettings.at(index++));
	saveGenericSettings(videoAdvancedSettings, "Video", ConfigManager::getInstance().getBasic());

	//Audio
	std::vector<SubCategory> audioAdvancedSettings;

	audioAdvancedSettings.push_back(advancedSettings.at(index++));
	saveGenericSettings(audioAdvancedSettings, "Audio", ConfigManager::getInstance().getBasic());
	audioAdvancedSettings.clear();
	audioAdvancedSettings.push_back(advancedSettings.at(index++));
	saveGenericSettings(audioAdvancedSettings, "Audio", ConfigManager::getInstance().getGlobal());

	//Recording
	std::vector<SubCategory> recordingAdvancedSettings;

	recordingAdvancedSettings.push_back(advancedSettings.at(index++));
	saveGenericSettings(recordingAdvancedSettings, "Output", ConfigManager::getInstance().getBasic());

	//Replay buffer
	std::vector<SubCategory> replayBufferAdvancedSettings;

	replayBufferAdvancedSettings.push_back(advancedSettings.at(index++));
	saveGenericSettings(replayBufferAdvancedSettings, "SimpleOutput", ConfigManager::getInstance().getBasic());

	//Stream Delay
	std::vector<SubCategory> stresmDelayAdvancedSettings;

	stresmDelayAdvancedSettings.push_back(advancedSettings.at(index++));
	saveGenericSettings(stresmDelayAdvancedSettings, "Output", ConfigManager::getInstance().getBasic());

	//Automatically Reconnect
	std::vector<SubCategory> automaticallyReconnectAdvancedSettings;

	automaticallyReconnectAdvancedSettings.push_back(advancedSettings.at(index++));
	saveGenericSettings(automaticallyReconnectAdvancedSettings, "Output", ConfigManager::getInstance().getBasic());

	//Network
	std::vector<SubCategory> networkAdvancedSettings;

	networkAdvancedSettings.push_back(advancedSettings.at(index++));
	saveGenericSettings(networkAdvancedSettings, "Output", ConfigManager::getInstance().getBasic());

	//Sources
	std::vector<SubCategory> sourcesSettings;

	sourcesSettings.push_back(advancedSettings.at(index++));
	saveGenericSettings(sourcesSettings, "General", ConfigManager::getInstance().getGlobal());

	//Media Files
	std::vector<SubCategory> mediaFilesSettings;

	mediaFilesSettings.push_back(advancedSettings.at(index++));
	saveGenericSettings(mediaFilesSettings, "General", ConfigManager::getInstance().getGlobal());
	MemoryManager::GetInstance().updateSourcesCache();
}

void OBS_settings::saveVirtualCamSettings(std::vector<SubCategory> settings)
{
	SubCategory sc = settings.at(0);

	Parameter outputType = sc.params.at(0);
	uint64_t *ot_value = reinterpret_cast<uint64_t *>(outputType.currentValue.data());
	config_set_uint(ConfigManager::getInstance().getBasic(), "Virtual Webcam", "OutputType", *ot_value);

	sc = settings.at(1);
	Parameter outputSelection = sc.params.at(0);
	std::string os(outputSelection.currentValue.data(), outputSelection.currentValue.size());
	config_set_string(ConfigManager::getInstance().getBasic(), "Virtual Webcam", "OutputSelection", os.c_str());

	config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);
}

std::vector<SubCategory> OBS_settings::getSettings(std::string nameCategory, CategoryTypes &type)
{
	std::vector<SubCategory> settings;

	if (nameCategory.compare("General") == 0) {
		settings = getGeneralSettings();
	} else if (nameCategory.compare("Stream") == 0) {
		settings = getStreamSettings(StreamServiceId::Main);
	} else if (nameCategory.compare("StreamSecond") == 0) {
		settings = getStreamSettings(StreamServiceId::Second);
	} else if (nameCategory.compare("Output") == 0) {
		settings = getOutputSettings(type);
	} else if (nameCategory.compare("Audio") == 0) {
		settings = getAudioSettings();
	} else if (nameCategory.compare("Video") == 0) {
		settings = getVideoSettings();
	} else if (nameCategory.compare("Advanced") == 0) {
		settings = getAdvancedSettings();
	} else if (nameCategory.compare("Virtual Webcam") == 0) {
		settings = getVirtualCamSettings();
	}

	return settings;
}

bool OBS_settings::saveSettings(std::string nameCategory, std::vector<SubCategory> settings)
{
	bool ret = true;

	if (nameCategory.compare("General") == 0) {
		saveGenericSettings(settings, "BasicWindow", ConfigManager::getInstance().getGlobal());
	} else if (nameCategory.compare("Stream") == 0) {
		if (saveStreamSettings(settings, StreamServiceId::Main)) {
			OBS_service::updateService(StreamServiceId::Main);
		} else {
			ret = false;
		}
	} else if (nameCategory.compare("StreamSecond") == 0) {
		if (saveStreamSettings(settings, StreamServiceId::Second)) {
			OBS_service::updateService(StreamServiceId::Second);
		} else {
			ret = false;
		}
	} else if (nameCategory.compare("Output") == 0) {
		saveOutputSettings(settings);
	} else if (nameCategory.compare("Audio") == 0) {
		saveAudioSettings(settings);
	} else if (nameCategory.compare("Video") == 0) {
		saveVideoSettings(settings);
	} else if (nameCategory.compare("Advanced") == 0) {
		saveAdvancedSettings(settings);

		if (!OBS_service::isStreamingOutputActive(StreamServiceId::Main) && !OBS_service::isStreamingOutputActive(StreamServiceId::Second)) {
			struct obs_video_info ovi = {0};
			obs_get_video_info(&ovi);
			obs_reset_video(&ovi);

			const float sdr_white_level = (float)config_get_uint(ConfigManager::getInstance().getBasic(), "Video", "SdrWhiteLevel");
			const float hdr_nominal_peak_level = (float)config_get_uint(ConfigManager::getInstance().getBasic(), "Video", "HdrNominalPeakLevel");
			obs_set_video_levels(sdr_white_level, hdr_nominal_peak_level);
		}

		OBS_API::setAudioDeviceMonitoring();
	} else if (nameCategory.compare("Virtual Webcam") == 0) {
		saveVirtualCamSettings(settings);
	}
	return ret;
}

void OBS_settings::saveGenericSettings(std::vector<SubCategory> genericSettings, std::string section, config_t *config)
{
	SubCategory sc;

	for (int i = 0; i < genericSettings.size(); i++) {
		sc = genericSettings.at(i);

		std::string nameSubcategory = sc.name;

		Parameter param;

		for (int j = 0; j < sc.params.size(); j++) {
			param = sc.params.at(j);

			std::string name = param.name;
			std::string type = param.type;
			std::string subType = param.subType;

			if (type.compare("OBS_PROPERTY_EDIT_TEXT") == 0 || type.compare("OBS_PROPERTY_PATH") == 0 || type.compare("OBS_PROPERTY_TEXT") == 0 ||
			    type.compare("OBS_INPUT_RESOLUTION_LIST") == 0) {
				std::string value(param.currentValue.data(), param.currentValue.size());
				config_set_string(config, section.c_str(), name.c_str(), value.c_str());
			} else if (type.compare("OBS_PROPERTY_INT") == 0) {
				int64_t *value = reinterpret_cast<int64_t *>(param.currentValue.data());
				config_set_int(config, section.c_str(), name.c_str(), *value);
			} else if (type.compare("OBS_PROPERTY_UINT") == 0) {
				uint64_t *value = reinterpret_cast<uint64_t *>(param.currentValue.data());
				config_set_uint(config, section.c_str(), name.c_str(), *value);
			} else if (type.compare("OBS_PROPERTY_BOOL") == 0) {
				bool *value = reinterpret_cast<bool *>(param.currentValue.data());
				config_set_bool(config, section.c_str(), name.c_str(), *value);

				if (name.compare("replayBufferUseStreamOutput") == 0) {
					if (*value)
						obs_set_replay_buffer_rendering_mode(OBS_STREAMING_REPLAY_BUFFER_RENDERING);
					else
						obs_set_replay_buffer_rendering_mode(OBS_RECORDING_REPLAY_BUFFER_RENDERING);
				}
			} else if (type.compare("OBS_PROPERTY_DOUBLE") == 0) {
				double *value = reinterpret_cast<double *>(param.currentValue.data());
				config_set_double(config, section.c_str(), name.c_str(), *value);
			} else if (type.compare("OBS_PROPERTY_LIST") == 0) {
				if (subType.compare("OBS_COMBO_FORMAT_INT") == 0) {
					int64_t *value = reinterpret_cast<int64_t *>(param.currentValue.data());
					config_set_int(config, section.c_str(), name.c_str(), *value);
				} else if (subType.compare("OBS_COMBO_FORMAT_FLOAT") == 0) {
					double *value = reinterpret_cast<double *>(param.currentValue.data());
					config_set_double(config, section.c_str(), name.c_str(), *value);
				} else if (subType.compare("OBS_COMBO_FORMAT_STRING") == 0) {
					std::string value(param.currentValue.data(), param.currentValue.size());

					if (name.compare("MonitoringDeviceName") == 0) {
						std::string monDevName;
						std::string monDevId;

						if (value.compare("Default") != 0) {
							std::vector<std::pair<std::string, std::string>> monitoringDevice;

							auto enum_devices = [](void *param, const char *name, const char *id) {
								std::vector<std::pair<std::string, std::string>> *monitoringDevice =
									(std::vector<std::pair<std::string, std::string>> *)param;
								monitoringDevice->push_back(std::make_pair(name, id));
								return true;
							};
							obs_enum_audio_monitoring_devices(enum_devices, &monitoringDevice);

							std::vector<std::pair<std::string, std::string>>::iterator it =
								std::find_if(monitoringDevice.begin(), monitoringDevice.end(),
									     [&value](const std::pair<std::string, std::string> device) {
										     return (device.first.compare(value) == 0);
									     });

							if (it != monitoringDevice.end()) {
								monDevName = it->first;
								monDevId = it->second;
							} else {
								monDevName = "Default";
								monDevId = "default";
							}
						} else {
							monDevName = value;
							monDevId = "default";
						}
						config_set_string(config, section.c_str(), "MonitoringDeviceName", monDevName.c_str());
						config_set_string(config, section.c_str(), "MonitoringDeviceId", monDevId.c_str());
					} else if (name.compare("ColorSpace") == 0) {
						config_set_string(config, "AdvVideo", name.c_str(), value.c_str());
						video_colorspace colorspace = osn::Video::ColorSpaceFromStr(value);
						osn::Video::Manager::GetInstance().for_each(
							[colorspace](obs_video_info *ovi) { ovi->colorspace = colorspace; });
					} else if (name.compare("ColorFormat") == 0) {
						config_set_string(config, "AdvVideo", name.c_str(), value.c_str());
						video_format outputFormat = osn::Video::OutputFormFromStr(value);
						osn::Video::Manager::GetInstance().for_each(
							[outputFormat](obs_video_info *ovi) { ovi->output_format = outputFormat; });
					} else if (name.compare("ColorRange") == 0) {
						config_set_string(config, "AdvVideo", name.c_str(), value.c_str());
						video_range_type colorRange = osn::Video::ColoRangeFromStr(value);
						osn::Video::Manager::GetInstance().for_each([colorRange](obs_video_info *ovi) { ovi->range = colorRange; });
					} else {
						config_set_string(config, section.c_str(), name.c_str(), value.c_str());
					}
				}
			} else {
				std::cout << "type not found ! " << type.c_str() << std::endl;
			}
		}
	}
	config_save_safe(config, "tmp", nullptr);
}

void getDevices(const char *source_id, const char *property_name, std::vector<ipc::value> &rval)
{
	auto settings = obs_get_source_defaults(source_id);
	if (!settings)
		return;

	const char *dummy_device_name = "does_not_exist";
	obs_data_set_string(settings, property_name, dummy_device_name);
	if (strcmp(source_id, "dshow_input") == 0) {
		obs_data_set_string(settings, "video_device_id", dummy_device_name);
		obs_data_set_string(settings, "audio_device_id", dummy_device_name);
	}

	auto dummy_source = obs_source_create(source_id, dummy_device_name, settings, nullptr);
	if (!dummy_source)
		return;

	auto props = obs_source_properties(dummy_source);
	if (!props)
		return;

	auto prop = obs_properties_get(props, property_name);
	if (!prop)
		return;

	size_t items = obs_property_list_item_count(prop);
	if (rval.size() > 1)
		rval[1].value_union.ui64 += items;
	else
		rval.push_back(ipc::value((uint64_t)items));

	for (size_t idx = 0; idx < items; idx++) {
		const char *description = obs_property_list_item_name(prop, idx);
		const char *device_id = obs_property_list_item_string(prop, idx);

		if (!description || !strcmp(description, "") || !device_id || !strcmp(device_id, "")) {
			rval[1].value_union.ui64--;
			continue;
		}

		rval.push_back(ipc::value(description));
		rval.push_back(ipc::value(device_id));
	}

	obs_properties_destroy(props);
	obs_data_release(settings);
	obs_source_release(dummy_source);
}

#ifdef WIN32
void enumVideoDevices(std::vector<ipc::value> &rval)
{
	ComPtr<ICreateDevEnum> deviceEnum;
	ComPtr<IEnumMoniker> enumMoniker;
	ComPtr<IMoniker> deviceInfo;
	HRESULT hr;
	DWORD count = 0;
	ComPtr<IPropertyBag> propertyData;

	hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void **)&deviceEnum);
	if (FAILED(hr)) {
		blog(LOG_ERROR, "Could not create ICreateDeviceEnum");
		return;
	}

	hr = deviceEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &enumMoniker, 0);
	if (FAILED(hr)) {
		blog(LOG_ERROR, "CreateClassEnumerator failed");
		return;
	}

	uint32_t nbDevices = 0;
	if (hr == S_OK) {
		VARIANT deviceName, devicePath;
		deviceName.vt = VT_BSTR;
		devicePath.vt = VT_BSTR;
		devicePath.bstrVal = NULL;
		while (enumMoniker->Next(1, &deviceInfo, &count) == S_OK) {
			hr = deviceInfo->BindToStorage(0, 0, IID_IPropertyBag, (void **)&propertyData);
			if (FAILED(hr))
				continue;
			hr = propertyData->Read(L"FriendlyName", &deviceName, NULL);
			if (FAILED(hr))
				continue;

			char *utf8Name = NULL;
			os_wcs_to_utf8_ptr(deviceName.bstrVal, 0, &utf8Name);
			std::string deviceId = utf8Name;
			deviceId += ':'; // Not a bug, dshow expects it as a separator

			hr = propertyData->Read(L"DevicePath", &devicePath, NULL);
			if (SUCCEEDED(hr)) {
				char *utf8Path = NULL;
				os_wcs_to_utf8_ptr(devicePath.bstrVal, 0, &utf8Path);
				deviceId += utf8Path;
			}

			nbDevices++;
			rval.push_back(ipc::value(utf8Name));
			rval.push_back(ipc::value(deviceId));
		}
	}
	rval[1] = ipc::value(nbDevices + rval[1].value_union.ui32);
}

std::string GetDeviceName(IMMDevice *device)
{
	if (!device) {
		return "";
	}
	std::string device_name;
	ComPtr<IPropertyStore> store;
	HRESULT res;

	if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, store.Assign()))) {
		PROPVARIANT nameVar;

		PropVariantInit(&nameVar);
		res = store->GetValue(PKEY_Device_FriendlyName, &nameVar);

		if (SUCCEEDED(res) && nameVar.pwszVal && *nameVar.pwszVal) {
			size_t len = wcslen(nameVar.pwszVal);
			size_t size;

			size = os_wcs_to_utf8(nameVar.pwszVal, len, nullptr, 0);
			device_name.resize(size);
			os_wcs_to_utf8(nameVar.pwszVal, len, &device_name[0], size);
		}
	}

	return device_name;
}

void enumAudioDevices(std::vector<ipc::value> &rval, EDataFlow dataFlow)
{
	ComPtr<IMMDeviceEnumerator> enumerator;
	ComPtr<IMMDeviceCollection> collection;
	UINT count;
	uint32_t finalCount;
	HRESULT res;

	res = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void **)enumerator.Assign());
	if (FAILED(res))
		blog(LOG_ERROR, "Failed to create enumerator");

	res = enumerator->EnumAudioEndpoints(dataFlow, DEVICE_STATE_ACTIVE, collection.Assign());
	if (FAILED(res))
		blog(LOG_ERROR, "Failed to enumerate devices");

	res = collection->GetCount(&count);
	if (FAILED(res))
		blog(LOG_ERROR, "Failed to get device count");

	finalCount = count + rval[1].value_union.ui32;

	for (UINT i = 0; i < count; i++) {
		ComPtr<IMMDevice> device;
		CoTaskMemPtr<WCHAR> w_id;

		res = collection->Item(i, device.Assign());
		if (FAILED(res)) {
			finalCount--;
			continue;
		}

		res = device->GetId(&w_id);
		if (FAILED(res) || !w_id || !*w_id) {
			finalCount--;
			continue;
		}
		rval.push_back(ipc::value(GetDeviceName(device)));
		char *id = NULL;
		os_wcs_to_utf8_ptr(w_id, 0, &id);
		rval.push_back(ipc::value(id));
	}

	rval[1] = ipc::value(finalCount);
}

#endif

void OBS_settings::OBS_settings_getInputAudioDevices(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

#ifdef WIN32
	rval.push_back(ipc::value((uint32_t)1));
	rval.push_back(ipc::value("Default"));
	rval.push_back(ipc::value("default"));
	enumAudioDevices(rval, eCapture);
#elif __APPLE__
	const char *source_id = "coreaudio_input_capture";
	getDevices(source_id, "device_id", rval);
#endif

	AUTO_DEBUG;
}

void OBS_settings::OBS_settings_getOutputAudioDevices(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

#ifdef WIN32
	rval.push_back(ipc::value((uint32_t)1));
	rval.push_back(ipc::value("Default"));
	rval.push_back(ipc::value("default"));
	enumAudioDevices(rval, eRender);
#elif __APPLE__
	const char *source_id = "coreaudio_output_capture";
	getDevices(source_id, "device_id", rval);
#endif

	AUTO_DEBUG;
}

void OBS_settings::OBS_settings_getVideoDevices(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

#ifdef WIN32
	rval.push_back(ipc::value((uint32_t)0));
	enumVideoDevices(rval);
#elif __APPLE__
	const char *source_id = "av_capture_input";
	const char *property_name = "device";
	getDevices(source_id, property_name, rval);
#endif

	AUTO_DEBUG;
}

void convert_nvenc_h264_presets(obs_data_t *data)
{
	const char *preset = obs_data_get_string(data, "preset");
	const char *rc = obs_data_get_string(data, "rate_control");

	// If already using SDK10+ preset, return early.
	if (astrcmpi_n(preset, "p", 1) == 0) {
		obs_data_set_string(data, "preset2", preset);
		return;
	}

	if (astrcmpi(rc, "lossless") == 0 && astrcmpi(preset, "mq")) {
		obs_data_set_string(data, "preset2", "p3");
		obs_data_set_string(data, "tune", "lossless");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(rc, "lossless") == 0 && astrcmpi(preset, "hp")) {
		obs_data_set_string(data, "preset2", "p2");
		obs_data_set_string(data, "tune", "lossless");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "mq") == 0) {
		obs_data_set_string(data, "preset2", "p5");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "qres");

	} else if (astrcmpi(preset, "hq") == 0) {
		obs_data_set_string(data, "preset2", "p5");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "default") == 0) {
		obs_data_set_string(data, "preset2", "p3");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "hp") == 0) {
		obs_data_set_string(data, "preset2", "p1");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "ll") == 0) {
		obs_data_set_string(data, "preset2", "p3");
		obs_data_set_string(data, "tune", "ll");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "llhq") == 0) {
		obs_data_set_string(data, "preset2", "p4");
		obs_data_set_string(data, "tune", "ll");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "llhp") == 0) {
		obs_data_set_string(data, "preset2", "p2");
		obs_data_set_string(data, "tune", "ll");
		obs_data_set_string(data, "multipass", "disabled");
	}
}

void convert_nvenc_hevc_presets(obs_data_t *data)
{
	const char *preset = obs_data_get_string(data, "preset");
	const char *rc = obs_data_get_string(data, "rate_control");

	// If already using SDK10+ preset, return early.
	if (astrcmpi_n(preset, "p", 1) == 0) {
		obs_data_set_string(data, "preset2", preset);
		return;
	}

	if (astrcmpi(rc, "lossless") == 0 && astrcmpi(preset, "mq")) {
		obs_data_set_string(data, "preset2", "p5");
		obs_data_set_string(data, "tune", "lossless");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(rc, "lossless") == 0 && astrcmpi(preset, "hp")) {
		obs_data_set_string(data, "preset2", "p3");
		obs_data_set_string(data, "tune", "lossless");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "mq") == 0) {
		obs_data_set_string(data, "preset2", "p6");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "qres");

	} else if (astrcmpi(preset, "hq") == 0) {
		obs_data_set_string(data, "preset2", "p6");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "default") == 0) {
		obs_data_set_string(data, "preset2", "p5");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "hp") == 0) {
		obs_data_set_string(data, "preset2", "p1");
		obs_data_set_string(data, "tune", "hq");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "ll") == 0) {
		obs_data_set_string(data, "preset2", "p3");
		obs_data_set_string(data, "tune", "ll");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "llhq") == 0) {
		obs_data_set_string(data, "preset2", "p4");
		obs_data_set_string(data, "tune", "ll");
		obs_data_set_string(data, "multipass", "disabled");

	} else if (astrcmpi(preset, "llhp") == 0) {
		obs_data_set_string(data, "preset2", "p2");
		obs_data_set_string(data, "tune", "ll");
		obs_data_set_string(data, "multipass", "disabled");
	}
}

const char *convert_nvenc_simple_preset(const char *old_preset)
{
	if (astrcmpi(old_preset, "mq") == 0) {
		return "p5";
	} else if (astrcmpi(old_preset, "hq") == 0) {
		return "p5";
	} else if (astrcmpi(old_preset, "default") == 0) {
		return "p3";
	} else if (astrcmpi(old_preset, "hp") == 0) {
		return "p1";
	} else if (astrcmpi(old_preset, "ll") == 0) {
		return "p3";
	} else if (astrcmpi(old_preset, "llhq") == 0) {
		return "p4";
	} else if (astrcmpi(old_preset, "llhp") == 0) {
		return "p2";
	}
	return "p5";
}

bool update_nvenc_presets(obs_data_t *data, const char *encoderId)
{
	bool modified = false;
	if (astrcmpi(encoderId, "jim_nvenc") == 0 || astrcmpi(encoderId, "ffmpeg_nvenc") == 0) {
		if (obs_data_has_user_value(data, "preset") && !obs_data_has_user_value(data, "preset2")) {
			convert_nvenc_h264_presets(data);

			modified = true;
		}
	} else if (astrcmpi(encoderId, "jim_hevc_nvenc") == 0 || astrcmpi(encoderId, "ffmpeg_hevc_nvenc") == 0) {

		if (obs_data_has_user_value(data, "preset") && !obs_data_has_user_value(data, "preset2")) {
			convert_nvenc_hevc_presets(data);

			modified = true;
		}
	}
	if (modified)
		blog(LOG_INFO, "Updated nvenc preset for %s", encoderId);

	return modified;
}
