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
#pragma once
#include <chrono>
#include <napi.h>
#include "utility-v8.hpp"
#ifdef WIN32
#include <windows.h>
#else
#include <semaphore.h>
#endif

struct AutoConfigInfo {
	std::string event;
	std::string description;
	double percentage = 0;
};

extern const char *ac_sem_name;
#ifdef WIN32
extern HANDLE ac_sem;
#else
extern sem_t *ac_sem;
#endif

namespace autoConfig {
extern bool isWorkerRunning;
extern bool worker_stop;
extern std::chrono::milliseconds sleepInterval;
extern Napi::ThreadSafeFunction js_thread;
extern std::thread *worker_thread;
extern std::vector<std::thread *> ac_queue_task_workers;

void worker(void);
void start_worker(void);
void stop_worker(void);
void queueTask(AutoConfigInfo *data);

void Init(Napi::Env env, Napi::Object exports);

Napi::Value InitializeAutoConfig(const Napi::CallbackInfo &info);
Napi::Value StartBandwidthTest(const Napi::CallbackInfo &info);
Napi::Value StartStreamEncoderTest(const Napi::CallbackInfo &info);
Napi::Value StartRecordingEncoderTest(const Napi::CallbackInfo &info);
Napi::Value StartCheckSettings(const Napi::CallbackInfo &info);
Napi::Value StartSetDefaultSettings(const Napi::CallbackInfo &info);
Napi::Value StartSaveStreamSettings(const Napi::CallbackInfo &info);
Napi::Value StartSaveSettings(const Napi::CallbackInfo &info);
Napi::Value TerminateAutoConfig(const Napi::CallbackInfo &info);
}
