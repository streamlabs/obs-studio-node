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

#include "nodeobs_api.h"
#include "osn-source.hpp"
#include "osn-scene.hpp"
#include "osn-sceneitem.hpp"
#include "osn-input.hpp"
#include "osn-transition.hpp"
#include "osn-filter.hpp"
#include "osn-volmeter.hpp"
#include "osn-fader.hpp"
#include "nodeobs_autoconfig.h"
#include "util/lexer.h"
#include "util-crashmanager.h"
#include "util-metricsprovider.h"

#include "osn-streaming.hpp"
#include "osn-recording.hpp"
#include "osn-video-encoder.hpp"
#include "osn-audio-encoder.hpp"
#include "osn-service.hpp"
#include "osn-delay.hpp"
#include "osn-reconnect.hpp"
#include "osn-network.hpp"
#include "osn-audio-track.hpp"
#include "memory-manager.h"

#include <sys/types.h>

#ifdef __APPLE
#include <unistd.h>
#endif

#ifdef _WIN32

#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif

#define _WIN32_WINNT 0x0502

#include <ShlObj.h>
#include <codecvt>
#include <locale>
#include <mutex>
#include <string>
#endif
#include "nodeobs_content.h"

#ifdef _MSC_VER
#include <direct.h>
#define getcwd _getcwd
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifdef WIN32
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <util/windows/ComPtr.hpp>
#include <util/windows/HRError.hpp>
#include <util/windows/WinHandle.hpp>
#endif

#ifdef __APPLE__
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

#include "osn-error.hpp"
#include "shared.hpp"

#include <fstream>

#define BUFFSIZE 512
#define CONNECTING_STATE 0
#define READING_STATE 1
#define MBYTE (1024ULL * 1024ULL)
#define GBYTE (1024ULL * 1024ULL * 1024ULL)
#define TBYTE (1024ULL * 1024ULL * 1024ULL * 1024ULL)

enum crashHandlerCommand {
	REGISTER = 0,
	UNREGISTER = 1,
	REGISTERMEMORYDUMP = 2,
	CRASHWITHCODE = 3,
	CRASHED_MODULE_INFO = 4,
};

struct NodeOBSLogParam final {
	std::fstream logStream;
	bool enableDebugLogs = true;
};

std::string g_moduleDirectory = "";
os_cpu_usage_info_t *cpuUsageInfo = nullptr;
#ifdef WIN32
std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
HMODULE hRtwq;
#endif
std::string slobs_plugin;
std::vector<std::pair<std::string, obs_module_t *>> obsModules;
OBS_API::LogReport logReport;
OBS_API::OutputStats streamingOutputStatsMain;
OBS_API::OutputStats streamingOutputStatsSecondary;
OBS_API::OutputStats recordingOutputStats;
std::mutex logMutex;
std::string currentVersion;
std::string username("unknown");
std::chrono::high_resolution_clock::time_point start_wait_acknowledge;

ipc::server *g_server = nullptr;

static bool browserAccel = true;
static bool mediaFileCaching = true;
static uint32_t sdrWhiteLevel = 300;
static uint32_t hdrNominalPeakLevel = 1000;
static bool lowLatencyAudioBuffering = false;
static bool forceGPURendering = true;
static std::string processPriority = "Normal";

void OBS_API::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("API");

	cls->register_function(std::make_shared<ipc::function>(
		"OBS_API_initAPI", std::vector<ipc::type>{ipc::type::String, ipc::type::String, ipc::type::String, ipc::type::String}, OBS_API_initAPI));
	cls->register_function(std::make_shared<ipc::function>("OBS_API_destroyOBS_API", std::vector<ipc::type>{}, OBS_API_destroyOBS_API));
	cls->register_function(std::make_shared<ipc::function>("OBS_API_getPerformanceStatistics", std::vector<ipc::type>{}, OBS_API_getPerformanceStatistics));
	cls->register_function(std::make_shared<ipc::function>("SetWorkingDirectory", std::vector<ipc::type>{ipc::type::String}, SetWorkingDirectory));
	cls->register_function(std::make_shared<ipc::function>("StopCrashHandler", std::vector<ipc::type>{}, StopCrashHandler));
	cls->register_function(std::make_shared<ipc::function>("OBS_API_QueryHotkeys", std::vector<ipc::type>{}, QueryHotkeys));
	cls->register_function(std::make_shared<ipc::function>("OBS_API_ProcessHotkeyStatus", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int32},
							       ProcessHotkeyStatus));
	cls->register_function(std::make_shared<ipc::function>("SetUsername", std::vector<ipc::type>{ipc::type::String}, SetUsername));
	cls->register_function(std::make_shared<ipc::function>("OBS_API_forceCrash", std::vector<ipc::type>{}, OBS_API_forceCrash));
	cls->register_function(std::make_shared<ipc::function>("SetBrowserAcceleration", std::vector<ipc::type>{ipc::type::UInt32}, SetBrowserAcceleration));
	cls->register_function(std::make_shared<ipc::function>("GetBrowserAcceleration", std::vector<ipc::type>{}, GetBrowserAcceleration));
	cls->register_function(std::make_shared<ipc::function>("GetBrowserAccelerationLegacy", std::vector<ipc::type>{}, GetBrowserAccelerationLegacy));
	cls->register_function(std::make_shared<ipc::function>("SetMediaFileCaching", std::vector<ipc::type>{ipc::type::UInt32}, SetMediaFileCaching));
	cls->register_function(std::make_shared<ipc::function>("GetMediaFileCaching", std::vector<ipc::type>{}, GetMediaFileCaching));
	cls->register_function(std::make_shared<ipc::function>("GetMediaFileCachingLegacy", std::vector<ipc::type>{}, GetMediaFileCachingLegacy));
	cls->register_function(std::make_shared<ipc::function>("SetProcessPriority", std::vector<ipc::type>{ipc::type::String}, SetProcessPriority));
	cls->register_function(std::make_shared<ipc::function>("GetProcessPriority", std::vector<ipc::type>{}, GetProcessPriority));
	cls->register_function(std::make_shared<ipc::function>("GetProcessPriorityLegacy", std::vector<ipc::type>{}, GetProcessPriorityLegacy));
	cls->register_function(std::make_shared<ipc::function>("OBS_API_forceCrash", std::vector<ipc::type>{}, OBS_API_forceCrash));
	cls->register_function(std::make_shared<ipc::function>("GetSdrWhiteLevel", std::vector<ipc::type>{}, GetSdrWhiteLevel));
	cls->register_function(std::make_shared<ipc::function>("SetSdrWhiteLevel", std::vector<ipc::type>{}, SetSdrWhiteLevel));
	cls->register_function(std::make_shared<ipc::function>("GetSdrWhiteLevelLegacy", std::vector<ipc::type>{}, GetSdrWhiteLevelLegacy));
	cls->register_function(std::make_shared<ipc::function>("GetHdrNominalPeakLevel", std::vector<ipc::type>{}, GetHdrNominalPeakLevel));
	cls->register_function(std::make_shared<ipc::function>("SetHdrNominalPeakLevel", std::vector<ipc::type>{}, SetHdrNominalPeakLevel));
	cls->register_function(std::make_shared<ipc::function>("GetHdrNominalPeakLevelLegacy", std::vector<ipc::type>{}, GetHdrNominalPeakLevelLegacy));
	cls->register_function(std::make_shared<ipc::function>("GetLowLatencyAudioBuffering", std::vector<ipc::type>{}, GetLowLatencyAudioBuffering));
	cls->register_function(std::make_shared<ipc::function>("SetLowLatencyAudioBuffering", std::vector<ipc::type>{}, SetLowLatencyAudioBuffering));
	cls->register_function(
		std::make_shared<ipc::function>("GetLowLatencyAudioBufferingLegacy", std::vector<ipc::type>{}, GetLowLatencyAudioBufferingLegacy));
	cls->register_function(std::make_shared<ipc::function>("GetForceGPURendering", std::vector<ipc::type>{}, GetForceGPURendering));
	cls->register_function(std::make_shared<ipc::function>("SetForceGPURendering", std::vector<ipc::type>{}, SetForceGPURendering));
	cls->register_function(std::make_shared<ipc::function>("GetForceGPURenderingLegacy", std::vector<ipc::type>{}, GetForceGPURenderingLegacy));

	srv.register_collection(cls);
	g_server = &srv;
}

void replaceAll(std::string &str, const std::string &from, const std::string &to)
{
	if (from.empty())
		return;
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
		str.replace(start_pos, from.length(), to);
		start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
	}
};

void OBS_API::SetWorkingDirectory(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	g_moduleDirectory = args[0].value_str;
	replaceAll(g_moduleDirectory, "\\", "/");
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(g_moduleDirectory));
	AUTO_DEBUG;
}

#ifdef _WIN32
static void SetPrivilegeForGPUPriority(void)
{
	const DWORD flags = TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY;
	TOKEN_PRIVILEGES tp;
	HANDLE token;
	LUID val;

	if (!OpenProcessToken(GetCurrentProcess(), flags, &token)) {
		return;
	}

	if (!!LookupPrivilegeValue(NULL, SE_INC_BASE_PRIORITY_NAME, &val)) {
		tp.PrivilegeCount = 1;
		tp.Privileges[0].Luid = val;
		tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

		if (!AdjustTokenPrivileges(token, false, &tp, sizeof(tp), NULL, NULL)) {
			blog(LOG_INFO, "Could not set privilege to increase GPU priority");
		}
	}

	CloseHandle(token);
}
#endif

static std::string GenerateTimeDateFilename(const char *extension)
{
	time_t now = time(0);
	char file[256] = {};
	struct tm *cur_time;

	cur_time = localtime(&now);
	snprintf(file, sizeof(file), "%d-%02d-%02d %02d-%02d-%02d.%s", cur_time->tm_year + 1900, cur_time->tm_mon + 1, cur_time->tm_mday, cur_time->tm_hour,
		 cur_time->tm_min, cur_time->tm_sec, extension);

	return std::string(file);
}

