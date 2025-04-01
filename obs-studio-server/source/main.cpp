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

#include <chrono>
#include <inttypes.h>
#include <iostream>
#include <ipc-class.hpp>
#include <ipc.hpp>
#include <ipc-function.hpp>
#include <ipc-server.hpp>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>
#include "osn-error.hpp"
#include "nodeobs_api.h"
#include "nodeobs_autoconfig.h"
#include "nodeobs_content.h"
#include "nodeobs_service.h"
#include "nodeobs_settings.h"
#include "osn-fader.hpp"
#include "osn-filter.hpp"
#include "osn-global.hpp"
#include "osn-input.hpp"
#include "osn-module.hpp"
#include "osn-properties.hpp"
#include "osn-scene.hpp"
#include "osn-sceneitem.hpp"
#include "osn-source.hpp"
#include "osn-transition.hpp"
#include "osn-video.hpp"
#include "osn-volmeter.hpp"
#include "callback-manager.h"
#include "osn-video-encoder.hpp"
#include "osn-service.hpp"
#include "osn-audio.hpp"
#include "osn-simple-streaming.hpp"
#include "osn-advanced-streaming.hpp"
#include "osn-delay.hpp"
#include "osn-reconnect.hpp"
#include "osn-network.hpp"
#include "osn-audio-track.hpp"
#include "osn-simple-recording.hpp"
#include "osn-audio-encoder.hpp"
#include "osn-advanced-recording.hpp"
#include "osn-simple-replay-buffer.hpp"
#include "osn-advanced-replay-buffer.hpp"
#include "osn-file-output.hpp"

#include "util-crashmanager.h"
#include "shared.hpp"

#ifndef OSN_VERSION
#define OSN_VERSION "DEVMODE_VERSION"
#endif

#define GET_OSN_VERSION                                  \
	[]() {                                           \
		const char *__CHECK_EMPTY = OSN_VERSION; \
		if (strlen(__CHECK_EMPTY) < 3) {         \
			return "DEVMODE_VERSION";        \
		}                                        \
		return OSN_VERSION;                      \
	}()

#ifdef __APPLE__
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

#if defined(_WIN32)
#include "Shlobj.h"

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

#define BUFFSIZE 512

struct ServerData {
	std::mutex mtx;
	std::chrono::high_resolution_clock::time_point last_connect, last_disconnect;
	size_t count_connected = 0;
};

bool ServerConnectHandler(void *data, int64_t)
{
	ServerData *sd = reinterpret_cast<ServerData *>(data);
	std::unique_lock<std::mutex> ulock(sd->mtx);
	sd->last_connect = std::chrono::high_resolution_clock::now();
	sd->count_connected++;
	return true;
}

void ServerDisconnectHandler(void *data, int64_t)
{
	ServerData *sd = reinterpret_cast<ServerData *>(data);
	std::unique_lock<std::mutex> ulock(sd->mtx);
	sd->last_disconnect = std::chrono::high_resolution_clock::now();
	sd->count_connected--;
}

namespace System {
static void Shutdown(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	bool *shutdown = (bool *)data;
	*shutdown = true;
#ifdef __APPLE__
	g_util_osx->stopApplication();
#endif
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	return;
}
} // namespace System

