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

#include "nodeobs_display.h"
#include <iostream>
#include <map>
#include <string>
#include "nodeobs_api.h"

#include <graphics/matrix4.h>
#include <graphics/vec4.h>
#include <util/platform.h>

#define HANDLE_RADIUS 5.0f
#define HANDLE_DIAMETER 10.0f

std::vector<std::pair<std::string, std::pair<uint32_t, uint32_t>>> sourcesSize;

extern std::string currentScene; /* defined in OBS_content.cpp */

static const uint32_t grayPaddingArea = 10ul;
std::mutex OBS::Display::m_displayMtx;
bool OBS::Display::m_dayTheme = false;

static void RecalculateApectRatioConstrainedSize(uint32_t origW, uint32_t origH, uint32_t sourceW, uint32_t sourceH, int32_t &outX, int32_t &outY,
						 uint32_t &outW, uint32_t &outH)
{
	double_t sourceAR = double_t(sourceW) / double_t(sourceH);
	double_t origAR = double_t(origW) / double_t(origH);
	if (origAR > sourceAR) {
		outW = uint32_t(double_t(origH) * sourceAR);
		outH = origH;
	} else {
		outW = origW;
		outH = uint32_t(double_t(origW) * (1.0 / sourceAR));
	}
	outX = (int32_t(origW / 2) - int32_t(outW / 2));
	outY = (int32_t(origH / 2) - int32_t(outH / 2));
}

#ifdef _WIN32
static void HandleWin32ErrorMessage(DWORD errorCode)
{
	LPSTR lpErrorStr = nullptr;
	DWORD dwErrorStrSize = 16;
	DWORD dwErrorStrLen = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, errorCode, LANG_USER_DEFAULT, lpErrorStr,
					     dwErrorStrSize, NULL);
	std::string exceptionMessage("Unexpected WinAPI error: " + std::string(lpErrorStr, dwErrorStrLen));
	LocalFree(lpErrorStr);
	throw std::system_error(errorCode, std::system_category(), exceptionMessage);
}

class OBS::Display::SystemWorkerThread final {
public:
	enum class Message : uint32_t {
		CreateWindow = WM_USER + 0,
		DestroyWindow = WM_USER + 1,
		StopThread = WM_USER + 2,
	};

	struct MessageQuestion {
		MessageQuestion(Message message) : m_message(message) {}
		virtual ~MessageQuestion() {}
		const Message m_message;
	};

	struct CreateWindowMessageQuestion : public MessageQuestion {
		CreateWindowMessageQuestion() : MessageQuestion(Message::CreateWindow) {}
		HWND m_parentWindow = NULL;
		uint32_t m_width = 0;
		uint32_t m_height = 0;
	};

	struct StopThreadMessageQuestion : public MessageQuestion {
		StopThreadMessageQuestion() : MessageQuestion(Message::StopThread) {}
	};

	struct DestroyWindowMessageQuestion : public MessageQuestion {
		DestroyWindowMessageQuestion() : MessageQuestion(Message::DestroyWindow) {}
		HWND m_window = NULL;
	};

	struct MessageAnswer {
		MessageAnswer() { m_event = CreateSemaphore(NULL, 0, INT32_MAX, NULL); }
		virtual ~MessageAnswer() { CloseHandle(m_event); }

		bool Wait() { return WaitForSingleObject(m_event, 1) == WAIT_OBJECT_0; }
		bool TryWait() { return WaitForSingleObject(m_event, 0) == WAIT_OBJECT_0; }
		void Signal() { ReleaseSemaphore(m_event, 1, NULL); }

		HANDLE m_event;
		bool m_called = false;
		bool m_success = false;
		DWORD m_errorCode = 0;
		std::string m_errorMessage;
	};

	struct CreateWindowMessageAnswer : MessageAnswer {
		HWND m_windowHandle = NULL;
	};

	struct DestroyWindowMessageAnswer : MessageAnswer {
	};

	SystemWorkerThread() : m_thread(&OBS::Display::SystemWorkerThread::Thread, this) {}
	~SystemWorkerThread()
	{
		if (m_thread.joinable()) {
			PostMessage(StopThreadMessageQuestion(), nullptr);
			m_thread.join();
		}
	}

	bool PostMessage(const MessageQuestion &question, MessageAnswer *answer)
	{
		return ::PostThreadMessage(GetThreadId(m_thread.native_handle()), static_cast<UINT>(question.m_message), reinterpret_cast<uintptr_t>(&question),
					   reinterpret_cast<intptr_t>(answer)) != 0;
	}

private:
	void Thread()
	{
		MSG message;
		PeekMessage(&message, NULL, WM_USER, WM_USER, PM_NOREMOVE);

		bool keepRunning = true;
		do {
			BOOL gotMessage = GetMessage(&message, NULL, 0, 0);
			if (gotMessage == 0) {
				continue;
			}
			if (gotMessage == -1) {
				break; // Some sort of error.
			}

			if (message.hwnd != NULL) {
				TranslateMessage(&message);
				DispatchMessage(&message);
				continue;
			}

			switch ((Message)message.message) {
			case Message::CreateWindow: {
				CreateWindowMessageQuestion *question = reinterpret_cast<CreateWindowMessageQuestion *>(message.wParam);
				CreateWindowMessageAnswer *answer = reinterpret_cast<CreateWindowMessageAnswer *>(message.lParam);

				BOOL enabled = FALSE;
				DwmIsCompositionEnabled(&enabled);
				DWORD windowStyle = WS_EX_TRANSPARENT;

				if (IsWindows8OrGreater() && enabled) {
					windowStyle |= WS_EX_COMPOSITED;
				}

				HWND newWindow = CreateWindowEx(WS_EX_LAYERED, TEXT("Win32DisplayClass"), TEXT("SlobsChildWindowPreview"), WS_POPUP, 0, 0, 0, 0,
								NULL, NULL, GetModuleHandle(NULL), this);

				if (!newWindow) {
					answer->m_success = false;
					HandleWin32ErrorMessage(GetLastError());
				} else {
					if (IsWindows8OrGreater() || !enabled) {
						SetLayeredWindowAttributes(newWindow, 0, 255, LWA_ALPHA);
					}

					SetParent(newWindow, question->m_parentWindow);

					LONG_PTR style = GetWindowLongPtr(newWindow, GWL_STYLE);
					style &= ~WS_POPUP;
					style |= WS_CHILD;
					SetWindowLongPtr(newWindow, GWL_STYLE, style);

					LONG_PTR exStyle = GetWindowLongPtr(newWindow, GWL_EXSTYLE);
					exStyle |= windowStyle;
					SetWindowLongPtr(newWindow, GWL_EXSTYLE, exStyle);

					answer->m_windowHandle = newWindow;
					answer->m_success = true;
				}

				answer->m_called = true;
				answer->Signal();
				break;
			}
			case Message::DestroyWindow: {
				DestroyWindowMessageQuestion *question = reinterpret_cast<DestroyWindowMessageQuestion *>(message.wParam);
				DestroyWindowMessageAnswer *answer = reinterpret_cast<DestroyWindowMessageAnswer *>(message.lParam);

				LONG_PTR exStyle = GetWindowLongPtr(question->m_window, GWL_EXSTYLE);
				if (exStyle == 0) {
					DWORD error = GetLastError();
					blog(LOG_ERROR, "Destroy Display Window failed to get GWL_EXSTYLE. Error code: %08X", error);
				}

				LONG_PTR style = GetWindowLongPtr(question->m_window, GWL_STYLE);
				if (style == 0) {
					DWORD error = GetLastError();
					blog(LOG_ERROR, "Destroy Display Window failed to get GWL_STYLE. Error code: %08X", error);
				}

				blog(LOG_INFO, "Destroy Display Window: exStyle: %08X, style: %08X", exStyle, style);

				if (!DestroyWindow(question->m_window)) {
					auto error = GetLastError();

					// We check for error 1400 because if this display is a projector, it is attached to a HTML DOM, so
					// we cannot directly control its destruction since the HTML will probably do this concurrently,
					// the DestroyWindow is allows to fail on this case, a better solution here woul be checking if this
					// display is really a projector and do not attempt to destroy it (let the HTML do it for us).
					if (error != 1400) {
						answer->m_success = false;
						HandleWin32ErrorMessage(error);
					} else {
						answer->m_success = true;
					}

				} else {
					answer->m_success = true;
				}

				answer->m_called = true;
				answer->Signal();
				break;
			}
			case Message::StopThread: {
				keepRunning = false;
				break;
			}
			}
		} while (keepRunning);
	}

