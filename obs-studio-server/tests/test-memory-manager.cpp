#include <catch2/catch_test_macros.hpp>
#include "memory-manager.h"
#include "obs-setup.hpp"
#include "osn-error.hpp"
#include "osn-source.hpp"
#include <obs.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <optional>
#include <thread>
#include <utility>

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
	// A settings update can deliberately leave a graphics query queued.
	static bool waitForWorkerPass(MediaCacheManager &manager)
	{
		std::unique_lock lock(manager.m_mutex);
		return manager.m_changed.wait_for(lock, 2s, [&] { return !manager.m_notified && manager.m_completions.empty(); });
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
	std::atomic<uint32_t> appliedWidth{10};
	std::atomic<unsigned> queries{0};
	std::atomic<unsigned> destroys{0};
	std::atomic<bool> graphicsOnly{true};
	std::function<void()> onQuery;
	std::function<void(obs_data_t *)> onProperties;
};

void applyMediaSettings(void *data, obs_data_t *settings)
{
	// Model player replacement only when OBS invokes the source's update callback.
	// One I420 frame is 150 bytes normally, 300 for medium.webm, or 1500 for large.webm.
	const char *file = obs_data_get_string(settings, "local_file");
	static_cast<MediaState *>(data)->appliedWidth = strcmp(file, "large.webm") == 0 ? 100 : strcmp(file, "medium.webm") == 0 ? 20 : 10;
}

void getFileInfo(void *data, calldata_t *cd)
{
	auto &state = *static_cast<MediaState *>(data);
	const auto width = state.appliedWidth.load();
	++state.queries;
	if (!obs_in_task_thread(OBS_TASK_GRAPHICS))
		state.graphicsOnly = false;
	if (state.onQuery)
		state.onQuery();
	calldata_set_bool(cd, "have_video", true);
	calldata_set_int(cd, "width", width);
	calldata_set_int(cd, "height", 10);
	calldata_set_int(cd, "num_frames", state.ready ? 1 : 0);
	calldata_set_int(cd, "pix_format", VIDEO_FORMAT_I420);
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
			applyMediaSettings(state, settings);
			proc_handler_t *handler = obs_source_get_proc_handler(source);
			proc_handler_add(handler, "void get_file_info(out int num_frames)", getFileInfo, state);
			proc_handler_add(handler, "void get_playing(out bool playing)", getPlaying, state);
			return state;
		};
		info.update = applyMediaSettings;
		info.destroy = [](void *data) { ++static_cast<MediaState *>(data)->destroys; };
		info.get_width = [](void *data) -> uint32_t { return static_cast<MediaState *>(data)->appliedWidth; };
		info.get_height = [](void *) -> uint32_t { return 10; };
		info.get_properties = [](void *data) {
			auto *properties = obs_properties_create();
			auto *file = obs_properties_add_text(properties, "local_file", "File", OBS_TEXT_DEFAULT);
			obs_property_set_modified_callback2(
				file,
				[](void *data, obs_properties_t *, obs_property_t *, obs_data_t *settings) {
					auto &state = *static_cast<MediaState *>(data);
					if (state.onProperties)
						state.onProperties(settings);
					return false;
				},
				data);
			return properties;
		};
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
	auto update = manager.trackSourceSettingsUpdate(source);
	obs_source_update(source, settings);
}

void setFile(MediaCacheManager &manager, obs_source_t *source, const char *file)
{
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "local_file", file);
	auto update = manager.trackSourceSettingsUpdate(source);
	obs_source_update(source, settings);
}

void runFrame(MediaCacheManager &manager, std::initializer_list<obs_source_t *> sources)
{
	REQUIRE(MediaCacheManagerTestAccess::waitForWorkerPass(manager));
	// Preserve libobs's order: tick callbacks precede deferred source updates.
	MediaCacheManagerTestAccess::tick(manager);
	for (auto *source : sources)
		obs_source_video_tick(source, 0);
	REQUIRE(MediaCacheManagerTestAccess::waitForWorkerPass(manager));
}