static bool GetToken(lexer *lex, std::string &str, base_token_type type)
{
	base_token token;
	if (!lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
		return false;
	if (token.type != type)
		return false;

	str.assign(token.text.array, token.text.len);
	return true;
}

static bool ExpectToken(lexer *lex, const char *str, base_token_type type)
{
	base_token token;
	if (!lexer_getbasetoken(lex, &token, IGNORE_WHITESPACE))
		return false;
	if (token.type != type)
		return false;

	return strref_cmp(&token.text, str) == 0;
}

/* os_dirent mimics POSIX dirent structure.
* Perhaps a better cross-platform solution can take
* place but this is as cross-platform as it gets
* for right now.  */
static uint64_t ConvertLogName(const char *name)
{
	lexer lex;
	std::string year, month, day, hour, minute, second;

	lexer_init(&lex);
	lexer_start(&lex, name);

	if (!GetToken(&lex, year, BASETOKEN_DIGIT))
		return 0;
	if (!ExpectToken(&lex, "-", BASETOKEN_OTHER))
		return 0;
	if (!GetToken(&lex, month, BASETOKEN_DIGIT))
		return 0;
	if (!ExpectToken(&lex, "-", BASETOKEN_OTHER))
		return 0;
	if (!GetToken(&lex, day, BASETOKEN_DIGIT))
		return 0;
	if (!GetToken(&lex, hour, BASETOKEN_DIGIT))
		return 0;
	if (!ExpectToken(&lex, "-", BASETOKEN_OTHER))
		return 0;
	if (!GetToken(&lex, minute, BASETOKEN_DIGIT))
		return 0;
	if (!ExpectToken(&lex, "-", BASETOKEN_OTHER))
		return 0;
	if (!GetToken(&lex, second, BASETOKEN_DIGIT))
		return 0;

	std::string timestring(year);
	timestring += month + day + hour + minute + second;
	lexer_free(&lex);
	return std::stoull(timestring);
}

static void DeleteOldestFile(const char *location, unsigned maxLogs)
{
	std::string oldestLog;
	uint64_t oldest_ts = (uint64_t)-1;
	struct os_dirent *entry;

	os_dir_t *dir = os_opendir(location);

	if (!dir) {
		std::cout << "Failed to open log directory." << std::endl;
	}

	unsigned count = 0;

	while ((entry = os_readdir(dir)) != NULL) {
		if (entry->directory || *entry->d_name == '.')
			continue;

		uint64_t ts = ConvertLogName(entry->d_name);

		if (ts) {
			if (ts < oldest_ts) {
				oldestLog = entry->d_name;
				oldest_ts = ts;
			}

			count++;
		}
	}

	os_closedir(dir);

	if (count > maxLogs) {
		std::string delPath;

		delPath = delPath + location + "/" + oldestLog;
		os_unlink(delPath.c_str());
	}
}

#include <chrono>
#include <cstdarg>
#include <stdarg.h>

#ifdef _WIN32
#include <io.h>
#include <stdio.h>
#else
#include <unistd.h>
#endif
#include <osn-error.hpp>

outdated_driver_error *outdated_driver_error::inst = nullptr;

outdated_driver_error *outdated_driver_error::instance()
{
	if (inst == nullptr) {
		inst = new outdated_driver_error();
	}
	return inst;
}

void outdated_driver_error::set_active(bool state)
{
	if (state) {
		if (!lookup_enabled) {
			line_1 = "";
			line_2 = "";
		}
		lookup_enabled++;
	} else {
		lookup_enabled--;
	}
}

std::string outdated_driver_error::get_error()
{
	if (line_1.size() && line_2.size())
		return line_1 + std::string("\n") + line_2;
	else
		return "";
}

void outdated_driver_error::catch_error(const char *msg)
{
	if (!lookup_enabled)
		return;

	const std::string msg_string = msg;

	if (line_1.size() == 0) {
		const std::string line_1_pattern = "Driver does not support the required nvenc API version";
		auto pattern_position = msg_string.find(line_1_pattern);
		if (pattern_position != std::string::npos) {
			line_1 = msg_string.substr(pattern_position);
			return;
		}
	} else if (line_2.size() == 0) {
		const std::string line_2_pattern = "The minimum required Nvidia driver for nvenc";
		auto pattern_position = msg_string.find(line_2_pattern);
		if (pattern_position != std::string::npos) {
			line_2 = msg_string.substr(pattern_position);
		}
	}
}

static std::vector<char> nodeobs_log_formatted_message(const char *format, va_list args)
{
	if (!format)
		return std::vector<char>();
#ifdef WIN32
	int length = _vscprintf(format, args);
#else
	va_list argcopy;
	va_copy(argcopy, args);
	int length = vsnprintf(NULL, 0, format, argcopy);
	va_end(argcopy);
#endif
	if (length <= 0)
		return std::vector<char>();
	std::vector<char> buf = std::vector<char>(length + 1, '\0');
	int written = vsprintf(buf.data(), format, args);
	if (written <= 0)
		return std::vector<char>();
	buf.resize(written);
	return buf;
}

std::chrono::high_resolution_clock hrc;
std::chrono::high_resolution_clock::time_point tp = std::chrono::high_resolution_clock::now();
static void node_obs_log(int log_level, const char *msg, va_list args, void *param)
{
	if (param == nullptr)
		return;

	// Calculate log time.
	auto timeSinceStart = (std::chrono::high_resolution_clock::now() - tp);
	auto days = std::chrono::duration_cast<std::chrono::duration<int, std::ratio<86400>>>(timeSinceStart);
	timeSinceStart -= days;
	auto hours = std::chrono::duration_cast<std::chrono::hours>(timeSinceStart);
	timeSinceStart -= hours;
	auto minutes = std::chrono::duration_cast<std::chrono::minutes>(timeSinceStart);
	timeSinceStart -= minutes;
	auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeSinceStart);
	timeSinceStart -= seconds;
	auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(timeSinceStart);
	timeSinceStart -= milliseconds;
	auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(timeSinceStart);
	timeSinceStart -= microseconds;
	auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(timeSinceStart);

	// Generate timestamp and log_level part.
	/// Convert level int to human readable name
	std::string_view levelname("");
	switch (log_level) {
	case LOG_INFO:
		levelname = "Info";
		break;
	case LOG_WARNING:
		levelname = "Warning";
		break;
	case LOG_ERROR:
		levelname = "Error";
		break;
	case LOG_DEBUG:
		levelname = "Debug";
		break;
	default:
		if (log_level <= 50) {
			levelname = "Critical";
		} else if (log_level > 50 && log_level < LOG_ERROR) {
			levelname = "Error";
		} else if (log_level > LOG_ERROR && log_level < LOG_WARNING) {
			levelname = "Alert";
		} else if (log_level > LOG_WARNING && log_level < LOG_INFO) {
			levelname = "Hint";
		} else if (log_level > LOG_INFO) {
			levelname = "Notice";
		}
		break;
	}

#ifdef WIN32
	std::string thread_id = std::to_string(GetCurrentThreadId());
#else
	// Not sure it is the best way for macOS.
	uint64_t tid;
	pthread_threadid_np(NULL, &tid);
	std::string thread_id = std::to_string(tid);
#endif

	std::array<char, 160> timebuf{};
	static const std::string_view timeformat("[%.3d:%.2d:%.2d:%.2d.%.3d.%.3d.%.3d][%*s][%*s]");
#ifdef WIN32
	int length = sprintf_s(timebuf.data(), timebuf.size(), timeformat.data(), days.count(), hours.count(), minutes.count(), seconds.count(),
			       milliseconds.count(), microseconds.count(), nanoseconds.count(), thread_id.length(), thread_id.c_str(), levelname.length(),
			       levelname.data());
#else
	int length = snprintf(timebuf.data(), timebuf.size(), timeformat.data(), days.count(), hours.count(), minutes.count(), seconds.count(),
			      milliseconds.count(), microseconds.count(), nanoseconds.count(), thread_id.length(), thread_id.c_str(), levelname.length(),
			      levelname.data());
#endif
	if (length < 0)
		return;

	std::string_view time_and_level(timebuf.data(), length);

	// Format incoming text
	std::vector<char> buf = nodeobs_log_formatted_message(msg, args);
	std::string_view text = (buf.size()) ? std::string_view(buf.data(), buf.size()) : std::string_view("");

	std::lock_guard<std::mutex> lock(logMutex);

	outdated_driver_error::instance()->catch_error(msg);
	NodeOBSLogParam *logParam = reinterpret_cast<NodeOBSLogParam *>(param);

	// Split by \n (new-line)
	size_t last_valid_idx = 0;
	for (size_t idx = 0; idx <= text.length(); idx++) {
		if ((idx == text.length()) || (text[idx] == '\n')) {
			std::string_view line = (idx > last_valid_idx) ? std::string_view(&text[last_valid_idx], idx - last_valid_idx) : std::string_view("");

			std::string newmsg;
			newmsg.reserve(time_and_level.size() + line.size() + 3);
			newmsg += time_and_level;
			newmsg += " ";
			newmsg += line;
			newmsg += '\n';

			last_valid_idx = idx + 1;

			// File Log
			if (log_level != LOG_DEBUG || logParam->enableDebugLogs) {
				logParam->logStream << newmsg << std::flush;
			}

			// Internal Log
			logReport.push(newmsg, log_level);

			// Std Out / Std Err
			/// Why fwrite and not std::cout and std::cerr?
			/// Well, it seems that std::cout and std::cerr break if you click in the console window and paste.
			/// Which is really bad, as nothing gets logged into the console anymore.
			if (log_level <= LOG_WARNING) {
				fwrite(newmsg.data(), sizeof(char), newmsg.length(), stderr);
			}
			fwrite(newmsg.data(), sizeof(char), newmsg.length(), stdout);

			// Debugger
#ifdef _WIN32
			if (IsDebuggerPresent()) {
				int wNum = MultiByteToWideChar(CP_UTF8, 0, newmsg.c_str(), -1, NULL, 0);
				if (wNum > 1) {
					std::wstring wide_buf;
					std::mutex wide_mutex;

					std::lock_guard<std::mutex> lock(wide_mutex);
					wide_buf.reserve(wNum + 1);
					wide_buf.resize(wNum - 1);
					MultiByteToWideChar(CP_UTF8, 0, newmsg.c_str(), -1, &wide_buf[0], wNum);

					OutputDebugStringW(wide_buf.c_str());
				}
			}
#endif
		}
	}
	if (log_level != LOG_DEBUG || logParam->enableDebugLogs) {
		logParam->logStream << std::flush;
	}

#if defined(_WIN32) && defined(OBS_DEBUGBREAK_ON_ERROR)
	if (log_level <= LOG_ERROR && IsDebuggerPresent())
		__debugbreak();
#endif
}