	std::thread m_thread;
};
#endif

static vec4 ConvertColorToVec4(uint32_t color)
{
	vec4 colorVec4;
	vec4_set(&colorVec4, static_cast<float>(color & 0xFF) / 255.0f, static_cast<float>((color & 0xFF00) >> 8) / 255.0f,
		 static_cast<float>((color & 0xFF0000) >> 16) / 255.0f, static_cast<float>((color & 0xFF000000) >> 24) / 255.0f);
	return colorVec4;
}

void OBS::Display::SetDayTheme(bool dayTheme)
{
	m_dayTheme = dayTheme;
}

OBS::Display::Display()
	: m_gsInitData({})
#if defined(_WIN32)
	  ,
	  m_systemWorkerThread(std::make_unique<SystemWorkerThread>())
#endif
{
#if defined(_WIN32)
	DisplayWndClass();
#elif defined(__APPLE__)
#elif defined(__linux__) || defined(__FreeBSD__)
#endif

	m_gsInitData.format = GS_BGRA;
	m_gsInitData.zsformat = GS_ZS_NONE;
	m_gsInitData.num_backbuffers = 1;
	m_display = nullptr;
	m_source = nullptr;
	m_position.first = 0;
	m_position.second = 0;

	{
		// This enters the OBS graphics context upon creation
		// and leaves it at the end of the C++ scope or when an exception is thrown.
		ScopedGraphicsContext scopedGraphicsContext;

		m_gsSolidEffect = obs_get_base_effect(OBS_EFFECT_SOLID);

		GS::Vertex v(nullptr, nullptr, nullptr, nullptr, nullptr);

		// Left solid outline
		m_leftSolidOutline = std::make_unique<GS::VertexBuffer>(2);
		m_leftSolidOutline->Resize(2);
		v = m_leftSolidOutline->At(0);
		vec3_set(v.position, 0.0f, 0.0f, 0);
		vec4_set(v.uv[0], 0, 0, 0, 0);
		*v.color = 0xFFFFFFFF;
		v = m_leftSolidOutline->At(1);
		vec3_set(v.position, 0.0f, 1.0f, 0);
		vec4_set(v.uv[0], 0, 0, 0, 0);
		*v.color = 0xFFFFFFFF;
		m_leftSolidOutline->Update();

		// Top solid outline
		m_topSolidOutline = std::make_unique<GS::VertexBuffer>(2);
		m_topSolidOutline->Resize(2);
		v = m_topSolidOutline->At(0);
		vec3_set(v.position, 0.0f, 0.0f, 0);
		vec4_set(v.uv[0], 0, 0, 0, 0);
		*v.color = 0xFFFFFFFF;
		v = m_topSolidOutline->At(1);
		vec3_set(v.position, 1.0f, 0.0f, 0);
		vec4_set(v.uv[0], 0, 0, 0, 0);
		*v.color = 0xFFFFFFFF;
		m_topSolidOutline->Update();

		// Right solid outline
		m_rightSolidOutline = std::make_unique<GS::VertexBuffer>(2);
		m_rightSolidOutline->Resize(2);
		v = m_rightSolidOutline->At(0);
		vec3_set(v.position, 1.0f, 0.0f, 0);
		vec4_set(v.uv[0], 0, 0, 0, 0);
		*v.color = 0xFFFFFFFF;
		v = m_rightSolidOutline->At(1);
		vec3_set(v.position, 1.0f, 1.0f, 0);
		vec4_set(v.uv[0], 0, 0, 0, 0);
		*v.color = 0xFFFFFFFF;
		m_rightSolidOutline->Update();

		// Bottom solid outline
		m_bottomSolidOutline = std::make_unique<GS::VertexBuffer>(2);
		m_bottomSolidOutline->Resize(2);
		v = m_bottomSolidOutline->At(0);
		vec3_set(v.position, 0.0f, 1.0f, 0);
		vec4_set(v.uv[0], 0, 0, 0, 0);
		*v.color = 0xFFFFFFFF;
		v = m_bottomSolidOutline->At(1);
		vec3_set(v.position, 1.0f, 1.0f, 0);
		vec4_set(v.uv[0], 0, 0, 0, 0);
		*v.color = 0xFFFFFFFF;
		m_bottomSolidOutline->Update();

		// Crop effect outline
		m_cropOutline = std::make_unique<GS::VertexBuffer>(4);
		m_cropOutline->Resize(4);

		m_boxLine = std::make_unique<GS::VertexBuffer>(6);
		m_boxLine->Resize(6);
		v = m_boxLine->At(0);
		vec3_set(v.position, 0, 0, 0);
		vec4_set(v.uv[0], 0, 0, 0, 0);
		*v.color = 0xFFFFFFFF;
		v = m_boxLine->At(1);
		vec3_set(v.position, 1, 0, 0);
		vec4_set(v.uv[0], 1, 0, 0, 0);
		*v.color = 0xFFFFFFFF;
		v = m_boxLine->At(2);
		vec3_set(v.position, 1, 1, 0);
		vec4_set(v.uv[0], 1, 1, 0, 0);
		*v.color = 0xFFFFFFFF;
		v = m_boxLine->At(3);
		vec3_set(v.position, 0, 1, 0);
		vec4_set(v.uv[0], 0, 1, 0, 0);
		*v.color = 0xFFFFFFFF;
		v = m_boxLine->At(4);
		vec3_set(v.position, 0, 0, 0);
		vec4_set(v.uv[0], 0, 0, 0, 0);
		*v.color = 0xFFFFFFFF;
		m_boxLine->Update();

		m_boxTris = std::make_unique<GS::VertexBuffer>(4);
		m_boxTris->Resize(4);
		v = m_boxTris->At(0);
		vec3_set(v.position, 0, 0, 0);
		vec4_set(v.uv[0], 0, 0, 0, 0);
		*v.color = 0xFFFFFFFF;
		v = m_boxTris->At(1);
		vec3_set(v.position, 1, 0, 0);
		vec4_set(v.uv[0], 1, 0, 0, 0);
		*v.color = 0xFFFFFFFF;
		v = m_boxTris->At(2);
		vec3_set(v.position, 0, 1, 0);
		vec4_set(v.uv[0], 0, 1, 0, 0);
		*v.color = 0xFFFFFFFF;
		v = m_boxTris->At(3);
		vec3_set(v.position, 1, 1, 0);
		vec4_set(v.uv[0], 1, 1, 0, 0);
		*v.color = 0xFFFFFFFF;
		m_boxTris->Update();

		// Rotation handle line
		m_rotHandleLine = std::make_unique<GS::VertexBuffer>(5);
		m_rotHandleLine->Resize(5);
		v = m_rotHandleLine->At(0);
		vec3_set(v.position, 0.5f - 0.34f / HANDLE_RADIUS, 0.5f, 0);
		vec4_set(v.uv[0], 0, 0, 0, 0);
		*v.color = 0xFFFFFFFF;
		v = m_rotHandleLine->At(1);
		vec3_set(v.position, 0.5f - 0.34f / HANDLE_RADIUS, -2.0f, 0);
		vec4_set(v.uv[0], 0, 0, 0, 0);
		*v.color = 0xFFFFFFFF;
		v = m_rotHandleLine->At(2);
		vec3_set(v.position, 0.5f + 0.34f / HANDLE_RADIUS, -2.0f, 0);
		vec4_set(v.uv[0], 0, 0, 0, 0);
		*v.color = 0xFFFFFFFF;
		v = m_rotHandleLine->At(3);
		vec3_set(v.position, 0.5f + 0.34f / HANDLE_RADIUS, 0.5f, 0);
		vec4_set(v.uv[0], 0, 0, 0, 0);
		*v.color = 0xFFFFFFFF;
		v = m_rotHandleLine->At(4);
		vec3_set(v.position, 0.5f - 0.34f / HANDLE_RADIUS, 0.5f, 0);
		vec4_set(v.uv[0], 0, 0, 0, 0);
		*v.color = 0xFFFFFFFF;
		m_rotHandleLine->Update();

		// Rotation handle circle
		m_rotHandleCircle = std::make_unique<GS::VertexBuffer>(120);
		m_rotHandleCircle->Resize(120);
		float angle = 180;
		for (int i = 0; i < 40; ++i) {
			v = m_rotHandleCircle->At(i * 3);
			vec3_set(v.position, sin(RAD(angle)) / 2 + 0.5f, cos(RAD(angle)) / 2 + 0.5f, 0);
			vec4_set(v.uv[0], 0, 0, 0, 0);
			*v.color = 0xFFFFFFFF;
			angle += 8.75f;
			v = m_rotHandleCircle->At((i * 3) + 1);
			vec3_set(v.position, sin(RAD(angle)) / 2 + 0.5f, cos(RAD(angle)) / 2 + 0.5f, 0);
			vec4_set(v.uv[0], 0, 0, 0, 0);
			*v.color = 0xFFFFFFFF;
			v = m_rotHandleCircle->At((i * 3) + 2);
			vec3_set(v.position, 0.5f, 1.0f, 0);
			vec4_set(v.uv[0], 0, 0, 0, 0);
			*v.color = 0xFFFFFFFF;
		}
		m_rotHandleCircle->Update();

		// Text
		m_textVertices = new GS::VertexBuffer(65535);
		m_textEffect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		m_textTexture = gs_texture_create_from_file((g_moduleDirectory + "/resources/roboto.png").c_str());
		if (!m_textTexture) {
			throw std::runtime_error("couldn't load roboto font");
		}

		// Overflow
		m_overflowNightTexture = gs_texture_create_from_file((g_moduleDirectory + "/resources/overflow-night-mode.png").c_str());
		if (!m_overflowNightTexture) {
			throw std::runtime_error("couldn't load the night pattern overflow texture");
		}
		m_overflowDayTexture = gs_texture_create_from_file((g_moduleDirectory + "/resources/overflow-day-mode.png").c_str());
		if (!m_overflowDayTexture) {
			throw std::runtime_error("couldn't load the day pattern overflow texture");
		}
	}

	m_paddingColorVec4 = ConvertColorToVec4(m_paddingColor);
	m_backgroundColorVec4 = ConvertColorToVec4(m_backgroundColor);
	m_outlineColorVec4 = ConvertColorToVec4(m_outlineColor);
	m_cropOutlineColorVec4 = ConvertColorToVec4(m_cropOutlineColor);
	m_guidelineColorVec4 = ConvertColorToVec4(m_guidelineColor);
	m_resizeOuterColorVec4 = ConvertColorToVec4(m_resizeOuterColor);
	m_resizeInnerColorVec4 = ConvertColorToVec4(m_resizeInnerColor);
	m_rotationHandleColorVec4 = ConvertColorToVec4(m_rotationHandleColor);
}

