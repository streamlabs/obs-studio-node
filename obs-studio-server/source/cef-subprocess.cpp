#include "cef-subprocess.hpp"

#include <array>
#include <cwchar>

namespace osn::cef {
namespace {

// Keep this list pinned to the supported CEF 6533/6613 packages. CEF may
// relaunch the main executable for its embedded Crashpad handler in addition
// to Content child processes.
constexpr std::array<std::string_view, 4> allowed_process_types = {
	"crashpad-handler",
	"gpu-process",
	"renderer",
	"utility",
};

bool IsAllowedProcessType(std::string_view process_type)
{
	for (const auto allowed_type : allowed_process_types) {
		if (process_type == allowed_type)
			return true;
	}

	return false;
}

std::string RenderArgument(const char *argument)
{
	if (!argument)
		return "<null>";

	constexpr char hex[] = "0123456789ABCDEF";
	std::string result{"\""};
	for (const unsigned char character : std::string_view(argument)) {
		switch (character) {
		case '\\':
			result.append("\\\\");
			break;
		case '\"':
			result.append("\\\"");
			break;
		case '\n':
			result.append("\\n");
			break;
		case '\r':
			result.append("\\r");
			break;
		case '\t':
			result.append("\\t");
			break;
		default:
			if (character >= 0x20 && character <= 0x7e) {
				result.push_back(static_cast<char>(character));
			} else {
				result.append("\\x");
				result.push_back(hex[character >> 4]);
				result.push_back(hex[character & 0x0f]);
			}
		}
	}
	result.push_back('\"');
	return result;
}

} // namespace

Invocation ClassifyInvocation(int argc, const char *const argv[])
{
	Invocation result;
	if (argc < 1 || !argv) {
		result.kind = InvocationKind::Invalid;
		result.error = "CEF child process command line is missing";
		return result;
	}

	size_t type_argument_count = 0;
	bool no_sandbox = false;
	bool separate_type_argument = false;

	for (int index = 1; index < argc; ++index) {
		const std::string_view argument = argv[index] ? argv[index] : "";

		if (argument == "--no-sandbox" || argument.starts_with("--no-sandbox=")) {
			no_sandbox = true;
			continue;
		}

		if (argument == "--type") {
			separate_type_argument = true;
			continue;
		}

		constexpr std::string_view type_prefix = "--type=";
		if (argument.starts_with(type_prefix)) {
			++type_argument_count;
			result.process_type = argument.substr(type_prefix.size());
		}
	}
	result.sandbox_opt_out = no_sandbox;

	if (separate_type_argument) {
		result.kind = InvocationKind::Invalid;
		result.error = "CEF child process type must use --type=<value>";
		return result;
	}

	if (type_argument_count == 0) {
		if (no_sandbox) {
			result.kind = InvocationKind::Invalid;
			result.error = "--no-sandbox is not permitted";
		}
		return result;
	}

	if (type_argument_count != 1) {
		result.kind = InvocationKind::Invalid;
		result.error = "CEF child process command line contains duplicate --type arguments";
		return result;
	}

	if (result.process_type.empty()) {
		result.kind = InvocationKind::Invalid;
		result.error = "CEF child process type is empty";
		return result;
	}

	if (!IsAllowedProcessType(result.process_type)) {
		result.kind = InvocationKind::Invalid;
		result.error = "CEF child process type is not supported";
		return result;
	}

	if (no_sandbox) {
		result.kind = InvocationKind::Invalid;
		result.error = "--no-sandbox is not permitted for CEF child processes";
		return result;
	}

	result.kind = InvocationKind::Child;
	return result;
}

std::string RenderInvocationArguments(int argc, const char *const argv[])
{
	if (!argv)
		return "<missing argv>";
	if (argc <= 0)
		return "<empty argv>";

	std::string result;
	for (int index = 0; index < argc; ++index) {
		if (index)
			result.push_back(' ');
		result.append(RenderArgument(argv[index]));
	}
	return result;
}

bool ContainsCefProcessSwitch(int argc, const wchar_t *const argv[])
{
	if (argc < 1 || !argv)
		return false;

	for (int index = 1; index < argc; ++index) {
		const std::wstring_view argument = argv[index] ? argv[index] : L"";
		if (argument == L"--type" || argument.starts_with(L"--type=") || argument == L"--no-sandbox" || argument.starts_with(L"--no-sandbox=")) {
			return true;
		}
	}

	return false;
}

std::filesystem::path BrowserPluginPath(const std::filesystem::path &executable_path)
{
	return (executable_path.parent_path() / "obs-plugins" / "64bit" / "obs-browser.dll").lexically_normal();
}

bool IsInstallOwnedPath(const std::filesystem::path &install_root, const std::filesystem::path &candidate)
{
	const std::filesystem::path normalized_root = install_root.lexically_normal();
	const std::filesystem::path normalized_candidate = candidate.lexically_normal();

	if (normalized_root.empty() || normalized_candidate.empty())
		return false;

	auto root_component = normalized_root.begin();
	auto candidate_component = normalized_candidate.begin();
	for (; root_component != normalized_root.end(); ++root_component, ++candidate_component) {
		if (candidate_component == normalized_candidate.end() || _wcsicmp(root_component->c_str(), candidate_component->c_str()) != 0) {
			return false;
		}
	}

	// The installation root itself is not an install-owned child path.
	return candidate_component != normalized_candidate.end();
}

} // namespace osn::cef
