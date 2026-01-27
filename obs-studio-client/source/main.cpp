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

#if defined(_WIN32)
#include "Shlobj.h"
#endif

#include <fstream>
#include <string>
#include "controller.hpp"
#include "fader.hpp"
#include "filter.hpp"
#include "global.hpp"
#include "input.hpp"
#include "module.hpp"
#include "nodeobs_api.hpp"
#include "properties.hpp"
#include "scene.hpp"
#include "sceneitem.hpp"
#include "transition.hpp"
#include "video.hpp"
#include "volmeter.hpp"
#include "nodeobs_settings.hpp"
#include "nodeobs_display.hpp"
#include "nodeobs_service.hpp"
#include "nodeobs_autoconfig.hpp"
#include "callback-manager.hpp"
#include "video-encoder.hpp"
#include "service.hpp"
#include "audio.hpp"
#include "simple-streaming.hpp"
#include "advanced-streaming.hpp"
#include "delay.hpp"
#include "reconnect.hpp"
#include "network.hpp"
#include "audio-track.hpp"
#include "simple-recording.hpp"
#include "audio-encoder.hpp"
#include "advanced-recording.hpp"
#include "simple-replay-buffer.hpp"
#include "advanced-replay-buffer.hpp"
#include "enhanced-broadcasting-advanced-streaming.hpp"
#include "enhanced-broadcasting-simple-streaming.hpp"

#if defined(_WIN32)
// Checks ForceGPUAsRenderDevice setting
extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = [] {
	LPWSTR roamingPath;
	std::wstring filePath;
	std::string line;
	std::fstream file;
	bool settingValue = true; // Default value (NvOptimusEnablement = 1)

	if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roamingPath))) {
		// Couldn't find roaming app data folder path, assume default value
		return settingValue;
	} else {
		filePath.assign(roamingPath);
		filePath.append(L"\\slobs-client\\basic.ini");
		CoTaskMemFree(roamingPath);
	}

	file.open(filePath);

	if (file.is_open()) {
		while (std::getline(file, line)) {
			if (line.find("ForceGPUAsRenderDevice", 0) != std::string::npos) {
				if (line.substr(line.find('=') + 1) == "false") {
					settingValue = false;
					file.close();
					break;
				}

				break;
			}
		}
	} else {
		//Couldn't open config file, assume default value
		return settingValue;
	}

	// Return setting value
	return settingValue;
}();
#endif

int main(int, char **, char **) {}

Napi::Object main_node(Napi::Env env, Napi::Object exports)
{
#ifdef __APPLE__
	g_util_osx = new UtilInt();
	g_util_osx->init();
#endif
	osn::Fader::Init(env, exports);
	Controller::Init(env, exports);
	api::Init(env, exports);
	osn::Input::Init(env, exports);
	osn::Properties::Init(env, exports);
	osn::PropertyObject::Init(env, exports);
	osn::Filter::Init(env, exports);
	osn::Global::Init(env, exports);
	osn::Scene::Init(env, exports);
	osn::SceneItem::Init(env, exports);
	osn::Transition::Init(env, exports);
	osn::Module::Init(env, exports);
	osn::Video::Init(env, exports);
	osn::Volmeter::Init(env, exports);
	settings::Init(env, exports);
	display::Init(env, exports);
	service::Init(env, exports);
	autoConfig::Init(env, exports);
	globalCallback::Init(env, exports);
	osn::VideoEncoder::Init(env, exports);
	osn::Service::Init(env, exports);
	osn::Audio::Init(env, exports);
	osn::SimpleStreaming::Init(env, exports);
	osn::AdvancedStreaming::Init(env, exports);
	osn::Delay::Init(env, exports);
	osn::Reconnect::Init(env, exports);
	osn::Network::Init(env, exports);
	osn::AudioTrack::Init(env, exports);
	osn::SimpleRecording::Init(env, exports);
	osn::AudioEncoder::Init(env, exports);
	osn::AdvancedRecording::Init(env, exports);
	osn::SimpleReplayBuffer::Init(env, exports);
	osn::AdvancedReplayBuffer::Init(env, exports);
	osn::EnhancedBroadcastingAdvancedStreaming::Init(env, exports);
	osn::EnhancedBroadcastingSimpleStreaming::Init(env, exports);
	return exports;
};

NODE_API_MODULE(obs_studio_node, main_node);
