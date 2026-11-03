#include <catch2/catch_test_macros.hpp>

#include <obs.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "nodeobs_content.h"
#include "obs-setup.hpp"
#include "osn-error.hpp"
#include "osn-video.hpp"

extern std::map<std::string, OBS::Display *> displays;
extern std::recursive_mutex displaysMutex;

namespace {

using namespace std::chrono_literals;

constexpr char TEST_SOURCE_ID[] = "display_canvas_lifecycle_source";
constexpr char TEST_SOURCE_NAME[] = "display canvas lifecycle source";
constexpr char TEST_DISPLAY_KEY[] = "display canvas lifecycle preview";

class ParentWindow {
public:
	ParentWindow()
	{
		handle = CreateWindowExW(0, L"STATIC", L"OBS display lifecycle test", WS_OVERLAPPEDWINDOW, 0, 0, 320, 180, nullptr, nullptr,
					 GetModuleHandleW(nullptr), nullptr);
		if (handle)
			ShowWindow(handle, SW_SHOWNOACTIVATE);
	}

	~ParentWindow()
	{
		if (handle)
			DestroyWindow(handle);
	}

	HWND get() const { return handle; }

private:
	HWND handle = nullptr;
};

struct RenderGate {
	std::mutex mutex;
	std::condition_variable condition;
	bool entered = false;
	bool released = false;
	std::atomic<bool> firstRender{true};

	~RenderGate() { release(); }

	void render()
	{
		if (!firstRender.exchange(false))
			return;
		std::unique_lock lock(mutex);
		entered = true;
		condition.notify_all();
		condition.wait_for(lock, 10s, [this] { return released; });
	}

	bool waitForRender()
	{
		std::unique_lock lock(mutex);
		return condition.wait_for(lock, 5s, [this] { return entered; });
	}

	void release()
	{
		{
			std::lock_guard lock(mutex);
			released = true;
		}
		condition.notify_all();
	}
};

RenderGate *activeRenderGate = nullptr;

struct GateBinding {
	explicit GateBinding(RenderGate &gate) : gate(gate) { activeRenderGate = &gate; }
	~GateBinding()
	{
		gate.release();
		activeRenderGate = nullptr;
	}
	RenderGate &gate;
};

struct SourceOwner {
	obs_source_t *source = nullptr;
	~SourceOwner()
	{
		if (source) {
			obs_source_remove(source);
			obs_source_release(source);
		}
	}
};

struct SceneOwner {
	obs_scene_t *scene = nullptr;
	obs_sceneitem_t *item = nullptr;
	~SceneOwner() { clear(); }

	void clear()
	{
		if (item) {
			obs_sceneitem_set_canvas(item, nullptr);
			obs_sceneitem_remove(item);
			item = nullptr;
		}
		if (scene) {
			obs_scene_release(scene);
			scene = nullptr;
		}
	}
};

bool sizeAndShowDisplay(const std::string &key)
{
	std::scoped_lock lock(displaysMutex);
	auto found = displays.find(key);
	if (found == displays.end())
		return false;
	auto *display = found->second;
	display->m_gsInitData.cx = 320;
	display->m_gsInitData.cy = 180;
	auto child = static_cast<HWND>(display->m_gsInitData.window.hwnd);
	if (!SetWindowPos(child, nullptr, 0, 0, 320, 180, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW))
		return false;
	obs_display_resize(display->m_display, 320, 180);
	display->UpdatePreviewArea();
	return true;
}

const char *sourceName(void *)
{
	return "Display canvas lifecycle source";
}

void *createSource(obs_data_t *, obs_source_t *)
{
	return activeRenderGate;
}

void renderSource(void *data, gs_effect_t *)
{
	static_cast<RenderGate *>(data)->render();
}

obs_source_info testSourceInfo()
{
	obs_source_info info{};
	info.id = TEST_SOURCE_ID;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = sourceName;
	info.create = createSource;
	info.destroy = [](void *) {};
	info.get_width = [](void *) { return 320u; };
	info.get_height = [](void *) { return 180u; };
	info.video_render = renderSource;
	return info;
}

using Handler = void (*)(void *, const int64_t, const std::vector<ipc::value> &, std::vector<ipc::value> &);

std::vector<ipc::value> call(Handler handler, std::vector<ipc::value> args)
{
	std::vector<ipc::value> response;
	handler(nullptr, 0, args, response);
	return response;
}

ErrorCode status(const std::vector<ipc::value> &response)
{
	return response.empty() ? ErrorCode::Error : static_cast<ErrorCode>(response[0].value_union.ui64);
}

struct CanvasOwner {
	std::optional<uint64_t> id;
	~CanvasOwner()
	{
		if (!id)
			return;
		OBS_content::OBS_content_detachDisplays();
		const auto removed = call(osn::Video::RemoveVideoContext, {ipc::value(*id)});
		if (status(removed) != ErrorCode::Ok && osn::Video::Manager::GetInstance().find(*id))
			osn::Video::Manager::GetInstance().free(*id);
	}
};

} // namespace

