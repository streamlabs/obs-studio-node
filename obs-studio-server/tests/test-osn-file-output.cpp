#include <catch2/catch_test_macros.hpp>
#include "osn-file-output.hpp"
#include <util/platform.h>
#include <fstream>
#include <string>

// FindBestFilename resolves a recording path against both the filesystem and the paths already
// claimed by other live outputs. The second half is what stops dual output pointing two muxers at
// one file: the first output has not created its file yet when the second one starts, so a
// disk-only check sees nothing and hands out the same name.

namespace {
// Registers an output with the manager for as long as the test needs it. FindBestFilename only
// sees outputs that are in the manager, which is also what makes a stale claim harmless -- a
// destroyed output is gone from the map and therefore invisible.
class ScopedOutput {
public:
	ScopedOutput() : output(std::vector<std::string>{}) { osn::IFileOutput::Manager::GetInstance().allocate(&output); }
	~ScopedOutput() { osn::IFileOutput::Manager::GetInstance().free(&output); }

	osn::FileOutput *get() { return &output; }

private:
	osn::FileOutput output;
};

std::string resolve(const std::string &path, osn::FileOutput *owner, bool noSpace = false, bool allowOverwrite = false)
{
	std::string resolved = path;
	osn::IFileOutput::FindBestFilename(resolved, noSpace, owner, allowOverwrite);
	return resolved;
}
}

TEST_CASE("FindBestFilename avoids a path claimed by another live output", "[file-output]")
{
	ScopedOutput first;
	ScopedOutput second;

	const std::string wanted = "C:/osn-test/2026-08-08 14-30-12.mp4";

	SECTION("second output is bumped, first keeps the name")
	{
		REQUIRE(resolve(wanted, first.get()) == wanted);
		REQUIRE(resolve(wanted, second.get()) == "C:/osn-test/2026-08-08 14-30-12 (2).mp4");
	}

	SECTION("noSpace uses the underscore form")
	{
		REQUIRE(resolve(wanted, first.get(), true) == wanted);
		REQUIRE(resolve(wanted, second.get(), true) == "C:/osn-test/2026-08-08 14-30-12_2.mp4");
	}

	SECTION("overwrite does not license two live outputs onto one file")
	{
		REQUIRE(resolve(wanted, first.get(), false, true) == wanted);
		REQUIRE(resolve(wanted, second.get(), false, true) == "C:/osn-test/2026-08-08 14-30-12 (2).mp4");
	}

	SECTION("separator and case differences do not evade the check")
	{
		REQUIRE(resolve(wanted, first.get()) == wanted);
		REQUIRE(resolve("C:\\OSN-Test\\2026-08-08 14-30-12.mp4", second.get()) == "C:\\OSN-Test\\2026-08-08 14-30-12 (2).mp4");
	}

	SECTION("the name is reusable once the first output stops")
	{
		REQUIRE(resolve(wanted, first.get()) == wanted);
		first.get()->claimedFilePath.clear();
		REQUIRE(resolve(wanted, second.get()) == wanted);
	}

	SECTION("re-resolving for the same owner is idempotent")
	{
		REQUIRE(resolve(wanted, first.get()) == wanted);
		REQUIRE(resolve(wanted, first.get()) == wanted);
	}
}

TEST_CASE("FindBestFilename keeps its existing on-disk behaviour", "[file-output]")
{
	ScopedOutput only;

	const std::string dir = std::string(OSN_TEST_WD) + "/osn-file-output-test";
	os_mkdirs(dir.c_str());
	const std::string wanted = dir + "/existing.mp4";

	{
		std::ofstream f(wanted, std::ios::binary);
		f << "x";
	}
	REQUIRE(os_file_exists(wanted.c_str()));

	SECTION("an existing file is stepped over")
	{
		REQUIRE(resolve(wanted, only.get()) == dir + "/existing (2).mp4");
	}

	SECTION("overwrite still reuses an existing file with no live claim")
	{
		REQUIRE(resolve(wanted, only.get(), false, true) == wanted);
	}

	os_unlink(wanted.c_str());
}
