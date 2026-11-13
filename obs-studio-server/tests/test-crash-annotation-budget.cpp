#include "crash-annotation-budget.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

#include "crashpad-bridge.h"
#endif

using namespace util::crash_budget;

namespace {

bool IsValidUtf8(const std::string &text)
{
	size_t index = 0;
	while (index < text.size()) {
		const unsigned char lead = static_cast<unsigned char>(text[index]);
		const size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
		if (!length || index + length > text.size())
			return false;
		for (size_t offset = 1; offset < length; ++offset) {
			if ((static_cast<unsigned char>(text[index + offset]) & 0xC0) != 0x80)
				return false;
		}
		index += length;
	}
	return true;
}

// Stand-in for the handler-side measurement: the sum of key and value sizes.
size_t PayloadSize(const Annotations &annotations)
{
	size_t size = 0;
	for (const auto &[key, value] : annotations)
		size += key.size() + value.size();
	return size;
}

std::vector<std::string> MakeLines(size_t count, size_t width)
{
	std::vector<std::string> lines;
	for (size_t index = 0; index < count; ++index)
		lines.push_back("line" + std::to_string(1000 + index) + std::string(width, 'x'));
	return lines;
}

size_t CountLines(const std::string &logAnnotation)
{
	if (logAnnotation == "null")
		return 0;
	return nlohmann::json::parse(logAnnotation).size();
}

Annotations FullAnnotations(const std::vector<std::string> &lines)
{
	Annotations annotations;
	annotations["sentry[tags][crash_reason]"] = "access_violation";
	annotations["Crash reason"] = "reason";
	annotations["Process List"] = std::string(100, 'p');
	annotations["Server warnings"] = std::string(100, 'w');
	annotations["Last actions"] = std::string(100, 'a');
	annotations[LogAnnotation] = BuildLogAnnotation(lines, lines.size());
	return annotations;
}

} // namespace

TEST_CASE("TruncateUtf8 keeps short text and marks cuts", "[crash-annotation-budget]")
{
	CHECK(TruncateUtf8("short", 16) == "short");
	CHECK(TruncateUtf8("exactly", 7) == "exactly");

	const std::string truncated = TruncateUtf8(std::string(100, 'a'), 20);
	CHECK(truncated.size() == 20);
	CHECK(truncated.substr(17) == "...");
}

TEST_CASE("TruncateUtf8 never splits a multi-byte character", "[crash-annotation-budget]")
{
	const std::string twoByte = "\xC3\xA9";          // e acute
	const std::string fourByte = "\xF0\x9F\x98\x80"; // emoji

	for (size_t limit = 3; limit < 40; ++limit) {
		std::string text;
		for (int index = 0; index < 20; ++index)
			text += (index % 2 ? twoByte : fourByte) + "a";
		const std::string truncated = TruncateUtf8(text, limit);
		INFO("limit " << limit);
		CHECK(truncated.size() <= limit);
		CHECK(IsValidUtf8(truncated));
	}
}

TEST_CASE("Command line length helpers count UTF-16 units and escapes", "[crash-annotation-budget]")
{
	CHECK(Utf16Units("abc") == 3);
	CHECK(Utf16Units("\xC3\xA9") == 1);
	CHECK(Utf16Units("\xE2\x82\xAC") == 1);
	CHECK(Utf16Units("\xF0\x9F\x98\x80") == 2);

	CHECK(QuotedLength("abc") == 3);
	CHECK(QuotedLength("a\"b") == 4);
	CHECK(QuotedLength("a\\\"b") == 6); // backslash before a quote doubles, plus the quote's own escape
	CHECK(QuotedLength("ab\\") == 4);   // trailing backslash doubles
	CHECK(QuotedLength("a\\b") == 3);   // backslash elsewhere is literal
}

