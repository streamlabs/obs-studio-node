#include "cef-sandbox-host.hpp"

#ifdef _WIN32

#include "cef-subprocess.hpp"
#include "token-label-policy.hpp"

#include <util/base.h>

#include <ShlObj.h>
#include <Shellapi.h>
#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

void *bootstrap_sandbox_info = nullptr;

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

// Retain the old export for DLL consumers. The NVIDIA driver normally reads
// this value from the executable, which is now CEF's bootstrap launcher;
// packaged GPU preference behavior still needs validation.
extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = InitialNvOptimusPreference();

namespace osn::cef {

namespace {

bool IsProcessElevated(HANDLE token)
{
	TOKEN_ELEVATION elevation = {};
	DWORD size = 0;
	return GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) && elevation.TokenIsElevated;
}

// Bootstrap has already initialized the broker sandbox before entering the
// host DLL. At High integrity its NO_READ_UP token label blocks Windows
// Graphics Capture, so retain the existing narrow workaround here.
void RestoreElevatedCaptureTokenAccess()
{
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | READ_CONTROL | WRITE_OWNER, &token)) {
		blog(LOG_WARNING, "[cef-sandbox] OpenProcessToken failed with Windows error %lu", GetLastError());
		return;
	}

	if (IsProcessElevated(token)) {
		const auto policy = CaptureTokenLabelPolicy(token);
		if (policy && (policy->mask & SYSTEM_MANDATORY_LABEL_NO_READ_UP)) {
			TokenLabelPolicy updated_policy = *policy;
			updated_policy.mask &= ~static_cast<DWORD>(SYSTEM_MANDATORY_LABEL_NO_READ_UP);
			if (SetTokenLabelPolicy(token, updated_policy))
				blog(LOG_INFO, "[cef-sandbox] Cleared NO_READ_UP from token integrity label policy (0x%lx -> 0x%lx)", policy->mask,
				     updated_policy.mask);
			else
				blog(LOG_WARNING, "[cef-sandbox] Failed to clear NO_READ_UP from token integrity label policy, Windows error %lu",
				     GetLastError());
		}
	}

	CloseHandle(token);
}

} // namespace

void SetBootstrapSandboxInfo(void *sandbox_info)
{
	bootstrap_sandbox_info = sandbox_info;
}

} // namespace osn::cef

extern "C" __declspec(dllexport) void *__cdecl OsnGetBrowserSandboxInfo()
{
	// The CEF bootstrap owns this object for the process lifetime. The plugin
	// retains its existing ABI but receives a borrowed pointer.
	if (bootstrap_sandbox_info)
		osn::cef::RestoreElevatedCaptureTokenAccess();
	return bootstrap_sandbox_info;
}

#endif
