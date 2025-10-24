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

#include "memory-manager.h"
#include "nodeobs_api.h"

struct MemoryManager::source_info {
	bool cached = false;
	uint64_t size = 0;
	obs_source_t *source = nullptr;
	std::vector<std::thread> workers;
	std::mutex mtx;
	bool have_video = false;
};

MemoryManager &MemoryManager::GetInstance()
{
	static MemoryManager instance;
	return instance;
}

MemoryManager::MemoryManager()
{
	blog(LOG_INFO, "MemoryManager: constructor called");
#ifdef WIN32
	MEMORYSTATUSEX statex;
	statex.dwLength = sizeof(statex);
	current_cached_size = 0;

	if (::GlobalMemoryStatusEx(&statex)) {
		available_memory = statex.ullTotalPhys;
		allowed_cached_size = std::min<uint64_t>(LIMIT, available_memory / 2);
	} else {
		available_memory = 0;
		allowed_cached_size = LIMIT;
	}
#elif __APPLE__
	available_memory = g_util_osx->getTotalPhysicalMemory();
	allowed_cached_size = std::min((uint64_t)LIMIT, (uint64_t)available_memory / 2);
#endif
}

MemoryManager::~MemoryManager()
{
	blog(LOG_INFO, "MemoryManager: destructor called");

	for (auto &source : sources) {
		unregisterSource(source.second->source);
	}
}

// Not thread safe. 'si.mtx' should be locked
void MemoryManager::calculateRawSize(source_info &si)
{
	calldata_t cd = {0};
	proc_handler_t *ph = obs_source_get_proc_handler(si.source);
	proc_handler_call(ph, "get_file_info", &cd);
	si.have_video = calldata_bool(&cd, "have_video");

	const uint64_t pix_fmt = calldata_int(&cd, "pix_format");
	double bpp = 0;
	switch (pix_fmt) {
	case VIDEO_FORMAT_I420:
	case VIDEO_FORMAT_NV12:
	case VIDEO_FORMAT_I40A:
		bpp = 1.5;
		break;
	case VIDEO_FORMAT_YVYU:
	case VIDEO_FORMAT_YUY2:
	case VIDEO_FORMAT_UYVY:
	case VIDEO_FORMAT_I422:
	case VIDEO_FORMAT_I42A:
		bpp = 2;
		break;
	case VIDEO_FORMAT_RGBA:
	case VIDEO_FORMAT_BGRA:
	case VIDEO_FORMAT_BGRX:
	case VIDEO_FORMAT_Y800:
	case VIDEO_FORMAT_I444:
	case VIDEO_FORMAT_BGR3:
	case VIDEO_FORMAT_YUVA:
	case VIDEO_FORMAT_AYUV:
		bpp = 4;
		break;
	}

	const uint64_t width = calldata_int(&cd, "width");
	const uint64_t height = calldata_int(&cd, "height");
	const uint64_t nb_frames = calldata_int(&cd, "num_frames");
	si.size = static_cast<uint64_t>(width * height * bpp * nb_frames);
}

// Not thread safe. 'si.mtx' AND 'manager_mutex' should be locked
bool MemoryManager::shouldCacheSource(source_info &si)
{
	obs_data_t *settings = obs_source_get_settings(si.source);

	const bool looping = obs_data_get_bool(settings, "looping");
	const bool local_file = obs_data_get_bool(settings, "is_local_file");
	const bool enable_caching = OBS_API::getMediaFileCaching();
	bool showing = obs_source_showing(si.source);

	const bool is_small = obs_data_get_bool(settings, "caching") ? current_cached_size < allowed_cached_size
								     : current_cached_size + si.size < allowed_cached_size;

	if (!showing && !obs_data_get_bool(settings, "close_when_inactive"))
		showing = true;

	obs_data_release(settings);

	return looping && local_file && enable_caching && is_small && showing;
}

void updateSource(obs_source_t *source, bool caching)
{
	obs_data_t *settings = obs_source_get_settings(source);
	if (!settings)
		return;

	if (obs_data_get_bool(settings, "caching") != caching) {
		obs_data_set_bool(settings, "caching", caching);
		obs_source_update(source, settings);
	}
	obs_data_release(settings);
}

