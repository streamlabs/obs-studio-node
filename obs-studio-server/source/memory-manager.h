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

#include <obs.h>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Manages in-memory caching of looping local media to reduce repeated decoding.
// Enables or disables source caches as activity and settings change, keeping
// their estimated total size within a shared memory budget.
//
// One worker owns cache decisions and accounting. OBS callbacks only invalidate
// entries; media queries and settings changes run in the graphics tick callback.
//
// The ffmpeg_source metadata handlers execute synchronously and access its
// current media player. OBS source updates and video ticks can destroy or
// replace that player on the graphics thread. Retaining the OBS source does
// not keep that particular player alive, and our mutex does not protect it.
// Run those queries in the graphics tick to serialize them with replacement,
// then return copied metadata to the worker.
class MediaCacheManager {
public:
	// Returns a non-owning reference to the process-wide singleton. Access is
	// thread-safe, but does not initialize OBS or start the cache worker.
	static MediaCacheManager &GetInstance();

	// Calls shutdown(). If still running, destruction has the same thread and
	// OBS-lifetime requirements; explicitly shut down before obs_shutdown().
	~MediaCacheManager();

	// Starts the worker and registers its graphics tick callback after OBS startup.
	// Call from the serialized OBS lifecycle, before connecting source callbacks;
	// do not overlap initialization with source operations or shutdown().
	// Does nothing if already running; can be called again after shutdown().
	// Video may be absent: queued media work waits for graphics ticks.
	void initialize();

	// Registers a borrowed, live ffmpeg_source and retains its own OBS reference.
	// Safe from OBS callbacks; schedules evaluation without waiting for completion.
	// Null/unsupported sources and calls while stopped or stopping are ignored.
	// Duplicate source identities keep their existing entry.
	// Evaluation requires graphics ticks. Registration does not guarantee caching:
	// settings, activity, media readiness and the memory budget govern the decision.
	void registerSource(obs_source_t *source);

	// Stops tracking this source identity and invalidates its pending jobs.
	// Safe from OBS callbacks; null or unregistered sources are ignored.
	// Does not wait for pending work to release its references; an operation already
	// executing may finish. Caller-owned references are unchanged. Does not call
	// obs_source_remove() or explicitly disable the source's cache setting.
	void unregisterSource(obs_source_t *source);

	// Requests reevaluation after a registered source's settings or activity change.
	// Safe from OBS callbacks; repeated requests are coalesced, and this call does
	// not wait for media queries or cache-setting changes to complete.
	// Null/unregistered sources and calls while stopped or stopping are ignored.
	void requestCacheUpdate(obs_source_t *source);

	// Requests reevaluation of every registered source, for example after a global
	// caching preference change. Safe from OBS callbacks; coalesces requests and
	// does not wait for completion. Ignored while stopped or stopping.
	void requestAllCacheUpdates();

	// Stops accepting requests, removes the tick callback, cancels queued work,
	// joins the worker and drops all retained source references.
	// Call before obs_shutdown(), from the serialized OBS lifecycle, on neither
	// the graphics thread nor this manager's worker. Do not overlap another
	// shutdown() or initialize(). Waits for any executing tick callback and worker,
	// but can cancel queued work with stopped or uninitialized video.
	// Does nothing if already stopped. Does not drain OBS's deferred destroy queue.
	// During OSN teardown, call this and then obs_wait_for_destroy_queue() before
	// traversing the source registry to connect/disconnect source signals.
	void shutdown();

private:
	friend class MediaCacheManagerTestAccess;
	using Clock = std::chrono::steady_clock;
	struct SourceEntry;
	struct Snapshot {
		std::string file;
		bool eligible = false;
		bool caching = false;
		bool playing = false;
		uint64_t estimatedCacheBytes = 0;
	};
	enum class JobType { QuerySource, SetCaching };
	struct GraphicsJob {
		std::shared_ptr<SourceEntry> source;
		uint64_t revision;
		JobType type = JobType::QuerySource;
		bool targetCachingEnabled = false;
		std::string file;
	};
	struct Completion {
		GraphicsJob job;
		Snapshot snapshot;
		bool valid = false;
		bool applied = false;
	};

	explicit MediaCacheManager(uint64_t budget = 0);
	MediaCacheManager(const MediaCacheManager &) = delete;
	MediaCacheManager &operator=(const MediaCacheManager &) = delete;
	static void graphicsTick(void *param, float seconds);
	static Snapshot readSettings(obs_source_t *source);
	static void queryMediaOnGraphicsThread(obs_source_t *source, Snapshot &snapshot);
	static void setCaching(obs_source_t *source, bool caching);
	void run();
	void complete(Completion &result);
	void queueCacheSettingUpdate(const std::shared_ptr<SourceEntry> &source, uint64_t revision, bool targetCachingEnabled);
	void releaseBudget(SourceEntry &source);

	// Protects only queues and bookkeeping. Never held during an OBS call,
	// source release, wait for graphics, or thread join.
	std::mutex m_mutex;
	std::condition_variable m_changed;
	std::map<obs_source_t *, std::shared_ptr<SourceEntry>> m_sources;
	std::vector<std::shared_ptr<SourceEntry>> m_retiredEntries;
	std::deque<GraphicsJob> m_graphicsJobs;
	std::vector<Completion> m_completions;
	std::thread m_worker;
	bool m_accepting = false;
	bool m_stopping = false;
	bool m_notified = false;
	bool m_rebalance = false;
	uint64_t m_reservedCacheBytes = 0;
	uint64_t m_cacheBudgetBytes;
	// Injectable clock for deterministic retry tests.
	std::function<Clock::time_point()> m_now = Clock::now;
};
