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
#include <atomic>
#include <memory>
#include <map>
#include <string>
#include "ipc.hpp"
#include "ipc-client.hpp"
#include <napi.h>

class Controller {
public:
	static Controller &GetInstance()
	{
		static Controller _inst;
		return _inst;
	}

private:
	Controller();
	~Controller();

public: // C++11
	Controller(Controller const &) = delete;
	void operator=(Controller const &) = delete;

	static void Init(Napi::Env env, Napi::Object exports);

public:
	std::shared_ptr<ipc::client> host(const std::string &uri);

	std::shared_ptr<ipc::client> connect(const std::string &uri);

	DWORD GetExitCode();

	void disconnect();

	std::shared_ptr<ipc::client> GetConnection();
	uint64_t GetConnectionEpoch() const;

private:
	bool m_isServer = false;
	std::shared_ptr<ipc::client> m_connection;
	ipc::ProcessInfo procId;
	// Monotonic "epoch" for the IPC connection lifecycle. Helps to detect stale wrappers
	// after disconnect/reconnect so finalizers can safely no-op.
	std::atomic<uint64_t> m_connectionEpoch{0};
};