#ifdef WIN32
uint32_t pid = GetCurrentProcessId();
#else
uint32_t pid = (uint32_t)getpid();
#endif

std::vector<char> registerProcess(void)
{
	std::vector<char> buffer;
	uint8_t action = crashHandlerCommand::REGISTER;
	bool isCritical = true;
	buffer.resize(sizeof(action) + sizeof(isCritical) + sizeof(pid));

	uint32_t offset = 0;

	memcpy(buffer.data(), &action, sizeof(action));
	offset++;
	memcpy(buffer.data() + offset, &isCritical, sizeof(isCritical));
	offset++;
	memcpy(buffer.data() + offset, &pid, sizeof(pid));

	return buffer;
}

std::vector<char> registerMemoryDump(void)
{
	blog(LOG_INFO, "registerMemoryDump");
	const uint8_t action = crashHandlerCommand::REGISTERMEMORYDUMP;

	//str
	std::wstring eventName_Start = util::CrashManager::GetMemoryDumpEventName_Start();
	uint32_t eventName_Start_Size = (eventName_Start.size() + 1) * sizeof(wchar_t);

	//str
	std::wstring eventName_Fail = util::CrashManager::GetMemoryDumpEventName_Fail();
	uint32_t eventName_Fail_Size = (eventName_Fail.size() + 1) * sizeof(wchar_t);

	//str
	std::wstring eventName_Success = util::CrashManager::GetMemoryDumpEventName_Success();
	uint32_t eventName_Success_Size = (eventName_Success.size() + 1) * sizeof(wchar_t);

	//str
	std::wstring dumpPath = util::CrashManager::GetMemoryDumpPath();
	uint32_t dumpPathSize = (dumpPath.size() + 1) * sizeof(wchar_t);

	//str
	std::wstring dumpName = util::CrashManager::GetMemoryDumpName();
	uint32_t dumpNameSize = (dumpName.size() + 1) * sizeof(wchar_t);

	// Buffer
	std::vector<char> buffer;
	buffer.resize(sizeof(action) + sizeof(pid) + sizeof(int) + eventName_Start_Size + sizeof(int) + eventName_Fail_Size + sizeof(int) +
		      eventName_Success_Size + sizeof(int) + dumpPathSize + sizeof(int) + dumpNameSize);
	uint32_t offset = 0;

	//@uint32_t - pid
	memcpy(buffer.data(), &action, sizeof(action));
	offset++;
	memcpy(buffer.data() + offset, &pid, sizeof(pid));
	offset += sizeof(pid);

	//@str - eventName_Start
	memcpy(buffer.data() + offset, &eventName_Start_Size, sizeof(eventName_Start_Size));
	offset += sizeof(eventName_Start_Size);
	memcpy(buffer.data() + offset, &eventName_Start[0], eventName_Start_Size);
	offset += eventName_Start_Size;

	//@str - eventName_Fail
	memcpy(buffer.data() + offset, &eventName_Fail_Size, sizeof(eventName_Fail_Size));
	offset += sizeof(eventName_Fail_Size);
	memcpy(buffer.data() + offset, &eventName_Fail[0], eventName_Fail_Size);
	offset += eventName_Fail_Size;

	//@str - eventName_Success
	memcpy(buffer.data() + offset, &eventName_Success_Size, sizeof(eventName_Success_Size));
	offset += sizeof(eventName_Success_Size);
	memcpy(buffer.data() + offset, &eventName_Success[0], eventName_Success_Size);
	offset += eventName_Success_Size;

	//@str - dumpPath
	memcpy(buffer.data() + offset, &dumpPathSize, sizeof(dumpPathSize));
	offset += sizeof(dumpPathSize);
	memcpy(buffer.data() + offset, &dumpPath[0], dumpPathSize);
	offset += dumpPathSize;

	//@str - dumpName
	memcpy(buffer.data() + offset, &dumpNameSize, sizeof(dumpNameSize));
	offset += sizeof(dumpNameSize);
	memcpy(buffer.data() + offset, &dumpName[0], dumpNameSize);
	offset += dumpNameSize;

	return buffer;
}

std::vector<char> unregisterProcess(void)
{
	std::vector<char> buffer;
	uint8_t action = crashHandlerCommand::UNREGISTER;
	buffer.resize(sizeof(action) + sizeof(pid));

	uint32_t offset = 0;

	memcpy(buffer.data(), &action, sizeof(action));
	offset++;
	memcpy(buffer.data() + offset, &pid, sizeof(pid));

	return buffer;
}

std::vector<char> crashedProcess(uint32_t crash_id)
{
	std::vector<char> buffer;
	uint8_t action = crashHandlerCommand::CRASHWITHCODE;
	buffer.resize(sizeof(action) + sizeof(crash_id) + sizeof(pid));

	uint32_t offset = 0;

	memcpy(buffer.data(), &action, sizeof(action));
	offset++;
	memcpy(buffer.data() + offset, &crash_id, sizeof(crash_id));
	offset += sizeof(crash_id);
	memcpy(buffer.data() + offset, &pid, sizeof(pid));

	return buffer;
}

std::vector<char> crashedModuleInfo(const std::string &moduleName, const std::string &binaryPath)
{
	std::vector<char> buffer;

	// Prepare
	const std::uint8_t messageAction = crashHandlerCommand::CRASHED_MODULE_INFO;
	const std::uint32_t messageModuleNameSize = (moduleName.size() + 1) * sizeof(std::remove_reference<decltype(moduleName)>::type::value_type);
	const std::uint32_t messageBinaryPathSize = (binaryPath.size() + 1) * sizeof(std::remove_reference<decltype(binaryPath)>::type::value_type);

	buffer.resize(sizeof(messageAction) + sizeof(std::uint32_t) + messageModuleNameSize + sizeof(std::uint32_t) + messageBinaryPathSize);

	// Pack
	uint32_t offset = 0;
	memcpy(buffer.data(), &messageAction, sizeof(messageAction));
	offset++;

	memcpy(buffer.data() + offset, &messageModuleNameSize, sizeof(messageModuleNameSize));
	offset += sizeof(messageModuleNameSize);
	memcpy(buffer.data() + offset, moduleName.data(), messageModuleNameSize);
	offset += messageModuleNameSize;

	memcpy(buffer.data() + offset, &messageBinaryPathSize, sizeof(messageBinaryPathSize));
	offset += sizeof(messageBinaryPathSize);
	memcpy(buffer.data() + offset, binaryPath.data(), messageBinaryPathSize);
	offset += messageBinaryPathSize;

	return buffer;
}

#ifdef WIN32
std::wstring crash_handler_pipe;

void OBS_API::SetCrashHandlerPipe(const std::wstring &new_pipe)
{
	crash_handler_pipe = std::wstring(L"\\\\.\\pipe\\") + new_pipe + std::wstring(L"-crash-handler");
}

