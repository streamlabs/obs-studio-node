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

#include <iomanip>
#include <map>
#include "nodeobs_content.h"

/* For sceneitem transform modifications.
 * We should consider moving this to another module */
#include <graphics/matrix4.h>

#include "osn-error.hpp"
#include "shared.hpp"
#include "osn-video.hpp"

#include <thread>

std::map<std::string, OBS::Display *> displays;
std::recursive_mutex displaysMutex;
std::string sourceSelected;
bool firstDisplayCreation = true;

std::thread *windowMessage = NULL;
ipc::server *g_srv;
bool displayCreated = false;

/* A lot of the sceneitem functionality is a lazy copy-pasta from the Qt UI. */
// https://github.com/jp9000/obs-studio/blob/master/UI/window-basic-main.cpp#L4888
static void GetItemBox(obs_sceneitem_t *item, vec3 &tl, vec3 &br)
{
	matrix4 boxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);

	vec3_set(&tl, M_INFINITE, M_INFINITE, 0.0f);
	vec3_set(&br, -M_INFINITE, -M_INFINITE, 0.0f);

	auto GetMinPos = [&](float x, float y) {
		vec3 pos;
		vec3_set(&pos, x, y, 0.0f);
		vec3_transform(&pos, &pos, &boxTransform);
		vec3_min(&tl, &tl, &pos);
		vec3_max(&br, &br, &pos);
	};

	GetMinPos(0.0f, 0.0f);
	GetMinPos(1.0f, 0.0f);
	GetMinPos(0.0f, 1.0f);
	GetMinPos(1.0f, 1.0f);
}

static vec3 GetItemTL(obs_sceneitem_t *item)
{
	vec3 tl, br;
	GetItemBox(item, tl, br);
	return tl;
}

static void SetItemTL(obs_sceneitem_t *item, const vec3 &tl)
{
	vec3 newTL;
	vec2 pos;

	obs_sceneitem_get_pos(item, &pos);
	newTL = GetItemTL(item);
	pos.x += tl.x - newTL.x;
	pos.y += tl.y - newTL.y;
	obs_sceneitem_set_pos(item, &pos);
}

static bool CenterAlignSelectedItems(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	obs_bounds_type boundsType = *reinterpret_cast<obs_bounds_type *>(param);

	if (!obs_sceneitem_selected(item))
		return true;

	obs_video_info ovi = {0};
	obs_get_video_info_scene_item(item, &ovi);

	obs_transform_info itemInfo;
	vec2_set(&itemInfo.pos, 0.0f, 0.0f);
	vec2_set(&itemInfo.scale, 1.0f, 1.0f);
	itemInfo.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
	itemInfo.rot = 0.0f;

	vec2_set(&itemInfo.bounds, float(ovi.base_width), float(ovi.base_height));
	itemInfo.bounds_type = boundsType;
	itemInfo.bounds_alignment = OBS_ALIGN_CENTER;

	obs_sceneitem_set_info2(item, &itemInfo);

	UNUSED_PARAMETER(scene);
	return true;
}

static bool MultiplySelectedItemScale(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	vec2 &mul = *reinterpret_cast<vec2 *>(param);

	if (!obs_sceneitem_selected(item))
		return true;

	vec3 tl = GetItemTL(item);

	vec2 scale;
	obs_sceneitem_get_scale(item, &scale);
	vec2_mul(&scale, &scale, &mul);
	obs_sceneitem_set_scale(item, &scale);

	SetItemTL(item, tl);

	UNUSED_PARAMETER(scene);
	return true;
}

#ifdef _WIN32

static void OnDeviceLost(void *data)
{
	// Do nothing
	UNUSED_PARAMETER(data);
}

static void OnDeviceRebuilt(void *device, void *data)
{
	std::scoped_lock lock(displaysMutex);

	for (const auto &p : displays) {
		if (auto display = p.second) {
			// After device is rebuilt, there are can be problems with incorrect size of display
			// In order to fix this, need to update the display by adjusting it's size
			// TODO: find a better way to fix this
			const auto size = display->GetSize();
			display->SetSize(size.first, size.second);
		}
	}
}
#endif