OBS::Display::Display(uint64_t windowHandle, enum obs_video_rendering_mode mode, bool renderAtBottom, obs_video_info *canvas) : Display()
{
	m_renderAtBottom = renderAtBottom;
#ifdef _WIN32
	SystemWorkerThread::CreateWindowMessageQuestion question;
	SystemWorkerThread::CreateWindowMessageAnswer answer;

	question.m_parentWindow = (HWND)windowHandle;

	RECT parentWindowRect = {};
	GetWindowRect((HWND)windowHandle, &parentWindowRect);

	blog(LOG_INFO, "[DISPLAY_SCALE] OBS::Display::Display() parent window [x*y: %d*%d] [w*h: %d*%d]; this window [w*h: %u*%u]; display: %p",
	     parentWindowRect.left, parentWindowRect.top, parentWindowRect.right - parentWindowRect.left, parentWindowRect.bottom - parentWindowRect.top,
	     m_gsInitData.cx, m_gsInitData.cy, this);

	question.m_width = m_gsInitData.cx;
	question.m_height = m_gsInitData.cy;
	while (!m_systemWorkerThread->PostMessage(question, &answer)) {
		Sleep(0);
	}

	if (!answer.TryWait()) {
		while (!answer.Wait()) {
			if (answer.m_called)
				break;
			Sleep(0);
		}
	}

	if (!answer.m_success) {
		throw std::system_error(answer.m_errorCode, std::system_category(), answer.m_errorMessage);
	}

	m_ourWindow = answer.m_windowHandle;
	m_parentWindow = reinterpret_cast<HWND>(windowHandle);
	m_gsInitData.window.hwnd = reinterpret_cast<void *>(m_ourWindow);

	DPI_AWARENESS dpiAwareness = GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext());
	UINT dpi = GetDpiForWindow(m_ourWindow);
	blog(LOG_INFO, "[DISPLAY_SCALE] OBS::Display::Display() dpiAwareness: %d; dpi: %u; display: %p", (int)dpiAwareness, dpi, this);

#endif
	{
		std::lock_guard lock(m_displayMtx);

		m_display = obs_display_create(&m_gsInitData, 0x0);

		if (!m_display) {
			blog(LOG_INFO, "Failed to create the display");
			throw std::runtime_error("unable to create display");
		}

		m_renderingMode = mode;
		m_canvas = canvas;

		obs_display_add_draw_callback(m_display, DisplayCallback, this);
	}

	UpdatePreviewArea();
}

OBS::Display::Display(uint64_t windowHandle, enum obs_video_rendering_mode mode, const std::string &sourceName, bool renderAtBottom, obs_video_info *canvas)
	: Display(windowHandle, mode, renderAtBottom, canvas)
{
	m_source = obs_get_source_by_name(sourceName.c_str());
	obs_source_inc_showing(m_source);
	if (m_source && obs_source_get_type(m_source) == OBS_SOURCE_TYPE_SCENE) {
		obs_activate_scene_on_backstage(m_source);
	}
}

OBS::Display::~Display()
{
	{
		std::lock_guard lock(m_displayMtx);

		obs_display_remove_draw_callback(m_display, DisplayCallback, this);

		if (m_source) {
			if (obs_source_get_type(m_source) == OBS_SOURCE_TYPE_SCENE) {
				obs_deactivate_scene_on_backstage(m_source);
			}
			obs_source_dec_showing(m_source);
			obs_source_release(m_source);
		}

		if (m_textVertices) {
			delete m_textVertices;
		}

		if (m_textTexture) {
			obs_enter_graphics();
			gs_texture_destroy(m_textTexture);
			obs_leave_graphics();
		}

		if (m_overflowNightTexture) {
			obs_enter_graphics();
			gs_texture_destroy(m_overflowNightTexture);
			obs_leave_graphics();
		}

		if (m_overflowDayTexture) {
			obs_enter_graphics();
			gs_texture_destroy(m_overflowDayTexture);
			obs_leave_graphics();
		}

		m_leftSolidOutline.reset();
		m_topSolidOutline.reset();
		m_rightSolidOutline.reset();
		m_bottomSolidOutline.reset();
		m_cropOutline.reset();
		m_boxLine = nullptr;
		m_boxTris = nullptr;
		m_rotHandleLine.reset();
		m_rotHandleCircle.reset();

		if (m_display)
			obs_display_destroy(m_display);
	}

#ifdef _WIN32
	SystemWorkerThread::DestroyWindowMessageQuestion question;
	SystemWorkerThread::DestroyWindowMessageAnswer answer;

	question.m_window = m_ourWindow;
	m_systemWorkerThread->PostMessage(question, &answer);

	if (!answer.TryWait()) {
		while (!answer.Wait()) {
			if (answer.m_called)
				break;
			Sleep(0);
		}
	}

	if (!answer.m_success) {
		std::cerr << "OBS::Display::~Display: " << answer.m_errorMessage << std::endl;
	}
#endif
}

void OBS::Display::SetPosition(uint32_t x, uint32_t y)
{
#if defined(_WIN32)
	// Store new position.
	m_position.first = x;
	m_position.second = y;

	if (m_source != NULL) {
		std::string msg = "<" + std::string(__FUNCTION__) + "> Adjusting display position for source %s to %ldx%ld. hwnd %d";
		blog(LOG_DEBUG, msg.c_str(), obs_source_get_name(m_source), x, y, m_ourWindow);
	}

	blog(LOG_INFO, "[DISPLAY_SCALE] Display::SetPosition() - Pos: %ux%u, Size: %ux%u, Window: 0x%p; display: %p", m_position.first, m_position.second,
	     m_gsInitData.cx, m_gsInitData.cy, (void *)m_ourWindow, this);

	HWND insertAfter = (m_renderAtBottom) ? HWND_BOTTOM : NULL;
	SetWindowPos(m_ourWindow, insertAfter, m_position.first, m_position.second, m_gsInitData.cx, m_gsInitData.cy,
		     SWP_NOCOPYBITS | SWP_NOSIZE | SWP_NOACTIVATE);
#endif
}

