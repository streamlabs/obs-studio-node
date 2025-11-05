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
#include <functional>
#include <limits>
#include <list>
#include <map>
#include <mutex>
#include <obs.h>
#include <ipc-server.hpp>

#if defined(_MSC_VER)
#define __PRETTY_FUNCTION__ __FUNCSIG__
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE __attribute__((always_inline))
#endif
#define force_inline FORCE_INLINE

#define PRETTY_ERROR_RETURN(_error_code, _message)                                                           \
	{                                                                                                    \
		if (utility::osn_current_version() == "0.00.00-preview.0") {                                 \
			rval.push_back(ipc::value((uint64_t)_error_code));                                   \
			rval.push_back(ipc::value(_message));                                                \
			auto error_message = std::string(__PRETTY_FUNCTION__) + " " + std::string(_message); \
			blog(LOG_ERROR, "%s", error_message.c_str());                                        \
			return;                                                                              \
		} else {                                                                                     \
			rval.push_back(ipc::value((uint64_t)_error_code));                                   \
			rval.push_back(ipc::value(_message));                                                \
			auto error_message = std::string(__PRETTY_FUNCTION__) + " " + std::string(_message); \
			blog(LOG_ERROR, "%s", error_message.c_str());                                        \
			return;                                                                              \
		}                                                                                            \
	}

namespace utility {
std::string osn_current_version(const std::string &_version = "");

class unique_id {
public:
	typedef uint64_t id_t;
	typedef std::pair<id_t, id_t> range_t;

public:
	unique_id();
	virtual ~unique_id();

	id_t allocate();
	void free(id_t);

	bool is_allocated(id_t);
	id_t count(bool count_free);

protected:
	bool mark_used(id_t);
	void mark_used_range(id_t, id_t);
	bool mark_free(id_t);
	void mark_free_range(id_t, id_t);

private:
	std::list<range_t> allocated;
};

template<typename T> class unique_object_manager {
protected:
	utility::unique_id id_generator;
	std::map<utility::unique_id::id_t, T *> object_map;
	std::recursive_mutex internal_mutex;

public:
	unique_object_manager() {}
	~unique_object_manager() { clear(); }

	utility::unique_id::id_t allocate(T *obj)
	{
		std::lock_guard<std::recursive_mutex> lock(internal_mutex);

		utility::unique_id::id_t uid = id_generator.allocate();
		if (uid == std::numeric_limits<utility::unique_id::id_t>::max()) {
			return uid;
		}
		object_map.insert_or_assign(uid, obj);
		return uid;
	}

	utility::unique_id::id_t find(T *obj)
	{
		std::lock_guard<std::recursive_mutex> lock(internal_mutex);

		for (auto kv : object_map) {
			if (kv.second == obj) {
				return kv.first;
			}
		}
		return std::numeric_limits<utility::unique_id::id_t>::max();
	}
	T *find(utility::unique_id::id_t id)
	{
		std::lock_guard<std::recursive_mutex> lock(internal_mutex);

		auto iter = object_map.find(id);
		if (iter != object_map.end()) {
			return iter->second;
		}
		return nullptr;
	}

	utility::unique_id::id_t free(T *obj)
	{
		std::lock_guard<std::recursive_mutex> lock(internal_mutex);

		utility::unique_id::id_t uid = std::numeric_limits<utility::unique_id::id_t>::max();
		for (auto kv : object_map) {
			if (kv.second == obj) {
				uid = kv.first;
				object_map.erase(kv.first);
				break;
			}
		}
		return uid;
	}
	T *free(utility::unique_id::id_t id)
	{
		std::lock_guard<std::recursive_mutex> lock(internal_mutex);

		auto iter = object_map.find(id);
		if (iter == object_map.end()) {
			return nullptr;
		}
		T *obj = iter->second;
		object_map.erase(iter);
		return obj;
	}

	void for_each(std::function<void(T *)> for_each_method)
	{
		std::lock_guard<std::recursive_mutex> lock(internal_mutex);
		for (auto it = object_map.begin(); it != object_map.end(); ++it) {
			for_each_method(it->second);
		}
	}

	size_t size()
	{
		std::lock_guard<std::recursive_mutex> lock(internal_mutex);
		return object_map.size();
	}

	void clear()
	{
		std::lock_guard<std::recursive_mutex> lock(internal_mutex);
		object_map.clear();
	}
};

template<typename T> class generic_object_manager {
protected:
	utility::unique_id id_generator;
	std::map<utility::unique_id::id_t, T> object_map;
	std::recursive_mutex internal_mutex;

public:
	generic_object_manager() {}
	~generic_object_manager() { clear(); }

	utility::unique_id::id_t allocate(T obj)
	{
		std::lock_guard<std::recursive_mutex> lock(internal_mutex);

		utility::unique_id::id_t uid = id_generator.allocate();
		if (uid == std::numeric_limits<utility::unique_id::id_t>::max()) {
			return uid;
		}
		object_map.insert_or_assign(uid, obj);
		return uid;
	}

	utility::unique_id::id_t find(T obj)
	{
		std::lock_guard<std::recursive_mutex> lock(internal_mutex);

		for (auto kv : object_map) {
			if (kv.second == obj) {
				return kv.first;
			}
		}
		return std::numeric_limits<utility::unique_id::id_t>::max();
	}
	T find(utility::unique_id::id_t id)
	{
		std::lock_guard<std::recursive_mutex> lock(internal_mutex);

		auto iter = object_map.find(id);
		if (iter != object_map.end()) {
			return iter->second;
		}
		return nullptr;
	}

	utility::unique_id::id_t free(T obj)
	{
		std::lock_guard<std::recursive_mutex> lock(internal_mutex);

		utility::unique_id::id_t uid = std::numeric_limits<utility::unique_id::id_t>::max();
		for (auto kv : object_map) {
			if (kv.second == obj) {
				uid = kv.first;
				object_map.erase(kv.first);
				break;
			}
		}
		return uid;
	}
	T free(utility::unique_id::id_t id)
	{
		std::lock_guard<std::recursive_mutex> lock(internal_mutex);

		auto iter = object_map.find(id);
		if (iter == object_map.end()) {
			return nullptr;
		}
		T obj = iter->second;
		object_map.erase(iter);
		return obj;
	}

	void for_each(std::function<void(T &)> for_each_method)
	{
		std::lock_guard<std::recursive_mutex> lock(internal_mutex);
		for (auto it = object_map.begin(); it != object_map.end(); ++it) {
			for_each_method(it->second);
		}
	}

	size_t size()
	{
		std::lock_guard<std::recursive_mutex> lock(internal_mutex);
		return object_map.size();
	}

	void clear()
	{
		std::lock_guard<std::recursive_mutex> lock(internal_mutex);
		object_map.clear();
	}
};

void ProcessProperties(obs_properties_t *prp, obs_data *settings, std::vector<ipc::value> &rval);
const char *GetSafeString(const char *str);
} // namespace utility