TEST_CASE("Log lines are limited by the soft budget in one pass", "[crash-annotation-budget]")
{
	const std::vector<std::string> lines = MakeLines(100, 94);
	const size_t perLine = QuotedLength(nlohmann::json(lines[0]).dump()) + 6;

	CHECK(LogLinesWithinSoftBudget(lines, perLine * 100) == 100);
	CHECK(LogLinesWithinSoftBudget(lines, perLine * 100 - 1) == 99);
	CHECK(LogLinesWithinSoftBudget(lines, perLine * 10) == 10);
	CHECK(LogLinesWithinSoftBudget(lines, perLine * 10 - 1) == 9);
	CHECK(LogLinesWithinSoftBudget(lines, 0) == 0);
	CHECK(LogLinesWithinSoftBudget({}, 0) == 0);
}

TEST_CASE("FitAnnotations leaves a normal crash untouched", "[crash-annotation-budget]")
{
	const std::vector<std::string> lines = MakeLines(50, 80);
	Annotations annotations = FullAnnotations({});
	const Annotations expected = FullAnnotations(lines);

	const TrimReport report = FitAnnotations(annotations, lines, PayloadSize, 1000000);

	CHECK_FALSE(report.Trimmed());
	CHECK(annotations == expected);
	CHECK(annotations.count(TrimmedTagAnnotation) == 0);
	CHECK(annotations.count(TrimmedDetailAnnotation) == 0);
}

TEST_CASE("FitAnnotations keeps an empty log as null", "[crash-annotation-budget]")
{
	Annotations annotations;
	FitAnnotations(annotations, {}, PayloadSize, 1000000);
	CHECK(annotations[LogAnnotation] == "null");
}

TEST_CASE("FitAnnotations caps each log line", "[crash-annotation-budget]")
{
	std::vector<std::string> lines = {"short", std::string(MaxLogLineBytes + 500, 'x'), "tail"};
	Annotations annotations;

	const TrimReport report = FitAnnotations(annotations, lines, PayloadSize, 1000000);

	CHECK(report.truncatedLogLines == 1);
	CHECK(report.droppedLogLines == 0);
	const nlohmann::json log = nlohmann::json::parse(annotations[LogAnnotation]);
	REQUIRE(log.size() == 3);
	CHECK(log[1].get<std::string>().size() == MaxLogLineBytes);
	CHECK(log[2] == "tail");
	CHECK(annotations[TrimmedTagAnnotation] == "true");
}

TEST_CASE("FitAnnotations drops the oldest lines in bulk to the soft budget", "[crash-annotation-budget]")
{
	const std::vector<std::string> lines = MakeLines(150, 400);
	Annotations annotations;
	int measurements = 0;

	const TrimReport report = FitAnnotations(
		annotations, lines,
		[&](const Annotations &candidate) {
			++measurements;
			return PayloadSize(candidate);
		},
		1000000);

	const size_t kept = CountLines(annotations[LogAnnotation]);
	CHECK(kept == LogLinesWithinSoftBudget(lines));
	CHECK(kept > 0);
	CHECK(kept < lines.size());
	CHECK(report.droppedLogLines == lines.size() - kept);
	CHECK(measurements == 1);
	CHECK(annotations[TrimmedTagAnnotation] == "true");

	// Newest lines are the leading ones; the oldest are the ones removed.
	const nlohmann::json log = nlohmann::json::parse(annotations[LogAnnotation]);
	CHECK(log.front() == lines.front());
	CHECK(log.back() == lines[kept - 1]);
}

TEST_CASE("FitAnnotations halves the log until the measurement fits", "[crash-annotation-budget]")
{
	const std::vector<std::string> lines = MakeLines(150, 100);
	Annotations annotations = FullAnnotations({});
	const size_t softKept = LogLinesWithinSoftBudget(lines);
	const size_t limit = 3000;

	const TrimReport report = FitAnnotations(annotations, lines, PayloadSize, limit);

	CHECK(PayloadSize(annotations) <= limit);
	const size_t kept = CountLines(annotations[LogAnnotation]);
	CHECK(kept < softKept);
	CHECK(kept > 0);
	CHECK(report.droppedLogLines == lines.size() - kept);
	CHECK(report.droppedAnnotations.empty());
	CHECK(annotations.count("Process List") == 1);
}

