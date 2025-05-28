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
#include <ipc-server.hpp>
#include <obs.h>
#include "utility.hpp"

namespace osn {
class Reconnect {
public:
	Reconnect()
	{
		enabled = true;
		retryDelay = 10;
		maxRetries = 20;
	}
	~Reconnect() {}

public:
	bool enabled;
	uint32_t retryDelay;
	uint32_t maxRetries;
};

class IReconnect {
public:
	class Manager : public utility::unique_object_manager<Reconnect> {
		friend class std::shared_ptr<Manager>;

	protected:
		Manager() {}
		~Manager() {}

	public:
		Manager(Manager const &) = delete;
		Manager operator=(Manager const &) = delete;

	public:
		static Manager &GetInstance();
	};

	static void Register(ipc::server &);

	static void Create(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void Destroy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetEnabled(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetEnabled(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetRetryDelay(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetRetryDelay(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetMaxRetries(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetMaxRetries(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
};
}