void OBS_content::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("Display");

	cls->register_function(std::make_shared<ipc::function>("OBS_content_setDayTheme", std::vector<ipc::type>{ipc::type::UInt32}, OBS_content_setDayTheme));

	cls->register_function(std::make_shared<ipc::function>("OBS_content_createDisplay",
							       std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String, ipc::type::Int32, ipc::type::Int32,
										      ipc::type::UInt64},
							       OBS_content_createDisplay));

	cls->register_function(
		std::make_shared<ipc::function>("OBS_content_destroyDisplay", std::vector<ipc::type>{ipc::type::String}, OBS_content_destroyDisplay));

	cls->register_function(std::make_shared<ipc::function>("OBS_content_getDisplayPreviewOffset", std::vector<ipc::type>{ipc::type::String},
							       OBS_content_getDisplayPreviewOffset));

	cls->register_function(std::make_shared<ipc::function>("OBS_content_getDisplayPreviewSize", std::vector<ipc::type>{ipc::type::String},
							       OBS_content_getDisplayPreviewSize));

	cls->register_function(std::make_shared<ipc::function>("OBS_content_createSourcePreviewDisplay",
							       std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String, ipc::type::String,
										      ipc::type::UInt32, ipc::type::UInt64},
							       OBS_content_createSourcePreviewDisplay));

	cls->register_function(std::make_shared<ipc::function>(
		"OBS_content_resizeDisplay", std::vector<ipc::type>{ipc::type::String, ipc::type::UInt32, ipc::type::UInt32}, OBS_content_resizeDisplay));

	cls->register_function(std::make_shared<ipc::function>(
		"OBS_content_moveDisplay", std::vector<ipc::type>{ipc::type::String, ipc::type::UInt32, ipc::type::UInt32}, OBS_content_moveDisplay));

	cls->register_function(std::make_shared<ipc::function>("OBS_content_setPaddingSize", std::vector<ipc::type>{ipc::type::String, ipc::type::UInt32},
							       OBS_content_setPaddingSize));

	cls->register_function(std::make_shared<ipc::function>("OBS_content_setPaddingColor",
							       std::vector<ipc::type>{ipc::type::String, ipc::type::UInt32, ipc::type::UInt32,
										      ipc::type::UInt32, ipc::type::UInt32},
							       OBS_content_setPaddingColor));

	cls->register_function(std::make_shared<ipc::function>("OBS_content_setBackgroundColor",
							       std::vector<ipc::type>{ipc::type::String, ipc::type::UInt32, ipc::type::UInt32,
										      ipc::type::UInt32, ipc::type::UInt32},
							       OBS_content_setBackgroundColor));

	cls->register_function(std::make_shared<ipc::function>("OBS_content_setOutlineColor",
							       std::vector<ipc::type>{ipc::type::String, ipc::type::UInt32, ipc::type::UInt32,
										      ipc::type::UInt32, ipc::type::UInt32},
							       OBS_content_setOutlineColor));

	cls->register_function(std::make_shared<ipc::function>("OBS_content_setCropOutlineColor",
							       std::vector<ipc::type>{ipc::type::String, ipc::type::UInt32, ipc::type::UInt32,
										      ipc::type::UInt32, ipc::type::UInt32},
							       OBS_content_setCropOutlineColor));

	cls->register_function(std::make_shared<ipc::function>("OBS_content_setShouldDrawUI", std::vector<ipc::type>{ipc::type::String, ipc::type::Int32},
							       OBS_content_setShouldDrawUI));

	cls->register_function(std::make_shared<ipc::function>("OBS_content_setDrawGuideLines", std::vector<ipc::type>{ipc::type::String, ipc::type::Int32},
							       OBS_content_setDrawGuideLines));

	cls->register_function(std::make_shared<ipc::function>("OBS_content_setDrawRotationHandle", std::vector<ipc::type>{ipc::type::String, ipc::type::Int32},
							       OBS_content_setDrawRotationHandle));

	cls->register_function(
		std::make_shared<ipc::function>("OBS_content_createIOSurface", std::vector<ipc::type>{ipc::type::String}, OBS_content_createIOSurface));

	srv.register_collection(cls);
	g_srv = &srv;
}