TEST_CASE("FitAnnotations drops sections in order after the log is gone", "[crash-annotation-budget]")
{
	const std::vector<std::string> lines = MakeLines(20, 50);
	const Annotations required = [] {
		Annotations annotations;
		annotations["sentry[tags][crash_reason]"] = "access_violation";
		annotations["sentry[tags][crash_kind]"] = "abort";
		return annotations;
	}();

	const auto run = [&](size_t extraPayload) {
		Annotations annotations = FullAnnotations({});
		annotations.insert(required.begin(), required.end());
		annotations["Process List"] = std::string(1000, 'p');
		annotations["Server warnings"] = std::string(1000, 'w');
		annotations["Last actions"] = std::string(1000, 'a');
		const TrimReport report = FitAnnotations(annotations, lines, [](const Annotations &candidate) { return PayloadSize(candidate); }, extraPayload);
		return std::make_pair(annotations, report);
	};

	SECTION("process list goes first")
	{
		const auto [annotations, report] = run(2800 + 300);
		CHECK(report.droppedAnnotations == std::vector<std::string>{"Process List"});
		CHECK(annotations.count("Process List") == 0);
		CHECK(annotations.count("Server warnings") == 1);
		CHECK(annotations.at(LogAnnotation) == "null");
	}

	SECTION("then server warnings, then last actions")
	{
		const auto [annotations, report] = run(800);
		CHECK(report.droppedAnnotations == std::vector<std::string>{"Process List", "Server warnings", "Last actions"});
		CHECK(annotations.count("Last actions") == 0);
	}

	SECTION("tags and reasons are never dropped")
	{
		const auto [annotations, report] = run(0);
		CHECK(annotations.count("sentry[tags][crash_reason]") == 1);
		CHECK(annotations.count("sentry[tags][crash_kind]") == 1);
		CHECK(annotations.count("Crash reason") == 1);
		CHECK(annotations.at(TrimmedTagAnnotation) == "true");
		CHECK(annotations.at(TrimmedDetailAnnotation).find("dropped=Process List,Server warnings,Last actions") != std::string::npos);
	}
}

TEST_CASE("FitAnnotations is bounded and ignores a failed measurement", "[crash-annotation-budget]")
{
	const std::vector<std::string> lines = MakeLines(150, 400);

	Annotations annotations = FullAnnotations({});
	int measurements = 0;
	FitAnnotations(annotations, lines, [&](const Annotations &) {
		++measurements;
		return static_cast<size_t>(MaxCommandLineLength * 10);
	});
	CHECK(measurements <= MaxTrimAttempts);

	Annotations failed = FullAnnotations({});
	measurements = 0;
	const TrimReport report = FitAnnotations(failed, lines, [&](const Annotations &) {
		++measurements;
		return MeasureFailed;
	});
	CHECK(measurements == 1);
	CHECK(report.droppedAnnotations.empty());
	CHECK(failed.count("Process List") == 1);
}

#if defined(_WIN32)

namespace {

// Transliteration of crashpad's AppendCommandLineArgument (util/win/command_line.cc).
void AppendReference(const std::wstring &argument, std::wstring &commandLine)
{
	if (!commandLine.empty())
		commandLine.push_back(L' ');

	if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
		commandLine.append(argument);
		return;
	}

	commandLine.push_back(L'"');
	for (auto it = argument.begin();; ++it) {
		size_t backslashes = 0;
		while (it != argument.end() && *it == L'\\') {
			++it;
			++backslashes;
		}

		if (it == argument.end()) {
			commandLine.append(backslashes * 2, L'\\');
			break;
		}
		if (*it == L'"')
			commandLine.append(backslashes * 2 + 1, L'\\');
		else
			commandLine.append(backslashes, L'\\');
		commandLine.push_back(*it);
	}
	commandLine.push_back(L'"');
}

std::wstring ToWide(const std::string &utf8)
{
	if (utf8.empty())
		return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
	std::wstring wide(length, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), length);
	return wide;
}

std::wstring DatabasePath()
{
	PWSTR roaming = nullptr;
	REQUIRE(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)));
	std::wstring path(roaming);
	CoTaskMemFree(roaming);
	return path + L"\\obs-studio-node-server";
}

constexpr const char *TestUrl = "http://127.0.0.1:9/api/1/minidump/?sentry_key=x";

