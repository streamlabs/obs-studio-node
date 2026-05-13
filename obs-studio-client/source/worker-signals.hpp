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

#pragma once
#include <chrono>
#include <napi.h>
#include "osn-error.hpp"
#include "polling-pacer.hpp"
#include "utility.hpp"

struct SignalOutput {
	std::string outputType;
	std::string signal;
	int code;
	std::string errorMessage;
	bool sent;
	bool tosend;
};

class WorkerSignals {
public:
	WorkerSignals()
	{
		isWorkerRunning = false;
		workerStop = true;
		sleepInterval = std::chrono::milliseconds(33);
		workerThread = nullptr;
	};
	~WorkerSignals(){};

protected:
	bool isWorkerRunning;
	bool workerStop;
	std::chrono::milliseconds sleepInterval;
	std::thread *workerThread;
	Napi::ThreadSafeFunction jsThread;
	Napi::FunctionReference cb;

	void startWorker(napi_env env, Napi::Function asyncCallback, const std::string &name, const uint64_t &refID)
	{
		if (!workerStop || isWorkerRunning)
			return;

		isWorkerRunning = true;
		workerStop = false;
		jsThread = Napi::ThreadSafeFunction::New(env, asyncCallback, name.c_str(), 0, 1, [](Napi::Env) {});
		workerThread = new std::thread(&WorkerSignals::worker, this, name, refID);
	}

	void worker(const std::string &name, const uint64_t &refID)
	{
		const static int maximum_signals_in_queue = 100;
		auto callback = [](Napi::Env env, Napi::Function jsCallback, SignalOutput *data) {
			try {
				Napi::Object result = Napi::Object::New(env);

				result.Set(Napi::String::New(env, "type"), Napi::String::New(env, data->outputType));
				result.Set(Napi::String::New(env, "signal"), Napi::String::New(env, data->signal));
				result.Set(Napi::String::New(env, "code"), Napi::Number::New(env, data->code));
				result.Set(Napi::String::New(env, "error"), Napi::String::New(env, data->errorMessage));

				jsCallback.Call({result});
			} catch (...) {
				data->tosend = true;
				return;
			}
			data->sent = true;
		};
		std::vector<SignalOutput *> signalsList;
		osn::PollingPacer pacer(sleepInterval);
		while (!workerStop) {
			auto tp_start = std::chrono::high_resolution_clock::now();

			// Validate Connection
			auto conn = Controller::GetInstance().GetConnection();
			if (conn) {
				std::vector<ipc::value> response = conn->call_synchronous_helper(name, "Query", {ipc::value(refID)});
				if ((response.size() == 5) && signalsList.size() < maximum_signals_in_queue) {
					ErrorCode error = (ErrorCode)response[0].value_union.ui64;
					if (error == ErrorCode::Ok) {
						SignalOutput *data = new SignalOutput{"", "", 0, ""};
						data->outputType = response[1].value_str;
						data->signal = response[2].value_str;
						data->code = response[3].value_union.i32;
						data->errorMessage = response[4].value_str;
						data->sent = false;
						data->tosend = true;
						signalsList.push_back(data);
					}
				}

				std::vector<SignalOutput *>::iterator i = signalsList.begin();
				while (i != signalsList.end()) {
					if ((*i)->tosend) {
						(*i)->tosend = false;
						napi_status status = jsThread.BlockingCall((*i), callback);
						if (status != napi_ok) {
							(*i)->tosend = true;
							break;
						}
					}
					if ((*i)->sent) {
						i = signalsList.erase(i);
					} else {
						i++;
					}
				}
			}

			auto tp_end = std::chrono::high_resolution_clock::now();
			auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(tp_end - tp_start);
			const bool shouldSleep = pacer.finishCycle(dur);
			if (shouldSleep)
				std::this_thread::sleep_for(pacer.sleepDuration());
		}

		return;
	}

	void stopWorker(void)
	{
		if (workerStop || !isWorkerRunning)
			return;

		workerStop = true;
		isWorkerRunning = false;
		if (workerThread->joinable()) {
			workerThread->join();
		}
	}
};
