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

#include "controller.hpp"
#include <codecvt>
#include <fstream>
#include <sstream>
#include <locale>
#include <sstream>
#include <string>
#include "shared.hpp"
#include "utility.hpp"

static std::string serverBinaryPath = "";
static std::string serverWorkingPath = "";
std::wstring utfWorkingDir = L"";

#ifndef OSN_VERSION
#define OSN_VERSION "DEVMODE_VERSION"
#endif

#define GET_OSN_VERSION                                  \
	[]() {                                           \
		const char *__CHECK_EMPTY = OSN_VERSION; \
		if (strlen(__CHECK_EMPTY) < 3) {         \
			return "DEVMODE_VERSION";        \
		}                                        \
		return OSN_VERSION;                      \
	}()

#ifdef _WIN32
#include <direct.h>
#include <psapi.h>
#include <wchar.h>
#include <windows.h>
#else
#include <signal.h>
#include <libproc.h>
#include <iostream>
#include <spawn.h>
extern char **environ;
#endif

using namespace ipc;

#ifdef _WIN32
ProcessInfo spawn(const std::string &program, const std::string &commandLine, const std::string &workingDirectory)
{
	PROCESS_INFORMATION m_win32_processInformation = {0};
	STARTUPINFOW m_win32_startupInfo = {0};

	const std::wstring utfProgram(from_utf8_to_utf16_wide(program.c_str()));

	std::wstring utfCommandLine(from_utf8_to_utf16_wide(commandLine.c_str()));

	utfWorkingDir = std::wstring(from_utf8_to_utf16_wide(workingDirectory.c_str()));

	BOOL success = CreateProcessW(utfProgram.c_str(),
				      /* Note that C++11 says this is fine since an
		 * std::string is guaranteed to be null-terminated. */
				      &utfCommandLine[0], NULL, NULL, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, NULL,
				      utfWorkingDir.empty() ? NULL : utfWorkingDir.c_str(), &m_win32_startupInfo, &m_win32_processInformation);

	if (!success)
		return {};

	return ProcessInfo(reinterpret_cast<uint64_t>(m_win32_processInformation.hProcess), static_cast<uint64_t>(m_win32_processInformation.dwProcessId));
}

ProcessInfo open_process(uint64_t handle)
{
	ProcessInfo pi;
	DWORD flags = PROCESS_QUERY_INFORMATION | PROCESS_TERMINATE | PROCESS_VM_READ;

	pi.handle = (uint64_t)OpenProcess(flags, false, (DWORD)handle);
	return pi;
}

bool close_process(ProcessInfo pi)
{
	return !!CloseHandle((HANDLE)pi.handle);
}

/* Credit to this dude: https://stackoverflow.com/a/28056007/2255625 */
/* Basically Win32 doesn't provide a way to fetch the length of the
 * name of a module because it's a C-string */
std::string get_process_name(ProcessInfo pi)
{
	LPWSTR lpBuffer = NULL;
	DWORD dwBufferLength = 256;
	HANDLE hProcess = (HANDLE)pi.handle;
	HMODULE hModule;
	DWORD unused1;
	BOOL bSuccess;

	/* We rely on undocumented behavior here where
	 * enumerating a process' modules will provide
	 * the process HMODULE first every time. */
	bSuccess = EnumProcessModules(hProcess, &hModule, sizeof(hModule), &unused1);

	if (!bSuccess)
		return {};

	while (32768 >= dwBufferLength) {
		std::wstring lpBuffer(dwBufferLength, wchar_t());

		DWORD dwReturnLength = GetModuleFileNameExW(hProcess, hModule, &lpBuffer[0], dwBufferLength);

		if (!dwReturnLength)
			return {};

		if (dwBufferLength <= dwReturnLength) {
			/* Increased buffer exponentially.
			 * Notice this will eventually match
			 * a perfect 32768 which is the max
			 * length of an NTFS file path. */
			dwBufferLength <<= 1;
			continue;
		}

		/* Notice that these are expensive
		 * but they do shrink the buffer to
		 * match the string */
		return from_utf16_wide_to_utf8(lpBuffer.data());
	}

	/* Path too long */
	return {};
}