uint32_t Measure(const Annotations &annotations, const std::vector<std::string> &extraArguments = {})
{
	std::vector<const char *> keys;
	std::vector<const char *> values;
	for (const auto &[key, value] : annotations) {
		keys.push_back(key.c_str());
		values.push_back(value.c_str());
	}
	std::vector<const char *> arguments;
	for (const std::string &argument : extraArguments)
		arguments.push_back(argument.c_str());

	return osn_crashpad_bridge_command_line_length(TestUrl, keys.data(), values.data(), static_cast<uint32_t>(keys.size()), arguments.data(),
						       static_cast<uint32_t>(arguments.size()));
}

} // namespace

TEST_CASE("Bridge command line length matches crashpad's escaping", "[crash-annotation-budget][bridge]")
{
	constexpr size_t InitialClientDataReserve = 160;
	constexpr size_t NullTerminator = 1;

	const std::vector<std::pair<std::string, std::string>> cases = {
		{"plain", "value"},
		{"spaces", "has some spaces"},
		{"quotes", "say \"hi\" twice"},
		{"backslash_quote", "path\\\\\"quoted\""},
		{"trailing_backslash", "dir\\"},
		{"trailing_backslashes_spaced", "a dir\\\\"},
		{"newlines", "line1\nline2\r\nline3"},
		{"empty", ""},
		{"non_ascii", "caf\xC3\xA9 \xE2\x82\xAC"},
		{"astral", "emoji \xF0\x9F\x98\x80 \xF0\x9F\x98\x80"},
	};

	for (const auto &[key, value] : cases) {
		INFO("case " << key);
		Annotations annotations;
		annotations[key] = value;

		std::wstring reference;
		AppendReference(L"crashpad_handler.exe", reference);
		AppendReference(L"--no-rate-limit", reference);
		AppendReference(L"--database=" + DatabasePath(), reference);
		AppendReference(L"--metrics-dir=" + DatabasePath(), reference);
		AppendReference(L"--url=" + ToWide(TestUrl), reference);
		AppendReference(L"--annotation=" + ToWide(key + "=" + value), reference);

		CHECK(Measure(annotations, {"--no-rate-limit"}) == reference.size() + InitialClientDataReserve + NullTerminator);
	}
}

TEST_CASE("Header length helper agrees with the bridge for quoted annotations", "[crash-annotation-budget][bridge]")
{
	const uint32_t base = Measure({});
	for (const std::string value : {"a b", "q\"q w", "s\\ \\\\", "multi\nline \"x\\\"", "caf\xC3\xA9 \xF0\x9F\x98\x80 z"}) {
		INFO("value " << value);
		Annotations annotations;
		annotations["k"] = value;
		// Space-containing values force the quoted form, so the whole argument is the escaped body, two quotes and a separator.
		CHECK(Measure(annotations) - base == QuotedLength("--annotation=k=" + value) + 3);
	}
}

TEST_CASE("Bridge command line length reports errors", "[crash-annotation-budget][bridge]")
{
	const char *key = "k";
	const char *const keys[] = {key};
	const char *const nullValues[] = {nullptr};

	CHECK(osn_crashpad_bridge_command_line_length(TestUrl, keys, nullValues, 1, nullptr, 0) == UINT32_MAX);
	CHECK(osn_crashpad_bridge_command_line_length(TestUrl, nullptr, nullptr, 1, nullptr, 0) == UINT32_MAX);
	CHECK(osn_crashpad_bridge_command_line_length(TestUrl, nullptr, nullptr, 0, nullptr, 1) == UINT32_MAX);
}

TEST_CASE("Bridge start refuses an oversized command line without touching crashpad", "[crash-annotation-budget][bridge]")
{
	const std::string huge(40000, 'x');
	const char *const keys[] = {"huge"};
	const char *const values[] = {huge.c_str()};

	CHECK(osn_crashpad_bridge_command_line_length(TestUrl, keys, values, 1, nullptr, 0) > MaxCommandLineLength);
	CHECK_FALSE(osn_crashpad_bridge_start(TestUrl, keys, values, 1, nullptr, 0));
}

#endif
