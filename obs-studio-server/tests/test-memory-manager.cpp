#include <catch2/catch_test_macros.hpp>
#include "memory-manager.h"
#include "obs-setup.hpp"
#include "osn-source.hpp"
#include <obs.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

using namespace std::chrono_literals;

// Control time and the graphics executor, but run the production worker,
// accounting and OBS adapter. No cache policy is duplicated in the tests.
class MediaCacheManagerTestAccess {
public:
	static std::unique_ptr<MediaCacheManager> create(uint64_t budget, std::atomic<int64_t> *milliseconds = nullptr)
	{
		auto manager = std::unique_ptr<MediaCacheManager>(new MediaCacheManager(budget));
		if (milliseconds)
			manager->m_now = [milliseconds] { return MediaCacheManager::Clock::time_point(std::chrono::milliseconds(milliseconds->load())); };
		manager->initialize();
		return manager;
	}
	static void tick(MediaCacheManager &manager) { MediaCacheManager::graphicsTick(&manager, 0); }
	static bool waitForQueuedGraphicsJob(MediaCacheManager &manager, std::chrono::milliseconds timeout = 2s)
	{
		std::unique_lock lock(manager.m_mutex);
		return manager.m_changed.wait_for(lock, timeout, [&] { return !manager.m_graphicsJobs.empty(); });
	}
	// Checks queues and notifications, excluding future retries and executing graphics jobs.
	static bool waitForQueuesToDrain(MediaCacheManager &manager)
	{
		std::unique_lock lock(manager.m_mutex);
		return manager.m_changed.wait_for(lock, 2s, [&] {
			return !manager.m_notified && manager.m_completions.empty() && manager.m_graphicsJobs.empty() && manager.m_retiredEntries.empty();
		});
	}
	static void wake(MediaCacheManager &manager)
	{
		{
			std::lock_guard lock(manager.m_mutex);
			manager.m_notified = true;
		}
		manager.m_changed.notify_all();
	}
	static size_t queuedGraphicsJobCount(MediaCacheManager &manager)
	{
		std::lock_guard lock(manager.m_mutex);
		return manager.m_graphicsJobs.size();
	}
	static uint64_t reservedBytes(MediaCacheManager &manager)
	{
		std::lock_guard lock(manager.m_mutex);
		return manager.m_reservedCacheBytes;
	}
	static bool isAcceptingRequests(MediaCacheManager &manager)
	{
		std::lock_guard lock(manager.m_mutex);
		return manager.m_accepting;
	}
};

namespace {
struct MediaState {
	std::atomic<bool> ready{true};
	std::atomic<unsigned> queries{0};
	std::atomic<unsigned> destroys{0};
	std::atomic<bool> graphicsOnly{true};
	std::function<void()> onQuery;
};

void getFileInfo(void *data, calldata_t *cd)
{
	auto &state = *static_cast<MediaState *>(data);
	++state.queries;
	if (!obs_in_task_thread(OBS_TASK_GRAPHICS))
		state.graphicsOnly = false;
	if (state.onQuery)
		state.onQuery();
	calldata_set_bool(cd, "have_video", true);
	calldata_set_int(cd, "width", 10);
	calldata_set_int(cd, "height", 10);
	calldata_set_int(cd, "num_frames", state.ready ? 1 : 0);
	calldata_set_int(cd, "pix_format", VIDEO_FORMAT_I420); // 10 × 10 × 1 × 1.5 = 150 bytes per source.
}

void getPlaying(void *data, calldata_t *cd)
{
	calldata_set_bool(cd, "playing", static_cast<MediaState *>(data)->ready);
}

class ObsCore {
public:
	ObsCore()
	{
		REQUIRE(obs_startup("en-US", nullptr, nullptr));
		// It's a fake ffmpeg_source used for testing.
		// As we didn't load any real plugins above, the source ID is available.
		// No external files or decoder are needed for the scheduling tests.
		obs_source_info info{};
		info.id = "ffmpeg_source";
		info.type = OBS_SOURCE_TYPE_INPUT;
		info.output_flags = OBS_SOURCE_VIDEO;
		info.get_name = [](void *) { return "Cache test media"; };
		info.create = [](obs_data_t *settings, obs_source_t *source) -> void * {
			auto *state = reinterpret_cast<MediaState *>(obs_data_get_int(settings, "test_state"));
			proc_handler_t *handler = obs_source_get_proc_handler(source);
			proc_handler_add(handler, "void get_file_info(out int num_frames)", getFileInfo, state);
			proc_handler_add(handler, "void get_playing(out bool playing)", getPlaying, state);
			return state;
		};
		info.destroy = [](void *data) { ++static_cast<MediaState *>(data)->destroys; };
		info.get_width = [](void *) -> uint32_t { return 10; };
		info.get_height = [](void *) -> uint32_t { return 10; };
		obs_register_source(&info);
	}
	~ObsCore()
	{
		obs_wait_for_destroy_queue();
		obs_shutdown();
	}
};

OBSSourceAutoRelease makeCacheTestSource(const char *name, MediaState &state)
{
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_int(settings, "test_state", reinterpret_cast<int64_t>(&state));
	obs_data_set_string(settings, "local_file", name);
	obs_data_set_bool(settings, "is_local_file", true);
	obs_data_set_bool(settings, "looping", true);
	obs_data_set_bool(settings, "close_when_inactive", false);
	return obs_source_create("ffmpeg_source", name, settings, nullptr);
}

bool isCachingEnabled(obs_source_t *source)
{
	OBSDataAutoRelease settings = obs_source_get_settings(source);
	return obs_data_get_bool(settings, "caching");
}

void setLooping(MediaCacheManager &manager, obs_source_t *source, bool looping)
{
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_bool(settings, "looping", looping);
	obs_source_update(source, settings);
	manager.requestCacheUpdate(source);
}

bool processGraphicsJobsUntil(MediaCacheManager &manager, const std::function<bool()> &predicate)
{
	const auto deadline = std::chrono::steady_clock::now() + 3s;
	while (!predicate() && std::chrono::steady_clock::now() < deadline) {
		if (MediaCacheManagerTestAccess::waitForQueuedGraphicsJob(manager, 20ms))
			MediaCacheManagerTestAccess::tick(manager);
	}
	return predicate();
}
}