bool is_process_alive(ProcessInfo &pinfo)
{
	DWORD status;

	if (GetExitCodeProcess(reinterpret_cast<HANDLE>(pinfo.handle), &status)) {
		if (status == static_cast<uint64_t>(ProcessInfo::ExitCode::STILL_RUNNING)) {
			return true;
		}
		pinfo.exit_code = status;
		return false;
	}
	//could not get exit code status, so assign a generic one
	pinfo.exit_code = ProcessInfo::ExitCode::OTHER_ERROR;
	return false;
}

bool kill(ProcessInfo pinfo, uint32_t code, uint32_t &exitcode)
{
	return TerminateProcess(reinterpret_cast<HANDLE>(pinfo.handle), code);
}

std::string get_working_directory()
{
	DWORD dwRequiredSize = GetCurrentDirectoryW(0, NULL);
	std::wstring lpBuffer(dwRequiredSize, wchar_t());

	GetCurrentDirectoryW(dwRequiredSize, &lpBuffer[0]);

	return from_utf16_wide_to_utf8(lpBuffer.data());
}

std::string get_temp_directory()
{
	constexpr DWORD tmp_size = MAX_PATH + 1;
	std::wstring tmp(tmp_size, wchar_t());
	GetTempPathW(tmp_size, &tmp[0]);

	/* Here we resize an in-use string and then re-use it.
	 * Note this is only okay because the long path name
	 * will always be equal to or larger than the short
	 * path name */
	DWORD tmp_len = GetLongPathNameW(&tmp[0], NULL, 0);
	tmp.resize(tmp_len);

	/* Note that this isn't a hack to use the same buffer,
	 * it's explicitly meant to be used this way per MSDN. */
	GetLongPathNameW(&tmp[0], &tmp[0], tmp_len);

	return from_utf16_wide_to_utf8(tmp.data());
}

std::fstream open_file(std::string &file_path, std::fstream::openmode mode)
{
	return std::fstream(from_utf8_to_utf16_wide(file_path.c_str()), mode);
}

static void check_pid_file(std::string &pid_path)
{
	std::fstream::openmode mode = std::fstream::in | std::fstream::binary;

	std::fstream pid_file(open_file(pid_path, mode));

	union {
		uint64_t pid;
		char pid_char[sizeof(uint64_t)];
	};

	if (!pid_file)
		return;

	pid_file.read(&pid_char[0], 8);

	ProcessInfo pi = open_process(pid);

	if (pi.handle == 0)
		return;

	std::string name = get_process_name(pi);

	/* FIXME TODO I don't like globals */
	if (name == serverBinaryPath) {
		uint32_t dontcare = 0;
		kill(pi, -1, dontcare);
	}

	close_process(pi);
}

static void write_pid_file(std::string &pid_path, uint64_t pid)
{
	std::fstream::openmode mode = std::fstream::out | std::fstream::binary | std::fstream::trunc;

	std::fstream pid_file(open_file(pid_path, mode));

	if (!pid_file)
		return;

	pid_file.write(reinterpret_cast<char *>(&pid), sizeof(pid));
}

#endif

Controller::Controller() {}

Controller::~Controller() {}