std::pair<uint32_t, uint32_t> OBS::Display::GetPosition()
{
	return m_position;
}

bool isNewerThanWindows7()
{
#ifdef WIN32
	static bool versionIsHigherThan7 = false;
	static bool versionIsChecked = false;
	if (!versionIsChecked) {
		versionIsHigherThan7 = IsWindows7OrGreater();
		versionIsChecked = true;
	}
	return versionIsHigherThan7;
#else
	return false;
#endif
}

void OBS::Display::setSizeCall(int step)
{
	BOOL ret = true;

#if defined(_WIN32)
	int use_x, use_y;
	int use_width, use_height;
	const double presizes[] = {1, 1.05, 1.25, 1.5, 2.0, 3.0};

	switch (step) {
	case -1:
		use_width = m_gsInitData.cx;
		use_height = m_gsInitData.cy;
		use_x = m_position.first;
		use_y = m_position.second;
		break;
	case 0:
		use_width = m_gsInitData.cx - 2;
		use_height = m_gsInitData.cy - 2;
		use_x = m_position.first + 1;
		use_y = m_position.second + 1;
		break;
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
		use_width = static_cast<int>(double(m_gsInitData.cx) / presizes[step]);
		use_height = static_cast<int>(double(m_gsInitData.cy) / presizes[step]);
		use_x = m_position.first + (m_gsInitData.cx - use_width) / 2;
		use_y = m_position.second + (m_gsInitData.cy - use_height) / 2;
		break;
	}

	blog(LOG_INFO, "[DISPLAY_SCALE] Display::setSizeCall() - Pos: %ux%u, Size: %ux%u, DPI Aware: %d, DPI: %u, Window: 0x%p; display: %p", use_x, use_y,
	     use_width, use_height, (int)GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext()), GetDpiForWindow(m_ourWindow), (void *)m_ourWindow,
	     this);

	// Resize Window
	if (step > 0) {
		ret = SetWindowPos(m_ourWindow, NULL, use_x, use_y, use_width, use_height, SWP_NOCOPYBITS | SWP_NOACTIVATE | SWP_NOZORDER | SWP_HIDEWINDOW);
	} else {
		ret = SetWindowPos(m_ourWindow, NULL, use_x, use_y, use_width, use_height, SWP_NOCOPYBITS | SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
		if (ret)
			RedrawWindow(m_ourWindow, NULL, NULL, RDW_ERASE | RDW_INVALIDATE);
	}
#elif defined(__APPLE__)
#elif defined(__linux__) || defined(__FreeBSD__)
#endif

	if (step >= 0 && ret) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		std::thread{&OBS::Display::setSizeCall, this, step - 1}.detach();
	}
};

void OBS::Display::SetSize(uint32_t width, uint32_t height)
{
#ifdef WIN32
	if (m_source != NULL) {
		std::string msg = "<" + std::string(__FUNCTION__) + "> Adjusting display size for source %s to %ldx%ld. hwnd %d";
		blog(LOG_DEBUG, msg.c_str(), obs_source_get_name(m_source), width, height, m_ourWindow);
	}

	blog(LOG_INFO, "[DISPLAY_SCALE] OBS::Display::SetSize() [w*h: %u*%u]; display: %p", width, height, this);

	m_gsInitData.cx = width;
	m_gsInitData.cy = height;

	if (width == 0 || height == 0 || isNewerThanWindows7()) {
		setSizeCall(-1);
	} else {
		setSizeCall(4);
	}

	// Resize Display
	obs_display_resize(m_display, m_gsInitData.cx, m_gsInitData.cy);

	// Store new size.
	UpdatePreviewArea();
#endif
}

std::pair<uint32_t, uint32_t> OBS::Display::GetSize()
{
	blog(LOG_INFO, "[DISPLAY_SCALE] OBS::Display::GetSize() [w*h: %u*%u]; display: %p", m_gsInitData.cx, m_gsInitData.cy, this);

	return std::make_pair(m_gsInitData.cx, m_gsInitData.cy);
}

std::pair<int32_t, int32_t> OBS::Display::GetPreviewOffset()
{
	return m_previewOffset;
}

std::pair<uint32_t, uint32_t> OBS::Display::GetPreviewSize()
{
	return m_previewSize;
}

void OBS::Display::SetDrawUI(bool v /*= true*/)
{
	m_shouldDrawUI = v;
}

bool OBS::Display::GetDrawUI()
{
	return m_shouldDrawUI;
}

static void PrepareColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint32_t *color, vec4 *colorVec4)
{
	*color = a << 24 | b << 16 | g << 8 | r;
	vec4_set(colorVec4, static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f, static_cast<float>(b) / 255.0f, static_cast<float>(a) / 255.0f);
}

void OBS::Display::SetPaddingColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a /*= 255u*/)
{
	PrepareColor(r, g, b, a, &m_paddingColor, &m_paddingColorVec4);
}

void OBS::Display::SetPaddingSize(uint32_t pixels)
{
	m_paddingSize = pixels;
	UpdatePreviewArea();
}

void OBS::Display::SetBackgroundColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a /*= 255u*/)
{
	PrepareColor(r, g, b, a, &m_backgroundColor, &m_backgroundColorVec4);
}

void OBS::Display::SetOutlineColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a /*= 255u*/)
{
	PrepareColor(r, g, b, a, &m_outlineColor, &m_outlineColorVec4);
}

void OBS::Display::SetCropOutlineColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a /*= 255u*/)
{
	PrepareColor(r, g, b, a, &m_cropOutlineColor, &m_cropOutlineColorVec4);
}

void OBS::Display::SetGuidelineColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a /*= 255u*/)
{
	PrepareColor(r, g, b, a, &m_guidelineColor, &m_guidelineColorVec4);
}

void OBS::Display::SetResizeBoxOuterColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a /*= 255u*/)
{
	PrepareColor(r, g, b, a, &m_resizeOuterColor, &m_resizeOuterColorVec4);
}

void OBS::Display::SetResizeBoxInnerColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a /*= 255u*/)
{
	PrepareColor(r, g, b, a, &m_resizeInnerColor, &m_resizeInnerColorVec4);
}

void OBS::Display::SetRotationHandleColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a /*= 255u*/)
{
	PrepareColor(r, g, b, a, &m_rotationHandleColor, &m_rotationHandleColorVec4);
}

static void DrawGlyph(GS::VertexBuffer *vb, float_t x, float_t y, float_t scale, float_t depth, char glyph, uint32_t color)
{
	float uvX = 0, uvY = 0, uvO = 1.0 / 4.0;
	switch (glyph) {
	default:
		return;
		break;
	case '1':
		uvX = 0;
		uvY = 0;
		break;
	case '2':
		uvX = uvO;
		uvY = 0;
		break;
	case '3':
		uvX = uvO * 2;
		uvY = 0;
		break;
	case '4':
		uvX = uvO * 3;
		uvY = 0;
		break;
	case '5':
		uvX = 0;
		uvY = uvO * 1;
		break;
	case '6':
		uvX = uvO;
		uvY = uvO * 1;
		break;
	case '7':
		uvX = uvO * 2;
		uvY = uvO * 1;
		break;
	case '8':
		uvX = uvO * 3;
		uvY = uvO * 1;
		break;
	case '9':
		uvX = 0;
		uvY = uvO * 2;
		break;
	case '0':
		uvX = uvO;
		uvY = uvO * 2;
		break;
	case 'p':
		uvX = uvO * 2;
		uvY = uvO * 2;
		break;
	case 'x':
		uvX = uvO * 3;
		uvY = uvO * 2;
		break;
	}

	GS::Vertex v(nullptr, nullptr, nullptr, nullptr, nullptr);
	size_t bs = vb->Size();
	vb->Resize(uint32_t(bs + 6));

	// Top Left
	v = vb->At(uint32_t(bs + 0));
	vec3_set(v.position, x, y, depth);
	vec4_set(v.uv[0], uvX, uvY, 0, 0);
	*v.color = color;
	// Top Right
	v = vb->At(uint32_t(bs + 1));
	vec3_set(v.position, x + scale, y, depth);
	vec4_set(v.uv[0], uvX + uvO, uvY, 0, 0);
	*v.color = color;
	// Bottom Left
	v = vb->At(uint32_t(bs + 2));
	vec3_set(v.position, x, y + scale * 2, depth);
	vec4_set(v.uv[0], uvX, uvY + uvO, 0, 0);
	*v.color = color;

	// Top Right
	v = vb->At(uint32_t(bs + 3));
	vec3_set(v.position, x + scale, y, depth);
	vec4_set(v.uv[0], uvX + uvO, uvY, 0, 0);
	*v.color = color;
	// Bottom Left
	v = vb->At(uint32_t(bs + 4));
	vec3_set(v.position, x, y + scale * 2, depth);
	vec4_set(v.uv[0], uvX, uvY + uvO, 0, 0);
	*v.color = color;
	// Bottom Right
	v = vb->At(uint32_t(bs + 5));
	vec3_set(v.position, x + scale, y + scale * 2, depth);
	vec4_set(v.uv[0], uvX + uvO, uvY + uvO, 0, 0);
	*v.color = color;
}