TEST_CASE("Media cache serializes rebalance and coalesces notifications", "[media-cache]")
{
	std::array<MediaState, 3> states;
	ObsCore core;
	auto manager = MediaCacheManagerTestAccess::create(300);
	std::array<OBSSourceAutoRelease, 3> sources{makeCacheTestSource("first", states[0]), makeCacheTestSource("second", states[1]),
						    makeCacheTestSource("third", states[2])};
	for (auto &source : sources)
		manager->registerSource(source);
	REQUIRE(MediaCacheManagerTestAccess::waitForQueuedGraphicsJob(*manager));
	for (int i = 0; i < 1000; ++i)
		manager->requestAllCacheUpdates();
	CHECK(MediaCacheManagerTestAccess::queuedGraphicsJobCount(*manager) <= sources.size());
	auto cachedCount = [&] { return std::count_if(sources.begin(), sources.end(), [](const auto &source) { return isCachingEnabled(source); }); };
	REQUIRE(processGraphicsJobsUntil(*manager, [&] { return cachedCount() == 2; }));
	REQUIRE(MediaCacheManagerTestAccess::waitForQueuesToDrain(*manager));
	CHECK(MediaCacheManagerTestAccess::reservedBytes(*manager) == 300);

	// Both cached entries become ineligible before either completion is handled.
	for (auto &source : sources) {
		if (isCachingEnabled(source))
			setLooping(*manager, source, false);
	}
	REQUIRE(processGraphicsJobsUntil(*manager, [&] { return cachedCount() == 1 && MediaCacheManagerTestAccess::reservedBytes(*manager) == 150; }));
	REQUIRE(MediaCacheManagerTestAccess::waitForQueuesToDrain(*manager));
	CHECK(MediaCacheManagerTestAccess::reservedBytes(*manager) == 150);
}

TEST_CASE("Media cache readiness retries do not block another source", "[media-cache]")
{
	MediaState waiting, ready;
	waiting.ready = false;
	std::atomic<int64_t> milliseconds{0};
	ObsCore core;
	auto manager = MediaCacheManagerTestAccess::create(300, &milliseconds);
	auto first = makeCacheTestSource("waiting", waiting);
	auto second = makeCacheTestSource("ready", ready);
	manager->registerSource(first);
	manager->registerSource(second);
	REQUIRE(processGraphicsJobsUntil(*manager, [&] { return isCachingEnabled(second); }));
	REQUIRE(MediaCacheManagerTestAccess::waitForQueuesToDrain(*manager));
	CHECK_FALSE(isCachingEnabled(first));
	CHECK(waiting.queries == 1);
	for (unsigned attempt = 2; attempt <= 10; ++attempt) {
		milliseconds += 500;
		MediaCacheManagerTestAccess::wake(*manager);
		REQUIRE(processGraphicsJobsUntil(*manager, [&] { return waiting.queries >= attempt; }));
		REQUIRE(MediaCacheManagerTestAccess::waitForQueuesToDrain(*manager));
	}
	milliseconds += 60000;
	MediaCacheManagerTestAccess::wake(*manager);
	REQUIRE(MediaCacheManagerTestAccess::waitForQueuesToDrain(*manager));
	CHECK_FALSE(MediaCacheManagerTestAccess::waitForQueuedGraphicsJob(*manager, 20ms));
	CHECK(waiting.queries == 10);
	CHECK(MediaCacheManagerTestAccess::reservedBytes(*manager) == 150);
}

