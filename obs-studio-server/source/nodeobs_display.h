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

#include <algorithm>
#include <memory>
#include <system_error>
#include <thread>
#include <vector>
#include "gs-vertexbuffer.h"
#include "obs.h"
#include "ipc-server.hpp"

#if defined(_WIN32)
#ifdef NOWINOFFSETS
#undef NOWINOFFSETS
#endif
#include <Dwmapi.h>
#include <versionhelpers.h>
#include <windows.h>
EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#define HINST_THISCOMPONENT ((HINSTANCE) & __ImageBase)

#elif defined(__APPLE__)

#elif defined(__linux__) || defined(__FreeBSD__)

#endif

extern ipc::server *g_srv;

namespace OBS {
class Display {
private:
	Display();

public:
	static void SetDayTheme(bool dayTheme);

	Display(uint64_t windowHandle,
		enum obs_video_rendering_mode mode, // Create a Main Preview one
		bool renderAtBottom, obs_video_info *canvas);
	Display(uint64_t windowHandle, enum obs_video_rendering_mode mode,
		const std::string &sourceName, // Create a Source-Specific one
		bool renderAtBottom, obs_video_info *canvas);
	~Display();

	void SetPosition(uint32_t x, uint32_t y);
	std::pair<uint32_t, uint32_t> GetPosition();

	void SetSize(uint32_t width, uint32_t height);
	std::pair<uint32_t, uint32_t> GetSize();

	std::pair<int32_t, int32_t> GetPreviewOffset();
	std::pair<uint32_t, uint32_t> GetPreviewSize();

	void SetDrawUI(bool v = true);
	bool GetDrawUI();

	void SetPaddingSize(uint32_t pixels);
	void SetPaddingColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255u);
	void SetBackgroundColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255u);
	void SetOutlineColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255u);
	void SetCropOutlineColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255u);
	void SetGuidelineColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255u);
	void SetResizeBoxOuterColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255u);
	void SetResizeBoxInnerColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255u);
	void SetRotationHandleColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255u);
	bool GetDrawGuideLines(void);
	void SetDrawGuideLines(bool drawGuideLines);
	bool GetDrawRotationHandle();
	void SetDrawRotationHandle(bool drawRotationHandle);
	void UpdatePreviewArea();

private:
	static void DisplayCallback(void *displayPtr, uint32_t cx, uint32_t cy);
	static bool DrawSelectedSource(obs_scene_t *scene, obs_sceneitem_t *item, void *param);
	static bool DrawSelectedOverflow(obs_scene_t *scene, obs_sceneitem_t *item, void *param);
	obs_source_t *GetSourceForUIEffects();
	void DrawCropOutline(float x1, float y1, float x2, float y2, vec2 scale);
	void DrawOutline(const matrix4 &mtx, const obs_sceneitem_crop &crop, const vec2 &boxScale, gs_eparam_t *color);
	void DrawRotationHandle(float rot, matrix4 &mtx);
	void setSizeCall(int step);

public: // Rendering code needs it.
	vec2 m_worldToPreviewScale, m_previewToWorldScale;

	gs_init_data m_gsInitData;
	obs_display_t *m_display;
	obs_source_t *m_source;
	bool m_drawGuideLines = true;
	bool m_drawRotationHandle = false;

	// Preview
	/// Window Position
	std::pair<uint32_t, uint32_t> m_position;
	/// Actual Preview Offset into Window
	std::pair<int32_t, int32_t> m_previewOffset;
	/// Actual Preview Size
	std::pair<uint32_t, uint32_t> m_previewSize;

private:
	struct ScopedGraphicsContext final {
		ScopedGraphicsContext() { obs_enter_graphics(); }
		~ScopedGraphicsContext() { obs_leave_graphics(); }
	};

	static bool m_dayTheme;
	// OBS Graphics API
	gs_effect_t *m_gsSolidEffect, *m_textEffect;
	gs_texture_t *m_textTexture;
	gs_texture_t *m_overflowNightTexture;
	gs_texture_t *m_overflowDayTexture;
	static std::mutex m_displayMtx;

	GS::VertexBuffer *m_textVertices;

	std::unique_ptr<GS::VertexBuffer> m_leftSolidOutline;
	std::unique_ptr<GS::VertexBuffer> m_topSolidOutline;
	std::unique_ptr<GS::VertexBuffer> m_rightSolidOutline;
	std::unique_ptr<GS::VertexBuffer> m_bottomSolidOutline;
	std::unique_ptr<GS::VertexBuffer> m_cropOutline;

	std::unique_ptr<GS::VertexBuffer> m_boxLine, m_boxTris;
	std::unique_ptr<GS::VertexBuffer> m_rotHandleLine, m_rotHandleCircle;

	// Theme/Style
	/// Padding
	uint32_t m_paddingSize = 10;
	uint32_t m_paddingColor = 0xFF222222;
	/// Other
	uint32_t m_backgroundColor = 0xFF000000;     // 0, 0, 0
	uint32_t m_outlineColor = 0xFFA8E61A;        // 26, 230, 168
	uint32_t m_cropOutlineColor = 0xFFA8E61A;    // 26, 230, 168
	uint32_t m_guidelineColor = 0xFFA8E61A;      // 26, 230, 168
	uint32_t m_resizeOuterColor = 0xFF7E7E7E;    // 126, 126, 126
	uint32_t m_resizeInnerColor = 0xFFFFFFFF;    // 255, 255, 255
	uint32_t m_rotationHandleColor = 0xFFA8E61A; // 26, 230, 168

	// The following values must be pre-calculated
	// in the constructor and the "Set" color methods!
	vec4 m_paddingColorVec4;
	vec4 m_backgroundColorVec4;
	vec4 m_outlineColorVec4;
	vec4 m_cropOutlineColorVec4;
	vec4 m_guidelineColorVec4;
	vec4 m_resizeOuterColorVec4;
	vec4 m_resizeInnerColorVec4;
	vec4 m_rotationHandleColorVec4;

	bool m_shouldDrawUI = true;
	bool m_renderAtBottom = false;

	enum obs_video_rendering_mode m_renderingMode = OBS_MAIN_VIDEO_RENDERING;
	struct obs_video_info *m_canvas;

#if defined(_WIN32)
	class SystemWorkerThread;
	std::unique_ptr<SystemWorkerThread> m_systemWorkerThread;
	HWND m_ourWindow;
	HWND m_parentWindow;
	static bool DisplayWndClassRegistered;
	static WNDCLASSEX DisplayWndClassObj;
	static ATOM DisplayWndClassAtom;
	static void DisplayWndClass();
	static LRESULT CALLBACK DisplayWndProc(_In_ HWND hwnd, _In_ UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam);
#elif defined(__APPLE__)
#elif defined(__linux__) || defined(__FreeBSD__)
#endif
};
} // namespace OBS
