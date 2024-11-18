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

#include <mutex>
#include <napi.h>
#include <thread>
#include "utility-v8.hpp"

struct ServiceSignalInfo {
	std::string outputType;
	std::string signal;
	int code;
	std::string errorMessage;
	int service;

	bool sent;
	bool tosend;
};

namespace service {

extern bool isWorkerRunning;
extern bool worker_stop;
extern uint32_t sleepIntervalMS;
extern std::thread *worker_thread;
extern Napi::ThreadSafeFunction js_thread;
extern Napi::FunctionReference cb;

void worker(void);
void start_worker(napi_env env, Napi::Function async_callback);
void stop_worker(void);

void Init(Napi::Env env, Napi::Object exports);

Napi::Value OBS_service_resetAudioContext(const Napi::CallbackInfo &info);
Napi::Value OBS_service_resetVideoContext(const Napi::CallbackInfo &info);
Napi::Value OBS_service_setVideoInfo(const Napi::CallbackInfo &info);

Napi::Value OBS_service_startStreaming(const Napi::CallbackInfo &info);
Napi::Value OBS_service_startRecording(const Napi::CallbackInfo &info);
Napi::Value OBS_service_startReplayBuffer(const Napi::CallbackInfo &info);
Napi::Value OBS_service_stopStreaming(const Napi::CallbackInfo &info);
Napi::Value OBS_service_stopRecording(const Napi::CallbackInfo &info);
Napi::Value OBS_service_stopReplayBuffer(const Napi::CallbackInfo &info);

Napi::Value OBS_service_connectOutputSignals(const Napi::CallbackInfo &info);
Napi::Value OBS_service_removeCallback(const Napi::CallbackInfo &info);
Napi::Value OBS_service_processReplayBufferHotkey(const Napi::CallbackInfo &info);
Napi::Value OBS_service_getLastReplay(const Napi::CallbackInfo &info);
Napi::Value OBS_service_getLastRecording(const Napi::CallbackInfo &info);
void OBS_service_splitFile(const Napi::CallbackInfo &info);
int getServiceIdByName(std::string serviceName);
std::string getServiceNameById(int serviceId);

Napi::Value OBS_service_startVirtualCam(const Napi::CallbackInfo &info);
Napi::Value OBS_service_stopVirtualCam(const Napi::CallbackInfo &info);
Napi::Value OBS_service_updateVirtualCam(const Napi::CallbackInfo &info);

Napi::Value OBS_service_installVirtualCamPlugin(const Napi::CallbackInfo &info);
Napi::Value OBS_service_uninstallVirtualCamPlugin(const Napi::CallbackInfo &info);
Napi::Value OBS_service_isVirtualCamPluginInstalled(const Napi::CallbackInfo &info);
}