std::shared_ptr<ipc::client> Controller::host(const std::string &uri)
{
	if (m_isServer)
		return nullptr;

	const std::string version = GET_OSN_VERSION;

	std::stringstream commandLine;
	commandLine << "\"" << serverBinaryPath << "\""
		    << " " << uri << " " << version;

	std::string workingDirectory;

#ifdef WIN32
	if (serverWorkingPath.empty())
		workingDirectory = get_working_directory();
	else
#endif
		workingDirectory = serverWorkingPath;

#ifdef WIN32
	// Test for existing process.
	std::string pid_path(get_temp_directory());
	pid_path.append("server.pid");

	check_pid_file(pid_path);

	procId = spawn(serverBinaryPath, commandLine.str(), workingDirectory);
	if (procId.id == 0) {
		return nullptr;
	}

	write_pid_file(pid_path, procId.id);

	// Connect
	std::shared_ptr<ipc::client> cl = connect(uri);
	if (!cl) { // Assume the server broke or was not allowed to run.
		disconnect();
		uint32_t exitcode;
		kill(procId, 0, exitcode);
		return nullptr;
	}
#else
    std::cout << "Controller::host workdir " << workingDirectory.c_str() << std::endl;
	g_util_osx->setServerWorkingDirectoryPath(workingDirectory);
	pid_t pids[2048];
	int bytes = proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pids));
	int n_proc = bytes / sizeof(pids[0]);
	for (int i = 0; i < n_proc; i++) {
		struct proc_bsdinfo proc;
		int st = proc_pidinfo(pids[i], PROC_PIDTBSDINFO, 0, &proc, PROC_PIDTBSDINFO_SIZE);
		if (st == PROC_PIDTBSDINFO_SIZE) {
			if (strcmp("obs64", proc.pbi_name) == 0) {
				if (pids[i] != 0)
					kill(pids[i], SIGKILL);
			}
		}
	}
    std::cout << "Controller::host setServerPath " << serverBinaryPath.c_str() << std::endl;

	pid_t pid;
	std::vector<char> uri_str(uri.c_str(), uri.c_str() + uri.size() + 1);
	char *argv[] = {"obs64", uri_str.data(), (char *)version.c_str(), (char *)serverBinaryPath.c_str(), NULL};
	remove(uri.c_str());

	int ret = posix_spawnp(&pid, serverBinaryPath.c_str(), NULL, NULL, argv, environ);
    std::cout << "Controller::host posix_spawnp " << ret << std::endl;
	// Connect
	std::shared_ptr<ipc::client> cl = connect(uri);
	if (!cl) { // Assume the server broke or was not allowed to run.
        std::cerr << "ipc::client fails" << std::endl;
		disconnect();
		uint32_t exitcode;
		kill(pid, SIGKILL);
		return nullptr;
	}
#endif

	m_isServer = true;
	return m_connection;
}

