#include <catch2/catch_test_macros.hpp>
#include "nodeobs_api.h"
#include "osn-error.hpp"
#include "osn-filter.hpp"
#include "osn-input.hpp"
#include "osn-scene.hpp"
#include "osn-source.hpp"
#include <obs.h>
#include "shared.hpp"
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace {

// Keep the test source independent of OBS plugins so its behavior depends only
// on the source manager and libobs reference lifecycle.
constexpr char TEST_SOURCE_ID[] = "source_manager_lifetime_test_source";

const char *testSourceGetName(void *)
{
	return "Source Manager Lifetime Test Source";
}

void *testSourceCreate(obs_data_t *, obs_source_t *source)
{
	return source;
}

void testSourceDestroy(void *) {}

obs_properties_t *testSourceGetProperties(void *)
{
	obs_properties_t *properties = obs_properties_create();
	obs_properties_add_bool(properties, "enabled", "Enabled");
	return properties;
}

obs_source_info makeTestSourceInfo()
{
	obs_source_info info{};
	info.id = TEST_SOURCE_ID;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.get_name = testSourceGetName;
	info.create = testSourceCreate;
	info.destroy = testSourceDestroy;
	info.get_properties = testSourceGetProperties;
	return info;
}

// A video filter can only be attached to a source that also outputs video.
constexpr char TEST_VIDEO_SOURCE_ID[] = "filter_release_test_video_source";
constexpr char TEST_VIDEO_FILTER_ID[] = "filter_release_test_video_filter";

obs_source_info makeTestVideoSourceInfo()
{
	obs_source_info info = makeTestSourceInfo();
	info.id = TEST_VIDEO_SOURCE_ID;
	info.output_flags = OBS_SOURCE_VIDEO;
	return info;
}

void *testFilterCreate(obs_data_t *settings, obs_source_t *source)
{
	obs_source_update(source, settings);
	return source;
}

void testFilterUpdate(void *, obs_data_t *) {}

obs_source_info makeTestVideoFilterInfo()
{
	obs_source_info info{};
	info.id = TEST_VIDEO_FILTER_ID;
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = testSourceGetName;
	info.create = testFilterCreate;
	info.destroy = testSourceDestroy;
	info.update = testFilterUpdate;
	return info;
}

// These tests only need the OBS core and its deferred-destruction queue.
class ObsCoreSetup {
public:
	ObsCoreSetup() { REQUIRE(obs_startup("en-US", nullptr, nullptr)); }
	~ObsCoreSetup()
	{
		// Do not let a queued source destruction outlive the OBS core.
		obs_wait_for_destroy_queue();
		obs_shutdown();
	}

	ObsCoreSetup(const ObsCoreSetup &) = delete;
	ObsCoreSetup &operator=(const ObsCoreSetup &) = delete;
};

// Source destruction runs on OBS_TASK_DESTROY. Blocking that queue creates the
// precise window where the last strong reference is gone but the manager's
// destroy callback has not removed the source registration yet.
class DestroyQueueGate {
public:
	DestroyQueueGate()
	{
		obs_queue_task(OBS_TASK_DESTROY, waitForRelease, this, false);
		// Wait until the gate is running so later destruction is guaranteed to
		// queue behind it.
		std::unique_lock lock(mutex);
		condition.wait(lock, [this] { return entered; });
	}

	~DestroyQueueGate() { releaseAndWait(); }

	void releaseAndWait()
	{
		{
			std::lock_guard lock(mutex);
			released = true;
		}
		condition.notify_all();

		if (!drained) {
			// Draining also runs the source's destroy signal, which unregisters
			// it from Source::Manager.
			obs_wait_for_destroy_queue();
			drained = true;
		}
	}

	DestroyQueueGate(const DestroyQueueGate &) = delete;
	DestroyQueueGate &operator=(const DestroyQueueGate &) = delete;

private:
	static void waitForRelease(void *data)
	{
		auto *gate = static_cast<DestroyQueueGate *>(data);
		std::unique_lock lock(gate->mutex);
		gate->entered = true;
		gate->condition.notify_all();
		gate->condition.wait(lock, [gate] { return gate->released; });
	}

	std::mutex mutex;
	std::condition_variable condition;
	bool entered = false;
	bool released = false;
	bool drained = false;
};

// The production manager deliberately hides its storage. This test-only
// subclass exposes one narrow operation so a stale raw-pointer entry can be
// made to look as if its address were reused by a different OBS source.
class AddressReuseSourceManager : public osn::Source::Manager {
public:
	void simulateAddressReuse(uint64_t sourceId, obs_source_t *replacement)
	{
		std::lock_guard<std::recursive_mutex> lock(internal_mutex);
		object_map.at(sourceId) = replacement;
	}
};

} // namespace

TEST_CASE("Scene AddSource rejects malformed argument counts")
{
	for (const std::size_t argumentCount : {std::size_t{0}, std::size_t{1}, std::size_t{3}, std::size_t{19}, std::size_t{21}}) {
		std::vector<ipc::value> args;
		for (std::size_t i = 0; i < argumentCount; i++)
			args.emplace_back(uint64_t{0});

		std::vector<ipc::value> response;
		osn::Scene::AddSource(nullptr, 0, args, response);

		REQUIRE(response.size() >= 2);
		CHECK((ErrorCode)response[0].value_union.ui64 == ErrorCode::Error);
		CHECK(response[1].value_str == "Invalid number of arguments to add a source to a scene.");
	}
}