void popupAeroDisabledWindow(void)
{
#ifdef WIN32
	MessageBox(NULL,
		   TEXT("Streamlabs Desktop needs Aero enabled to run properly on Windows 7.  "
			"If you've disabled Aero for performance reasons, "
			"you may still use the app, but you will need to keep the window maximized.\n\n\n\n\n"
			"This is a workaround to keep Streamlabs Desktop running and not the preferred route. "
			"We recommend upgrading to Windows 10 or enabling Aero."),
		   TEXT("Aero is disabled"), MB_OK);
#endif
}

void OBS_content::OBS_content_setDayTheme(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	OBS::Display::SetDayTheme((bool)args[0].value_union.ui32);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_content::OBS_content_createDisplay(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	std::scoped_lock lock(displaysMutex);

	uint64_t windowHandle = args[0].value_union.ui64;
	auto found = displays.find(args[1].value_str);

	/* If found, do nothing since it would
	be a memory leak otherwise. */
	if (found != displays.end()) {
		std::cerr << "Duplicate key provided to createDisplay: " << args[1].value_str << std::endl;
		rval.push_back(ipc::value((uint64_t)ErrorCode::Error));
		rval.push_back(ipc::value("Duplicate key provided to createDisplay: " + args[1].value_str));
		return;
	}

	enum obs_video_rendering_mode mode = OBS_MAIN_VIDEO_RENDERING;
	switch (args[2].value_union.i32) {
	case 0:
		mode = OBS_MAIN_VIDEO_RENDERING;
		break;
	case 1:
		mode = OBS_STREAMING_VIDEO_RENDERING;
		break;
	case 2:
		mode = OBS_RECORDING_VIDEO_RENDERING;
		break;
	}

	obs_video_info *canvas = osn::Video::Manager::GetInstance().find(args[4].value_union.ui64);
	try {
#ifdef WIN32
		displays.insert_or_assign(args[1].value_str, new OBS::Display(windowHandle, mode, args[3].value_union.ui32, canvas));
		if (!IsWindows8OrGreater()) {
			BOOL enabled = FALSE;
			DwmIsCompositionEnabled(&enabled);
			if (!enabled && firstDisplayCreation) {
				windowMessage = new std::thread(popupAeroDisabledWindow);
			}
		}
#else
		OBS::Display *display = new OBS::Display(windowHandle, mode, args[3].value_union.i32, canvas);
		displays.insert_or_assign(args[1].value_str, display);
#endif

		// device rebuild functionality available only with D3D
#ifdef _WIN32
		if (firstDisplayCreation) {
			obs_enter_graphics();

			gs_device_loss callbacks;
			callbacks.device_loss_release = &OnDeviceLost;
			callbacks.device_loss_rebuild = &OnDeviceRebuilt;
			callbacks.data = nullptr;

			gs_register_loss_callbacks(&callbacks);
			obs_leave_graphics();
		}
#endif
	} catch (const std::exception &e) {
		std::string message(std::string("Display creation failed: ") + e.what());
		std::cerr << message << std::endl;
		blog(LOG_ERROR, "%s", message.data());
	}

	firstDisplayCreation = false;
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_content::OBS_content_destroyDisplay(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	std::scoped_lock lock(displaysMutex);

	auto found = displays.find(args[0].value_str);

	if (found == displays.end()) {
		std::cerr << "Failed to find key for destruction: " << args[0].value_str << std::endl;
		rval.push_back(ipc::value((uint64_t)ErrorCode::Error));
		rval.push_back(ipc::value("Failed to find key for destruction: " + args[0].value_str));
		return;
	}

	if (windowMessage != NULL && windowMessage->joinable())
		windowMessage->join();

	delete found->second;
	displays.erase(found);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_content::OBS_content_shutdownDisplays()
{
	std::scoped_lock lock(displaysMutex);

	blog(LOG_DEBUG, "Displays remaining till shutdown %d", displays.size());
	while (displays.size() > 0) {
		auto itr = displays.begin();
		delete itr->second;
		displays.erase(itr);
	}
}

void OBS_content::OBS_content_createSourcePreviewDisplay(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	std::scoped_lock lock(displaysMutex);

	uint64_t windowHandle = args[0].value_union.ui64;

	auto found = displays.find(args[2].value_str);

	/* If found, do nothing since it would
	be a memory leak otherwise. */
	if (found != displays.end()) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Error));
		rval.push_back(ipc::value("Duplicate key provided to createDisplay!"));
		return;
	}

	obs_video_info *canvas = osn::Video::Manager::GetInstance().find(args[4].value_union.ui64);
	try {
		OBS::Display *display = new OBS::Display(windowHandle, OBS_MAIN_VIDEO_RENDERING, args[1].value_str, args[3].value_union.ui32, canvas);
		displays.insert_or_assign(args[2].value_str, display);
	} catch (const std::exception &e) {
		std::string message(std::string("Source preview display creation failed: ") + e.what());
		std::cerr << message << std::endl;
		blog(LOG_ERROR, "%s", message.data());
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_content::OBS_content_resizeDisplay(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	auto value = displays.find(args[0].value_str);
	if (value == displays.end()) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Error));
		rval.push_back(ipc::value("Invalid key provided to resizeDisplay: " + args[0].value_str));
		return;
	}

	OBS::Display *display = value->second;

	blog(LOG_INFO, ">> OBS_content::OBS_content_resizeDisplay() width: %u; height: %u", args[1].value_union.ui32, args[2].value_union.ui32);

	display->m_gsInitData.cx = args[1].value_union.ui32;
	display->m_gsInitData.cy = args[2].value_union.ui32;

