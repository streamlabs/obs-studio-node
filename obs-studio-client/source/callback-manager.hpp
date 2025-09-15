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
#include <string>
#include <thread>
#include <unordered_set>
#include "utility-v8.hpp"

struct SourceSizeInfo {
	std::string name;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t flags = 0;
};

struct SourceSizeInfoData {
	std::vector<std::unique_ptr<SourceSizeInfo>> items;
};

struct TransitionInfo {
	enum EventType : uint32_t {
		START = 0,
		STOP = 1,
	};

	std::string id;
	EventType event;
};

struct TransitionInfoData {
	std::vector<std::unique_ptr<TransitionInfo>> items;
};

struct SourceMessageInfo {
	std::string source_name;
	std::string message;
};

struct SourceMessageInfoData {
	std::vector<std::unique_ptr<SourceMessageInfo>> items;
};

namespace globalCallback {
extern bool isWorkerRunning;
extern bool worker_stop;
extern uint32_t sleepIntervalMS;
extern std::thread *worker_thread;
extern Napi::ThreadSafeFunction js_source_callback;
extern Napi::ThreadSafeFunction js_transition_callback;
extern Napi::ThreadSafeFunction js_volmeter_callback;
extern Napi::ThreadSafeFunction js_source_message_callback;
extern bool m_all_workers_stop;

extern std::mutex mtx_volmeters;
extern std::unordered_set<uint64_t> volmeters;

void worker(void);
void start_worker(napi_env env, Napi::Function async_callback);
void stop_worker(void);

void add_volmeter(uint64_t id);
void remove_volmeter(uint64_t id);

void Init(Napi::Env env, Napi::Object exports);

Napi::Value RegisterSourceCallback(const Napi::CallbackInfo &info);
Napi::Value RemoveSourceCallback(const Napi::CallbackInfo &info);

Napi::Value RegisterTransitionCallback(const Napi::CallbackInfo &info);
Napi::Value RemoveTransitionCallback(const Napi::CallbackInfo &info);

Napi::Value RegisterVolmeterCallback(const Napi::CallbackInfo &info);
Napi::Value RemoveVolmeterCallback(const Napi::CallbackInfo &info);

Napi::Value RegisterSourceMessageCallback(const Napi::CallbackInfo &info);
Napi::Value RemoveSourceMessageCallback(const Napi::CallbackInfo &info);
}