TEST_CASE("Private scene registrations are removed after release")
{
	ObsCoreSetup setupOBS;
	auto &manager = osn::Source::Manager::GetInstance();
	const auto sourceCount = manager.size();

	auto createPrivateScene = [](const char *name) {
		std::vector<ipc::value> response;
		osn::Scene::CreatePrivate(nullptr, 0, {ipc::value(name)}, response);

		REQUIRE(response.size() >= 2);
		REQUIRE((ErrorCode)response[0].value_union.ui64 == ErrorCode::Ok);
		return response[1].value_union.ui64;
	};
	auto checkProperties = [](uint64_t sourceId) {
		std::vector<ipc::value> response;
		osn::Source::GetProperties(nullptr, 0, {ipc::value(sourceId)}, response);

		REQUIRE(!response.empty());
		CHECK((ErrorCode)response[0].value_union.ui64 == ErrorCode::Ok);
	};
	auto releasePrivateScene = [](uint64_t sourceId) {
		std::vector<ipc::value> response;
		osn::Scene::Release(nullptr, 0, {ipc::value(sourceId)}, response);

		REQUIRE(!response.empty());
		CHECK((ErrorCode)response[0].value_union.ui64 == ErrorCode::Ok);
		// Source destruction may run on OBS_TASK_DESTROY. Wait for its destroy
		// signal to remove the manager registration before inspecting the map.
		obs_wait_for_destroy_queue();
	};

	// Exercise the same handlers used by SceneFactory.createPrivate and the
	// scene properties accessor. Private sources skip OBS's global source-create
	// signal, so CreatePrivate itself must attach the destroy callback.
	const uint64_t firstSceneId = createPrivateScene("first private scene");
	CHECK(manager.size() == sourceCount + 1);
	checkProperties(firstSceneId);
	releasePrivateScene(firstSceneId);
	CHECK(manager.size() == sourceCount);

	// Recreating the scene verifies that no stale registration can capture the
	// new source if OBS's allocator gives it a recently released address.
	const uint64_t secondSceneId = createPrivateScene("second private scene");
	CHECK(manager.size() == sourceCount + 1);
	checkProperties(secondSceneId);
	releasePrivateScene(secondSceneId);
	CHECK(manager.size() == sourceCount);
}

TEST_CASE("Source manager rejects an expired identity after address reuse")
{
	ObsCoreSetup setupOBS;
	static const obs_source_info testSourceInfo = makeTestSourceInfo();
	obs_register_source(&testSourceInfo);
	AddressReuseSourceManager manager;

	OBSSourceAutoRelease firstSource = obs_source_create_private(TEST_SOURCE_ID, "first source", nullptr);
	REQUIRE(firstSource != nullptr);
	const uint64_t firstSourceId = manager.allocate(firstSource);
	REQUIRE(firstSourceId != UINT64_MAX);

	// Leave the raw pointer and weak reference registered while destroying the
	// source, matching the stale state that existed without a destroy callback.
	firstSource = nullptr;
	obs_wait_for_destroy_queue();
	CHECK(!manager.findAndRef(firstSourceId));

	OBSSourceAutoRelease secondSource = obs_source_create_private(TEST_SOURCE_ID, "second source", nullptr);
	REQUIRE(secondSource != nullptr);
	// ASan and different allocators do not reliably reuse an address on demand.
	// Rewrite only the stored raw pointer to model that reuse deterministically;
	// the entry's weak control block still belongs to the expired first source.
	manager.simulateAddressReuse(firstSourceId, secondSource);

	const uint64_t secondSourceId = manager.allocate(secondSource);
	REQUIRE(secondSourceId != UINT64_MAX);
	CHECK(secondSourceId != firstSourceId);
	CHECK(manager.size() == 1);

	// allocate() must replace the stale identity with the second source's weak
	// reference, so production lookups retain and return the live source.
	OBSSourceAutoRelease retainedSource = manager.findAndRef(secondSourceId);
	REQUIRE(retainedSource != nullptr);
	CHECK(retainedSource.Get() == secondSource.Get());
}