inline bool CloseFloat(float a, float b, float epsilon = 0.01)
{
	return abs(a - b) <= epsilon;
}

void OBS::Display::DrawCropOutline(float x1, float y1, float x2, float y2, vec2 scale)
{
	// This is partially code from OBS Studio. See window-basic-preview.cpp in obs-studio for copyright/license.

	float ySide = (y1 == y2) ? (y1 < 0.5f ? 1.0f : -1.0f) : 0.0f;
	float xSide = (x1 == x2) ? (x1 < 0.5f ? 1.0f : -1.0f) : 0.0f;

	float dist = static_cast<float>(sqrt(pow((x1 - x2) * scale.x, 2) + pow((y1 - y2) * scale.y, 2)));
	float offX = (x2 - x1) / dist;
	float offY = (y2 - y1) / dist;

	int l = static_cast<int>(ceil(dist / 15));
	for (int i = 0; i < l; ++i) {
		float xx1 = x1 + i * 15 * offX;
		float yy1 = y1 + i * 15 * offY;

		float dx;
		float dy;

		if (x1 < x2) {
			dx = std::min(xx1 + 7.5f * offX, x2);
		} else {
			dx = std::max(xx1 + 7.5f * offX, x2);
		}

		if (y1 < y2) {
			dy = std::min(yy1 + 7.5f * offY, y2);
		} else {
			dy = std::max(yy1 + 7.5f * offY, y2);
		}

		GS::Vertex v(nullptr, nullptr, nullptr, nullptr, nullptr);

		v = m_cropOutline->At(0);
		vec3_set(v.position, xx1, yy1, 0);
		vec4_set(v.uv[0], 0, 0, 0, 0);
		*v.color = 0xFFFFFFFF;

		v = m_cropOutline->At(1);
		vec3_set(v.position, xx1 + (xSide * (5 / scale.x)), yy1 + (ySide * (5 / scale.y)), 0);
		vec4_set(v.uv[0], 0, 0, 0, 0);
		*v.color = 0xFFFFFFFF;

		v = m_cropOutline->At(2);
		vec3_set(v.position, dx, dy, 0);
		vec4_set(v.uv[0], 0, 0, 0, 0);
		*v.color = 0xFFFFFFFF;

		v = m_cropOutline->At(3);
		vec3_set(v.position, dx + (xSide * (5 / scale.x)), dy + (ySide * (5 / scale.y)), 0);
		vec4_set(v.uv[0], 0, 0, 0, 0);
		*v.color = 0xFFFFFFFF;

		gs_load_vertexbuffer(m_cropOutline->Update());
		gs_draw(GS_TRISTRIP, 0, 0);
	}
}

static void DrawSolidOutline(GS::VertexBuffer *vertexBuffer)
{
	gs_load_vertexbuffer(vertexBuffer->Update(false));
	gs_draw(GS_LINES, 0, 0);
}

inline void DrawBoxAt(OBS::Display *dp, float_t x, float_t y, matrix4 &mtx)
{
	gs_matrix_push();

	vec3 pos = {x, y, 0.0f};
	vec3_transform(&pos, &pos, &mtx);

	vec3 offset = {-HANDLE_RADIUS, -HANDLE_RADIUS, 0.0f};
	offset.x *= dp->m_previewToWorldScale.x;
	offset.y *= dp->m_previewToWorldScale.y;

	gs_matrix_translate(&pos);
	gs_matrix_translate(&offset);
	gs_matrix_scale3f(HANDLE_DIAMETER * dp->m_previewToWorldScale.x, HANDLE_DIAMETER * dp->m_previewToWorldScale.y, 1.0f);

	gs_draw(GS_LINESTRIP, 0, 0);
	gs_matrix_pop();
}

inline void DrawSquareAt(OBS::Display *dp, float_t x, float_t y, matrix4 &mtx)
{
	gs_matrix_push();

	vec3 pos = {x, y, 0.0f};
	vec3_transform(&pos, &pos, &mtx);

	vec3 offset = {-HANDLE_RADIUS, -HANDLE_RADIUS, 0.0f};
	offset.x *= dp->m_previewToWorldScale.x;
	offset.y *= dp->m_previewToWorldScale.y;

	gs_matrix_translate(&pos);
	gs_matrix_translate(&offset);
	gs_matrix_scale3f(HANDLE_DIAMETER * dp->m_previewToWorldScale.x, HANDLE_DIAMETER * dp->m_previewToWorldScale.y, 1.0f);

	gs_draw(GS_TRISTRIP, 0, 0);
	gs_matrix_pop();
}

inline void DrawGuideline(OBS::Display *dp, bool rot45, float_t x, float_t y, matrix4 &mtx)
{
	gs_rect rect;
	rect.x = dp->GetPreviewOffset().first;
	rect.y = dp->GetPreviewOffset().second;
	rect.cx = dp->GetPreviewSize().first;
	rect.cy = dp->GetPreviewSize().second;

	gs_set_scissor_rect(&rect);
	gs_matrix_push();

	vec3 center = {0.5, 0.5, 0.0f};
	vec3_transform(&center, &center, &mtx);

	vec3 pos = {x, y, 0.0f};
	vec3_transform(&pos, &pos, &mtx);

	vec3 normal;
	vec3_sub(&normal, &center, &pos);
	vec3_norm(&normal, &normal);

	gs_matrix_translate(&pos);

	vec3 up, dn, lt, rt;

	if (rot45) {
		up = {(float)-0.2, (float)1.0, 0};
		dn = {(float)0.2, (float)-1.0, 0};
		lt = {(float)-1.0, (float)-0.2, 0};
		rt = {(float)1.0, (float)0.2, 0};
	} else {
		up = {0, 1.0, 0};
		dn = {0, -1.0, 0};
		lt = {-1.0, 0, 0};
		rt = {1.0, 0, 0};
	}

	if (vec3_dot(&up, &normal) > 0.707f) {
		// Dominantly looking up.
		gs_matrix_rotaa4f(0, 0, 1, RAD(-90.0f));
	} else if (vec3_dot(&dn, &normal) > 0.707f) {
		// Dominantly looking down.
		gs_matrix_rotaa4f(0, 0, 1, RAD(90.0f));
	} else if (vec3_dot(&lt, &normal) > 0.707f) {
		// Dominantly looking left.
		gs_matrix_rotaa4f(0, 0, 1, RAD(0.0f));
	} else if (vec3_dot(&rt, &normal) > 0.707f) {
		// Dominantly looking right.
		gs_matrix_rotaa4f(0, 0, 1, RAD(180.0f));
	}

	gs_matrix_scale3f(65535, 65535, 65535);

	gs_draw(GS_LINES, 0, 2);

	gs_matrix_pop();
	gs_set_scissor_rect(nullptr);
}