void writeCrashHandler(std::vector<char> buffer)
{
	HANDLE hPipe = CreateFile(crash_handler_pipe.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

	if (hPipe == INVALID_HANDLE_VALUE)
		hPipe = CreateFile(TEXT("\\\\.\\pipe\\slobs-crash-handler"), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

	if (hPipe == INVALID_HANDLE_VALUE)
		return;

	DWORD bytesWritten;

	WriteFile(hPipe, buffer.data(), buffer.size(), &bytesWritten, NULL);

	CloseHandle(hPipe);
}
#else
std::string crash_handler_pipe;

void OBS_API::SetCrashHandlerPipe(const std::wstring &new_pipe)
{
	crash_handler_pipe = std::string(new_pipe.begin(), new_pipe.end()) + std::string("-crash-handler");
}

void writeCrashHandler(std::vector<char> buffer)
{
	int file_descriptor = open(crash_handler_pipe.c_str(), O_WRONLY | O_DSYNC);
	if (file_descriptor < 0) {
		blog(LOG_DEBUG, "failed to open pipe %s ", crash_handler_pipe.c_str());
		file_descriptor = open("/tmp/slobs-crash-handler", O_WRONLY | O_DSYNC);
		if (file_descriptor < 0) {
			blog(LOG_DEBUG, "failed to open pipe /tmp/slobs-crash-handler ");
			return;
		}
	}

	::write(file_descriptor, buffer.data(), buffer.size());
	close(file_descriptor);
}
#endif

static bool checkIfDebugLogsEnabled(const std::string &appdata)
{
#if defined(_DEBUG)
	return true;
#else
	if (currentVersion.find("preview") != std::string::npos) {
		return true;
	}
	// When you change the environment variable and start Streamlabs Desktop
	// via a console/terminal, you may have to restart the console/terminal.
	// On macOS, execute "export SLOBS_PRODUCTION_DEBUG=true" before starting
	// Streamlabs Desktop via the console/terminal.
	// To set the environment variable globally on macOS, use the solution from the question here:
	// https://apple.stackexchange.com/questions/289060/setting-variables-in-environment-plist
	// Reboot is required!
	char *envValue = getenv("SLOBS_PRODUCTION_DEBUG");
	if (envValue != nullptr) {
		if (astrcmpi(envValue, "true") == 0) {
			return true;
		}
	}
	// Even if the environment variable is set "off"/"no" explicitly,
	// we ignore it and check for the file.
	const std::string filename = appdata + "/enable-debug-logs";
#if defined(_WIN32) && defined(UNICODE)
	return std::fstream(converter.from_bytes(filename).data()).is_open();
#else
	return std::fstream(filename).is_open();
#endif
#endif
}

void addModulePaths()
{
#if defined(_WIN32)
	obs_add_module_path(std::string(g_moduleDirectory + "/obs-plugins/64bit").c_str(),
			    std::string(g_moduleDirectory + "/data/obs-plugins/%module%").c_str());
	obs_add_module_path(std::string(slobs_plugin + "/obs-plugins/64bit").c_str(), std::string(slobs_plugin + "/data/obs-plugins/%module%").c_str());
#elif defined(__APPLE__)

#else

#endif
}

static void listEncoders(obs_encoder_type type)
{
	constexpr uint32_t hide_flags = OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL;

	size_t idx = 0;
	const char *encoder_type;

	while (obs_enum_encoder_types(idx++, &encoder_type)) {
		if (obs_get_encoder_caps(encoder_type) & hide_flags || obs_get_encoder_type(encoder_type) != type) {
			continue;
		}

		blog(LOG_INFO, "\t- %s (%s)", encoder_type, obs_encoder_get_display_name(encoder_type));
	}
};

static void logEncoders()
{
	blog(LOG_INFO, "Available Encoders:");
	blog(LOG_INFO, "  Video Encoders:");
	listEncoders(OBS_ENCODER_VIDEO);
	blog(LOG_INFO, "  Audio Encoders:");
	listEncoders(OBS_ENCODER_AUDIO);
}

void OBS_API::OBS_API_initAPI(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	writeCrashHandler(registerProcess());

	/* Map base DLLs as soon as possible into the current process space.
	* In particular, we need to load obs.dll into memory before we call
	* any functions from obs else if we delay-loaded the dll, it will
	* fail miserably. */

	/* FIXME These should be configurable */
	/* FIXME g_moduleDirectory really needs to be a wstring */
	std::string appdata = args[0].value_str;
	std::string locale = args[1].value_str;
	currentVersion = args[2].value_str;
	utility::osn_current_version(currentVersion);

	/* Logging */
	std::string filename = GenerateTimeDateFilename("txt");
	std::string log_path = appdata;
	log_path.append("/node-obs/logs/");

	/* Make sure the path is created
	before attempting to make a file there. */
	if (os_mkdirs(log_path.c_str()) == MKDIR_ERROR) {
		std::cerr << "Failed to open log file" << std::endl;
#ifdef WIN32
		util::CrashManager::AddWarning("Error on log file, failed to create path: " + log_path);
#endif
	}

	/* Delete oldest file in the folder to imitate rotating */
	DeleteOldestFile(log_path.c_str(), 3);
	log_path.append(filename);

	auto logParam = std::make_unique<NodeOBSLogParam>();
	logParam->enableDebugLogs = checkIfDebugLogsEnabled(appdata);

#if defined(_WIN32) && defined(UNICODE)
	logParam->logStream = std::fstream(converter.from_bytes(log_path.c_str()).c_str(), std::ios_base::out | std::ios_base::trunc);
#else
	logParam->logStream = std::fstream(log_path, std::ios_base::out | std::ios_base::trunc);
#endif
	if (!logParam->logStream.is_open()) {
		logParam.reset();
		util::CrashManager::AddWarning("Error on log file, failed to open: " + log_path);
		std::cerr << "Failed to open log file" << std::endl;
	}
	base_set_log_handler(node_obs_log, (logParam) ? logParam.release() : nullptr);
#ifndef _DEBUG
	// Redirect the ipc log callbacks to our log handler
	ipc::register_log_callback([](void *data, const char *fmt, va_list args) { blogva(LOG_ERROR, fmt, args); }, nullptr);
#endif

#ifdef ENABLE_CRASHREPORT
	util::CrashManager crashManager;
	crashManager.SetVersionName(currentVersion);
	crashManager.SetReportServerUrl(args[3].value_str);
	char *path = g_moduleDirectory.data();
	if (crashManager.Initialize(path, appdata)) {
		crashManager.Configure();
		if (crashManager.InitializeMemoryDump()) {
			writeCrashHandler(registerMemoryDump());
		}
	}

#ifdef WIN32
	// Register the pre and post server callbacks to log the data into the crashmanager
	g_server->set_pre_callback(
		[](std::string cname, std::string fname, const std::vector<ipc::value> &args, void *data) {
			util::CrashManager &crashManager = *static_cast<util::CrashManager *>(data);
			crashManager.ProcessPreServerCall(cname, fname, args);
		},
		&crashManager);
	g_server->set_post_callback(
		[](std::string cname, std::string fname, const std::vector<ipc::value> &args, void *data) {
			util::CrashManager &crashManager = *static_cast<util::CrashManager *>(data);
			crashManager.ProcessPostServerCall(cname, fname, args);
		},
		&crashManager);

#endif
#endif

#ifdef WIN32
	// Connect the metrics provider with our crash handler process, sending our current version tag
	// and enabling metrics
	util::CrashManager::GetMetricsProvider()->Initialize("\\\\.\\pipe\\metrics_pipe", currentVersion, false);
	hRtwq = LoadLibrary(L"RTWorkQ.dll");
	if (hRtwq) {
		typedef HRESULT(STDAPICALLTYPE * PFN_RtwqStartup)();
		PFN_RtwqStartup func = (PFN_RtwqStartup)GetProcAddress(hRtwq, "RtwqStartup");
		func();
	}
#endif
	obs_add_data_path((g_moduleDirectory + "/data/libobs/").c_str());
	slobs_plugin = appdata.substr(0, appdata.size() - strlen("/slobs-client"));
	slobs_plugin.append("/slobs-plugins");
	obs_add_data_path((slobs_plugin + "/data/").c_str());

	std::vector<char> userData = std::vector<char>(1024);
	os_get_config_path(userData.data(), userData.capacity() - 1, "slobs-client/plugin_config");
	if (!obs_startup(locale.c_str(), userData.data(), NULL)) {
		// TODO: We should return an error code if obs fails to initialize.
		// This was added as a temporary measure to detect what could be happening in some
		// cases (if the user data path is wrong for ex). This will be correctly adjusted
		// when init API supports more return codes.
#ifdef WIN32
		std::string userDataPath = std::string(userData.begin(), userData.end());
		util::CrashManager::AddWarning("Failed to start OBS, locale: " + locale + " user data: " + userDataPath);
#endif
	}

#ifdef _WIN32
	SetPrivilegeForGPUPriority();
#endif

	osn::Source::initialize_global_signals();

	cpuUsageInfo = os_cpu_usage_info_start();
	ConfigManager::getInstance().setAppdataPath(appdata);

	browserAccel = config_get_bool(ConfigManager::getInstance().getGlobal(), "General", "BrowserHWAccel");

	/* Set global private settings for whomever it concerns */
	obs_data_t *private_settings = obs_data_create();
	obs_data_set_bool(private_settings, "BrowserHWAccel", browserAccel);
	obs_apply_private_data(private_settings);
	obs_data_release(private_settings);

	addModulePaths();
	struct obs_module_failure_info mfi;
	obs_load_all_modules2(&mfi);
	obs_log_loaded_modules();
	obs_post_load_modules();

	if (mfi.count) {
		char **plugin = mfi.failed_modules;
		while (*plugin) {
			blog(LOG_ERROR, "Failed to load plugin: %s", *plugin);
			plugin++;
		}
	}

	OBS_service::createService(StreamServiceId::Main);
	OBS_service::createService(StreamServiceId::Second);
	OBS_service::createStreamingOutput(StreamServiceId::Main);
	OBS_service::createStreamingOutput(StreamServiceId::Second);
	OBS_service::createRecordingOutput();
	OBS_service::createReplayBufferOutput();

	OBS_service::createVideoStreamingEncoder(StreamServiceId::Main);
	OBS_service::createVideoStreamingEncoder(StreamServiceId::Second);
	OBS_service::createVideoRecordingEncoder();

	OBS_service::resetAudioContext();

	OBS_service::setupRecordingAudioEncoder();

	setAudioDeviceMonitoring();

	// Enable the hotkey callback rerouting that will be used when manually handling hotkeys on the frontend
	obs_hotkey_enable_callback_rerouting(true);

	// Init replay buffer rendering mode
	const char *currentOutputMode = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "Mode");
	bool simple = true;

	if (currentOutputMode)
		simple = strcmp(currentOutputMode, "Simple") == 0;

	bool useStreamOutput = config_get_bool(ConfigManager::getInstance().getBasic(), simple ? "SimpleOutput" : "AdvOut", "replayBufferUseStreamOutput");

	obs_set_replay_buffer_rendering_mode(useStreamOutput ? OBS_STREAMING_REPLAY_BUFFER_RENDERING : OBS_RECORDING_REPLAY_BUFFER_RENDERING);

	util::CrashManager::setAppState("idle");

	blog(LOG_INFO, "---------------------------------");
	obs_log_loaded_modules();
	blog(LOG_INFO, "---------------------------------");
	logEncoders();
	blog(LOG_INFO, "---------------------------------");

	// We are returning a video result here because the frontend needs to know if we sucessfully
	// initialized the Dx11 API
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(OBS_VIDEO_SUCCESS));

	AUTO_DEBUG;
}