TEST_CASE("Source manager safely promotes references during deferred destruction")
{
	ObsCoreSetup setupOBS;
	static const obs_source_info testSourceInfo = makeTestSourceInfo();
	obs_register_source(&testSourceInfo);
	auto &manager = osn::Source::Manager::GetInstance();
	const auto sourceCount = manager.size();

	{
		INFO("The final source release wins the race");
		DestroyQueueGate destroyQueue;
		OBSSourceAutoRelease source = obs_source_create_private(TEST_SOURCE_ID, "released source", nullptr);
		REQUIRE(source != nullptr);

		// Private sources do not emit the global source-create signal, so mirror
		// production registration here. Repeated allocation must keep one ID and
		// one retained weak reference for the source.
		const uint64_t sourceId = manager.allocate(source);
		osn::Source::attach_source_signals(source);
		REQUIRE(sourceId != UINT64_MAX);
		CHECK(manager.allocate(source) == sourceId);
		CHECK(manager.size() == sourceCount + 1);

		// The gate keeps deferred destruction from unregistering the source.
		// Dropping the only strong reference therefore leaves an expired source
		// ID in the manager, reproducing the original race window.
		source = nullptr;

		CHECK(manager.size() == sourceCount + 1);
		std::vector<ipc::value> args = {ipc::value(sourceId)};
		std::vector<ipc::value> response;
		// Promotion of the retained weak reference must fail cleanly instead of
		// dereferencing the stale raw pointer stored by the old implementation.
		osn::Source::GetProperties(nullptr, 0, args, response);
		REQUIRE(!response.empty());
		CHECK((ErrorCode)response[0].value_union.ui64 == ErrorCode::InvalidReference);

		// Let OBS finish destruction and deliver the manager's destroy callback.
		destroyQueue.releaseAndWait();
		CHECK(manager.size() == sourceCount);
	}

	{
		INFO("The source lookup wins the race");
		DestroyQueueGate destroyQueue;
		OBSSourceAutoRelease source = obs_source_create_private(TEST_SOURCE_ID, "retained source", nullptr);
		REQUIRE(source != nullptr);

		const uint64_t sourceId = manager.allocate(source);
		osn::Source::attach_source_signals(source);
		REQUIRE(sourceId != UINT64_MAX);
		// This time lookup wins: promotion happens while the original strong
		// reference is still alive and must keep the source usable.
		OBSSourceAutoRelease retainedSource = manager.findAndRef(sourceId);
		REQUIRE(retainedSource != nullptr);

		source = nullptr;
		CHECK(std::string(obs_source_get_name(retainedSource)) == "retained source");
		OBSSourceAutoRelease secondReference = manager.findAndRef(sourceId);
		CHECK(secondReference != nullptr);

		// Releasing every promoted reference expires the source. Its registration
		// remains until the blocked destroy callback runs, but another promotion
		// must already report that the source is gone.
		secondReference = nullptr;
		retainedSource = nullptr;
		CHECK(manager.size() == sourceCount + 1);
		CHECK(!manager.findAndRef(sourceId));

		// Draining the queue completes destruction and removes the registration.
		destroyQueue.releaseAndWait();
		CHECK(manager.size() == sourceCount);
	}
}

TEST_CASE("Releasing a filter after attaching it keeps the filter active")
{
	ObsCoreSetup setupOBS;
	static const obs_source_info testVideoSourceInfo = makeTestVideoSourceInfo();
	static const obs_source_info testVideoFilterInfo = makeTestVideoFilterInfo();
	obs_register_source(&testVideoSourceInfo);
	obs_register_source(&testVideoFilterInfo);
	auto &manager = osn::Source::Manager::GetInstance();
	const auto sourceCount = manager.size();

	OBSSourceAutoRelease input = obs_source_create_private(TEST_VIDEO_SOURCE_ID, "filtered input", nullptr);
	REQUIRE(input != nullptr);
	const uint64_t inputId = manager.allocate(input);
	osn::Source::attach_source_signals(input);
	REQUIRE(inputId != UINT64_MAX);

	// Follow the sequence used by Streamlabs Desktop when it adds a filter.
	std::vector<ipc::value> response;
	osn::Filter::Create(nullptr, 0, {ipc::value(TEST_VIDEO_FILTER_ID), ipc::value("released filter")}, response);
	REQUIRE(response.size() >= 2);
	REQUIRE((ErrorCode)response[0].value_union.ui64 == ErrorCode::Ok);
	const uint64_t filterId = response[1].value_union.ui64;

	response.clear();
	osn::Input::AddFilter(nullptr, 0, {ipc::value(inputId), ipc::value(filterId)}, response);
	REQUIRE(!response.empty());
	REQUIRE((ErrorCode)response[0].value_union.ui64 == ErrorCode::Ok);

	response.clear();
	osn::Source::Release(nullptr, 0, {ipc::value(filterId)}, response);
	REQUIRE(!response.empty());
	CHECK((ErrorCode)response[0].value_union.ui64 == ErrorCode::Ok);

	{
		// The input now holds the only reference. The filter must not be marked
		// removed since libobs skips removed sources when ticking.
		OBSSourceAutoRelease filter = manager.findAndRef(filterId);
		REQUIRE(filter != nullptr);
		CHECK(!obs_source_removed(filter));
	}

	response.clear();
	osn::Input::RemoveFilter(nullptr, 0, {ipc::value(inputId), ipc::value(filterId)}, response);
	REQUIRE(!response.empty());
	CHECK((ErrorCode)response[0].value_union.ui64 == ErrorCode::Ok);
	obs_wait_for_destroy_queue();
	CHECK(!manager.findAndRef(filterId));
	CHECK(manager.size() == sourceCount + 1);

	input = nullptr;
	obs_wait_for_destroy_queue();
	CHECK(manager.size() == sourceCount);
}
