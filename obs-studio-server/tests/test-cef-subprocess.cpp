#include "cef-subprocess.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>

namespace {

osn::cef::Invocation Classify(std::initializer_list<const char *> arguments)
{
	return osn::cef::ClassifyInvocation(static_cast<int>(arguments.size()), arguments.begin());
}

osn::cef::Invocation ClassifyWide(std::initializer_list<const wchar_t *> arguments)
{
	return osn::cef::ClassifyInvocation(static_cast<int>(arguments.size()), arguments.begin());
}

constexpr std::wstring_view chromium_wide_whitespace =
	L" \t\n\r\f\v\u0085\u00a0\u1680\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200a\u2028\u2029\u202f\u205f\u3000";

} // namespace

TEST_CASE("Normal OSN startup is not classified as a CEF child", "[cef-sandbox]")
{
	const auto invocation = Classify({"obs64.exe", R"(\\.\pipe\slobs)", "1.2.3"});
	CHECK(invocation.kind == osn::cef::InvocationKind::Normal);
	const auto after_terminator = Classify({"obs64.exe", "pipe", "version", " -- ", "--type=renderer", "--no-sandbox"});
	CHECK(after_terminator.kind == osn::cef::InvocationKind::Normal);
	CHECK_FALSE(after_terminator.sandbox_opt_out);
	CHECK(ClassifyWide({L"obs64.exe", L"pipe", L"version", L"\u3000--\u00a0", L"/TYPE=renderer", L"/no-sandbox"}).kind == osn::cef::InvocationKind::Normal);
	CHECK(ClassifyWide({L"obs64.exe", L"--type=renderer", L"--", L"/no-sandbox"}).kind == osn::cef::InvocationKind::Child);
}

TEST_CASE("CEF child process types are accepted", "[cef-sandbox]")
{
	for (const char *type : {"--type=renderer", "-type=gpu-process", "/type=utility", "/TYPE=crashpad-handler"}) {
		CAPTURE(type);
		const auto invocation = Classify({"obs64.exe", type, "--some-cef-switch"});
		CHECK(invocation.kind == osn::cef::InvocationKind::Child);
	}

	const auto padded = Classify({"obs64.exe", " \t/type=renderer\r ", "--some-cef-switch"});
	CHECK(padded.kind == osn::cef::InvocationKind::Child);
	CHECK(padded.process_type == "renderer");
}

TEST_CASE("Malformed CEF child invocations fail closed", "[cef-sandbox]")
{
	SECTION("missing command line")
	{
		CHECK(osn::cef::ClassifyInvocation(0, static_cast<const char *const *>(nullptr)).kind == osn::cef::InvocationKind::Invalid);
		CHECK(osn::cef::ClassifyInvocation(0, static_cast<const wchar_t *const *>(nullptr)).kind == osn::cef::InvocationKind::Invalid);
	}

	SECTION("missing process type value")
	{
		CHECK(Classify({"obs64.exe", "--type="}).kind == osn::cef::InvocationKind::Invalid);
	}

	SECTION("separate process type value")
	{
		CHECK(Classify({"obs64.exe", "--type", "renderer"}).kind == osn::cef::InvocationKind::Invalid);
		CHECK(Classify({"obs64.exe", "-type", "renderer"}).kind == osn::cef::InvocationKind::Invalid);
		CHECK(Classify({"obs64.exe", "/TYPE", "renderer"}).kind == osn::cef::InvocationKind::Invalid);
	}

	SECTION("duplicate process type")
	{
		CHECK(Classify({"obs64.exe", "--type=renderer", "--type=utility"}).kind == osn::cef::InvocationKind::Invalid);
		CHECK(Classify({"obs64.exe", "-type=renderer", "/TYPE=utility"}).kind == osn::cef::InvocationKind::Invalid);
		CHECK(Classify({"obs64.exe", "--type=renderer", " \t/type=utility\r"}).kind == osn::cef::InvocationKind::Invalid);
	}

	SECTION("unknown process type")
	{
		CHECK(Classify({"obs64.exe", "--type=zygote"}).kind == osn::cef::InvocationKind::Invalid);
		CHECK(Classify({"obs64.exe", "--type=Renderer"}).kind == osn::cef::InvocationKind::Invalid);
	}

	SECTION("sandbox opt out")
	{
		for (const char *opt_out :
		     {"--no-sandbox", "--no-sandbox=1", "-no-sandbox", "-no-sandbox=1", "/no-sandbox", "/no-sandbox=1", "/NO-SANDBOX=0"}) {
			CAPTURE(opt_out);
			for (const auto invocation : {Classify({"obs64.exe", "-type=renderer", opt_out}), Classify({"obs64.exe", opt_out})}) {
				CHECK(invocation.kind == osn::cef::InvocationKind::Invalid);
				CHECK(invocation.sandbox_opt_out);
			}
		}

		const auto malformed = Classify({"obs64.exe", "--type", "renderer", "--no-sandbox"});
		CHECK(malformed.kind == osn::cef::InvocationKind::Invalid);
		CHECK(malformed.sandbox_opt_out);

		for (const char whitespace : std::string_view{" \t\n\r\f\v"}) {
			CAPTURE(static_cast<unsigned int>(whitespace));
			std::string opt_out(1, whitespace);
			opt_out += "-no-sandbox=1";
			opt_out.push_back(whitespace);
			for (const auto invocation : {Classify({"obs64.exe", "--type=renderer", opt_out.c_str()}), Classify({"obs64.exe", opt_out.c_str()})}) {
				CHECK(invocation.kind == osn::cef::InvocationKind::Invalid);
				CHECK(invocation.sandbox_opt_out);
			}
		}
	}

	SECTION("switch names require an exact match")
	{
		CHECK(Classify({"obs64.exe", "-typewriter=renderer"}).kind == osn::cef::InvocationKind::Normal);
		CHECK(Classify({"obs64.exe", "/no-sandboxed"}).kind == osn::cef::InvocationKind::Normal);
		CHECK(Classify({"obs64.exe", "type=renderer"}).kind == osn::cef::InvocationKind::Normal);
	}
}

