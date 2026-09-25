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
#include <obs.hpp>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <utility>

#ifdef WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <shared.hpp>
#endif

namespace {
constexpr uint64_t CACHE_LIMIT = 2004800000;
constexpr unsigned MAX_POLLS = 10;
constexpr size_t JOBS_PER_TICK = 8;

uint64_t cacheBudget()
{
#ifdef WIN32
	MEMORYSTATUSEX status{};
	status.dwLength = sizeof(status);
	if (GlobalMemoryStatusEx(&status))
		return std::min<uint64_t>(CACHE_LIMIT, status.ullTotalPhys / 2);
#elif defined(__APPLE__)
	if (g_util_osx)
		return std::min<uint64_t>(CACHE_LIMIT, g_util_osx->getTotalPhysicalMemory() / 2);
#endif
	return CACHE_LIMIT;
}
}

struct MediaCacheManager::SourceEntry {
	explicit SourceEntry(obs_source_t *source) : source(obs_source_get_ref(source)) {}
	~SourceEntry() { obs_source_release(source); }
	// Keeps the OBS source alive; its current media player can still be replaced.
	obs_source_t *source;
	std::atomic<uint64_t> revision{1};
	std::atomic<bool> removed{false};

	// Only the worker changes these fields, under the queue mutex.
	uint64_t scheduledRevision = 0;
	uint64_t reservedBytes = 0; // Includes an enable operation awaiting completion.
	std::string file;
	unsigned retries = 0;
	bool initialized = false;
	bool hasPendingGraphicsJob = false;
	bool waitingForBudget = false;
	Clock::time_point retryAt = Clock::time_point::max();
};

MediaCacheManager &MediaCacheManager::GetInstance()
{
	static MediaCacheManager instance;
	return instance;
}

MediaCacheManager::MediaCacheManager(uint64_t budget) : m_cacheBudgetBytes(budget ? budget : cacheBudget()) {}

MediaCacheManager::~MediaCacheManager()
{
	shutdown();
}

void MediaCacheManager::initialize()
{
	// Initialization and shutdown are serialized by the OBS API lifecycle.
	if (m_worker.joinable())
		return;
	m_stopping = false;
	m_accepting = true;
	m_notified = false;
	m_rebalance = false;
	m_reservedCacheBytes = 0;
	obs_add_tick_callback(graphicsTick, this);
	m_worker = std::thread(&MediaCacheManager::run, this);
}

void MediaCacheManager::registerSource(obs_source_t *source)
{
	if (!source)
		return;
	const char *id = obs_source_get_unversioned_id(source);
	if (!id || strcmp(id, "ffmpeg_source") != 0)
		return;

	// Keep every possible last release outside the queue lock.
	auto entry = std::make_shared<SourceEntry>(source);
	{
		std::lock_guard lock(m_mutex);
		if (!m_accepting)
			return;
		m_sources.emplace(source, entry);
		m_notified = true;
	}
	m_changed.notify_all();
}

void MediaCacheManager::unregisterSource(obs_source_t *source)
{
	{
		std::lock_guard lock(m_mutex);
		auto it = m_sources.find(source);
		if (it == m_sources.end())
			return;
		it->second->removed = true;
		++it->second->revision;
		m_retiredEntries.push_back(std::move(it->second));
		m_sources.erase(it);
		m_notified = true;
	}
	m_changed.notify_all();
}

void MediaCacheManager::requestCacheUpdate(obs_source_t *source)
{
	{
		std::lock_guard lock(m_mutex);
		auto it = m_sources.find(source);
		if (!m_accepting || it == m_sources.end())
			return;
		++it->second->revision;
		m_notified = true;
	}
	m_changed.notify_all();
}

void MediaCacheManager::requestAllCacheUpdates()
{
	{
		std::lock_guard lock(m_mutex);
		if (!m_accepting)
			return;
		for (auto &item : m_sources)
			++item.second->revision;
		m_notified = true;
	}
	m_changed.notify_all();
}

MediaCacheManager::Snapshot MediaCacheManager::readSettings(obs_source_t *source)
{
	OBSDataAutoRelease settings = obs_source_get_settings(source);
	Snapshot snapshot;
	snapshot.file = obs_data_get_string(settings, "local_file");
	snapshot.caching = obs_data_get_bool(settings, "caching");
	snapshot.eligible = !obs_source_removed(source) && OBS_API::getMediaFileCaching() && obs_data_get_bool(settings, "looping") &&
			    obs_data_get_bool(settings, "is_local_file") && (obs_source_showing(source) || !obs_data_get_bool(settings, "close_when_inactive"));
	return snapshot;
}