void OBS_API::OBS_API_destroyOBS_API(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	/* INJECT osn::Source::Manager */
	// Alright, you're probably wondering: Why is osn code here?
	// Well, simply because the hooks need to run as soon as possible. We don't
	//  want to miss a single create or destroy signal OBS gives us for the
	//  osn::Source::Manager.
	osn::Source::finalize_global_signals();
	/* END INJECT osn::Source::Manager */
	destroyOBS_API();
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_API::OBS_API_getPerformanceStatistics(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

	rval.push_back(ipc::value(getCPU_Percentage()));
	rval.push_back(ipc::value(getNumberOfDroppedFrames()));
	rval.push_back(ipc::value(getDroppedFramesPercentage()));

	getCurrentOutputStats(OBS_service::getStreamingOutput(StreamServiceId::Main), streamingOutputStatsMain);
	rval.push_back(ipc::value(streamingOutputStatsMain.kbitsPerSec));
	rval.push_back(ipc::value(streamingOutputStatsMain.dataOutput));

	getCurrentOutputStats(OBS_service::getRecordingOutput(), recordingOutputStats);
	rval.push_back(ipc::value(recordingOutputStats.kbitsPerSec));
	rval.push_back(ipc::value(recordingOutputStats.dataOutput));

	rval.push_back(ipc::value(getCurrentFrameRate()));
	rval.push_back(ipc::value(getAverageTimeToRenderFrame()));
	rval.push_back(ipc::value(getMemoryUsage()));
	rval.push_back(ipc::value(getDiskSpaceAvailable()));

	getCurrentOutputStats(OBS_service::getStreamingOutput(StreamServiceId::Second), streamingOutputStatsSecondary);
	rval.push_back(ipc::value(streamingOutputStatsSecondary.kbitsPerSec));
	rval.push_back(ipc::value(streamingOutputStatsSecondary.dataOutput));
	AUTO_DEBUG;
}

void OBS_API::QueryHotkeys(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	struct HotkeyInfo {
		std::string objectName;
		obs_hotkey_registerer_type objectType;
		std::string hotkeyName;
		std::string hotkeyDesc;
		obs_hotkey_id hotkeyId;
	};

	// For each registered hotkey
	std::vector<HotkeyInfo> hotkeyInfos;
	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id id, obs_hotkey_t *key) {
			// Make sure every word has an initial capital letter
			auto ToTitle = [](std::string s) {
				bool last = true;
				for (char &c : s) {
					int full_c = (uint8_t)c;
					full_c = last ? ::toupper(full_c) : ::tolower(full_c);
					last = ::isspace(full_c);
				}
				return s;
			};
			std::vector<HotkeyInfo> &hotkeyInfos = *static_cast<std::vector<HotkeyInfo> *>(data);
			auto registerer_type = obs_hotkey_get_registerer_type(key);
			void *registerer = obs_hotkey_get_registerer(key);
			HotkeyInfo currentHotkeyInfo;
			if (registerer == nullptr)
				return true;

			// Discover the type of object registered with this hotkey
			switch (registerer_type) {
			case OBS_HOTKEY_REGISTERER_NONE:
			case OBS_HOTKEY_REGISTERER_FRONTEND: {
				// Ignore any frontend hotkey
				return true;
				break;
			}
			case OBS_HOTKEY_REGISTERER_SOURCE: {
				auto *weak_source = static_cast<obs_weak_source_t *>(registerer);
				auto key_source = OBSGetStrongRef(weak_source);
				if (key_source == nullptr)
					return true;
				currentHotkeyInfo.objectName = obs_source_get_name(key_source);
				currentHotkeyInfo.objectType = OBS_HOTKEY_REGISTERER_SOURCE;
				break;
			}
			case OBS_HOTKEY_REGISTERER_OUTPUT: {
				auto *weak_output = static_cast<obs_weak_output_t *>(registerer);
				auto key_output = OBSGetStrongRef(weak_output);
				if (key_output == nullptr)
					return true;
				currentHotkeyInfo.objectName = obs_output_get_name(key_output);
				currentHotkeyInfo.objectType = OBS_HOTKEY_REGISTERER_OUTPUT;
				break;
			}
			case OBS_HOTKEY_REGISTERER_ENCODER: {
				auto *weak_encoder = static_cast<obs_weak_encoder_t *>(registerer);
				auto key_encoder = OBSGetStrongRef(weak_encoder);
				if (key_encoder == nullptr)
					return true;
				currentHotkeyInfo.objectName = obs_encoder_get_name(key_encoder);
				currentHotkeyInfo.objectType = OBS_HOTKEY_REGISTERER_ENCODER;
				break;
			}
			case OBS_HOTKEY_REGISTERER_SERVICE: {
				auto *weak_service = static_cast<obs_weak_service_t *>(registerer);
				auto key_service = OBSGetStrongRef(weak_service);
				if (key_service == nullptr)
					return true;
				currentHotkeyInfo.objectName = obs_service_get_name(key_service);
				currentHotkeyInfo.objectType = OBS_HOTKEY_REGISTERER_SERVICE;
				break;
			}
			}

			// Key defs
			const char *_key_name = obs_hotkey_get_name(key);
			const char *_desc = obs_hotkey_get_description(key);

			if (!_key_name)
				return true;

			if (!_desc)
				_desc = "";

			auto key_name = std::string(_key_name);
			auto desc = std::string(_desc);
			const auto hotkeyId = obs_hotkey_get_id(key);

			// Parse the key name and the description
			key_name = key_name.substr(key_name.find_first_of(".") + 1);
			std::replace(key_name.begin(), key_name.end(), '-', '_');
			std::transform(key_name.begin(), key_name.end(), key_name.begin(), ::toupper);
			std::replace(desc.begin(), desc.end(), '-', ' ');
			desc = ToTitle(desc);

			currentHotkeyInfo.hotkeyName = key_name;
			currentHotkeyInfo.hotkeyDesc = desc;
			currentHotkeyInfo.hotkeyId = hotkeyId;
			hotkeyInfos.push_back(currentHotkeyInfo);

			return true;
		},
		&hotkeyInfos);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

	// For each hotkey that we've found
	for (auto &hotkeyInfo : hotkeyInfos) {
		rval.push_back(ipc::value(hotkeyInfo.objectName));
		rval.push_back(ipc::value(uint32_t(hotkeyInfo.objectType)));
		rval.push_back(ipc::value(hotkeyInfo.hotkeyName));
		rval.push_back(ipc::value(hotkeyInfo.hotkeyDesc));
		rval.push_back(ipc::value(uint64_t(hotkeyInfo.hotkeyId)));
	}

	AUTO_DEBUG;
}

void OBS_API::ProcessHotkeyStatus(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_hotkey_id hotkeyId = args[0].value_union.ui64;
	uint64_t press = args[1].value_union.i32;

	// TODO: Check if the hotkey ID is valid
	obs_hotkey_trigger_routed_callback(hotkeyId, (bool)press);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

	AUTO_DEBUG;
}

void OBS_API::SetUsername(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	username = args[0].value_str;
	util::CrashManager::SetUsername(username);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

	AUTO_DEBUG;
}

void OBS_API::SetProcessPriorityOld(const char *priority)
{
	if (!priority)
		return;
#ifdef WIN32
	if (strcmp(priority, "High") == 0)
		SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	else if (strcmp(priority, "AboveNormal") == 0)
		SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
	else if (strcmp(priority, "Normal") == 0)
		SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
	else if (strcmp(priority, "BelowNormal") == 0)
		SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
	else if (strcmp(priority, "Idle") == 0)
		SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
#endif
}

void OBS_API::OBS_API_forceCrash(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	throw std::runtime_error("Simulated crash to test crash handling functionality");

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

	AUTO_DEBUG;
}

bool DisableAudioDucking(bool disable)
{
#ifdef WIN32

	if (disable) {
		// When windows detects communications activity / 0 - Mute all other sounds / 1 - Reduce all other by 80% / 2 - Reduce all other by 50% / 3 - Do nothing
		DWORD nValue = 3;

		HKEY hKey = 0;
		HKEY hMainKey = HKEY_CURRENT_USER;
		std::string sKeyPath = "Software\\Microsoft\\Multimedia\\Audio";

		if (RegCreateKeyExA(hMainKey, sKeyPath.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
			LSTATUS lStatus = ::RegSetValueExA(hKey, "UserDuckingPreference", 0, REG_DWORD, (const BYTE *)&nValue, (DWORD)sizeof(DWORD));
			RegCloseKey(hKey);
		}
	}

	ComPtr<IMMDeviceEnumerator> devEmum;
	ComPtr<IMMDevice> device;
	ComPtr<IAudioSessionManager2> sessionManager2;
	ComPtr<IAudioSessionControl> sessionControl;
	ComPtr<IAudioSessionControl2> sessionControl2;

	HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (void **)&devEmum);
	if (FAILED(result))
		return false;

	result = devEmum->GetDefaultAudioEndpoint(eRender, eConsole, &device);
	if (FAILED(result))
		return false;

	result = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_INPROC_SERVER, nullptr, (void **)&sessionManager2);
	if (FAILED(result))
		return false;

	result = sessionManager2->GetAudioSessionControl(nullptr, 0, &sessionControl);
	if (FAILED(result))
		return false;

	result = sessionControl->QueryInterface(&sessionControl2);
	if (FAILED(result))
		return false;

	result = sessionControl2->SetDuckingPreference(disable);
	return SUCCEEDED(result);
#else
	return false;
#endif
}

void OBS_API::setAudioDeviceMonitoring(void)
{
/* load audio monitoring */
#if defined(_WIN32) || defined(__APPLE__)
	const char *device_name = config_get_string(ConfigManager::getInstance().getBasic(), "Audio", "MonitoringDeviceName");
	const char *device_id = config_get_string(ConfigManager::getInstance().getBasic(), "Audio", "MonitoringDeviceId");

	obs_set_audio_monitoring_device(device_name, device_id);

	blog(LOG_INFO, "Audio monitoring device:\n\tname: %s\n\tid: %s", device_name, device_id);

	bool disableAudioDucking = config_get_bool(ConfigManager::getInstance().getBasic(), "Audio", "DisableAudioDucking");
	if (disableAudioDucking)
		DisableAudioDucking(true);
#endif
}

std::shared_ptr<std::thread> crash_handler_responce_thread;
bool crash_handler_timeout_activated = false;
bool crash_handler_exit = false;

#ifdef WIN32
typedef struct {
	OVERLAPPED oOverlap;
	HANDLE hPipeInst;
	std::vector<char> chRequest;
	DWORD cbRead;
	TCHAR chReply[BUFFSIZE];
	DWORD cbToWrite;
	DWORD dwState;
	BOOL fPendingIO;
} PIPEINST, *LPPIPEINST;

PIPEINST Pipe = {0};
HANDLE hEvents = {0};

BOOL ConnectToNewClient(HANDLE hPipe, LPOVERLAPPED lpo)
{
	BOOL fConnected, fPendingIO = FALSE;
	fConnected = ConnectNamedPipe(hPipe, lpo);

	if (fConnected) {
		return 0;
	}

	switch (GetLastError()) {
	case ERROR_IO_PENDING:
		fPendingIO = TRUE;
		break;
	case ERROR_PIPE_CONNECTED:
		if (SetEvent(lpo->hEvent))
			break;
	default: {
		return 0;
	}
	}

	return fPendingIO;
}