TEST_CASE("Media cache removal cancels stale work after a rename", "[media-cache]")
{
	MediaState oldState, newState;
	ObsCore core;
	auto manager = MediaCacheManagerTestAccess::create(300);
	auto oldSource = makeCacheTestSource("reused name", oldState);
	manager->registerSource(oldSource);
	REQUIRE(MediaCacheManagerTestAccess::waitForQueuedGraphicsJob(*manager));
	obs_source_set_name(oldSource, "renamed");
	manager->unregisterSource(oldSource);
	oldSource = nullptr;
	auto replacement = makeCacheTestSource("reused name", newState);
	manager->registerSource(replacement);
	REQUIRE(processGraphicsJobsUntil(*manager, [&] { return isCachingEnabled(replacement); }));
	REQUIRE(MediaCacheManagerTestAccess::waitForQueuesToDrain(*manager));
	obs_wait_for_destroy_queue();
	CHECK(oldState.queries == 0);
	CHECK(oldState.destroys == 1);
	CHECK(MediaCacheManagerTestAccess::reservedBytes(*manager) == 150);
}

TEST_CASE("Media cache shutdown cancels work without graphics and can restart", "[media-cache]")
{
	std::array<MediaState, 2> states;
	ObsCore core;
	auto manager = MediaCacheManagerTestAccess::create(300);
	for (auto &state : states) {
		auto source = makeCacheTestSource("pending at shutdown", state);
		manager->registerSource(source);
		REQUIRE(MediaCacheManagerTestAccess::waitForQueuedGraphicsJob(*manager));
		source = nullptr;
		manager->shutdown();
		obs_wait_for_destroy_queue();
		CHECK(state.queries == 0);
		CHECK(state.destroys == 1);
		CHECK(MediaCacheManagerTestAccess::reservedBytes(*manager) == 0);
		manager->initialize();
	}
}

namespace {
void checkApiShutdown(bool removeMedia)
{
	std::atomic<unsigned> mediaDestroys{0};
	std::atomic<bool> observerDestroyed{false};
	std::atomic<bool> cacheAcceptingAtDestroy{true};
	struct Observation {
		std::atomic<bool> &destroyed;
		std::atomic<bool> &cacheAccepting;
	} observation{observerDestroyed, cacheAcceptingAtDestroy};
	{
		// Use the real OSN source registry, callbacks and shutdown entry point,
		// including the crash-reporting state required by leftover-source cleanup.
		osn::tests::ObsSetup setup;
		auto &manager = MediaCacheManager::GetInstance();
		// Initialization removes its temporary video mix. Leave graphics stopped
		// so the cache query remains pending until shutdown cancels it.
		REQUIRE(obs_get_video_info_by_index2(0) == nullptr);
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_bool(settings, "is_local_file", true);
		obs_data_set_bool(settings, "looping", true);
		OBSSourceAutoRelease media = obs_source_create("ffmpeg_source", "pending at API shutdown", settings, nullptr);
		REQUIRE(media != nullptr);
		REQUIRE(MediaCacheManagerTestAccess::waitForQueuedGraphicsJob(manager));
		signal_handler_connect(
			obs_source_get_signal_handler(media), "destroy", [](void *data, calldata_t *) { ++*static_cast<std::atomic<unsigned> *>(data); },
			&mediaDestroys);

		// Leave one frontend-owned reference for the API's leftover-source cleanup.
		// The cache manager holds its own reference until shutdown joins the worker.
		obs_source_get_ref(media);
		obs_source_info info{};
		info.id = "cache_shutdown_observer";
		info.type = OBS_SOURCE_TYPE_INPUT;
		info.get_name = [](void *) { return "Cache shutdown observer"; };
		info.create = [](obs_data_t *, obs_source_t *source) -> void * { return source; };
		info.destroy = [](void *) {};
		obs_register_source(&info);
		obs_source_t *observer = obs_source_create(info.id, "shutdown observer", nullptr, nullptr);
		REQUIRE(observer != nullptr);
		// This input is not retained by the media cache. Its destroy callback therefore
		// runs during the API's explicit source-release/drain phase. Stopping the
		// cache worker after that phase is too late to protect the registry walks.
		signal_handler_connect(
			obs_source_get_signal_handler(observer), "destroy",
			[](void *data, calldata_t *) {
				auto &result = *static_cast<Observation *>(data);
				result.cacheAccepting = MediaCacheManagerTestAccess::isAcceptingRequests(MediaCacheManager::GetInstance());
				result.destroyed = true;
			},
			&observation);

		if (removeMedia)
			obs_source_remove(media);
	}
	CHECK(observerDestroyed);
	CHECK_FALSE(cacheAcceptingAtDestroy);
	CHECK(mediaDestroys == 1);
	CHECK(osn::Source::Manager::GetInstance().size() == 0);
	CHECK_FALSE(MediaCacheManagerTestAccess::isAcceptingRequests(MediaCacheManager::GetInstance()));
}
}

