/******************************************************************************
    Copyright (C) 2016-2022 by Streamlabs (General Workings Inc)

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

#include "osn-audio.hpp"
#include "osn-error.hpp"
#include "shared.hpp"

#ifdef WIN32
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <util/windows/ComPtr.hpp>
#include <util/windows/HRError.hpp>
#include <util/windows/WinHandle.hpp>
#endif

// DELETE ME WHEN REMOVING NODEOBS
#include "nodeobs_configManager.hpp"

static bool disableAudioDucking = true;

void osn::Audio::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("Audio");
	cls->register_function(std::make_shared<ipc::function>("GetAudioContext", std::vector<ipc::type>{}, GetAudioContext));
	cls->register_function(
		std::make_shared<ipc::function>("SetAudioContext", std::vector<ipc::type>{ipc::type::UInt32, ipc::type::UInt32}, SetAudioContext));
	cls->register_function(std::make_shared<ipc::function>("GetLegacySettings", std::vector<ipc::type>{}, GetLegacySettings));
	cls->register_function(
		std::make_shared<ipc::function>("SetLegacySettings", std::vector<ipc::type>{ipc::type::UInt32, ipc::type::UInt32}, SetLegacySettings));
	cls->register_function(
		std::make_shared<ipc::function>("GetMonitoringDevice", std::vector<ipc::type>{ipc::type::String, ipc::type::String}, GetMonitoringDevice));
	cls->register_function(std::make_shared<ipc::function>("SetMonitoringDevice", std::vector<ipc::type>{}, SetMonitoringDevice));
	cls->register_function(std::make_shared<ipc::function>("GetMonitoringDeviceLegacy", std::vector<ipc::type>{}, GetMonitoringDeviceLegacy));
	cls->register_function(std::make_shared<ipc::function>("GetMonitoringDevices", std::vector<ipc::type>{}, GetMonitoringDevices));
	cls->register_function(std::make_shared<ipc::function>("GetDisableAudioDucking", std::vector<ipc::type>{}, GetDisableAudioDucking));
	cls->register_function(std::make_shared<ipc::function>("SetDisableAudioDucking", std::vector<ipc::type>{ipc::type::UInt32}, SetDisableAudioDucking));
	cls->register_function(std::make_shared<ipc::function>("GetDisableAudioDuckingLegacy", std::vector<ipc::type>{}, GetDisableAudioDuckingLegacy));
	srv.register_collection(cls);
}

void osn::Audio::GetAudioContext(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_audio_info audio;
	if (!obs_get_audio_info(&audio)) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "No audio context is currently set.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(audio.samples_per_sec));
	rval.push_back(ipc::value((uint32_t)audio.speakers));
	AUTO_DEBUG;
}

static const char *GetSpeakers(enum speaker_layout speakers)
{
	switch (speakers) {
	case SPEAKERS_UNKNOWN:
		return "Unknown";
	case SPEAKERS_MONO:
		return "Mono";
	case SPEAKERS_STEREO:
		return "Stereo";
	case SPEAKERS_2POINT1:
		return "2.1";
	case SPEAKERS_4POINT0:
		return "4.0";
	case SPEAKERS_4POINT1:
		return "4.1";
	case SPEAKERS_5POINT1:
		return "5.1";
	case SPEAKERS_7POINT1:
		return "7.1";
	default:
		return "Stereo";
	}
}

static enum speaker_layout GetSpeakersFromStr(const std::string &value)
{
	if (value.compare("Unknown") == 0)
		return SPEAKERS_UNKNOWN;
	else if (value.compare("Mono") == 0)
		return SPEAKERS_MONO;
	else if (value.compare("Stereo") == 0)
		return SPEAKERS_STEREO;
	else if (value.compare("2.1") == 0)
		return SPEAKERS_2POINT1;
	else if (value.compare("4.0") == 0)
		return SPEAKERS_4POINT0;
	else if (value.compare("4.1") == 0)
		return SPEAKERS_4POINT1;
	else if (value.compare("5.1") == 0)
		return SPEAKERS_5POINT1;
	else if (value.compare("7.1") == 0)
		return SPEAKERS_7POINT1;
	else
		return SPEAKERS_STEREO;
}

void osn::Audio::SetAudioContext(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	if (args.size() != 2) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Invalid number of arguments to set the audio context.");
	}

	uint32_t sampleRate = args[0].value_union.ui32;
	uint32_t speakers = args[1].value_union.ui32;

	obs_audio_info audio;
	audio.samples_per_sec = sampleRate;
	audio.speakers = (enum speaker_layout)speakers;

	if (!obs_reset_audio(&audio)) {
		blog(LOG_ERROR, "Failed to reset audio context, sampleRate: %d and speakers: %d", audio.samples_per_sec, audio.speakers);
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

static void SaveAudioSettings(obs_audio_info audio)
{
	config_set_uint(ConfigManager::getInstance().getBasic(), "Audio", "SampleRate", audio.samples_per_sec);
	config_set_string(ConfigManager::getInstance().getBasic(), "Audio", "ChannelSetup", GetSpeakers(audio.speakers));

	config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);
}

void osn::Audio::GetLegacySettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	uint64_t sampleRate = config_get_uint(ConfigManager::getInstance().getBasic(), "Audio", "SampleRate");
	std::string channelSetup = config_get_string(ConfigManager::getInstance().getBasic(), "Audio", "ChannelSetup");

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(sampleRate));
	rval.push_back(ipc::value((uint32_t)GetSpeakersFromStr(channelSetup)));
	AUTO_DEBUG;
}

void osn::Audio::SetLegacySettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	uint32_t sampleRate = args[0].value_union.ui32;
	uint32_t channelSetup = args[1].value_union.ui32;

	config_set_uint(ConfigManager::getInstance().getBasic(), "Audio", "SampleRate", sampleRate);
	config_set_string(ConfigManager::getInstance().getBasic(), "Audio", "ChannelSetup", GetSpeakers((enum speaker_layout)channelSetup));

	config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Audio::GetMonitoringDevice(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	const char *name = "";
	const char *idDevice = "";
	obs_get_audio_monitoring_device(&name, &idDevice);
	if (name && idDevice) {
		rval.push_back(ipc::value(name));
		rval.push_back(ipc::value(idDevice));
	}
	AUTO_DEBUG;
}

void osn::Audio::SetMonitoringDevice(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	if (args.size() != 2) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Invalid number of arguments to set the monitoring device.");
	}

	const char *name = args[0].value_str.c_str();
	const char *idDevice = args[1].value_str.c_str();

	obs_set_audio_monitoring_device(name, idDevice);

	config_set_string(ConfigManager::getInstance().getBasic(), "Audio", "MonitoringDeviceName", name);
	config_set_string(ConfigManager::getInstance().getBasic(), "Audio", "MonitoringDeviceId", idDevice);
	config_save_safe(ConfigManager::getInstance().getBasic(), "Audio", nullptr);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Audio::GetMonitoringDeviceLegacy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "Audio", "MonitoringDeviceName"))));
	rval.push_back(ipc::value(utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "Audio", "MonitoringDeviceId"))));
	AUTO_DEBUG;
}

void osn::Audio::GetMonitoringDevices(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)1));
	rval.push_back(ipc::value("Default"));
	rval.push_back(ipc::value("default"));

	auto enum_devices = [](void *param, const char *name, const char *id) {
		std::vector<ipc::value> *rval = (std::vector<ipc::value> *)param;
		if (name && id) {
			rval->push_back(ipc::value(name));
			rval->push_back(ipc::value(id));
		}
		return true;
	};

	obs_enum_audio_monitoring_devices(enum_devices, &rval);
	rval[1] = ipc::value((uint32_t)(rval.size() - 2) / 2);
	AUTO_DEBUG;
}

void osn::Audio::GetDisableAudioDucking(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)disableAudioDucking));
	AUTO_DEBUG;
}

void osn::Audio::SetDisableAudioDucking(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	disableAudioDucking = args[0].value_union.ui32;
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

#ifdef WIN32

	if (disableAudioDucking) {
		// When windows detects communications activity / 0 - Mute all other sounds / 1 - Reduce all other by 80% / 2 - Reduce all other by 50% / 3 - Do nothing
		DWORD nValue = 3;

		HKEY hKey = 0;
		HKEY hMainKey = HKEY_CURRENT_USER;
		std::string sKeyPath = "Software\\Microsoft\\Multimedia\\Audio";

		if (RegCreateKeyExA(hMainKey, sKeyPath.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
			LSTATUS lStatus = ::RegSetValueExA(hKey, "UserDuckingPreference", 0, REG_DWORD, (const BYTE *)&nValue, (DWORD)sizeof(DWORD));
			RegCloseKey(hKey);
		}
	}

	ComPtr<IMMDeviceEnumerator> devEmum;
	ComPtr<IMMDevice> device;
	ComPtr<IAudioSessionManager2> sessionManager2;
	ComPtr<IAudioSessionControl> sessionControl;
	ComPtr<IAudioSessionControl2> sessionControl2;

	HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (void **)&devEmum);
	if (FAILED(result))
		return;

	result = devEmum->GetDefaultAudioEndpoint(eRender, eConsole, &device);
	if (FAILED(result))
		return;

	result = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER, nullptr, (void **)&sessionManager2);
	if (FAILED(result))
		return;

	result = sessionManager2->GetAudioSessionControl(nullptr, 0, &sessionControl);
	if (FAILED(result))
		return;

	result = sessionControl->QueryInterface(&sessionControl2);
	if (FAILED(result))
		return;

	result = sessionControl2->SetDuckingPreference(disableAudioDucking);
	if (FAILED(result))
		return;
#endif

	config_set_bool(ConfigManager::getInstance().getBasic(), "Audio", "DisableAudioDucking", disableAudioDucking);
	config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);

	AUTO_DEBUG;
}

void osn::Audio::GetDisableAudioDuckingLegacy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)config_get_bool(ConfigManager::getInstance().getBasic(), "Audio", "DisableAudioDucking")));
	AUTO_DEBUG;
}