void OBS::Display::DrawRotationHandle(float rot, matrix4 &mtx)
{
	struct vec3 pos;
	vec3_set(&pos, 0.5f, 0.0f, 0.0f);
	vec3_transform(&pos, &pos, &mtx);

	gs_load_vertexbuffer(m_rotHandleLine->Update(false));

	gs_matrix_push();
	gs_matrix_identity();
	gs_matrix_translate(&pos);

	gs_matrix_rotaa4f(0.0f, 0.0f, 1.0f, RAD(rot));
	gs_matrix_translate3f(-HANDLE_RADIUS * 1.5, -HANDLE_RADIUS * 1.5, 0.0f);
	gs_matrix_scale3f(HANDLE_RADIUS * 3, HANDLE_RADIUS * 3, 1.0f);

	gs_draw(GS_TRISTRIP, 0, 0);

	gs_matrix_translate3f(0.0f, -HANDLE_RADIUS * 0.6, 0.0f);

	gs_load_vertexbuffer(m_rotHandleCircle->Update(false));
	gs_draw(GS_TRISTRIP, 0, 0);

	gs_matrix_pop();
}

void OBS::Display::DrawOutline(const matrix4 &mtx, const obs_sceneitem_crop &crop, const vec2 &boxScale, gs_eparam_t *color)
{
	gs_matrix_push();
	gs_matrix_mul(&mtx);

	if (crop.left) {
		gs_effect_set_vec4(color, &m_cropOutlineColorVec4);
		DrawCropOutline(0.0f, 0.0f, 0.0f, 1.0f, boxScale);
	} else {
		gs_effect_set_vec4(color, &m_outlineColorVec4);
		DrawSolidOutline(m_leftSolidOutline.get());
	}
	if (crop.top) {
		gs_effect_set_vec4(color, &m_cropOutlineColorVec4);
		DrawCropOutline(0.0f, 0.0f, 1.0f, 0.0f, boxScale);
	} else {
		gs_effect_set_vec4(color, &m_outlineColorVec4);
		DrawSolidOutline(m_topSolidOutline.get());
	}
	if (crop.right) {
		gs_effect_set_vec4(color, &m_cropOutlineColorVec4);
		DrawCropOutline(1.0f, 0.0f, 1.0f, 1.0f, boxScale);
	} else {
		gs_effect_set_vec4(color, &m_outlineColorVec4);
		DrawSolidOutline(m_rightSolidOutline.get());
	}
	if (crop.bottom) {
		gs_effect_set_vec4(color, &m_cropOutlineColorVec4);
		DrawCropOutline(0.0f, 1.0f, 1.0f, 1.0f, boxScale);
	} else {
		gs_effect_set_vec4(color, &m_outlineColorVec4);
		DrawSolidOutline(m_bottomSolidOutline.get());
	}

	gs_matrix_pop();
}

bool OBS::Display::DrawSelectedSource(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	// This is partially code from OBS Studio. See window-basic-preview.cpp in obs-studio for copyright/license.
	if (obs_sceneitem_locked(item))
		return true;

	OBS::Display *dp = reinterpret_cast<OBS::Display *>(param);

	if (dp->m_canvas != obs_sceneitem_get_canvas(item))
		return true;

	obs_source_t *itemSource = obs_sceneitem_get_source(item);
	uint32_t flags = obs_source_get_output_flags(itemSource);
	bool isOnlyAudio = (flags & OBS_SOURCE_VIDEO) == 0;

	obs_source_t *sceneSource = obs_scene_get_source(scene);

	uint32_t sceneWidth = obs_source_get_width(sceneSource);
	uint32_t sceneHeight = obs_source_get_height(sceneSource);
	uint32_t itemWidth = obs_source_get_width(itemSource);
	uint32_t itemHeight = obs_source_get_height(itemSource);

	if (!obs_sceneitem_selected(item) || isOnlyAudio || ((itemWidth <= 0) && (itemHeight <= 0)))
		return true;

	matrix4 boxTransform;
	matrix4 invBoxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);
	matrix4_inv(&invBoxTransform, &boxTransform);

	{
		vec3 bounds[] = {
			{{{0.f, 0.f, 0.f}}},
			{{{1.f, 0.f, 0.f}}},
			{{{0.f, 1.f, 0.f}}},
			{{{1.f, 1.f, 0.f}}},
		};
		bool visible = std::all_of(std::begin(bounds), std::end(bounds), [&](const vec3 &b) {
			vec3 pos;
			vec3_transform(&pos, &b, &boxTransform);
			vec3_transform(&pos, &pos, &invBoxTransform);
			return CloseFloat(pos.x, b.x) && CloseFloat(pos.y, b.y);
		});

		if (!visible)
			return true;
	}

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *solid_color = gs_effect_get_param_by_name(solid, "color");

	float rot = obs_sceneitem_get_rot(item);
	bool rot45 = (rot == 45.0f || rot == 135.0f || rot == 225.0f || rot == 315.0f);

	// Prepare data for outline
	matrix4 curTransform;
	gs_matrix_get(&curTransform);

	vec2 boxScale;
	obs_sceneitem_get_box_scale(item, &boxScale);

	boxScale.x *= curTransform.x.x;
	boxScale.y *= curTransform.y.y;

	obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(item, &crop);

	dp->DrawOutline(boxTransform, crop, boxScale, solid_color);

	if (dp->m_drawGuideLines) {
		gs_load_vertexbuffer(dp->m_boxLine->Update(false));
		gs_effect_set_vec4(solid_color, &dp->m_guidelineColorVec4);
		DrawGuideline(dp, rot45, 0.5, 0, boxTransform);
		DrawGuideline(dp, rot45, 0.5, 1, boxTransform);
		DrawGuideline(dp, rot45, 0, 0.5, boxTransform);
		DrawGuideline(dp, rot45, 1, 0.5, boxTransform);

		// TEXT RENDERING
		// THIS DESPERATELY NEEDS TO BE REWRITTEN INTO SHADER CODE
		// DO SO WHENEVER...
		GS::Vertex v(nullptr, nullptr, nullptr, nullptr, nullptr);

		matrix4 itemMatrix, sceneToView;
		obs_sceneitem_get_box_transform(item, &itemMatrix);
		matrix4_identity(&sceneToView);
		sceneToView.x.x = dp->m_worldToPreviewScale.x;
		sceneToView.y.y = dp->m_worldToPreviewScale.y;

		// Retrieve actual corner and edge positions.
		vec3 edge[4], center;
		{
			vec3_set(&edge[0], 0, 0.5, 0);
			vec3_transform(&edge[0], &edge[0], &itemMatrix);
			vec3_set(&edge[1], 0.5, 0, 0);
			vec3_transform(&edge[1], &edge[1], &itemMatrix);
			vec3_set(&edge[2], 1, 0.5, 0);
			vec3_transform(&edge[2], &edge[2], &itemMatrix);
			vec3_set(&edge[3], 0.5, 1, 0);
			vec3_transform(&edge[3], &edge[3], &itemMatrix);

			vec3_set(&center, 0.5, 0.5, 0);
			vec3_transform(&center, &center, &itemMatrix);
			;
		}

		std::vector<char> buf(8);
		float_t pt = 8 * dp->m_previewToWorldScale.y;
		for (size_t n = 0; n < 4; n++) {
			bool isIn = (edge[n].x >= 0) && (edge[n].x < sceneWidth) && (edge[n].y >= 0) && (edge[n].y < sceneHeight);

			if (!isIn)
				continue;

			vec3 alignLeft, alignTop;

			if (rot45) {
				alignLeft = {-1, (float)-0.2, 0};
				alignTop = {(float)0.2, -1, 0};
			} else {
				alignLeft = {-1, 0, 0};
				alignTop = {0, -1, 0};
			}

			vec3 temp;
			vec3_sub(&temp, &edge[n], &center);
			vec3_norm(&temp, &temp);
			float left = vec3_dot(&temp, &alignLeft), top = vec3_dot(&temp, &alignTop);
			if (left > 0.707f) { // LEFT
				float_t dist = edge[n].x;
				if (dist > (pt * 4)) {
					size_t len = (size_t)snprintf(buf.data(), buf.size(), "%ld px", (uint32_t)dist);
					float_t offset = float((pt * len) / 2.0);

					for (size_t p = 0; p < len; p++) {
						char v = buf.data()[p];
						DrawGlyph(dp->m_textVertices, (edge[n].x / 2) - offset + (p * pt), edge[n].y - pt * 2, pt, 0, v,
							  dp->m_guidelineColor);
					}
				}
			} else if (left < -0.707f) { // RIGHT
				float_t dist = sceneWidth - edge[n].x;
				if (dist > (pt * 4)) {
					size_t len = (size_t)snprintf(buf.data(), buf.size(), "%ld px", (uint32_t)dist);
					float_t offset = float((pt * len) / 2.0);

					for (size_t p = 0; p < len; p++) {
						char v = buf.data()[p];
						DrawGlyph(dp->m_textVertices, edge[n].x + (dist / 2) - offset + (p * pt), edge[n].y - pt * 2, pt, 0, v,
							  dp->m_guidelineColor);
					}
				}
			} else if (top > 0.707f) { // UP
				float_t dist = edge[n].y;
				if (dist > pt) {
					size_t len = (size_t)snprintf(buf.data(), buf.size(), "%ld px", (uint32_t)dist);
					float_t offset = float((pt * len) / 2.0);

					for (size_t p = 0; p < len; p++) {
						char v = buf.data()[p];
						DrawGlyph(dp->m_textVertices, edge[n].x + (p * pt) + 15, edge[n].y - (dist / 2) - pt, pt, 0, v,
							  dp->m_guidelineColor);
					}
				}
			} else if (top < -0.707f) { // DOWN
				float_t dist = sceneHeight - edge[n].y;
				if (dist > (pt * 4)) {
					size_t len = (size_t)snprintf(buf.data(), buf.size(), "%ld px", (uint32_t)dist);
					float_t offset = float((pt * len) / 2.0);

					for (size_t p = 0; p < len; p++) {
						char v = buf.data()[p];
						DrawGlyph(dp->m_textVertices, edge[n].x + (p * pt) + 15, edge[n].y + (dist / 2) - pt, pt, 0, v,
							  dp->m_guidelineColor);
					}
				}
			}
		}
	}

	if (dp->m_drawRotationHandle) {
		gs_effect_set_vec4(solid_color, &dp->m_rotationHandleColorVec4);
		dp->DrawRotationHandle(rot, boxTransform);
	}

	gs_load_vertexbuffer(dp->m_boxTris->Update(false));
	gs_effect_set_vec4(solid_color, &dp->m_resizeInnerColorVec4);
	DrawSquareAt(dp, 0, 0, boxTransform);
	DrawSquareAt(dp, 1, 0, boxTransform);
	DrawSquareAt(dp, 0, 1, boxTransform);
	DrawSquareAt(dp, 1, 1, boxTransform);
	DrawSquareAt(dp, 0.5, 0, boxTransform);
	DrawSquareAt(dp, 0.5, 1, boxTransform);
	DrawSquareAt(dp, 0, 0.5, boxTransform);
	DrawSquareAt(dp, 1, 0.5, boxTransform);

	gs_load_vertexbuffer(dp->m_boxLine->Update(false));
	gs_effect_set_vec4(solid_color, &dp->m_resizeOuterColorVec4);
	DrawBoxAt(dp, 0, 0, boxTransform);
	DrawBoxAt(dp, 1, 0, boxTransform);
	DrawBoxAt(dp, 0, 1, boxTransform);
	DrawBoxAt(dp, 1, 1, boxTransform);
	DrawBoxAt(dp, 0.5, 0, boxTransform);
	DrawBoxAt(dp, 0.5, 1, boxTransform);
	DrawBoxAt(dp, 0, 0.5, boxTransform);
	DrawBoxAt(dp, 1, 0.5, boxTransform);

	return true;
}