void MediaCacheManager::queryMediaOnGraphicsThread(obs_source_t *source, Snapshot &snapshot)
{
	// These handlers dereference the plugin's current media player. Run them on
	// graphics, alongside the plugin's deferred update and video_tick, and never
	// carry a player pointer across ticks. Cached players do not expose this info.
	if (!snapshot.eligible || snapshot.caching)
		return;
	calldata_t cd{};
	proc_handler_t *handler = obs_source_get_proc_handler(source);
	proc_handler_call(handler, "get_file_info", &cd);
	const auto width = calldata_int(&cd, "width");
	const auto height = calldata_int(&cd, "height");
	const auto frames = calldata_int(&cd, "num_frames");
	double bpp = 0;
	switch (calldata_int(&cd, "pix_format")) {
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
	const long double bytes = static_cast<long double>(width) * height * frames * bpp;
	if (calldata_bool(&cd, "have_video") && width > 0 && height > 0 && frames > 0 && bytes > 0 && bytes < std::numeric_limits<uint64_t>::max())
		snapshot.estimatedCacheBytes = static_cast<uint64_t>(bytes);
	calldata_free(&cd);
	calldata_init(&cd);
	proc_handler_call(handler, "get_playing", &cd);
	snapshot.playing = calldata_bool(&cd, "playing");
	calldata_free(&cd);
}

void MediaCacheManager::setCaching(obs_source_t *source, bool caching)
{
	// Apply only our setting; never write back an old copy of the source's
	// unrelated settings after the user has edited them.
	OBSDataAutoRelease patch = obs_data_create();
	// "caching" is a custom Streamlabs setting, OBS does not use it
	obs_data_set_bool(patch, "caching", caching);
	obs_source_update(source, patch);
}

void MediaCacheManager::graphicsTick(void *param, float)
{
	auto &manager = *static_cast<MediaCacheManager *>(param);
	std::vector<Completion> results;
	{
		std::lock_guard lock(manager.m_mutex);
		if (!manager.m_accepting)
			return;

		// Take one batch per tick.
		while (!manager.m_graphicsJobs.empty() && results.size() < JOBS_PER_TICK) {
			results.push_back({std::move(manager.m_graphicsJobs.front())});
			manager.m_graphicsJobs.pop_front();
		}
	}
	if (results.empty())
		return;

	// Process only the batch taken above. After a SetCaching job, any follow-up
	// QuerySource job must wait for a later tick so ffmpeg_source can process
	// its deferred settings update first.
	for (auto &result : results) {
		auto &job = result.job;
		if (job.source->removed || job.source->revision != job.revision)
			continue;
		result.valid = true;
		result.snapshot = readSettings(job.source->source);
		if (job.type == JobType::QuerySource) {
			queryMediaOnGraphicsThread(job.source->source, result.snapshot);
		} else if (!job.targetCachingEnabled || (result.snapshot.eligible && result.snapshot.file == job.file)) {
			if (result.snapshot.caching != job.targetCachingEnabled)
				setCaching(job.source->source, job.targetCachingEnabled);
			result.applied = true;
		}
	}
	{
		std::lock_guard lock(manager.m_mutex);
		for (auto &result : results)
			manager.m_completions.push_back(std::move(result));
		manager.m_notified = true;
	}
	manager.m_changed.notify_all();
}

void MediaCacheManager::releaseBudget(SourceEntry &source)
{
	if (source.reservedBytes) {
		m_reservedCacheBytes -= source.reservedBytes;
		source.reservedBytes = 0;
		m_rebalance = true;
	}
}

void MediaCacheManager::queueCacheSettingUpdate(const std::shared_ptr<SourceEntry> &source, uint64_t revision, bool targetCachingEnabled)
{
	source->hasPendingGraphicsJob = true;
	m_graphicsJobs.push_back({source, revision, JobType::SetCaching, targetCachingEnabled, source->file});
}

void MediaCacheManager::complete(Completion &result)
{
	auto &entry = *result.job.source;
	entry.hasPendingGraphicsJob = false;
	if (entry.removed)
		return;

	if (result.job.type == JobType::SetCaching) {
		// Graphics completion confirms only that the caching setting was applied
		// or already matched. Decoder cache creation is deferred.
		// Process this even if a newer notification arrived during the operation.
		if ((result.job.targetCachingEnabled && !result.applied) || (!result.job.targetCachingEnabled && result.applied))
			releaseBudget(entry);
		if (!result.applied || !result.job.targetCachingEnabled)
			entry.scheduledRevision = 0;
		if (result.applied)
			entry.initialized = true;
		return;
	}
	if (!result.valid || entry.revision != result.job.revision) {
		entry.scheduledRevision = 0;
		return;
	}

	const auto &snapshot = result.snapshot;
	const bool fileChanged = entry.file != snapshot.file;
	entry.file = snapshot.file;
	if (snapshot.caching && (!entry.initialized || fileChanged || !snapshot.eligible || !entry.reservedBytes)) {
		queueCacheSettingUpdate(result.job.source, result.job.revision, false);
		return;
	}
	entry.initialized = true;
	if (snapshot.caching)
		return;
	releaseBudget(entry);
	if (!snapshot.eligible)
		return;
	if (!snapshot.estimatedCacheBytes || !snapshot.playing) {
		if (++entry.retries < MAX_POLLS)
			entry.retryAt = m_now() + (snapshot.estimatedCacheBytes ? std::chrono::milliseconds(100) : std::chrono::milliseconds(500));
		return;
	}
	if (snapshot.estimatedCacheBytes > m_cacheBudgetBytes - m_reservedCacheBytes) {
		entry.waitingForBudget = true;
		return;
	}
	entry.reservedBytes = snapshot.estimatedCacheBytes;
	m_reservedCacheBytes += snapshot.estimatedCacheBytes;
	queueCacheSettingUpdate(result.job.source, result.job.revision, true);
}

void MediaCacheManager::run()
{
	std::unique_lock lock(m_mutex);
	while (!m_stopping) {
		m_notified = false;
		std::vector<std::shared_ptr<SourceEntry>> released;
		released.swap(m_retiredEntries);
		for (auto &entry : released)
			releaseBudget(*entry);
		std::vector<GraphicsJob> cancelled;
		for (auto it = m_graphicsJobs.begin(); it != m_graphicsJobs.end();) {
			if (it->source->removed) {
				cancelled.push_back(std::move(*it));
				it = m_graphicsJobs.erase(it);
			} else {
				++it;
			}
		}
		std::vector<Completion> finished;
		finished.swap(m_completions);
		for (auto &result : finished)
			complete(result);

		const auto currentTime = m_now();
		auto nextRetry = Clock::time_point::max();
		for (auto &item : m_sources) {
			auto &entry = *item.second;
			if (entry.removed || entry.hasPendingGraphicsJob)
				continue;
			const auto revision = entry.revision.load();
			const bool updated = entry.scheduledRevision != revision;
			if (updated || (m_rebalance && entry.waitingForBudget) || entry.retryAt <= currentTime) {
				if (updated)
					entry.retries = 0;
				entry.scheduledRevision = revision;
				entry.retryAt = Clock::time_point::max();
				entry.waitingForBudget = false;
				entry.hasPendingGraphicsJob = true;
				m_graphicsJobs.push_back({item.second, revision, JobType::QuerySource});
			} else {
				nextRetry = std::min(nextRetry, entry.retryAt);
			}
		}
		m_rebalance = false;
		m_changed.notify_all();

		// Final source release performs synchronous OBS cleanup and can invoke
		// callbacks, so release references without holding m_mutex.
		// Destruction also queues deferred callbacks; these clears do not wait
		// for them to finish.
		lock.unlock();
		released.clear();
		cancelled.clear();
		finished.clear();
		lock.lock();
		if (nextRetry == Clock::time_point::max())
			m_changed.wait(lock, [this] { return m_stopping || m_notified; });
		else
			m_changed.wait_for(lock, nextRetry - m_now(), [this] { return m_stopping || m_notified; });
	}

	// These locals keep source references alive until after the mutex is unlocked.
	// Final release can acquire OBS locks or invoke callbacks that reenter the
	// manager; doing that while holding the mutex could deadlock.
	auto old_sources = std::exchange(m_sources, {});
	auto old_graphicsJobs = std::exchange(m_graphicsJobs, {});
	auto old_completions = std::exchange(m_completions, {});
	auto old_retiredEntries = std::exchange(m_retiredEntries, {});
	m_reservedCacheBytes = 0;
	lock.unlock();
}

void MediaCacheManager::shutdown()
{
	if (!m_worker.joinable())
		return;
	{
		std::lock_guard lock(m_mutex);
		m_accepting = false;
		for (auto &item : m_sources)
			item.second->removed = true;
	}
	// OBS synchronizes removal with any tick currently executing. Our queue is
	// owned here, not by obs_queue_task: reset/stopped video cannot discard a
	// task payload or strand the worker waiting for a graphics completion.
	obs_remove_tick_callback(graphicsTick, this);
	{
		std::lock_guard lock(m_mutex);
		m_stopping = true;
	}
	m_changed.notify_all();
	m_worker.join();
}
