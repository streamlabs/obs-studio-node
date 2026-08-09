#include <catch2/catch_test_macros.hpp>
#include "osn-file-output.hpp"
#include "osn-recording.hpp"
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

#ifdef WIN32
	// Windows only: separator and case are not part of path identity there. On POSIX a backslash
	// is an ordinary filename character, so these really are different paths.
	SECTION("separator and case differences do not evade the check")
	{
		REQUIRE(resolve(wanted, first.get()) == wanted);
		REQUIRE(resolve("C:\\OSN-Test\\2026-08-08 14-30-12.mp4", second.get()) == "C:\\OSN-Test\\2026-08-08 14-30-12 (2).mp4");
	}
#else
	SECTION("paths differing only by separator are distinct files")
	{
		REQUIRE(resolve(wanted, first.get()) == wanted);
		const std::string backslashed = "C:\\osn-test\\2026-08-08 14-30-12.mp4";
		REQUIRE(resolve(backslashed, second.get()) == backslashed);
	}
#endif

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

	// osn_generate_formatted_filename() appends the extension and then truncates to 255 bytes, so a
	// long enough prefix/suffix leaves no extension at all. The claim must still hold.
	SECTION("an extensionless name is still made unique")
	{
		const std::string noExt = "C:/osn-test/2026-08-08 14-30-12";
		REQUIRE(resolve(noExt, first.get()) == noExt);
		REQUIRE(resolve(noExt, second.get()) == noExt + " (2)");
	}

	// A dot in a directory name is not an extension; the counter belongs on the filename.
	SECTION("a dot in a directory is not treated as the extension")
	{
		const std::string dottedDir = "C:/osn.test/recording";
		REQUIRE(resolve(dottedDir, first.get()) == dottedDir);
		REQUIRE(resolve(dottedDir, second.get()) == "C:/osn.test/recording (2)");
	}
}

// prefix/suffix are how a caller keeps two canvases apart on disk. The rename in FindBestFilename
// is only the backstop for when it did not.
TEST_CASE("DecoratedFileFormat wraps the configured pattern", "[file-output]")
{
	osn::Recording recording;
	recording.fileFormat = "%CCYY-%MM-%DD %hh-%mm-%ss";

	SECTION("untouched when neither is set")
	{
		REQUIRE(recording.DecoratedFileFormat() == "%CCYY-%MM-%DD %hh-%mm-%ss");
	}

	SECTION("suffix is appended after a space")
	{
		recording.suffix = "Vertical";
		REQUIRE(recording.DecoratedFileFormat() == "%CCYY-%MM-%DD %hh-%mm-%ss Vertical");
	}

	SECTION("prefix is prepended before a space")
	{
		recording.prefix = "Stream";
		REQUIRE(recording.DecoratedFileFormat() == "Stream %CCYY-%MM-%DD %hh-%mm-%ss");
	}

	SECTION("caller-supplied spacing is not doubled")
	{
		recording.prefix = "Stream ";
		recording.suffix = " Vertical";
		REQUIRE(recording.DecoratedFileFormat() == "Stream %CCYY-%MM-%DD %hh-%mm-%ss Vertical");
	}

	SECTION("reserved path characters are stripped from prefix and suffix")
	{
		recording.suffix = "a/b\\c:d";
		REQUIRE(recording.DecoratedFileFormat() == "%CCYY-%MM-%DD %hh-%mm-%ss a_b_c_d");
	}

	SECTION("fileFormat itself is left exactly as configured")
	{
		recording.fileFormat = "%CCYY-%MM-%DD/%hh-%mm-%ss";
		recording.suffix = "Vertical";
		REQUIRE(recording.DecoratedFileFormat() == "%CCYY-%MM-%DD/%hh-%mm-%ss Vertical");
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