// Not thread safe. 'manager_mutex' and 'si.mtx' should be locked
void MemoryManager::addCachedMemory(source_info &si)
{
	if (!si.size || si.cached || current_cached_size + si.size > allowed_cached_size)
		return;

	int32_t retry = MAX_POLLS;

	proc_handler_t *ph = obs_source_get_proc_handler(si.source);
	bool playing = false;
	while (retry > 0) {
		calldata_t cd = {0};
		proc_handler_call(ph, "get_playing", &cd);
		playing = calldata_bool(&cd, "playing");
		if (playing)
			break;

		retry--;
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	if (!playing)
		return;

	blog(LOG_INFO, "adding %dMB, source: %s", si.size / 1000000, obs_source_get_name(si.source));
	current_cached_size += si.size;
	si.cached = true;

	updateSource(si.source, true);
}

// Not thread safe. 'manager_mutex' and 'si.mtx' should be locked
void MemoryManager::removeCachedMemory(source_info &si, bool cacheNewFiles, const std::string &sourceName)
{
	if (!si.cached)
		return;

	blog(LOG_INFO, "removing %dMB, source: %s", si.size / 1000000, sourceName.c_str());
	current_cached_size -= si.size;
	si.cached = false;

	updateSource(si.source, false);

	if (!cacheNewFiles || current_cached_size >= allowed_cached_size)
		return;

	for (const auto &data : sources) {
		if (data.second.get() == &si) {
			// Do not check self
			continue;
		}

		std::unique_lock ulock(data.second->mtx);
		if (shouldCacheSource(*data.second))
			addCachedMemory(*data.second);
	}
}

void MemoryManager::sourceManager(const std::string &sourceName)
{
	std::shared_ptr<source_info> si;
	{
		std::unique_lock manager_lock(manager_mutex);
		auto it = sources.find(sourceName);
		if (it == sources.end()) {
			return;
		}
		si = it->second;
		si->mtx.lock();
	}

	bool need_get_size = false;
	{
		std::unique_lock si_mtx_lock(si->mtx, std::adopt_lock);

		obs_data_t *settings = obs_source_get_settings(si->source);
		const bool looping = obs_data_get_bool(settings, "looping");
		const bool local_file = obs_data_get_bool(settings, "is_local_file");
		obs_data_release(settings);

		if (!looping || !local_file) {
			return;
		}

		need_get_size = si->size == 0;
	}

	if (need_get_size) {
		uint32_t retry = MAX_POLLS;
		while (retry > 0) {
			{
				std::unique_lock si_mtx_lock(si->mtx);
				// Double-check if source still registered in the manager before proceeding
				{
					std::unique_lock manager_lock(manager_mutex);
					if (sources.find(sourceName) == sources.end()) {
						return;
					}
				}
				calculateRawSize(*si); // This also sets 'si.have_video'
				if (si->size || !si->have_video) {
					break;
				}
			}
			retry--;
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}
	}

	{
		std::unique_lock si_mtx_lock(si->mtx);
		if (!si->size) {
			return;
		}

		// Double-check if source still registered in the manager before proceeding
		{
			std::unique_lock manager_lock(manager_mutex);
			if (sources.find(sourceName) == sources.end()) {
				return;
			}
		}

		const bool should_cache = shouldCacheSource(*si);
		if (should_cache) {
			addCachedMemory(*si);
		} else {
			removeCachedMemory(*si, true, sourceName);
		}
	}
}

// Not thread safe, should be called with locked 'manager_mutex'
void MemoryManager::updateSettings(obs_source_t *source)
{
	std::string sourceName = obs_source_get_name(source);
	auto it = sources.find(sourceName);
	if (it == sources.end()) {
		return;
	}

	std::unique_lock si_lock(it->second->mtx);
	it->second->workers.push_back(std::thread(&MemoryManager::sourceManager, this, sourceName));
}

void MemoryManager::updateSourceCache(obs_source_t *source)
{
	std::unique_lock ulock(manager_mutex);

	updateSettings(source);
}

void MemoryManager::updateSourcesCache()
{
	std::unique_lock ulock(manager_mutex);

	for (const auto &data : sources)
		updateSettings(data.second->source);
}

bool MemoryManager::isSourceValid(obs_source_t *source) const
{
	if (!source)
		return false;

	const char *source_id = obs_source_get_unversioned_id(source);
	if (!source_id)
		return false;

	if (strcmp(source_id, "ffmpeg_source") != 0)
		return false;

	return true;
}

void MemoryManager::registerSource(obs_source_t *source)
{
	if (!isSourceValid(source)) {
		return;
	}

	std::unique_lock ulock(manager_mutex);

	auto si = std::make_shared<source_info>();
	si->source = obs_source_get_ref(source);
	sources.emplace(obs_source_get_name(source), si);
	updateSource(source, false);
}

void MemoryManager::unregisterSource(obs_source_t *source)
{
	if (!isSourceValid(source)) {
		return;
	}

	const std::string source_name = obs_source_get_name(source);

	std::shared_ptr<source_info> moved_ptr;
	std::vector<std::thread> workers_to_join;
	{
		std::unique_lock ulock(manager_mutex);
		auto it = sources.find(source_name);
		if (it == sources.end()) {
			return;
		}
		std::unique_lock si_lock(it->second->mtx);

		// Moving pointer to have a valid object when proceeding with further deinit.
		moved_ptr = std::move(it->second);
		// Removing object from the collection early to be sure that it is unavailable anymore for outer clients
		// and someone can not spawn a new worker while we are waiting for worker threads to join.
		// Also this prevents data race if someone called 'unregisterSource' from other thread
		// while removal is in progress.
		workers_to_join = std::move(moved_ptr->workers);
		sources.erase(source_name);
	}

	for (auto &worker : workers_to_join) {
		if (worker.joinable()) {
			worker.join();
		}
	}

	{
		std::lock_guard lock(moved_ptr->mtx);
		removeCachedMemory(*moved_ptr, true, source_name);
		obs_source_release(moved_ptr->source);
	}
}

void MemoryManager::shutdownAllSources()
{
	std::unique_lock ulock(manager_mutex);

	std::vector<std::string> sourceKeys;
	for (const auto &pair : sources) {
		sourceKeys.push_back(pair.first);
	}

	for (auto &source_key : sourceKeys) {
		blog(LOG_INFO, "MemoryManager: shutdownAllSources: source %s", source_key.c_str());
		auto it = sources.find(source_key);
		if (it == sources.end()) {
			continue;
		}

		auto moved_ptr = std::move(it->second);
		sources.erase(source_key);

		for (auto &worker : moved_ptr->workers) {
			if (worker.joinable())
				worker.join();
		}

		moved_ptr->mtx.lock();
		removeCachedMemory(*moved_ptr, false, source_key);
		moved_ptr->mtx.unlock();
	}
}