VOID DisconnectAndReconnect(void)
{
	if (!DisconnectNamedPipe(Pipe.hPipeInst)) {
		return;
	}

	Pipe.fPendingIO = ConnectToNewClient(Pipe.hPipeInst, &(Pipe.oOverlap));

	Pipe.dwState = Pipe.fPendingIO ? CONNECTING_STATE : READING_STATE;
}

bool prepareTerminationPipe()
{
	LPTSTR lpszPipename = TEXT("\\\\.\\pipe\\exit-slobs-crash-handler");

	hEvents = CreateEvent(NULL, TRUE, TRUE, NULL);

	if (hEvents == NULL) {
		return false;
	}

	Pipe.oOverlap.hEvent = hEvents;

	Pipe.hPipeInst = CreateNamedPipe(lpszPipename, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1,
					 BUFFSIZE * sizeof(TCHAR), BUFFSIZE * sizeof(TCHAR), 5000, NULL);

	if (Pipe.hPipeInst == INVALID_HANDLE_VALUE) {
		CloseHandle(hEvents);
		hEvents = NULL;
		Pipe.oOverlap.hEvent = NULL;
		return false;
	}

	Pipe.fPendingIO = ConnectToNewClient(Pipe.hPipeInst, &(Pipe.oOverlap));

	Pipe.dwState = Pipe.fPendingIO ? CONNECTING_STATE : READING_STATE;

	return true;
}

void acknowledgeTerminate()
{
	BOOL fSuccess;
	DWORD i, dwWait, cbRet, dwErr;

	while (!crash_handler_exit) {
		if (crash_handler_timeout_activated) {
			auto tp = std::chrono::high_resolution_clock::now();
			auto delta = tp - start_wait_acknowledge;

			if (std::chrono::duration_cast<std::chrono::milliseconds>(delta).count() > 5000) {
				// We timeout, crash handler failed to send the shutdown acknowledge,
				// we move forward with the shutdown procedure
				crash_handler_exit = true;
				CloseHandle(Pipe.hPipeInst);
				break;
			}
		}
		dwWait = WaitForSingleObject(hEvents, 500);

		if (dwWait == WAIT_OBJECT_0) {
			if (Pipe.fPendingIO) {
				fSuccess = GetOverlappedResult(Pipe.hPipeInst, &(Pipe.oOverlap), &cbRet, FALSE);

				switch (Pipe.dwState) {
				case CONNECTING_STATE: {
					if (!fSuccess) {
						break;
					}
					Pipe.dwState = READING_STATE;
					break;
				}
				case READING_STATE: {
					if (!fSuccess || cbRet == 0) {
						crash_handler_exit = true;
						DisconnectAndReconnect();
						break;
					}
					Pipe.cbRead = cbRet;
					Pipe.dwState = READING_STATE;
					break;
				}
				default: {
					break;
				}
				}
			}

			switch (Pipe.dwState) {
			case READING_STATE: {
				Pipe.chRequest.resize(BUFFSIZE);
				fSuccess = ReadFile(Pipe.hPipeInst, Pipe.chRequest.data(), BUFFSIZE * sizeof(TCHAR), &Pipe.cbRead, &Pipe.oOverlap);

				GetOverlappedResult(Pipe.hPipeInst, &Pipe.oOverlap, &Pipe.cbRead, false);

				// The read operation completed successfully.
				if (Pipe.cbRead > 0) {
					Pipe.fPendingIO = FALSE;
				}
				dwErr = GetLastError();
				if (!fSuccess && (dwErr == ERROR_IO_PENDING)) {
					Pipe.fPendingIO = TRUE;
					break;
				}
				crash_handler_exit = true;
				DisconnectAndReconnect();
				break;
			}
			}
		}
	}
}
#else __APPLE__
void acknowledgeTerminate(void)
{
	std::vector<char> buffer;
	buffer.resize(64);
	int file_descriptor = open("exit-slobs-crash-handler", O_RDONLY);
	if (file_descriptor < 0) {
		blog(LOG_ERROR, "Could not open crash-handler exit fifo");
		return;
	}

	if (::read(file_descriptor, buffer.data(), buffer.size()) <= 0)
		blog(LOG_ERROR, "Error while reading crash-handler exit message");
}

bool prepareTerminationPipe()
{
	// TODO
	return false;
}
#endif

void OBS_API::CreateCrashHandlerExitPipe()
{
	if (prepareTerminationPipe()) {
		crash_handler_responce_thread = std::make_shared<std::thread>(acknowledgeTerminate);
	} else {
#ifdef WIN32
		blog(LOG_ERROR, "Failed to create pipe for crash-handler exit message");
#endif
	}
}

void OBS_API::WaitCrashHandlerClose(bool waitBeforeClosing)
{
	if (crash_handler_responce_thread) {
		if (!waitBeforeClosing)
			crash_handler_exit = true;

		if (crash_handler_responce_thread->joinable())
			crash_handler_responce_thread->join();
	}
}

void debug_enum_sources(std::string inside)
{
	auto PreEnum = [](void *data, obs_source_t *source) -> bool {
		blog(LOG_INFO, "[ENUM] obs_enum_%s %s %p , removed %d", (char *)data, obs_source_get_name(source), source, obs_source_removed(source) ? 1 : 0);
		return true;
	};
	obs_enum_sources(PreEnum, (void *)(std::string("sources ") + inside).c_str());
	obs_enum_scenes(PreEnum, (void *)(std::string("scenes ") + inside).c_str());
}