TEST_CASE("Wide CEF child invocations honor Chromium whitespace", "[cef-sandbox]")
{
	const auto child = ClassifyWide({L"obs64.exe", L"\u3000/TYPE=renderer\u00a0"});
	CHECK(child.kind == osn::cef::InvocationKind::Child);
	CHECK(child.process_type == "renderer");

	CHECK(ClassifyWide({L"obs64.exe", L"--type=renderer", L"\u2007-type=utility\u0085"}).kind == osn::cef::InvocationKind::Invalid);
	CHECK(ClassifyWide({L"obs64.exe", L"\u202f/type\u205f", L"renderer"}).kind == osn::cef::InvocationKind::Invalid);

	for (const wchar_t whitespace : chromium_wide_whitespace) {
		CAPTURE(static_cast<unsigned int>(whitespace));
		std::wstring opt_out(1, whitespace);
		opt_out += L"/NO-SANDBOX=0";
		opt_out.push_back(whitespace);
		for (const auto invocation :
		     {ClassifyWide({L"obs64.exe", L"--type=renderer", opt_out.c_str()}), ClassifyWide({L"obs64.exe", opt_out.c_str()})}) {
			CHECK(invocation.kind == osn::cef::InvocationKind::Invalid);
			CHECK(invocation.sandbox_opt_out);
		}
	}
}

TEST_CASE("Rejected CEF invocations render every argument safely", "[cef-sandbox]")
{
	const char *arguments[] = {"obs64.exe", "--type=renderer", "--no-sandbox=1", "line\nbreak", "quote\"slash\\", "\x01"};
	CHECK(osn::cef::RenderInvocationArguments(6, arguments) ==
	      "\"obs64.exe\" \"--type=renderer\" \"--no-sandbox=1\" \"line\\nbreak\" \"quote\\\"slash\\\\\" \"\\x01\"");

	const char *with_null[] = {"obs64.exe", nullptr};
	CHECK(osn::cef::RenderInvocationArguments(2, with_null) == R"("obs64.exe" <null>)");
	CHECK(osn::cef::RenderInvocationArguments(0, nullptr) == "<missing argv>");
}

TEST_CASE("Pre-main initialization recognizes CEF process switches", "[cef-sandbox]")
{
	const wchar_t *normal[] = {L"obs64.exe", L"socket-name", L"1.2.3"};

	CHECK_FALSE(osn::cef::ContainsCefProcessSwitch(3, normal));
	for (const wchar_t *cef_switch :
	     {L"--type=renderer", L"-type=renderer", L"/TYPE=renderer", L"--type", L"-type", L"/TYPE", L"--no-sandbox=1", L"-no-sandbox", L"/NO-SANDBOX=0"}) {
		CAPTURE(cef_switch);
		const wchar_t *arguments[] = {L"obs64.exe", cef_switch};
		CHECK(osn::cef::ContainsCefProcessSwitch(2, arguments));
	}
	for (const wchar_t whitespace : chromium_wide_whitespace) {
		CAPTURE(static_cast<unsigned int>(whitespace));
		std::wstring opt_out(1, whitespace);
		opt_out += L"/NO-SANDBOX=0";
		opt_out.push_back(whitespace);
		const wchar_t *arguments[] = {L"obs64.exe", opt_out.c_str()};
		CHECK(osn::cef::ContainsCefProcessSwitch(2, arguments));
	}
	const wchar_t *padded_type[] = {L"obs64.exe", L"\u3000--type=renderer\u00a0"};
	CHECK(osn::cef::ContainsCefProcessSwitch(2, padded_type));
	const wchar_t *near_match[] = {L"obs64.exe", L"/no-sandboxed"};
	CHECK_FALSE(osn::cef::ContainsCefProcessSwitch(2, near_match));
	const wchar_t *after_terminator[] = {L"obs64.exe", L"\u3000--\u00a0", L"/TYPE=renderer", L"/no-sandbox"};
	CHECK_FALSE(osn::cef::ContainsCefProcessSwitch(4, after_terminator));
}

TEST_CASE("Browser plugin path is fixed relative to obs64.exe", "[cef-sandbox]")
{
	const auto path = osn::cef::BrowserPluginPath(R"(C:\Program Files\Streamlabs\obs64.exe)");
	CHECK(path == R"(C:\Program Files\Streamlabs\obs-plugins\64bit\obs-browser.dll)");
}

TEST_CASE("Install-owned path validation is component-aware", "[cef-sandbox]")
{
	const std::filesystem::path root = R"(C:\Program Files\Streamlabs)";

	CHECK(osn::cef::IsInstallOwnedPath(root, root / "obs-plugins" / "64bit"));
	CHECK(osn::cef::IsInstallOwnedPath(root, root / "obs-plugins" / "64bit" / "obs-browser.dll"));
	CHECK_FALSE(osn::cef::IsInstallOwnedPath(root, root));
	CHECK_FALSE(osn::cef::IsInstallOwnedPath(root, R"(C:\Program Files\Streamlabs-evil\obs-browser.dll)"));
	CHECK_FALSE(osn::cef::IsInstallOwnedPath(root, R"(C:\Program Files\Streamlabs\obs-plugins\..\..\outside\obs-browser.dll)"));
}