TEST_CASE("Display detachment drains rendering before removing its canvas", "[display][canvas][shutdown]")
{
	// The parent must outlive the OBS API, which destroys the child display on shutdown.
	ParentWindow parent;
	REQUIRE(parent.get() != nullptr);
	RenderGate gate;
	osn::tests::ObsSetup setup;
	CanvasOwner canvasOwner;
	SourceOwner sourceOwner;
	GateBinding binding(gate);

	const auto added = call(osn::Video::AddVideoContext, {});
	REQUIRE(status(added) == ErrorCode::Ok);
	REQUIRE(added.size() >= 2);
	const uint64_t canvasId = added[1].value_union.ui64;
	canvasOwner.id = canvasId;
	obs_video_info *canvas = osn::Video::Manager::GetInstance().find(canvasId);
	REQUIRE(canvas != nullptr);

	const auto configured =
		call(osn::Video::SetVideoContext,
		     {ipc::value(30u), ipc::value(1u), ipc::value(320u), ipc::value(180u), ipc::value(320u), ipc::value(180u), ipc::value(0u), ipc::value(0u),
		      ipc::value(0u), ipc::value(static_cast<uint32_t>(OBS_SCALE_BILINEAR)), ipc::value(0u), ipc::value(canvasId)});
	REQUIRE(status(configured) == ErrorCode::Ok);

	std::cerr << "Display lifecycle: registering source" << std::endl;
	const auto sourceInfo = testSourceInfo();
	obs_register_source(&sourceInfo);
	std::cerr << "Display lifecycle: creating source" << std::endl;
	obs_source_t *source = sourceOwner.source = obs_source_create(TEST_SOURCE_ID, TEST_SOURCE_NAME, nullptr, nullptr);
	REQUIRE(source != nullptr);

	std::cerr << "Display lifecycle: creating preview" << std::endl;
	const auto created = call(OBS_content::OBS_content_createSourcePreviewDisplay,
				  {ipc::value(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(parent.get()))), ipc::value(TEST_SOURCE_NAME),
				   ipc::value(TEST_DISPLAY_KEY), ipc::value(0u), ipc::value(canvasId)});
	REQUIRE(status(created) == ErrorCode::Ok);
	std::cerr << "Display lifecycle: sizing preview" << std::endl;
	REQUIRE(sizeAndShowDisplay(TEST_DISPLAY_KEY));

	// A display holding the canvas rejects removal. The later retry also checks
	// that this rejection leaves the removal gate open.
	const auto displayRejection = call(osn::Video::RemoveVideoContext, {ipc::value(canvasId)});
	REQUIRE(status(displayRejection) == ErrorCode::Error);
	REQUIRE(displayRejection.size() >= 2);
	CHECK(displayRejection[1].value_str == "Cannot remove video context while a display is using it.");
	std::cerr << "Display lifecycle: waiting for render" << std::endl;
	REQUIRE(gate.waitForRender());

	std::cerr << "Display lifecycle: detaching preview" << std::endl;
	auto detaching = std::async(std::launch::async, [] { return call(OBS_content::OBS_content_detachDisplaysIPC, {}); });
	bool shutdownStarted = false;
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (std::chrono::steady_clock::now() < deadline) {
		const auto preview = call(OBS_content::OBS_content_getDisplayPreviewSize, {ipc::value(TEST_DISPLAY_KEY)});
		if (status(preview) == ErrorCode::Error && preview.size() >= 2 && preview[1].value_str == "Display is shutting down.") {
			shutdownStarted = true;
			break;
		}
		std::this_thread::sleep_for(2ms);
	}
	const bool waitedForRender = shutdownStarted && detaching.wait_for(150ms) == std::future_status::timeout;
	gate.release();
	CHECK(shutdownStarted);
	CHECK(waitedForRender);
	// The worker must finish before display and OBS cleanup. A true deadlock is
	// caught by the CTest timeout rather than abandoning a live draw callback.
	CHECK(status(detaching.get()) == ErrorCode::Ok);

	// A scene item reaches libobs's OBS_VIDEO_INFO_IN_USE failure after the
	// server has reserved removal. Clearing it must permit a second attempt.
	SceneOwner sceneOwner;
	sceneOwner.scene = obs_scene_create_private("display canvas lifecycle scene");
	REQUIRE(sceneOwner.scene != nullptr);
	sceneOwner.item = obs_scene_add(sceneOwner.scene, source);
	REQUIRE(sceneOwner.item != nullptr);
	obs_sceneitem_set_canvas(sceneOwner.item, canvas);
	const auto itemRejection = call(osn::Video::RemoveVideoContext, {ipc::value(canvasId)});
	REQUIRE(status(itemRejection) == ErrorCode::Error);
	REQUIRE(itemRejection.size() >= 2);
	CHECK(itemRejection[1].value_str == "Cannot remove video context while scene items are assigned to it.");
	sceneOwner.clear();
	REQUIRE(status(call(osn::Video::RemoveVideoContext, {ipc::value(canvasId)})) == ErrorCode::Ok);
	canvasOwner.id.reset();
	CHECK(osn::Video::Manager::GetInstance().find(canvasId) == nullptr);
}
