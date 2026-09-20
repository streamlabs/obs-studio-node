#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace osn::cef {

enum class InvocationKind {
	Normal,
	Child,
	Invalid,
};

struct Invocation {
	InvocationKind kind = InvocationKind::Normal;
	std::string_view process_type;
	std::string error;
	bool sandbox_opt_out = false;
};

Invocation ClassifyInvocation(int argc, const char *const argv[]);
std::string RenderInvocationArguments(int argc, const char *const argv[]);
bool ContainsCefProcessSwitch(int argc, const wchar_t *const argv[]);
std::filesystem::path BrowserPluginPath(const std::filesystem::path &executable_path);
bool IsInstallOwnedPath(const std::filesystem::path &install_root, const std::filesystem::path &candidate);

} // namespace osn::cef
