#include "cef-sandbox-host.hpp"

#ifdef _WIN32

#include "cef-subprocess.hpp"

#define OBS_BROWSER_SANDBOX_HOST_IMPLEMENTATION
#include <obs-browser-sandbox.h>
#undef OBS_BROWSER_SANDBOX_HOST_IMPLEMENTATION
#include <include/cef_sandbox_win.h>

#include <ShlObj.h>
#include <Shellapi.h>
#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

DWORD InitialNvOptimusPreference()
{
	int argument_count = 0;
	LPWSTR *arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
	if (!arguments)
		return 1;

	const bool cef_process = osn::cef::ContainsCefProcessSwitch(argument_count, arguments);
	LocalFree(arguments);
	if (cef_process)
		return 1;

	PWSTR roaming_path = nullptr;
	if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming_path)))
		return 1;

	std::filesystem::path file_path(roaming_path);
	CoTaskMemFree(roaming_path);
	file_path /= L"slobs-client\\basic.ini";

	std::ifstream file(file_path);
	std::string line;
	while (std::getline(file, line)) {
		const size_t setting_position = line.find("ForceGPUAsRenderDevice");
		if (setting_position == std::string::npos)
			continue;

		const size_t separator_position = line.find('=', setting_position);
		if (separator_position != std::string::npos && line.substr(separator_position + 1) == "false")
			return 0;
		break;
	}

	return 1;
}

} // namespace

// NVIDIA reads this exported value during process startup. Preserve the
// existing pre-main behavior for the normal OSN host, but do not touch the
// user profile for CEF child invocations.
extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = InitialNvOptimusPreference();

extern "C" __declspec(dllexport) uint32_t __cdecl obs_browser_sandbox_abi_version(void)
{
	return OBS_BROWSER_SANDBOX_ABI_VERSION;
}

extern "C" __declspec(dllexport) void *__cdecl obs_browser_sandbox_info_create(void)
{
	return cef_sandbox_info_create();
}

extern "C" __declspec(dllexport) void __cdecl obs_browser_sandbox_info_destroy(void *sandbox_info)
{
	cef_sandbox_info_destroy(sandbox_info);
}

namespace osn::cef {
namespace {

constexpr int dispatch_failure = EXIT_FAILURE;

void ReportWindowsError(const char *operation)
{
	const DWORD error = GetLastError();
	std::cerr << operation << " failed with Windows error " << error << std::endl;
}

bool PathsEqual(const std::filesystem::path &left, const std::filesystem::path &right)
{
	const std::wstring left_path = left.native();
	const std::wstring right_path = right.native();
	return _wcsicmp(left_path.c_str(), right_path.c_str()) == 0;
}

bool GetExecutablePath(std::filesystem::path &result)
{
	std::vector<wchar_t> buffer(512);
	for (;;) {
		SetLastError(ERROR_SUCCESS);
		const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0) {
			ReportWindowsError("GetModuleFileNameW");
			return false;
		}

		if (length < buffer.size() - 1) {
			result = std::wstring(buffer.data(), length);
			return true;
		}

		if (buffer.size() >= 32768) {
			SetLastError(ERROR_INSUFFICIENT_BUFFER);
			ReportWindowsError("GetModuleFileNameW");
			return false;
		}

		buffer.resize(buffer.size() * 2);
	}
}

int ExecuteSubprocess()
{
	void *sandbox_info = obs_browser_sandbox_info_create();
	if (!sandbox_info) {
		std::cerr << "cef_sandbox_info_create returned null" << std::endl;
		return dispatch_failure;
	}

	auto destroy_sandbox_info = [&]() {
		if (sandbox_info) {
			obs_browser_sandbox_info_destroy(sandbox_info);
			sandbox_info = nullptr;
		}
	};

	std::filesystem::path executable_path;
	if (!GetExecutablePath(executable_path)) {
		destroy_sandbox_info();
		return dispatch_failure;
	}

	std::error_code error;
	executable_path = std::filesystem::canonical(executable_path, error);
	if (error) {
		std::cerr << "Could not canonicalize obs64.exe path: " << error.message() << std::endl;
		destroy_sandbox_info();
		return dispatch_failure;
	}

	const std::filesystem::path install_root = executable_path.parent_path();
	std::filesystem::path plugin_directory = install_root / "obs-plugins" / "64bit";
	plugin_directory = std::filesystem::canonical(plugin_directory, error);
	if (error) {
		std::cerr << "Could not resolve the packaged obs-browser directory: " << error.message() << std::endl;
		destroy_sandbox_info();
		return dispatch_failure;
	}
	if (!IsInstallOwnedPath(install_root, plugin_directory)) {
		std::cerr << "Packaged obs-browser directory resolved outside the installation directory" << std::endl;
		destroy_sandbox_info();
		return dispatch_failure;
	}

	const std::filesystem::path requested_plugin_path = BrowserPluginPath(executable_path);
	const std::filesystem::path plugin_path = std::filesystem::canonical(requested_plugin_path, error);
	if (error) {
		std::cerr << "Could not resolve packaged obs-browser.dll: " << error.message() << std::endl;
		destroy_sandbox_info();
		return dispatch_failure;
	}

	if (!IsInstallOwnedPath(install_root, plugin_path) || !PathsEqual(plugin_path.parent_path(), plugin_directory)) {
		std::cerr << "Packaged obs-browser.dll resolved outside the installation directory" << std::endl;
		destroy_sandbox_info();
		return dispatch_failure;
	}

	if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)) {
		ReportWindowsError("SetDefaultDllDirectories");
		destroy_sandbox_info();
		return dispatch_failure;
	}

	DLL_DIRECTORY_COOKIE plugin_cookie = AddDllDirectory(plugin_directory.c_str());
	if (!plugin_cookie) {
		ReportWindowsError("AddDllDirectory");
		destroy_sandbox_info();
		return dispatch_failure;
	}

	HMODULE browser_module = LoadLibraryExW(plugin_path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
	if (!browser_module) {
		ReportWindowsError("LoadLibraryExW(obs-browser.dll)");
		RemoveDllDirectory(plugin_cookie);
		destroy_sandbox_info();
		return dispatch_failure;
	}

	const auto execute_subprocess = reinterpret_cast<obs_browser_execute_subprocess_proc>(GetProcAddress(browser_module, "obs_browser_execute_subprocess"));
	if (!execute_subprocess) {
		ReportWindowsError("GetProcAddress(obs_browser_execute_subprocess)");
		FreeLibrary(browser_module);
		RemoveDllDirectory(plugin_cookie);
		destroy_sandbox_info();
		return dispatch_failure;
	}

	const int exit_code = execute_subprocess(sandbox_info);
	destroy_sandbox_info();
	FreeLibrary(browser_module);
	RemoveDllDirectory(plugin_cookie);

	if (exit_code < 0) {
		std::cerr << "CEF child dispatcher returned an invalid negative exit code: " << exit_code << std::endl;
		return dispatch_failure;
	}

	return exit_code;
}

} // namespace

std::optional<int> DispatchSubprocessIfNeeded(int argc, char *argv[])
{
	const Invocation invocation = ClassifyInvocation(argc, argv);
	if (invocation.kind == InvocationKind::Normal)
		return std::nullopt;

	if (invocation.kind == InvocationKind::Invalid) {
		std::cerr << "Rejected CEF child process invocation: " << invocation.error << std::endl;
		return dispatch_failure;
	}

	return ExecuteSubprocess();
}

} // namespace osn::cef

#endif
