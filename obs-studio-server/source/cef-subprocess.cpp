#include "cef-subprocess.hpp"

#include <array>
#include <cwchar>

namespace osn::cef {
namespace {

// Keep this list pinned to the supported CEF package. CEF may
// relaunch the main executable for its embedded Crashpad handler in addition
// to Content child processes.
constexpr std::array<std::string_view, 4> allowed_process_types = {
	"crashpad-handler",
	"gpu-process",
	"renderer",
	"utility",
};

template<typename Character> bool IsAllowedProcessType(std::basic_string_view<Character> process_type)
{
	for (const auto allowed_type : allowed_process_types) {
		if (process_type.size() != allowed_type.size())
			continue;
		bool matches = true;
		for (size_t index = 0; index < process_type.size(); ++index) {
			if (process_type[index] != Character(allowed_type[index])) {
				matches = false;
				break;
			}
		}
		if (matches)
			return true;
	}

	return false;
}

bool IsCommandLineWhitespace(char character)
{
	return std::string_view{" \t\n\r\f\v"}.find(character) != std::string_view::npos;
}

bool IsCommandLineWhitespace(wchar_t character)
{
	constexpr std::wstring_view whitespace =
		L" \t\n\r\f\v\u0085\u00a0\u1680\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200a\u2028\u2029\u202f\u205f\u3000";
	return whitespace.find(character) != std::wstring_view::npos;
}

// Chromium trims each Windows argument before parsing switches. The narrow CRT
// argv preserves ASCII whitespace; the wide command line also has Unicode whitespace.
template<typename Character> std::basic_string_view<Character> TrimCommandLineWhitespace(std::basic_string_view<Character> argument)
{
	while (!argument.empty() && IsCommandLineWhitespace(argument.front()))
		argument.remove_prefix(1);
	while (!argument.empty() && IsCommandLineWhitespace(argument.back()))
		argument.remove_suffix(1);
	return argument;
}

template<typename Character> bool IsSwitchTerminator(std::basic_string_view<Character> argument)
{
	argument = TrimCommandLineWhitespace(argument);
	return argument.size() == 2 && argument[0] == Character('-') && argument[1] == Character('-');
}

// Chromium's Windows command line parser accepts all three switch prefixes
// and folds ASCII switch names to lowercase before looking them up.
template<typename Character> std::basic_string_view<Character> SwitchBody(std::basic_string_view<Character> argument)
{
	argument = TrimCommandLineWhitespace(argument);
	if (argument.empty() || (argument.front() != Character('-') && argument.front() != Character('/')))
		return {};

	const size_t prefix_size = argument.size() > 1 && argument[0] == Character('-') && argument[1] == Character('-') ? 2 : 1;
	return argument.substr(prefix_size);
}

template<typename Character> bool IsSwitch(std::basic_string_view<Character> body, std::string_view name)
{
	if (body.size() < name.size())
		return false;

	for (size_t index = 0; index < name.size(); ++index) {
		Character character = body[index];
		if (character >= Character('A') && character <= Character('Z'))
			character = static_cast<Character>(character + Character('a' - 'A'));
		if (character != Character(name[index]))
			return false;
	}

	return body.size() == name.size() || body[name.size()] == Character('=');
}

void AssignProcessType(Invocation &result, std::string_view process_type)
{
	result.process_type.assign(process_type.data(), process_type.size());
}

void AssignProcessType(Invocation &result, std::wstring_view process_type)
{
	result.process_type.clear();
	for (const wchar_t character : process_type) {
		if (character > 0x7f) {
			result.process_type.clear();
			return;
		}
		result.process_type.push_back(static_cast<char>(character));
	}
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

template<typename Character> Invocation ClassifyInvocationImpl(int argc, const Character *const argv[])
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
	std::basic_string_view<Character> process_type;

	for (int index = 1; index < argc; ++index) {
		const std::basic_string_view<Character> argument = argv[index] ? std::basic_string_view<Character>{argv[index]}
									       : std::basic_string_view<Character>{};
		if (IsSwitchTerminator(argument))
			break;
		const std::basic_string_view<Character> switch_body = SwitchBody(argument);

		if (IsSwitch(switch_body, std::string_view{"no-sandbox"})) {
			no_sandbox = true;
			continue;
		}

		constexpr std::string_view type_name = "type";
		if (IsSwitch(switch_body, type_name)) {
			if (switch_body.size() == type_name.size()) {
				separate_type_argument = true;
			} else {
				++type_argument_count;
				process_type = switch_body.substr(type_name.size() + 1);
				AssignProcessType(result, process_type);
			}
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

	if (process_type.empty()) {
		result.kind = InvocationKind::Invalid;
		result.error = "CEF child process type is empty";
		return result;
	}

	if (!IsAllowedProcessType(process_type)) {
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

} // namespace

Invocation ClassifyInvocation(int argc, const char *const argv[])
{
	return ClassifyInvocationImpl(argc, argv);
}

Invocation ClassifyInvocation(int argc, const wchar_t *const argv[])
{
	return ClassifyInvocationImpl(argc, argv);
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
		if (IsSwitchTerminator(argument))
			break;
		const std::wstring_view switch_body = SwitchBody(argument);
		if (IsSwitch(switch_body, std::string_view{"type"}) || IsSwitch(switch_body, std::string_view{"no-sandbox"})) {
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
