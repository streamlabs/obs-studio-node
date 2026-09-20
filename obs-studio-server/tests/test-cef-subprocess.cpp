#include "cef-subprocess.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <initializer_list>

namespace {

osn::cef::Invocation Classify(std::initializer_list<const char *> arguments)
{
	return osn::cef::ClassifyInvocation(static_cast<int>(arguments.size()), arguments.begin());
}

} // namespace

TEST_CASE("Normal OSN startup is not classified as a CEF child", "[cef-sandbox]")
{
	const auto invocation = Classify({"obs64.exe", R"(\\.\pipe\slobs)", "1.2.3"});
	CHECK(invocation.kind == osn::cef::InvocationKind::Normal);
}

TEST_CASE("CEF child process types are accepted", "[cef-sandbox]")
{
	for (const char *type : {"--type=renderer", "--type=gpu-process", "--type=utility", "--type=crashpad-handler"}) {
		CAPTURE(type);
		const auto invocation = Classify({"obs64.exe", type, "--some-cef-switch"});
		CHECK(invocation.kind == osn::cef::InvocationKind::Child);
	}
}

TEST_CASE("Malformed CEF child invocations fail closed", "[cef-sandbox]")
{
	SECTION("missing command line")
	{
		CHECK(osn::cef::ClassifyInvocation(0, nullptr).kind == osn::cef::InvocationKind::Invalid);
	}

	SECTION("missing process type value")
	{
		CHECK(Classify({"obs64.exe", "--type="}).kind == osn::cef::InvocationKind::Invalid);
	}

	SECTION("separate process type value")
	{
		CHECK(Classify({"obs64.exe", "--type", "renderer"}).kind == osn::cef::InvocationKind::Invalid);
	}

	SECTION("duplicate process type")
	{
		CHECK(Classify({"obs64.exe", "--type=renderer", "--type=utility"}).kind == osn::cef::InvocationKind::Invalid);
	}

	SECTION("unknown process type")
	{
		CHECK(Classify({"obs64.exe", "--type=zygote"}).kind == osn::cef::InvocationKind::Invalid);
		CHECK(Classify({"obs64.exe", "--type=Renderer"}).kind == osn::cef::InvocationKind::Invalid);
	}

	SECTION("sandbox opt out")
	{
		for (const auto invocation : {
			     Classify({"obs64.exe", "--type=renderer", "--no-sandbox"}),
			     Classify({"obs64.exe", "--no-sandbox"}),
			     Classify({"obs64.exe", "--type=renderer", "--no-sandbox=1"}),
		     }) {
			CHECK(invocation.kind == osn::cef::InvocationKind::Invalid);
			CHECK(invocation.sandbox_opt_out);
		}

		const auto malformed = Classify({"obs64.exe", "--type", "renderer", "--no-sandbox"});
		CHECK(malformed.kind == osn::cef::InvocationKind::Invalid);
		CHECK(malformed.sandbox_opt_out);
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
	const wchar_t *renderer[] = {L"obs64.exe", L"--type=renderer"};
	const wchar_t *malformed_type[] = {L"obs64.exe", L"--type", L"renderer"};
	const wchar_t *sandbox_opt_out[] = {L"obs64.exe", L"--no-sandbox=1"};

	CHECK_FALSE(osn::cef::ContainsCefProcessSwitch(3, normal));
	CHECK(osn::cef::ContainsCefProcessSwitch(2, renderer));
	CHECK(osn::cef::ContainsCefProcessSwitch(3, malformed_type));
	CHECK(osn::cef::ContainsCefProcessSwitch(2, sandbox_opt_out));
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