TEST_CASE("OBS API stops the media cache before source teardown", "[media-cache][shutdown]")
{
	checkApiShutdown(false);
}

TEST_CASE("OBS API stops the media cache after removing a source", "[media-cache][shutdown]")
{
	checkApiShutdown(true);
}

TEST_CASE("OBS API stops the media cache with an empty source registry", "[media-cache][shutdown]")
{
	{
		osn::tests::ObsSetup setup;
		CHECK(MediaCacheManagerTestAccess::isAcceptingRequests(MediaCacheManager::GetInstance()));
		CHECK(osn::Source::Manager::GetInstance().size() == 0);
	}
	CHECK_FALSE(MediaCacheManagerTestAccess::isAcceptingRequests(MediaCacheManager::GetInstance()));
}

TEST_CASE("Media cache uses graphics across video reset and permits callback reentry", "[media-cache][graphics]")
{
	MediaState state;
	ObsCore core;
	obs_add_data_path((std::string(OSN_TEST_WD) + "/data/libobs/").c_str());
	auto manager = MediaCacheManagerTestAccess::create(300);
	auto source = makeCacheTestSource("graphics media", state);
	std::atomic<bool> reentered{false};
	state.onQuery = [&] {
		if (!reentered.exchange(true))
			manager->requestCacheUpdate(source);
	};
	manager->registerSource(source);
	REQUIRE(MediaCacheManagerTestAccess::waitForQueuedGraphicsJob(*manager));

	obs_video_info info{};
#ifdef _WIN32
	info.graphics_module = "libobs-d3d11.dll";
#elif defined(__APPLE__)
	info.graphics_module = "libobs-opengl.dylib";
#else
	info.graphics_module = "libobs-opengl.so";
#endif
	info.fps_num = 60;
	info.fps_den = 1;
	info.base_width = info.output_width = 320;
	info.base_height = info.output_height = 180;
	info.output_format = VIDEO_FORMAT_NV12;
	info.colorspace = VIDEO_CS_709;
	info.range = VIDEO_RANGE_PARTIAL;
	info.scale_type = OBS_SCALE_BILINEAR;
	REQUIRE(obs_reset_video(&info) == OBS_VIDEO_SUCCESS);
	auto waitCached = [&] {
		const auto deadline = std::chrono::steady_clock::now() + 3s;
		while (!isCachingEnabled(source) && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(5ms);
		return isCachingEnabled(source);
	};
	REQUIRE(waitCached());
	CHECK(reentered);
	CHECK(state.graphicsOnly);

	REQUIRE(obs_remove_video_info(obs_get_video_info_by_index2(0)) == OBS_VIDEO_SUCCESS);
	setLooping(*manager, source, false);
	REQUIRE(MediaCacheManagerTestAccess::waitForQueuedGraphicsJob(*manager));
	// The cache manager owns this pending task, so resetting video cannot lose it.
	REQUIRE(obs_reset_video(&info) == OBS_VIDEO_SUCCESS);
	const auto deadline = std::chrono::steady_clock::now() + 3s;
	while (isCachingEnabled(source) && std::chrono::steady_clock::now() < deadline)
		std::this_thread::sleep_for(5ms);
	CHECK_FALSE(isCachingEnabled(source));
	CHECK(state.graphicsOnly);
	REQUIRE(obs_remove_video_info(obs_get_video_info_by_index2(0)) == OBS_VIDEO_SUCCESS);
	manager->requestCacheUpdate(source);
	REQUIRE(MediaCacheManagerTestAccess::waitForQueuedGraphicsJob(*manager));
	manager->shutdown();
}