std::shared_ptr<ipc::client> Controller::connect(const std::string &uri)
{
	procId.exit_code = 0;
	if (m_isServer)
		return nullptr;

	if (m_connection)
		return nullptr;

	std::shared_ptr<ipc::client> cl;
	using std::chrono::high_resolution_clock;
	while (!cl) {
		try {
			std::string path;
#ifdef WIN32
			path = uri;
#else
			path = "/tmp/" + uri;
#endif
			cl = ipc::client::create(path);
		} catch (...) {
			cl = nullptr;
		}
		if (cl)
			break;

		if (procId.handle != 0) {
#ifdef WIN32
			// We are the owner of the server, but m_isServer is false for now.
			if (!is_process_alive(procId)) {
				break;
			}
#endif
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	if (!cl) {
		return nullptr;
	}

	m_connection = cl;
	return m_connection;
}

void Controller::disconnect()
{
	if (m_isServer) {
		m_connection->call_synchronous_helper("System", "Shutdown", {});
		m_isServer = false;
	}
	m_connection = nullptr;
}

DWORD Controller::GetExitCode()
{
	return procId.exit_code;
}

std::shared_ptr<ipc::client> Controller::GetConnection()
{
	return m_connection;
}

Napi::Value js_setServerPath(const Napi::CallbackInfo &info)
{
	if (info.Length() == 0) {
		Napi::Error::New(info.Env(), "Too few arguments, usage: setServerPath(<string> binaryPath[, <string> workingDirectoryPath = "
					     "get_working_directory()]).")
			.ThrowAsJavaScriptException();
		return info.Env().Undefined();
	} else if (info.Length() > 2) {
		Napi::Error::New(info.Env(), "Too many arguments.").ThrowAsJavaScriptException();
		return info.Env().Undefined();
	}

	if (!info[0].IsString()) {
		Napi::Error::New(info.Env(), "Argument 'binaryPath' must be of type 'String'.").ThrowAsJavaScriptException();
		return info.Env().Undefined();
	}
	serverBinaryPath = info[0].ToString().Utf8Value();
    std::cout << "js_setServerPath " << serverBinaryPath.c_str() << std::endl;

	if (info.Length() == 2) {
		if (!info[1].IsString()) {
			Napi::Error::New(info.Env(), "Argument 'workingDirectoryPath' must be of type 'String'.").ThrowAsJavaScriptException();
			return info.Env().Undefined();
		}

		serverWorkingPath = info[1].ToString().Utf8Value();
	} else {
#ifdef WIN32
		serverWorkingPath = get_working_directory();
#endif
	}

	return info.Env().Undefined();
}

Napi::Value js_connect(const Napi::CallbackInfo &info)
{
	if (info.Length() == 0) {
		Napi::Error::New(info.Env(), "Too few arguments, usage: connect(<string> uri).").ThrowAsJavaScriptException();
		return info.Env().Undefined();
	} else if (info.Length() > 1) {
		Napi::Error::New(info.Env(), "Too many arguments.").ThrowAsJavaScriptException();
		return info.Env().Undefined();
	} else if (!info[0].IsString()) {
		Napi::Error::New(info.Env(), "Argument 'uri' must be of type 'String'.").ThrowAsJavaScriptException();
		return info.Env().Undefined();
	}

	std::string uri = info[0].ToString().Utf8Value();
	auto cl = Controller::GetInstance().connect(uri);
	DWORD exit_code = Controller::GetInstance().GetExitCode();

#ifdef WIN32
	if (exit_code == STATUS_DLL_NOT_FOUND)
		exit_code = ProcessInfo::MISSING_DEPENDENCY;
#endif

	return Napi::Number::New(info.Env(), exit_code);
}

Napi::Value js_host(const Napi::CallbackInfo &info)
{
	if (info.Length() == 0) {
		Napi::Error::New(info.Env(), "Too few arguments, usage: host(uri).").ThrowAsJavaScriptException();
		return info.Env().Undefined();
	} else if (info.Length() > 1) {
		Napi::Error::New(info.Env(), "Too many arguments.").ThrowAsJavaScriptException();
		return info.Env().Undefined();
	} else if (!info[0].IsString()) {
		Napi::Error::New(info.Env(), "Argument 'uri' must be of type 'String'.").ThrowAsJavaScriptException();
		return info.Env().Undefined();
	}

	std::string uri = info[0].ToString().Utf8Value();
	auto cl = Controller::GetInstance().host(uri);
	DWORD exit_code = Controller::GetInstance().GetExitCode();

#ifdef WIN32
	if (exit_code == STATUS_DLL_NOT_FOUND)
		exit_code = ProcessInfo::MISSING_DEPENDENCY;
#endif

	return Napi::Number::New(info.Env(), exit_code);
}

Napi::Value js_disconnect(const Napi::CallbackInfo &info)
{
	Controller::GetInstance().disconnect();
	return info.Env().Undefined();
}

void Controller::Init(Napi::Env env, Napi::Object exports)
{
	auto obj = Napi::Object::New(env);
	obj.Set(Napi::String::New(env, "setServerPath"), Napi::Function::New(env, js_setServerPath));
	obj.Set(Napi::String::New(env, "connect"), Napi::Function::New(env, js_connect));
	obj.Set(Napi::String::New(env, "host"), Napi::Function::New(env, js_host));
	obj.Set(Napi::String::New(env, "disconnect"), Napi::Function::New(env, js_disconnect));
	exports.Set("IPC", obj);
}