int main(int argc, char *argv[])
{
#ifdef __APPLE__
	std::string_view slobsStdOutPath("/tmp/slobs-stdout");
	std::string_view slobsStdErrPath("/tmp/slobs-stderr");
	// Reuse file discriptors 1 and 2 in case they not open at launch so output to stdout and stderr not redirected to unexpected file
	struct stat sb;
	bool override_std_fd = false;
	int out_pid = -1;
	int out_err = -1;
	if (fstat(1, &sb) != 0) {
		override_std_fd = true;
		int out_pid = open(slobsStdOutPath.data(), O_WRONLY | O_CREAT | O_DSYNC);
		int out_err = open(slobsStdErrPath.data(), O_WRONLY | O_CREAT | O_DSYNC);
	}

	g_util_osx = new UtilInt();
	g_util_osx->init();
#endif
	std::string socketPath = "";
	std::string receivedVersion = "";
#ifdef __APPLE__
	socketPath = "/tmp/";
	if (argc != 4) {
#else
	if (argc != 3) {
#endif
		std::cerr << "Version mismatch. Expected <socketpath> <version> params" << std::endl;
		std::cerr << "argc: " << argc << std::endl;
		for (int nArg = 0; nArg < argc; nArg++) {
			std::cerr << "argv[" << nArg << "]: " << argv[nArg] << std::endl;
		}
		return ipc::ProcessInfo::ExitCode::VERSION_MISMATCH;
	}

	socketPath += argv[1];
	receivedVersion = argv[2];

	std::string myVersion = GET_OSN_VERSION;

#ifdef __APPLE__
	std::cerr << "Version recv: " << receivedVersion << std::endl;
	std::cerr << "Version compiled " << myVersion << std::endl;
#endif

	// Check versions
	if (receivedVersion != myVersion) {
		std::cerr << "Versions mismatch. Server version: " << myVersion << "but received client version: " << receivedVersion;
		return ipc::ProcessInfo::ExitCode::VERSION_MISMATCH;
	}

	// Usage:
	// argv[0] = Path to this application. (Usually given by default if run via path-based command!)
	// argv[1] = Path to a named socket.
	// argv[2] = version from client ; must match the server version

	// Instance
	ipc::server myServer;
	bool doShutdown = false;
	ServerData sd;
	sd.last_disconnect = sd.last_connect = std::chrono::high_resolution_clock::now();
	sd.count_connected = 0;
	OBS_API::SetCrashHandlerPipe(std::wstring(socketPath.begin(), socketPath.end()));
	if (myVersion.find("preview") != std::string::npos)
		myServer.set_call_timeout(30);
	// Classes
	/// System
	{
		std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("System");
		cls->register_function(std::make_shared<ipc::function>("Shutdown", std::vector<ipc::type>{}, System::Shutdown, &doShutdown));
		myServer.register_collection(cls);
	};

	/// OBS Studio Node
	osn::Global::Register(myServer);
	osn::Source::Register(myServer);
	osn::Input::Register(myServer);
	osn::Filter::Register(myServer);
	osn::Transition::Register(myServer);
	osn::Scene::Register(myServer);
	osn::SceneItem::Register(myServer);
	osn::Fader::Register(myServer);
	osn::Volmeter::Register(myServer);
	osn::Properties::Register(myServer);
	osn::Video::Register(myServer);
	osn::Module::Register(myServer);
	CallbackManager::Register(myServer);
	OBS_API::Register(myServer);
	OBS_content::Register(myServer);
	OBS_service::Register(myServer);
	OBS_settings::Register(myServer);
	OBS_settings::Register(myServer);
	autoConfig::Register(myServer);
	osn::VideoEncoder::Register(myServer);
	osn::Service::Register(myServer);
	osn::Audio::Register(myServer);
	osn::ISimpleStreaming::Register(myServer);
	osn::IAdvancedStreaming::Register(myServer);
	osn::IDelay::Register(myServer);
	osn::IReconnect::Register(myServer);
	osn::INetwork::Register(myServer);
	osn::IAudioTrack::Register(myServer);
	osn::ISimpleRecording::Register(myServer);
	osn::AudioEncoder::Register(myServer);
	osn::IAdvancedRecording::Register(myServer);
	osn::ISimpleReplayBuffer::Register(myServer);
	osn::IAdvancedReplayBuffer::Register(myServer);
	osn::IFileOutput::Register(myServer);

	OBS_API::CreateCrashHandlerExitPipe();

	// Register Connect/Disconnect Handlers
	myServer.set_connect_handler(ServerConnectHandler, &sd);
	myServer.set_disconnect_handler(ServerDisconnectHandler, &sd);

	// Initialize Server
	try {
		myServer.initialize(socketPath.c_str());
	} catch (std::exception &e) {
		std::cerr << "Initialization failed with error " << e.what() << "." << std::endl;
		return ipc::ProcessInfo::ExitCode::OTHER_ERROR;
	} catch (...) {
		std::cerr << "Failed to initialize server" << std::endl;
		return ipc::ProcessInfo::ExitCode::OTHER_ERROR;
	}

	// Reset Connect/Disconnect time.
	sd.last_disconnect = sd.last_connect = std::chrono::high_resolution_clock::now();

#ifdef __APPLE__
	// WARNING: Blocking function -> this won't return until the application
	// receives a stop or terminate event
	g_util_osx->runApplication();
#endif
#ifdef WIN32
	bool waitBeforeClosing = false;
	while (!doShutdown) {
		if (sd.count_connected == 0) {
			auto tp = std::chrono::high_resolution_clock::now();
			auto delta = tp - sd.last_disconnect;
			if (std::chrono::duration_cast<std::chrono::milliseconds>(delta).count() > 5000) {
				doShutdown = true;
				waitBeforeClosing = true;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	// Wait for crash handler listening thread to finish.
	// flag waitBeforeClosing: server process expect to receive the exit message from the crash-handler
	// before going further with shutdown. It needed for usecase where obs64 process stay alive and
	// continue streaming till user confirms exit in crash-handler.
	OBS_API::WaitCrashHandlerClose(waitBeforeClosing);
#endif
	osn::Source::finalize_global_signals();

	// First, be sure there are no connected clients
	myServer.finalize();

	// Then, shutdown OBS
	OBS_API::destroyOBS_API();
#ifdef __APPLE__
	util::CrashManager::DeleteBriefCrashInfoFile();
	if (override_std_fd) {
		close(out_pid);
		close(out_err);
		unlink(slobsStdOutPath.data());
		unlink(slobsStdErrPath.data());
	}
#endif
	return 0;
}