// Exercise OSN source entry points with the fake ffmpeg_source registered by
// ObsCore. Full OSN initialization would load the real implementation under
// the same source ID.
class RegisteredApiSource {
public:
	explicit RegisteredApiSource(obs_source_t *source) : id(osn::Source::Manager::GetInstance().allocate(source))
	{
		MediaCacheManager::GetInstance().initialize();
		MediaCacheManager::GetInstance().registerSource(source);
	}
	~RegisteredApiSource()
	{
		MediaCacheManager::GetInstance().shutdown();
		osn::Source::Manager::GetInstance().free(id);
	}
	RegisteredApiSource(const RegisteredApiSource &) = delete;
	RegisteredApiSource &operator=(const RegisteredApiSource &) = delete;
	const uint64_t id;
};

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

TEST_CASE("Media cache waits for deferred file updates before reserving memory", "[media-cache][settings]")
{
	bool editAgain = false;
	SECTION("One file replacement") {}
	SECTION("Another edit during the settling interval")
	{
		editAgain = true;
	}

	MediaState state;
	state.ready = false;
	std::atomic<int64_t> milliseconds{0};
	ObsCore core;
	auto manager = MediaCacheManagerTestAccess::create(300, &milliseconds);
	auto source = makeCacheTestSource("small.webm", state);
	manager->registerSource(source);
	runFrame(*manager, {source});
	REQUIRE(state.queries == 1);
	REQUIRE(MediaCacheManagerTestAccess::waitForQueuesToDrain(*manager));

	state.ready = true;
	setFile(*manager, source, editAgain ? "medium.webm" : "large.webm");
	REQUIRE(state.appliedWidth == 10); // Live settings changed, but the player has not.
	runFrame(*manager, {source});
	CHECK(state.queries == 1);
	CHECK(state.appliedWidth == (editAgain ? 20 : 100));
	if (editAgain) {
		setFile(*manager, source, "large.webm");
		runFrame(*manager, {source});
		CHECK(state.queries == 1);
	}
	for (int i = 0; i < 4; ++i)
		runFrame(*manager, {source});
	REQUIRE(MediaCacheManagerTestAccess::waitForQueuesToDrain(*manager));
	CHECK(state.queries == 2);
	CHECK_FALSE(isCachingEnabled(source));
	CHECK(MediaCacheManagerTestAccess::reservedBytes(*manager) == 0);
}

TEST_CASE("Media cache settings guards cover paused nested and moved updates", "[media-cache][settings]")
{
	MediaState changing, ready;
	std::atomic<int64_t> milliseconds{0};
	ObsCore core;
	auto manager = MediaCacheManagerTestAccess::create(300, &milliseconds);
	auto source = makeCacheTestSource("small.webm", changing);
	auto other = makeCacheTestSource("other", ready);
	manager->registerSource(source);
	REQUIRE(MediaCacheManagerTestAccess::waitForQueuedGraphicsJob(*manager));

	std::optional<MediaCacheManager::SourceSettingsUpdate> update(manager->trackSourceSettingsUpdate(source));
	OBSDataAutoRelease settings = obs_source_get_settings(source);
	obs_data_set_string(settings, "local_file", "large.webm");
	// Pause after the live settings mutation, before scheduling the plugin update.
	// More than MAX_POLLS frames must neither query old metadata nor consume retries.
	manager->registerSource(other);
	for (int i = 0; i < 12; ++i)
		runFrame(*manager, {source, other});
	CHECK(changing.queries == 0);
	CHECK(changing.appliedWidth == 10);
	CHECK_FALSE(isCachingEnabled(source));
	CHECK(isCachingEnabled(other));
	CHECK(MediaCacheManagerTestAccess::reservedBytes(*manager) == 150);

	std::optional<MediaCacheManager::SourceSettingsUpdate> moved(std::move(*update));
	update.reset(); // The moved-from guard must not finish the update.
	{
		auto nested = manager->trackSourceSettingsUpdate(source);
		obs_source_update(source, nullptr);
	}
	runFrame(*manager, {source, other});
	CHECK(changing.queries == 0); // The outer guard still holds the query.
	CHECK(changing.appliedWidth == 100);
	moved.reset();
	runFrame(*manager, {source, other});
	CHECK(changing.queries == 0);
	for (int i = 0; i < 4; ++i)
		runFrame(*manager, {source, other});
	REQUIRE(MediaCacheManagerTestAccess::waitForQueuesToDrain(*manager));
	CHECK(changing.queries == 1);
	CHECK_FALSE(isCachingEnabled(source));
	CHECK(MediaCacheManagerTestAccess::reservedBytes(*manager) == 150);
}