bool OBS::Display::DrawSelectedOverflow(obs_scene_t *scene, obs_sceneitem_t *item, void *param)
{
	if (obs_sceneitem_locked(item))
		return true;

	OBS::Display *dp = reinterpret_cast<OBS::Display *>(param);

	if (dp->m_canvas != obs_sceneitem_get_canvas(item))
		return true;

	obs_source_t *itemSource = obs_sceneitem_get_source(item);
	uint32_t flags = obs_source_get_output_flags(itemSource);
	bool isOnlyAudio = (flags & OBS_SOURCE_VIDEO) == 0;

	obs_source_t *sceneSource = obs_scene_get_source(scene);

	uint32_t sceneWidth = obs_source_get_width(sceneSource);
	uint32_t sceneHeight = obs_source_get_height(sceneSource);
	uint32_t itemWidth = obs_source_get_width(itemSource);
	uint32_t itemHeight = obs_source_get_height(itemSource);

	if (!obs_sceneitem_selected(item) || isOnlyAudio || ((itemWidth <= 0) && (itemHeight <= 0)))
		return true;

	matrix4 boxTransform;
	matrix4 invBoxTransform;
	obs_sceneitem_get_box_transform(item, &boxTransform);
	matrix4_inv(&invBoxTransform, &boxTransform);

	vec3 bounds[] = {
		{{{0.f, 0.f, 0.f}}},
		{{{1.f, 0.f, 0.f}}},
		{{{0.f, 1.f, 0.f}}},
		{{{1.f, 1.f, 0.f}}},
	};

	bool visible = std::all_of(std::begin(bounds), std::end(bounds), [&](const vec3 &b) {
		vec3 pos;
		vec3_transform(&pos, &b, &boxTransform);
		vec3_transform(&pos, &pos, &invBoxTransform);
		return CloseFloat(pos.x, b.x) && CloseFloat(pos.y, b.y);
	});

	if (!visible)
		return true;

	gs_effect_t *repeat = obs_get_base_effect(OBS_EFFECT_REPEAT);
	gs_eparam_t *image = gs_effect_get_param_by_name(repeat, "image");
	gs_eparam_t *scale = gs_effect_get_param_by_name(repeat, "scale");

	vec2 s;
	vec2_set(&s, boxTransform.x.x / 96, boxTransform.y.y / 96);

	gs_effect_set_vec2(scale, &s);

	gs_texture_t *texture = (dp->m_dayTheme) ? dp->m_overflowDayTexture : dp->m_overflowNightTexture;
	gs_effect_set_texture(image, texture);

	gs_matrix_push();
	gs_matrix_mul(&boxTransform);

	obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(item, &crop);

	while (gs_effect_loop(repeat, "Draw")) {
		gs_draw_sprite(texture, 0, 1, 1);
	}

	gs_matrix_pop();

	return true;
}