#ifdef WIN32
	display->SetSize(display->m_gsInitData.cx, display->m_gsInitData.cy);
#else
	// Resize Display
	obs_display_resize(display->m_display, display->m_gsInitData.cx, display->m_gsInitData.cy);

	// Store new size.
	display->UpdatePreviewArea();
#endif
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_content::OBS_content_moveDisplay(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	auto value = displays.find(args[0].value_str);
	if (value == displays.end()) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Error));
		rval.push_back(ipc::value("Invalid key provided to moveDisplay: " + args[0].value_str));
		return;
	}

	OBS::Display *display = value->second;

	int x = args[1].value_union.ui32;
	int y = args[2].value_union.ui32;

	display->m_position.first = x;
	display->m_position.second = y;

	display->SetPosition(x, y);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_content::OBS_content_setPaddingSize(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Find Display
	auto it = displays.find(args[0].value_str);
	if (it == displays.end()) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Error));
		rval.push_back(ipc::value("Display key is not valid!"));
		return;
	}

	it->second->SetPaddingSize(args[1].value_union.ui32);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
	return;
}

void OBS_content::OBS_content_setPaddingColor(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	union {
		uint32_t rgba;
		uint8_t c[4];
	} color;

	// Assign Color
	color.c[0] = (uint8_t)(args[1].value_union.ui32);
	color.c[1] = (uint8_t)(args[2].value_union.ui32);
	color.c[2] = (uint8_t)(args[3].value_union.ui32);
	if (args[4].value_union.ui32 != NULL)
		color.c[3] = (uint8_t)(args[4].value_union.ui32 * 255.0);

	else
		color.c[3] = 255;

	// Find Display
	auto it = displays.find(args[0].value_str);
	if (it == displays.end()) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Error));
		rval.push_back(ipc::value("Display key is not valid!"));
		return;
	}

	it->second->SetPaddingColor(color.c[0], color.c[1], color.c[2], color.c[3]);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
	return;
}

void OBS_content::OBS_content_setBackgroundColor(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	union {
		uint32_t rgba;
		uint8_t c[4];
	} color;

	// Assign Color
	color.c[0] = (uint8_t)(args[1].value_union.ui32);
	color.c[1] = (uint8_t)(args[2].value_union.ui32);
	color.c[2] = (uint8_t)(args[3].value_union.ui32);
	if (args[4].value_union.ui32 != NULL)
		color.c[3] = (uint8_t)(args[4].value_union.ui32 * 255.0);

	else
		color.c[3] = 255;

	// Find Display
	auto it = displays.find(args[0].value_str);
	if (it == displays.end()) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Error));
		rval.push_back(ipc::value("Display key is not valid!"));
		return;
	}

	it->second->SetBackgroundColor(color.c[0], color.c[1], color.c[2], color.c[3]);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
	return;
}