TEST_CASE("Media cache discards metadata when settings change during a query", "[media-cache][settings]")
{
	MediaState state;
	ObsCore core;
	auto manager = MediaCacheManagerTestAccess::create(300);
	auto source = makeCacheTestSource("small.webm", state);
	bool edited = false;
	state.onQuery = [&] {
		if (!std::exchange(edited, true))
			setFile(*manager, source, "large.webm");
	};
	manager->registerSource(source);
	runFrame(*manager, {source});
	REQUIRE(edited);
	CHECK(MediaCacheManagerTestAccess::reservedBytes(*manager) == 0);
	for (int i = 0; i < 4; ++i)
		runFrame(*manager, {source});
	REQUIRE(MediaCacheManagerTestAccess::waitForQueuesToDrain(*manager));
	CHECK(state.queries == 2);
	CHECK_FALSE(isCachingEnabled(source));
	CHECK(MediaCacheManagerTestAccess::reservedBytes(*manager) == 0);
}

TEST_CASE("Media cache settings guards retain removed sources through shutdown", "[media-cache][settings][shutdown]")
{
	bool stop = false;
	SECTION("Unregister with a pending guard") {}
	SECTION("Stop with a pending guard")
	{
		stop = true;
	}

	MediaState state;
	ObsCore core;
	auto manager = MediaCacheManagerTestAccess::create(300);
	auto source = makeCacheTestSource("small.webm", state);
	manager->registerSource(source);
	std::optional<MediaCacheManager::SourceSettingsUpdate> update(manager->trackSourceSettingsUpdate(source));
	setFile(*manager, source, "large.webm");
	runFrame(*manager, {source});
	CHECK(state.queries == 0);
	if (stop)
		manager->shutdown();
	else {
		manager->unregisterSource(source);
		REQUIRE(MediaCacheManagerTestAccess::waitForQueuesToDrain(*manager));
	}
	source = nullptr;
	obs_wait_for_destroy_queue();
	CHECK(state.destroys == 0);
	update.reset();
	manager->shutdown(); // Join before checking the worker's final reference release.
	obs_wait_for_destroy_queue();
	CHECK(state.destroys == 1);
	CHECK(MediaCacheManagerTestAccess::queuedGraphicsJobCount(*manager) == 0);
	CHECK(MediaCacheManagerTestAccess::reservedBytes(*manager) == 0);
}

TEST_CASE("Media cache settings guards keep the original registration identity", "[media-cache][settings]")
{
	MediaState state;
	ObsCore core;
	auto manager = MediaCacheManagerTestAccess::create(300);
	auto source = makeCacheTestSource("small.webm", state);
	manager->registerSource(source);
	std::optional<MediaCacheManager::SourceSettingsUpdate> update(manager->trackSourceSettingsUpdate(source));
	manager->unregisterSource(source);
	manager->registerSource(source); // Same OBS pointer, different cache entry.
	update.reset();
	for (int i = 0; i < 4; ++i)
		runFrame(*manager, {source});
	REQUIRE(MediaCacheManagerTestAccess::waitForQueuesToDrain(*manager));
	CHECK(state.queries == 1);
	CHECK(isCachingEnabled(source));
	CHECK(MediaCacheManagerTestAccess::reservedBytes(*manager) == 150);
}