void OBS_API::StopCrashHandler(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	util::CrashManager::setAppState("shutdown");

	blog(LOG_DEBUG, "OBS_API::StopCrashHandler called, objects allocated %d", bnum_allocs());

	if (crash_handler_responce_thread) {
		writeCrashHandler(unregisterProcess());

		start_wait_acknowledge = std::chrono::high_resolution_clock::now();
		crash_handler_timeout_activated = true;

		if (crash_handler_responce_thread->joinable())
			crash_handler_responce_thread->join();
	} else {
		writeCrashHandler(unregisterProcess());

		// Waiting 1 sec to let crash handler process unregister command before continuing
		// with shutdown sequence.
		// Only for a case when it failed to create a pipe to recieve confirmation from crash handler.
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}
struct release_sources {
	std::mutex mtx;
	int num_sources = 0;
	std::chrono::steady_clock::time_point start_time;

	void static signal_handler(void *param, calldata_t *data)
	{
		struct release_sources *sources = (struct release_sources *)param;
		std::lock_guard<std::mutex> lock(sources->mtx);
		sources->num_sources--;
	}

	void add_to_delete(obs_source_t *source)
	{
		if (obs_source_get_type(source) != OBS_SOURCE_TYPE_SCENE) {
			signal_handler_connect(obs_source_get_signal_handler(source), "destroy", signal_handler, this);

			std::lock_guard<std::mutex> lock(mtx);
			num_sources++;
		}
	}
};

void OBS_API::InformCrashHandler(const int crash_id)
{
	writeCrashHandler(crashedProcess(crash_id));
}

void OBS_API::CrashModuleInfo(const std::string &moduleName, const std::string &binaryPath)
{
	writeCrashHandler(crashedModuleInfo(moduleName, binaryPath));
}

void OBS_API::destroyOBS_API(void)
{
	blog(LOG_DEBUG, "OBS_API::destroyOBS_API started, objects allocated %d", bnum_allocs());
	debug_enum_sources(" on destroyOBS_API");
	os_cpu_usage_info_destroy(cpuUsageInfo);

#ifdef _WIN32
	config_t *basicConfig = ConfigManager::getInstance().getBasic();
	if (basicConfig) {
		bool disableAudioDucking = config_get_bool(basicConfig, "Audio", "DisableAudioDucking");
		if (disableAudioDucking)
			DisableAudioDucking(false);
	}
#endif
	OBS_content::OBS_content_shutdownDisplays();

	autoConfig::WaitPendingTests();

	OBS_service::stopAllOutputs();
	OBS_service::waitReleaseWorker();

	for (int i = 0; i < MAX_CHANNELS; i++)
		obs_set_output_source(i, nullptr);

	obs_encoder_t *streamingEncoder;
	streamingEncoder = OBS_service::getStreamingEncoder(StreamServiceId::Main);
	if (streamingEncoder != NULL) {
		obs_encoder_release(streamingEncoder);
		streamingEncoder = nullptr;
	}

	streamingEncoder = OBS_service::getStreamingEncoder(StreamServiceId::Second);
	if (streamingEncoder != NULL) {
		obs_encoder_release(streamingEncoder);
		streamingEncoder = nullptr;
	}

	obs_encoder_t *recordingEncoder = OBS_service::getRecordingEncoder();
	if (recordingEncoder != NULL && (OBS_service::useRecordingPreset() || obs_get_multiple_rendering())) {
		obs_encoder_release(recordingEncoder);
		recordingEncoder = nullptr;
	}

	OBS_service::setAudioStreamingEncoder(nullptr, StreamServiceId::Main);
	OBS_service::setAudioStreamingEncoder(nullptr, StreamServiceId::Second);

	obs_encoder_t *audioRecordingEncoder = OBS_service::getAudioSimpleRecordingEncoder();
	if (audioRecordingEncoder != NULL && (OBS_service::useRecordingPreset() || obs_get_multiple_rendering())) {
		obs_encoder_release(audioRecordingEncoder);
		audioRecordingEncoder = nullptr;
	}

	obs_encoder_t *archiveEncoder = OBS_service::getArchiveEncoder();
	if (archiveEncoder != NULL) {
		obs_encoder_release(archiveEncoder);
		archiveEncoder = nullptr;
	}

	obs_output_t *streamingOutput;
	streamingOutput = OBS_service::getStreamingOutput(StreamServiceId::Main);
	if (streamingOutput != NULL) {
		obs_output_release(streamingOutput);
		streamingEncoder = nullptr;
	}

	streamingOutput = OBS_service::getStreamingOutput(StreamServiceId::Second);
	if (streamingOutput != NULL) {
		obs_output_release(streamingOutput);
		streamingEncoder = nullptr;
	}

	obs_output_t *recordingOutput = OBS_service::getRecordingOutput();
	if (recordingOutput != NULL) {
		obs_output_release(recordingOutput);
		recordingOutput = nullptr;
	}

	obs_output_t *replayBufferOutput = OBS_service::getReplayBufferOutput();
	if (replayBufferOutput != NULL) {
		obs_output_release(replayBufferOutput);
		replayBufferOutput = nullptr;
	}

	obs_output *virtualWebcamOutput = OBS_service::getVirtualWebcamOutput();
	if (virtualWebcamOutput != NULL) {
		if (obs_output_active(virtualWebcamOutput))
			obs_output_stop(virtualWebcamOutput);

		obs_output_release(virtualWebcamOutput);
		virtualWebcamOutput = nullptr;
	}

	obs_service_t *service;
	service = OBS_service::getService(StreamServiceId::Main);
	if (service != NULL) {
		obs_service_release(service);
		service = nullptr;
	}

	service = OBS_service::getService(StreamServiceId::Second);
	if (service != NULL) {
		obs_service_release(service);
		service = nullptr;
	}

	OBS_service::clearRecordingAudioEncoder();
	osn::Volmeter::ClearVolmeters();
	osn::Fader::ClearFaders();

	obs_wait_for_destroy_queue();

	// Release all video encoders
	osn::VideoEncoder::Manager::GetInstance().for_each([](obs_encoder_t *videoEncoder) {
		if (videoEncoder) {
			obs_encoder_release(videoEncoder);
			videoEncoder = nullptr;
		}
	});

	// Release all audio encoders
	osn::AudioEncoder::Manager::GetInstance().for_each([](obs_encoder_t *audioEncoder) {
		if (audioEncoder) {
			obs_encoder_release(audioEncoder);
			audioEncoder = nullptr;
		}
	});

	// Release all audio track encoders
	std::vector<osn::AudioTrack *> audioTrackEncoders;
	// osn::IAudioTrack::Manager::GetInstance().
	// 	for_each([&audioTrackEncoders](osn::AudioTrack* audioTrackEncoder)
	for (auto const &audioTrack : osn::IAudioTrack::audioTracks) {
		if (audioTrack && audioTrack->audioEnc) {
			obs_encoder_release(audioTrack->audioEnc);
			audioTrack->audioEnc = nullptr;
		}
	};

	// Release all services
	osn::Service::Manager::GetInstance().for_each([](obs_service_t *service) {
		if (service) {
			obs_service_release(service);
			service = nullptr;
		}
	});

	// Release all delays
	osn::IDelay::Manager::GetInstance().for_each([](osn::Delay *delay) {
		if (delay)
			delete delay;
	});

	// Release all reconnects
	osn::IReconnect::Manager::GetInstance().for_each([](osn::Reconnect *reconnect) {
		if (reconnect)
			delete reconnect;
	});

	// Release all networks
	osn::INetwork::Manager::GetInstance().for_each([](osn::Network *network) {
		if (network)
			delete network;
	});

	// Release all streaming ouputs
	osn::IStreaming::Manager::GetInstance().for_each([](osn::Streaming *streamingOutput) {
		if (streamingOutput)
			delete streamingOutput;
	});

	// Release all recording ouputs
	osn::IFileOutput::Manager::GetInstance().for_each([](osn::FileOutput *fileOutput) {
		if (fileOutput)
			delete fileOutput;
	});

	obs_wait_for_destroy_queue();
	// obs_set_output_source might cause destruction of some sources.
	// Wait for the destruction thread to destroy the sources to be sure
	// |for_each| below will only return actual remaining sources.

	// Check if the frontend was able to shutdown correctly:
	// If there are some sources here it's because it ended unexpectedly, this represents a
	// problem since obs doesn't handle releasing leaked sources very well. The best we can
	// do is to insert a try-catch block and disable the crash handler to avoid false positives
	if (osn::Source::Manager::GetInstance().size() > 0 || osn::Scene::Manager::GetInstance().size() > 0 ||
	    osn::SceneItem::Manager::GetInstance().size() > 0 || osn::Transition::Manager::GetInstance().size() > 0 ||
	    osn::Filter::Manager::GetInstance().size() > 0 || osn::Input::Manager::GetInstance().size() > 0) {

		release_sources releasing_counter;

		std::vector<obs_source_t *> sources;
		osn::Source::Manager::GetInstance().for_each([&sources, &releasing_counter](obs_source_t *source) {
			blog(LOG_INFO, "OBS_API::destroyOBS_API before source %p destroy %s", source, obs_source_get_name(source));
			if (source) {
				if (!obs_source_removed(source)) {
					sources.push_back(source);
				} else {
					blog(LOG_INFO, "OBS_API::destroyOBS_API source %s is in removed state", obs_source_get_name(source));
					releasing_counter.add_to_delete(source);
				}
			} else {
				blog(LOG_ERROR, "OBS_API::destroyOBS_API source in memory manager is null");
			}
		});

		if (sources.size() > 0 || releasing_counter.num_sources > 0)
			blog(LOG_INFO, "OBS_API::destroyOBS_API sources to destroy %d, to wait %d", sources.size(), releasing_counter.num_sources);

		for (const auto &source : sources) {
			if (!source)
				continue;

			const char *source_id = obs_source_get_id(source);
			if (!source_id)
				continue;

			if (!strcmp(source_id, "scene")) {
				std::list<obs_sceneitem_t *> items;
				auto cb = [](obs_scene_t *scene, obs_sceneitem_t *item, void *data) {
					if (item) {
						obs_sceneitem_remove(item);
						obs_sceneitem_release(item);
					}
					return true;
				};
				obs_scene_t *scene = obs_scene_from_source(source);
				if (scene)
					obs_scene_enum_items(scene, cb, nullptr);
			}
		}

		// Release filters only
		for (int i = 0; i < sources.size(); i++) {
			if (sources[i] && obs_source_get_type(sources[i]) == OBS_SOURCE_TYPE_FILTER) {
				obs_source_release(sources[i]);
				sources[i] = nullptr;
			}
		}

		// Release all remaining sources that are not transitions
		for (int i = 0; i < sources.size(); i++) {
			if (sources[i] && obs_source_get_type(sources[i]) != OBS_SOURCE_TYPE_TRANSITION) {
				releasing_counter.add_to_delete(sources[i]);

				obs_source_release(sources[i]);
				sources[i] = nullptr;
			}
		}

		// Release all remaning transitions
		for (auto source : sources) {
			if (source)
				obs_source_release(source);
		}

		// In rare cases (bugs?), some sources may not be released yet.
		// Remove the 'destruction' callback. Otherwise, it will try to
		// access data released by |obs_shutdown| which leads to crashes.
		obs_wait_for_destroy_queue();
		blog(LOG_WARNING, "OBS_API::destroyOBS_API - obs_wait_for_destroy_queue has finished, osn::Source::Manager::GetInstance() size is %d",
		     osn::Source::Manager::GetInstance().size());
		osn::Source::Manager::GetInstance().for_each([&sources](obs_source_t *source) { osn::Source::detach_source_signals(source); });
		MemoryManager::GetInstance().shutdownAllSources();

#ifdef WIN32
		// Directly blame the frontend since it didn't release all objects and that could cause
		// a crash on the backend
		// This is necessary since the frontend could still finish after the backend, causing the
		// crash manager to think the backend crashed first while the real culprit is the frontend
		util::CrashManager::GetMetricsProvider()->BlameFrontend();

		util::CrashManager::DisableReports();
#endif

		releasing_counter.start_time = std::chrono::steady_clock::now();
		while (true) {
			{
				std::lock_guard<std::mutex> lock(releasing_counter.mtx);
				if (releasing_counter.num_sources <= 0)
					break;
			}

			if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - releasing_counter.start_time).count() > 10) {
				blog(LOG_WARNING, "OBS_API::destroyOBS_API timeout waiting for sources to be released. %d sources remaining",
				     releasing_counter.num_sources);
				break;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		debug_enum_sources(" on shutdown");

		blog(LOG_DEBUG, "OBS_API::destroyOBS_API unreleased objects detected before obs_shutdown, objects allocated %d", bnum_allocs());
		// Try-catch should suppress any error message that could be thrown to the user
		try {
			obs_shutdown();
		} catch (...) {
		}

	} else {
		blog(LOG_DEBUG, "OBS_API::destroyOBS_API calling obs_shutdown, objects allocated %d", bnum_allocs());
		obs_shutdown();
	}

	// Release each obs module (dlls for windows)
	// TODO: We should release these modules (dlls) manually and not let the garbage
	// collector do this for us on shutdown
	for (auto &moduleInfo : obsModules) {
	}

#ifdef _WIN32
	if (hRtwq) {
		typedef HRESULT(STDAPICALLTYPE * PFN_RtwqShutdown)();
		PFN_RtwqShutdown func = (PFN_RtwqShutdown)GetProcAddress(hRtwq, "RtwqShutdown");
		func();
		FreeLibrary(hRtwq);
	}
#endif

	// The goal is to reduce this number to zero and add a throw here, so if in the future
	// a leak is detected, any developer will know for sure what is causing it
	int totalLeaks = bnum_allocs();
	std::cout << "Total leaks: " << totalLeaks << std::endl;
	if (totalLeaks) {
		// throw "OBS has memory leaks";
	}
	blog(LOG_DEBUG, "OBS_API::destroyOBS_API after obs_shutdown, objects allocated %d", bnum_allocs());
}

struct ci_char_traits : public std::char_traits<char> {
	static bool eq(char c1, char c2) { return toupper(c1) == toupper(c2); }
	static bool ne(char c1, char c2) { return toupper(c1) != toupper(c2); }
	static bool lt(char c1, char c2) { return toupper(c1) < toupper(c2); }
	static int compare(const char *s1, const char *s2, size_t n)
	{
		while (n-- != 0) {
			if (toupper(*s1) < toupper(*s2))
				return -1;
			if (toupper(*s1) > toupper(*s2))
				return 1;
			++s1;
			++s2;
		}
		return 0;
	}
	static const char *find(const char *s, int n, char a)
	{
		while (n-- > 0 && toupper(*s) != toupper(a)) {
			++s;
		}
		return s;
	}
};

double OBS_API::getCPU_Percentage(void)
{
	double cpuPercentage = os_cpu_usage_info_query(cpuUsageInfo);

	cpuPercentage *= 10;
	cpuPercentage = trunc(cpuPercentage);
	cpuPercentage /= 10;

	return cpuPercentage;
}

int OBS_API::getNumberOfDroppedFrames(void)
{
	obs_output_t *streamOutput = OBS_service::getStreamingOutput(StreamServiceId::Main);

	int totalDropped = 0;

	if (streamOutput && obs_output_active(streamOutput)) {
		totalDropped = obs_output_get_frames_dropped(streamOutput);
	}

	return totalDropped;
}

double OBS_API::getDroppedFramesPercentage(void)
{
	obs_output_t *streamOutput = OBS_service::getStreamingOutput(StreamServiceId::Main);

	double percent = 0;

	if (streamOutput && obs_output_active(streamOutput)) {
		int totalDropped = obs_output_get_frames_dropped(streamOutput);
		int totalFrames = obs_output_get_total_frames(streamOutput);
		if (totalFrames == 0) {
			percent = 0.0;
		} else {
			percent = (double)totalDropped / (double)totalFrames * 100.0;
		}
	}

	return percent;
}

void OBS_API::getCurrentOutputStats(obs_output_t *output, OBS_API::OutputStats &outputStats)
{
	outputStats.kbitsPerSec = 0.0;
	outputStats.dataOutput = 0.0;

	if (!output) {
		return;
	}

	if (obs_output_active(output)) {
		uint64_t bytesSent = obs_output_get_total_bytes(output);
		uint64_t bytesSentTime = os_gettime_ns();

		if (bytesSent < outputStats.lastBytesSent)
			bytesSent = 0;
		if (bytesSent == 0)
			outputStats.lastBytesSent = 0;

		uint64_t bitsBetween = (bytesSent - outputStats.lastBytesSent) * 8;

		double timePassed = double(bytesSentTime - outputStats.lastBytesSentTime) / 1000000000.0;
		if (timePassed < std::numeric_limits<double>::epsilon() && timePassed > -std::numeric_limits<double>::epsilon()) {
			outputStats.kbitsPerSec = 0.0;
		} else {
			outputStats.kbitsPerSec = double(bitsBetween) / timePassed / 1000.0;
		}

		outputStats.lastBytesSent = bytesSent;
		outputStats.lastBytesSentTime = bytesSentTime;
		outputStats.dataOutput = bytesSent / (1024.0 * 1024.0);
	}
}

double OBS_API::getCurrentFrameRate(void)
{
	return obs_get_active_fps();
}

double OBS_API::getAverageTimeToRenderFrame()
{
	return (double)obs_get_average_frame_time_ns() / 1000000.0;
}

std::string OBS_API::getDiskSpaceAvailable()
{
	const char *path = nullptr;
	const char *mode = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "Mode");

	if (strcmp(mode, "Advanced") == 0) {
		const char *advanced_mode = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecType");

		if (strcmp(advanced_mode, "FFmpeg") == 0) {
			path = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "FFFilePath");
		} else {
			path = config_get_string(ConfigManager::getInstance().getBasic(), "AdvOut", "RecFilePath");
		}
	} else {
		path = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "FilePath");
	}

	uint64_t bytes = os_get_free_disk_space(path);

	double free_bytes = 0;
	std::string type;

	if (bytes > TBYTE) {
		free_bytes = (double)bytes / TBYTE;
		type = " TB";
	} else if (bytes > GBYTE) {
		free_bytes = (double)bytes / GBYTE;
		type = " GB";
	} else {
		free_bytes = (double)bytes / MBYTE;
		type = " MB";
	}

	std::stringstream remainingHDSpace;
	remainingHDSpace << free_bytes << type;
	return remainingHDSpace.str();
}

