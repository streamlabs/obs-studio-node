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
#include <ipc-server.hpp>
#include <obs.h>
#include <obs.hpp>
#include "utility.hpp"
#undef strtoll
#include "nlohmann/json.hpp"

namespace osn {
class Source {
public:
	class Manager : public utility::unique_object_manager<obs_source_t> {
		friend class std::shared_ptr<Manager>;

	protected:
		Manager() {}
		~Manager() {}

	public:
		Manager(Manager const &) = delete;
		Manager operator=(Manager const &) = delete;

	public:
		static Manager &GetInstance();

		// Atomically finds the source and acquires a strong reference under the
		// manager lock, preventing destruction between find() and obs_source_get_ref().
		// Returns null (as OBSSourceAutoRelease) if not found or already destroyed.
		OBSSourceAutoRelease findAndRef(utility::unique_id::id_t id)
		{
			std::lock_guard<std::recursive_mutex> lock(internal_mutex);
			auto iter = object_map.find(id);
			if (iter == object_map.end())
				return nullptr;
			return obs_source_get_ref(iter->second);
		}
	};

	static void initialize_global_signals();
	static void finalize_global_signals();
	static void global_source_create_cb(void *ptr, calldata_t *cd);
	static void global_source_activate_cb(void *ptr, calldata_t *cd);
	static void global_source_deactivate_cb(void *ptr, calldata_t *cd);
	static void global_source_destroy_cb(void *ptr, calldata_t *cd);
	static void global_source_remove_cb(void *ptr, calldata_t *cd);

	static void attach_source_signals(obs_source_t *src);
	static void detach_source_signals(obs_source_t *src);

public:
	static void Register(ipc::server &);

	// Type Info
	static void GetTypeDefaults(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetTypeOutputFlags(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);

	// References
	static void Remove(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void Release(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);

	// Settings & Properties
	static void IsConfigurable(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetProperties(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetSettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void CallHandler(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void Update(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void Load(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void Save(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);

	static void GetType(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetName(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetName(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetOutputFlags(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetFlags(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetFlags(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetStatus(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetId(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SendMessage(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);

	// Flags
	static void GetMuted(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetMuted(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetEnabled(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetEnabled(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);

	// Browser source interaction
	static void SendMouseClick(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SendMouseMove(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SendMouseWheel(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SendFocus(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SendKeyClick(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
};
} // namespace osn