TEST_CASE("OSN source settings entry points defer media cache queries", "[media-cache][settings]")
{
	bool useProperties = false;
	SECTION("Source Update") {}
	SECTION("Source GetProperties")
	{
		useProperties = true;
	}

	MediaState state;
	state.ready = false;
	ObsCore core;
	auto source = makeCacheTestSource("small.webm", state);
	RegisteredApiSource registration(source);
	auto &manager = MediaCacheManager::GetInstance();
	runFrame(manager, {source});
	REQUIRE(state.queries == 1);
	state.ready = true;
	std::vector<ipc::value> response;
	if (useProperties) {
		// Ensure old work can run inside the property callback if invalidation
		// is moved after obs_source_properties().
		manager.requestCacheUpdate(source);
		REQUIRE(MediaCacheManagerTestAccess::waitForQueuedGraphicsJob(manager));
		bool called = false;
		state.onProperties = [&](obs_data_t *settings) {
			called = true;
			obs_data_set_string(settings, "local_file", "large.webm");
			// A property callback can expose new settings before GetProperties
			// calls obs_source_update(). The guard must already be active here.
			for (int i = 0; i < 3; ++i)
				runFrame(manager, {source});
			CHECK(state.queries == 1);
			CHECK(state.appliedWidth == 10);
		};
		osn::Source::GetProperties(nullptr, 0, {ipc::value(registration.id)}, response);
		CHECK(called);
		state.onProperties = {};
	} else {
		osn::Source::Update(nullptr, 0, {ipc::value(registration.id), ipc::value(R"({"local_file":"large.webm"})")}, response);
	}
	REQUIRE(!response.empty());
	REQUIRE(static_cast<ErrorCode>(response[0].value_union.ui64) == ErrorCode::Ok);
	CHECK(state.appliedWidth == 10);
	runFrame(manager, {source});
	CHECK(state.queries == 1);
	CHECK(state.appliedWidth == 100);
	for (int i = 0; i < 4; ++i)
		runFrame(manager, {source});
	REQUIRE(MediaCacheManagerTestAccess::waitForQueuesToDrain(manager));
	CHECK(state.queries == 2);
	CHECK(isCachingEnabled(source));
	CHECK(MediaCacheManagerTestAccess::reservedBytes(manager) == 1500);
	// The manager's own caching write must settle, without invalidating itself.
	for (int i = 0; i < 3; ++i)
		runFrame(manager, {source});
	CHECK(state.queries == 2);
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

	// Replace an uncached player's file with no graphics thread running. The
	// pending query must survive reset and wait for the new player's metadata.
	const auto queriesBeforeReset = state.queries.load();
	{
		auto update = manager->trackSourceSettingsUpdate(source);
		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_string(settings, "local_file", "large.webm");
		obs_data_set_bool(settings, "looping", true);
		obs_source_update(source, settings);
	}
	REQUIRE(MediaCacheManagerTestAccess::waitForQueuedGraphicsJob(*manager));
	REQUIRE(obs_reset_video(&info) == OBS_VIDEO_SUCCESS);
	const auto resetDeadline = std::chrono::steady_clock::now() + 3s;
	while (state.queries == queriesBeforeReset && std::chrono::steady_clock::now() < resetDeadline)
		std::this_thread::sleep_for(5ms);
	REQUIRE(state.queries > queriesBeforeReset);
	REQUIRE(MediaCacheManagerTestAccess::waitForQueuesToDrain(*manager));
	CHECK(state.appliedWidth == 100);
	CHECK_FALSE(isCachingEnabled(source));
	CHECK(MediaCacheManagerTestAccess::reservedBytes(*manager) == 0);
	CHECK(state.graphicsOnly);
	REQUIRE(obs_remove_video_info(obs_get_video_info_by_index2(0)) == OBS_VIDEO_SUCCESS);
	manager->requestCacheUpdate(source);
	REQUIRE(MediaCacheManagerTestAccess::waitForQueuedGraphicsJob(*manager));
	manager->shutdown();
}
