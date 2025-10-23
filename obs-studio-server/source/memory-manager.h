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
#include "obs.h"
#include "nodeobs_configManager.hpp"
#include <map>
#include <mutex>
#include <algorithm>
#include <vector>
#include <thread>
#include <shared.hpp>

#ifdef WIN32
#include <windows.h>
#include "psapi.h"
#endif

#define LIMIT 2004800000ul
#define MAX_POLLS 10

// Implements 'Singleton' design pattern
class MemoryManager {
public:
	static MemoryManager &GetInstance();
	virtual ~MemoryManager();

	void registerSource(obs_source_t *source);
	void unregisterSource(obs_source_t *source);
	void shutdownAllSources();
	void updateSourceCache(obs_source_t *source);
	void updateSourcesCache();

private:
	// Types
	struct source_info;

	// Constructors
	MemoryManager();

	// Copiers/movers
	MemoryManager(MemoryManager const &) = delete;
	MemoryManager &operator=(MemoryManager const &) = delete;
	MemoryManager(MemoryManager const &&) = delete;
	MemoryManager &operator=(MemoryManager const &&) = delete;

	// Methods
	bool isSourceValid(obs_source_t *source) const;
	void updateSettings(obs_source_t *source);

	void calculateRawSize(source_info &si);
	bool shouldCacheSource(source_info &si);
	void addCachedMemory(source_info &si);
	void removeCachedMemory(source_info &si, bool cacheNewFiles, const std::string &sourceName);
	void sourceManager(const std::string &sourceName);

	// Data
	std::mutex manager_mutex;
	std::map<std::string, std::shared_ptr<source_info>> sources;
	uint64_t available_memory;
	uint64_t current_cached_size;
	uint64_t allowed_cached_size;
};
