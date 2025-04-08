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

#include "utility.hpp"
#include <codecvt>
#include <locale>

#include "nlohmann/json.hpp"
#include <iomanip>
#include <sstream>
#include <fstream>

// This is from enc-amf
#if (defined _WIN32) || (defined _WIN64) // Windows

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifdef WINVER
#undef WINVER
#endif
#define WINVER 0x601

#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x601
#include <windows.h>

const DWORD MS_VC_EXCEPTION = 0x406D1388;

#pragma pack(push, 8)
typedef struct tagTHREADNAME_INFO {
	DWORD dwType;     // Must be 0x1000.
	LPCSTR szName;    // Pointer to name (in user addr space).
	DWORD dwThreadID; // Thread ID (-1=caller thread).
	DWORD dwFlags;    // Reserved for future use, must be zero.
} THREADNAME_INFO;
#pragma pack(pop)

void utility::SetThreadName(uint32_t dwThreadID, const char *threadName)
{
	// DWORD dwThreadID = ::GetThreadId( static_cast<HANDLE>( t.native_handle() ) );

	THREADNAME_INFO info;
	info.dwType = 0x1000;
	info.szName = threadName;
	info.dwThreadID = dwThreadID;
	info.dwFlags = 0;

	__try {
		RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR *)&info);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
	}
}
void utility::SetThreadName(const char *threadName)
{
	utility::SetThreadName(GetCurrentThreadId(), threadName);
}
#endif

#if (defined _WIN32) || (defined _WIN64) // Windows
#include <process.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif

static thread_local std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

std::string from_utf16_wide_to_utf8(const wchar_t *from, size_t length)
{
	const wchar_t *from_end;

	if (length == 0)
		return {};
	else if (length != -1)
		from_end = from + length;
	else
		return converter.to_bytes(from);

	return converter.to_bytes(from, from_end);
}

std::wstring from_utf8_to_utf16_wide(const char *from, size_t length)
{
	const char *from_end;

	if (length == 0)
		return {};
	else if (length != -1)
		from_end = from + length;
	else
		return converter.from_bytes(from);

	return converter.from_bytes(from, from_end);
}

std::string read_app_state_data(const std::string &app_state_path)
{
	std::ostringstream buffer;
	std::ifstream state_file(app_state_path, std::ios::in);
	if (state_file.is_open()) {
		buffer << state_file.rdbuf();
		state_file.close();
		return buffer.str();
	}
	return "";
}

void write_app_state_data(const std::string &app_state_path, std::string updated_status)
{
	std::ofstream out_state_file;
	out_state_file.open(app_state_path, std::ios::trunc | std::ios::out);
	if (out_state_file.is_open()) {
		out_state_file << updated_status << "\n";
		out_state_file.flush();
		out_state_file.close();
	}
}

void limit_log_file_size(const std::string &log_file, size_t limit)
{
	long size = 0;
	FILE *fp = fopen(log_file.c_str(), "r");
	if (fp != NULL) {
		if (fseek(fp, 0, SEEK_END) != -1)
			size = ftell(fp);
		fclose(fp);
	}
	if (size > limit) {
		const std::string old_log_name = log_file + ".old";
		remove(old_log_name.c_str());
		rename(log_file.c_str(), old_log_name.c_str());
	}
}

void ipc_freeze_callback(const std::string &app_state_dir, const std::string &call_name, int total_time, int obs_time)
{
	static const std::string flag_name = "detected";
	static const std::string flag_value = "ipc_freeze";

	static const std::string call_log_path = app_state_dir + "\\long_calls.txt";
	static const std::string app_state_path = app_state_dir + "\\appState";
	static const auto pid = ::getpid();

	const bool freeze_detected = obs_time < 0;

	static std::mutex file_mutex;
	std::unique_lock lock(file_mutex);

	std::string current_status = read_app_state_data(app_state_path);
	if (current_status.size() != 0) {
		std::string updated_status = "";
		std::string existing_flag_value = "";
		try {
			nlohmann::json jsonEntry = nlohmann::json::parse(current_status);

			try {
				existing_flag_value = jsonEntry.at(flag_name);
			} catch (...) {
			}

			if (freeze_detected) {
				if (existing_flag_value.empty())
					jsonEntry[flag_name] = flag_value;
			} else {
				if (existing_flag_value.compare(flag_value) == 0)
					jsonEntry[flag_name] = "";
			}
			updated_status = jsonEntry.dump(-1);
			write_app_state_data(app_state_path, updated_status);
		} catch (...) {
		}
	}

	try {
		static bool limited = false;
		if (!limited) {
			limited = true;
			limit_log_file_size(call_log_path, 1024 * 1024);
		}

		std::ofstream out_state_file;
		out_state_file.open(call_log_path, std::ios::app | std::ios::out);
		if (out_state_file.is_open()) {
			const auto now = std::chrono::system_clock::now();
			const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
			char time_buf[256];
			const auto time = std::chrono::system_clock::to_time_t(now);
			::strftime(time_buf, 256, "%Y-%m-%d %H:%M:%S", std::localtime(&time));

			out_state_file << "[" << time_buf << "." << std::setw(3) << std::setfill('0') << (now_ms.count() % 1000) << "] [pid:" << pid
				       << ", tid:" << std::this_thread::get_id() << "] " << (freeze_detected ? "(freeze) " : "") << call_name
				       << ", total:" << total_time << "ms, obs:" << obs_time << "ms" << std::endl;
		}
	} catch (...) {
	}
}