void OBS_content::OBS_content_setOutlineColor(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	union {
		uint32_t rgba;
		uint8_t c[4];
	} color;

	// Assign Color
	color.c[0] = (uint8_t)(args[1].value_union.ui32);
	color.c[1] = (uint8_t)(args[2].value_union.ui32);
	color.c[2] = (uint8_t)(args[3].value_union.ui32);
	if (args[4].value_union.ui32 != NULL)
		color.c[3] = (uint8_t)(args[4].value_union.ui32 * 255.0);

	else
		color.c[3] = 255;

	// Find Display
	auto it = displays.find(args[0].value_str);
	if (it == displays.end()) {
		/*isolate->ThrowException(
		      v8::Exception::SyntaxError(
		            v8::String::NewFromUtf8(isolate, "{displayKey} is not valid!")
		      )
		);*/
		rval.push_back(ipc::value((uint64_t)ErrorCode::Error));
		rval.push_back(ipc::value("Display key is not valid!"));
		return;
	}

	it->second->SetOutlineColor(color.c[0], color.c[1], color.c[2], color.c[3]);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
	return;
}

void OBS_content::OBS_content_setCropOutlineColor(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	union {
		uint32_t rgba;
		uint8_t c[4];
	} color;

	// Assign Color
	color.c[0] = (uint8_t)(args[1].value_union.ui32);
	color.c[1] = (uint8_t)(args[2].value_union.ui32);
	color.c[2] = (uint8_t)(args[3].value_union.ui32);
	if (args[4].value_union.ui32 != NULL) {
		color.c[3] = (uint8_t)(args[4].value_union.ui32);
	} else
		color.c[3] = 255;

	// Find Display
	auto it = displays.find(args[0].value_str);
	if (it == displays.end()) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Error));
		rval.push_back(ipc::value("Display key is not valid!"));
		return;
	}

	it->second->SetCropOutlineColor(color.c[0], color.c[1], color.c[2], color.c[3]);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
	return;
}

void OBS_content::OBS_content_setShouldDrawUI(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Find Display
	auto it = displays.find(args[0].value_str);
	if (it == displays.end()) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Error));
		rval.push_back(ipc::value("Display key is not valid!"));
		return;
	}

	it->second->SetDrawUI((bool)args[1].value_union.i32);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_content::OBS_content_getDisplayPreviewOffset(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	auto value = displays.find(args[0].value_str);
	if (value == displays.end()) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Error));
		rval.push_back(ipc::value("Invalid key provided to moveDisplay: " + args[0].value_str));
		return;
	}

	OBS::Display *display = value->second;

	auto offset = display->GetPreviewOffset();

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((int32_t)offset.first));
	rval.push_back(ipc::value((int32_t)offset.second));
	AUTO_DEBUG;
}

void OBS_content::OBS_content_getDisplayPreviewSize(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	auto value = displays.find(args[0].value_str);
	if (value == displays.end()) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Error));
		rval.push_back(ipc::value("Invalid key provided to moveDisplay: " + args[0].value_str));
		return;
	}

	OBS::Display *display = value->second;

	auto size = display->GetPreviewSize();

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((int32_t)size.first));
	rval.push_back(ipc::value((int32_t)size.second));
	AUTO_DEBUG;
}

void OBS_content::OBS_content_setDrawGuideLines(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Find Display
	auto it = displays.find(args[0].value_str);
	if (it == displays.end()) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Error));
		rval.push_back(ipc::value("Display key is not valid!"));
		return;
	}
	it->second->SetDrawGuideLines((bool)args[1].value_union.i32);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_content::OBS_content_setDrawRotationHandle(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Find Display
	auto it = displays.find(args[0].value_str);
	if (it == displays.end()) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Error));
		rval.push_back(ipc::value("Display key is not valid!"));
		return;
	}
	it->second->SetDrawRotationHandle((bool)args[1].value_union.ui32);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_content::OBS_content_createIOSurface(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
#ifdef __APPLE__
	// Find Display
	auto it = displays.find(args[0].value_str);
	if (it == displays.end()) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Error));
		rval.push_back(ipc::value("Display key is not valid!"));
		return;
	}
	if (it->second->m_display == nullptr) {
		return;
	}

	uint32_t surfaceID = obs_display_create_iosurface(it->second->m_display, it->second->GetSize().first, it->second->GetSize().second);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)surfaceID));
#elif WIN32
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
#endif
	AUTO_DEBUG;
}