void OBS::Display::DisplayCallback(void *displayPtr, uint32_t cx, uint32_t cy)
{
	Display *dp = static_cast<Display *>(displayPtr);
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *solid_color = gs_effect_get_param_by_name(solid, "color");
	gs_technique_t *solid_tech = gs_effect_get_technique(solid, "Solid");

	if (dp->m_canvas)
		obs_set_video_rendering_canvas(dp->m_canvas);

	dp->UpdatePreviewArea();

	// Get proper source/base size.
	uint32_t sourceW = 0, sourceH = 0;
	if (dp->m_source) {
		sourceW = obs_source_get_width(dp->m_source);
		sourceH = obs_source_get_height(dp->m_source);
	}
	if (sourceW <= 1 || sourceH <= 1) {
		if (dp->m_canvas) {
			sourceW = dp->m_canvas->base_width;
			sourceH = dp->m_canvas->base_height;
		}
	}

	if (sourceW == 0)
		sourceW = 1;
	if (sourceH == 0)
		sourceH = 1;

	// Get a source and its scene for the UI effects
	obs_source_t *source = dp->GetSourceForUIEffects();

	/* This should work for both individual sources
	 * that are actually scenes and our main transition scene */
	obs_scene_t *scene = (source) ? obs_scene_from_source(source) : nullptr;

	gs_viewport_push();
	gs_projection_push();

	//------------------------------------------------------------------------------

	// Padding
	gs_clear(GS_CLEAR_COLOR | GS_CLEAR_DEPTH | GS_CLEAR_STENCIL, &dp->m_paddingColorVec4, 100, 0);

	//------------------------------------------------------------------------------

	// Overflow effect
	if (scene && dp->m_shouldDrawUI) {

		uint32_t width, height;
		obs_display_size(dp->m_display, &width, &height);
		float right = float(width) - dp->m_previewOffset.first;
		float bottom = float(height) - dp->m_previewOffset.second;

		gs_ortho(-float(dp->m_previewOffset.first), right, -float(dp->m_previewOffset.second), bottom, -100.0f, 100.0f);

		gs_matrix_push();
		gs_matrix_scale3f(dp->m_worldToPreviewScale.x, dp->m_worldToPreviewScale.y, 1.0f);
		obs_scene_enum_items(scene, DrawSelectedOverflow, dp);
		gs_matrix_pop();
	}

	//------------------------------------------------------------------------------

	gs_ortho(0.0f, float(sourceW), 0.0f, float(sourceH), -100.0f, 100.0f);

	gs_set_viewport(dp->m_previewOffset.first, dp->m_previewOffset.second, dp->m_previewSize.first, dp->m_previewSize.second);

	// Background
	if (dp->m_boxTris) {
		gs_effect_set_vec4(solid_color, &dp->m_backgroundColorVec4);

		gs_technique_begin(solid_tech);
		gs_technique_begin_pass(solid_tech, 0);

		gs_matrix_push();
		gs_matrix_identity();
		gs_matrix_scale3f(float(sourceW), float(sourceH), 1.0f);

		gs_load_vertexbuffer(dp->m_boxTris->Update(false));
		gs_draw(GS_TRISTRIP, 0, 0);

		gs_matrix_pop();

		gs_technique_end_pass(solid_tech);
		gs_technique_end(solid_tech);
	}

	//------------------------------------------------------------------------------

	// Source Rendering
	if (dp->m_source) {
		/* If the source is a transition it means this display
		 * is for Studio Mode and that the scene it contains is a
		 * duplicate of the current scene, apply selective recording
		 * layer rendering if it is enabled */
		if (obs_get_multiple_rendering() && obs_source_get_type(dp->m_source) == OBS_SOURCE_TYPE_TRANSITION)
			obs_set_video_rendering_mode(dp->m_renderingMode);
		obs_source_video_render(dp->m_source);
	} else {
		obs_render_texture(dp->m_canvas, dp->m_renderingMode);
	}

	//------------------------------------------------------------------------------

	// The other UI effects
	if (scene && dp->m_shouldDrawUI) {

		// Display-Aligned Drawing
		vec2 tlCorner = {(float)-dp->m_previewOffset.first, (float)-dp->m_previewOffset.second};
		vec2 brCorner = {(float)(cx - dp->m_previewOffset.first), (float)(cy - dp->m_previewOffset.second)};
		vec2_mul(&tlCorner, &tlCorner, &dp->m_previewToWorldScale);
		vec2_mul(&brCorner, &brCorner, &dp->m_previewToWorldScale);

		gs_ortho(tlCorner.x, brCorner.x, tlCorner.y, brCorner.y, -100.0f, 100.0f);
		gs_reset_viewport();

		dp->m_textVertices->Resize(0);

		gs_technique_begin(solid_tech);
		gs_technique_begin_pass(solid_tech, 0);

		obs_scene_enum_items(scene, DrawSelectedSource, dp);

		gs_technique_end_pass(solid_tech);
		gs_technique_end(solid_tech);

		// Text Rendering
		if (dp->m_textVertices->Size() > 0) {
			gs_vertbuffer_t *vb = dp->m_textVertices->Update();
			while (gs_effect_loop(dp->m_textEffect, "Draw")) {
				gs_effect_set_texture(gs_effect_get_param_by_name(dp->m_textEffect, "image"), dp->m_textTexture);
				gs_load_vertexbuffer(vb);
				gs_load_indexbuffer(nullptr);
				gs_draw(GS_TRIS, 0, (uint32_t)dp->m_textVertices->Size());
			}
		}
	}

	obs_source_release(source);
	gs_projection_pop();
	gs_viewport_pop();
}

obs_source_t *OBS::Display::GetSourceForUIEffects()
{
	obs_source_t *source = nullptr;
	if (m_source) {
		/* If we want to draw UI effects, we need a scene,
		 * not a transition. This may not be a scene which
		 * we'll check later. */
		if (obs_source_get_type(m_source) == OBS_SOURCE_TYPE_TRANSITION) {
			source = obs_transition_get_active_source(m_source);
		} else {
			source = m_source;
			obs_source_get_ref(source);
		}
	} else {
		/* Here we assume that channel 0 holds the primary transition.
		* We also assume that the active source within that transition is
		* the scene that we need */
		obs_source_t *transition = obs_get_output_source(0);
		source = obs_transition_get_active_source(transition);
		obs_source_release(transition);
	}
	return source;
}

void OBS::Display::UpdatePreviewArea()
{
	uint32_t offsetX = 0, offsetY = 0;
	uint32_t sourceW = 0, sourceH = 0;
	if (m_source) {
		sourceW = obs_source_get_width(m_source);
		sourceH = obs_source_get_height(m_source);
	}
	if (sourceW <= 1 || sourceH <= 1) {
		if (m_canvas) {
			sourceW = m_canvas->base_width;
			sourceH = m_canvas->base_height;
		}
	}

	if (sourceW == 0)
		sourceW = 1;
	if (sourceH == 0)
		sourceH = 1;

	RecalculateApectRatioConstrainedSize(m_gsInitData.cx, m_gsInitData.cy, sourceW, sourceH, m_previewOffset.first, m_previewOffset.second,
					     m_previewSize.first, m_previewSize.second);

	offsetX = m_paddingSize;
	offsetY = static_cast<uint32_t>(float_t(offsetX) * float_t(sourceH) / float_t(sourceW));

	m_previewOffset.first += offsetX;
	m_previewSize.first -= offsetX * 2;

	if (m_previewSize.second <= offsetY * 2) {
		m_previewOffset.second = (m_previewOffset.second - 1) / 2;
		m_previewSize.second = 1;
	} else {
		m_previewOffset.second += offsetY;
		m_previewSize.second -= offsetY * 2;
	}

	m_worldToPreviewScale.x = float_t(m_previewSize.first) / float_t(sourceW);
	m_worldToPreviewScale.y = float_t(m_previewSize.second) / float_t(sourceH);
	m_previewToWorldScale.x = float_t(sourceW) / float_t(m_previewSize.first);
	m_previewToWorldScale.y = float_t(sourceH) / float_t(m_previewSize.second);
}

#if defined(_WIN32)

bool OBS::Display::DisplayWndClassRegistered;

WNDCLASSEX OBS::Display::DisplayWndClassObj;

ATOM OBS::Display::DisplayWndClassAtom;

std::mutex displayWndClassMutex; // Global or class-level static mutex

void OBS::Display::DisplayWndClass()
{
	std::lock_guard<std::mutex> lock(displayWndClassMutex);
	if (DisplayWndClassRegistered)
		return;

	DWORD style = CS_NOCLOSE | CS_HREDRAW | CS_VREDRAW; // CS_DBLCLKS | CS_HREDRAW | CS_NOCLOSE | CS_VREDRAW | CS_OWNDC;
	BOOL enabled = FALSE;
	DwmIsCompositionEnabled(&enabled);

	if (IsWindows8OrGreater() || !enabled) {
		style |= CS_OWNDC;
	}

	DisplayWndClassObj.cbSize = sizeof(WNDCLASSEX);
	DisplayWndClassObj.style = style;
	DisplayWndClassObj.lpfnWndProc = DisplayWndProc;
	DisplayWndClassObj.cbClsExtra = 0;
	DisplayWndClassObj.cbWndExtra = 0;
	DisplayWndClassObj.hInstance = GetModuleHandle(NULL); // HINST_THISCOMPONENT;
	DisplayWndClassObj.hIcon = NULL;
	DisplayWndClassObj.hCursor = NULL;
	DisplayWndClassObj.hbrBackground = NULL;
	DisplayWndClassObj.lpszMenuName = NULL;
	DisplayWndClassObj.lpszClassName = TEXT("Win32DisplayClass");
	DisplayWndClassObj.hIconSm = NULL;

	DisplayWndClassAtom = RegisterClassEx(&DisplayWndClassObj);
	if (DisplayWndClassAtom == NULL) {
		HandleWin32ErrorMessage(GetLastError());
	}

	DisplayWndClassRegistered = true;
}

LRESULT CALLBACK OBS::Display::DisplayWndProc(_In_ HWND hwnd, _In_ UINT uMsg, _In_ WPARAM wParam, _In_ LPARAM lParam)
{
	switch (uMsg) {
	case WM_NCHITTEST:
		return HTTRANSPARENT;
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

#endif

bool OBS::Display::GetDrawGuideLines(void)
{
	return m_drawGuideLines;
}

void OBS::Display::SetDrawGuideLines(bool drawGuideLines)
{
	m_drawGuideLines = drawGuideLines;
}

bool OBS::Display::GetDrawRotationHandle()
{
	return m_drawRotationHandle;
}

void OBS::Display::SetDrawRotationHandle(bool drawRotationHandle)
{
	m_drawRotationHandle = drawRotationHandle;
}
