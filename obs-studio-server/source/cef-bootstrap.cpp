#include "cef-subprocess.hpp"

#include <include/cef_sandbox_win.h>
#define OBS_BROWSER_SANDBOX_HOST_IMPLEMENTATION
#include <obs-browser-sandbox.h>

#include <corecrt_startup.h>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

obs_browser_sandbox_info_create_proc host_sandbox_info = nullptr;

std::filesystem::path ExecutablePath()
{
	std::vector<wchar_t> buffer(512);
	while (buffer.size() <= 32768) {
		const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (!length)
			break;
		if (length < buffer.size())
			return std::filesystem::canonical(std::wstring(buffer.data(), length));
		buffer.resize(buffer.size() * 2);
	}
	return {};
}

HMODULE LoadInstalledModule(bool child)
{
	try {
		const auto executable = ExecutablePath();
		if (executable.empty())
			return nullptr;
		const auto root = executable.parent_path();
		const auto requested = child ? osn::cef::BrowserChildPath(executable) : root / L"osncore.dll";
		const auto directory = std::filesystem::canonical(requested.parent_path());
		const auto path = std::filesystem::canonical(requested);
		if (!osn::cef::IsInstallOwnedPath(root, path) || _wcsicmp(directory.c_str(), path.parent_path().c_str()) ||
		    _wcsicmp(requested.filename().c_str(), path.filename().c_str())) {
			std::wcerr << L"Unexpected installed module path: " << path << std::endl;
			return nullptr;
		}
		HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
		if (!module) {
			const DWORD error = GetLastError();
			std::wcerr << L"Unable to load " << path << L", Windows error " << error << std::endl;
		}
		return module;
	} catch (const std::filesystem::filesystem_error &error) {
		std::cerr << "Could not resolve the installed OSN runtime: " << error.what() << std::endl;
		return nullptr;
	}
}

} // namespace

extern "C" uint32_t __cdecl obs_browser_sandbox_abi_version()
{
	return OBS_BROWSER_SANDBOX_ABI_VERSION;
}

extern "C" void *__cdecl obs_browser_sandbox_info_create()
{
	return host_sandbox_info ? host_sandbox_info() : nullptr;
}

extern "C" void __cdecl obs_browser_sandbox_info_destroy(void *) {}

extern "C" int RunConsoleMain(int argc, char *argv[], void *sandbox_info, cef_version_info_t *version_info)
{
	// Read the wide arguments without loading SHELL32 into the sandboxed renderer.
	if (!sandbox_info || _configure_wide_argv(_crt_argv_unexpanded_arguments) || !__wargv)
		return EXIT_FAILURE;
	int count = 0;
	while (__wargv[count])
		++count;
	const auto invocation = osn::cef::ClassifyInvocation(count, __wargv);
	if (invocation.kind == osn::cef::InvocationKind::Invalid) {
		std::cerr << "Rejected CEF invocation: " << invocation.error;
		if (invocation.sandbox_opt_out)
			std::cerr << "; argv=" << osn::cef::RenderInvocationArguments(argc, argv);
		std::cerr << std::endl;
		return EXIT_FAILURE;
	}

	const bool child = invocation.kind == osn::cef::InvocationKind::Child;
	if (child && !SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS))
		return EXIT_FAILURE;
	const auto module = LoadInstalledModule(child);
	if (!module)
		return EXIT_FAILURE;
	if (child) {
		const auto run = reinterpret_cast<obs_browser_execute_subprocess_proc>(GetProcAddress(module, "obs_browser_execute_subprocess"));
		if (!run) {
			std::cerr << "Missing obs_browser_execute_subprocess in the OBS browser child DLL" << std::endl;
			return EXIT_FAILURE;
		}
		const int result = run(sandbox_info);
		return result < 0 ? EXIT_FAILURE : result;
	}

	host_sandbox_info = reinterpret_cast<obs_browser_sandbox_info_create_proc>(GetProcAddress(module, "OsnGetBrowserSandboxInfo"));
	const auto run = reinterpret_cast<decltype(&RunConsoleMain)>(GetProcAddress(module, "RunConsoleMain"));
	if (!run || !host_sandbox_info) {
		std::cerr << "Missing OSN entry points in osncore.dll" << std::endl;
		return EXIT_FAILURE;
	}
	return run(argc, argv, sandbox_info, version_info);
}

extern "C" int RunWinMain(HINSTANCE, LPTSTR, int, void *sandbox_info, cef_version_info_t *version_info)
{
	if (_configure_narrow_argv(_crt_argv_unexpanded_arguments) || !__argv)
		return EXIT_FAILURE;
	return RunConsoleMain(__argc, __argv, sandbox_info, version_info);
}