double OBS_API::getMemoryUsage()
{
	return (double)os_get_proc_resident_size() / (1024.0 * 1024.0);
}

const std::vector<std::string> &OBS_API::getOBSLogErrors()
{
	return logReport.errors;
}

const std::vector<std::string> &OBS_API::getOBSLogWarnings()
{
	return logReport.warnings;
}

std::queue<std::string> &OBS_API::getOBSLogGeneral()
{
	return logReport.general;
}

std::string OBS_API::getCurrentVersion()
{
	return currentVersion;
}

std::string OBS_API::getUsername()
{
	return username;
}
#ifdef WIN32
static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
{
	MONITORINFO info;
	info.cbSize = sizeof(info);
	if (GetMonitorInfo(hMonitor, &info)) {
		std::vector<std::pair<uint32_t, uint32_t>> *resolutions = reinterpret_cast<std::vector<std::pair<uint32_t, uint32_t>> *>(dwData);

		resolutions->push_back(
			std::make_pair(std::abs(info.rcMonitor.left - info.rcMonitor.right), std::abs(info.rcMonitor.top - info.rcMonitor.bottom)));
	}
	return true;
}
#endif
std::vector<std::pair<uint32_t, uint32_t>> OBS_API::availableResolutions(void)
{
	std::vector<std::pair<uint32_t, uint32_t>> resolutions;
#ifdef WIN32
	EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, reinterpret_cast<LPARAM>(&resolutions));
#else
	resolutions = g_util_osx->getAvailableScreenResolutions();
#endif
	return resolutions;
}

void OBS_API::SetBrowserAcceleration(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	browserAccel = args[0].value_union.ui32;
	config_set_bool(ConfigManager::getInstance().getGlobal(), "General", "BrowserHWAccel", browserAccel);
	config_save_safe(ConfigManager::getInstance().getGlobal(), "tmp", nullptr);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_API::SetMediaFileCaching(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	mediaFileCaching = args[0].value_union.ui32;
	config_set_bool(ConfigManager::getInstance().getGlobal(), "General", "fileCaching", mediaFileCaching);
	config_save_safe(ConfigManager::getInstance().getGlobal(), "tmp", nullptr);
	MemoryManager::GetInstance().updateSourcesCache();
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_API::SetProcessPriority(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	processPriority = args[0].value_str;
	config_set_string(ConfigManager::getInstance().getGlobal(), "General", "ProcessPriority", processPriority.c_str());
	config_save_safe(ConfigManager::getInstance().getGlobal(), "tmp", nullptr);

#ifdef WIN32
	if (processPriority.compare("High") == 0)
		SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	else if (processPriority.compare("AboveNormal") == 0)
		SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
	else if (processPriority.compare("Normal") == 0)
		SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
	else if (processPriority.compare("BelowNormal") == 0)
		SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
	else if (processPriority.compare("Idle") == 0)
		SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
#endif

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

bool OBS_API::getBrowserAcceleration()
{
	return browserAccel;
}

bool OBS_API::getMediaFileCaching()
{
	return mediaFileCaching;
}

void OBS_API::GetBrowserAcceleration(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)browserAccel));
	AUTO_DEBUG;
}

void OBS_API::GetMediaFileCaching(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)mediaFileCaching));
	AUTO_DEBUG;
}

void OBS_API::GetProcessPriority(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(processPriority));
	AUTO_DEBUG;
}

void OBS_API::GetBrowserAccelerationLegacy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)config_get_bool(ConfigManager::getInstance().getGlobal(), "General", "BrowserHWAccel")));
	AUTO_DEBUG;
}

void OBS_API::GetMediaFileCachingLegacy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)config_get_bool(ConfigManager::getInstance().getGlobal(), "General", "fileCaching")));
	AUTO_DEBUG;
}

void OBS_API::GetProcessPriorityLegacy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(config_get_string(ConfigManager::getInstance().getGlobal(), "General", "ProcessPriority")));
	AUTO_DEBUG;
}

void OBS_API::GetSdrWhiteLevel(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)sdrWhiteLevel));
	AUTO_DEBUG;
}

void OBS_API::SetSdrWhiteLevel(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	sdrWhiteLevel = args[0].value_union.ui32;
	config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "SdrWhiteLevel", sdrWhiteLevel);
	config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_API::GetSdrWhiteLevelLegacy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)config_get_uint(ConfigManager::getInstance().getBasic(), "Video", "SdrWhiteLevel")));
	AUTO_DEBUG;
}

void OBS_API::GetHdrNominalPeakLevel(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)hdrNominalPeakLevel));
	AUTO_DEBUG;
}

void OBS_API::SetHdrNominalPeakLevel(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	hdrNominalPeakLevel = args[0].value_union.ui32;
	config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "HdrNominalPeakLevel", hdrNominalPeakLevel);
	config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_API::GetHdrNominalPeakLevelLegacy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)config_get_uint(ConfigManager::getInstance().getBasic(), "Video", "HdrNominalPeakLevel")));
	AUTO_DEBUG;
}

void OBS_API::GetLowLatencyAudioBuffering(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)lowLatencyAudioBuffering));
	AUTO_DEBUG;
}

void OBS_API::SetLowLatencyAudioBuffering(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	lowLatencyAudioBuffering = args[0].value_union.ui32;
	config_set_bool(ConfigManager::getInstance().getGlobal(), "Audio", "LowLatencyAudioBuffering", lowLatencyAudioBuffering);
	config_save_safe(ConfigManager::getInstance().getGlobal(), "tmp", nullptr);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_API::GetLowLatencyAudioBufferingLegacy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)config_get_bool(ConfigManager::getInstance().getGlobal(), "Audio", "LowLatencyAudioBuffering")));
	AUTO_DEBUG;
}

void OBS_API::GetForceGPURendering(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)forceGPURendering));
	AUTO_DEBUG;
}

void OBS_API::SetForceGPURendering(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	forceGPURendering = args[0].value_union.ui32;
	config_set_bool(ConfigManager::getInstance().getBasic(), "Video", "ForceGPUAsRenderDevice", forceGPURendering);
	config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void OBS_API::GetForceGPURenderingLegacy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)config_get_bool(ConfigManager::getInstance().getBasic(), "Video", "ForceGPUAsRenderDevice")));
	AUTO_DEBUG;
}