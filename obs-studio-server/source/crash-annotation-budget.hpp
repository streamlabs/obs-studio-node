#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

// Crashpad passes every annotation on the handler's command line, which Windows
// limits to 32,767 UTF-16 units. Past that the handler never starts and the
// crash leaves no report, so the crash-time annotations are trimmed to fit.
namespace util::crash_budget {

using Annotations = std::map<std::string, std::string>;
using MeasureFunction = std::function<size_t(const Annotations &)>;

constexpr size_t MaxCommandLineLength = 32767;
// Covers what the exact measurement leaves out, e.g. a longer trim detail annotation.
constexpr size_t CommandLineMargin = 512;
constexpr size_t MaxLogLineBytes = 2048;
// About 3x the non-log annotations seen in field reports (~7K), so the exact check rarely has to iterate.
constexpr size_t LogSoftBudget = 12 * 1024;
constexpr size_t MeasureFailed = UINT32_MAX;
constexpr int MaxTrimAttempts = 16;

constexpr const char *LogAnnotation = "OBS log general";
constexpr const char *TrimmedTagAnnotation = "sentry[tags][annotations_trimmed]";
constexpr const char *TrimmedDetailAnnotation = "Annotations trimmed";
// Dropped in this order once the log is empty.
constexpr const char *DroppableAnnotations[] = {"Process List", "Server warnings", "Last actions"};

struct TrimReport {
	size_t truncatedLogLines = 0;
	size_t droppedLogLines = 0;
	std::vector<std::string> droppedAnnotations;

	bool Trimmed() const { return truncatedLogLines || droppedLogLines || !droppedAnnotations.empty(); }
};

// Cuts at a character boundary and marks the cut, so the result never exceeds maxBytes.
inline std::string TruncateUtf8(const std::string &text, size_t maxBytes)
{
	constexpr char Ellipsis[] = "...";
	constexpr size_t EllipsisBytes = sizeof(Ellipsis) - 1;
	if (text.size() <= maxBytes)
		return text;

	size_t end = maxBytes > EllipsisBytes ? maxBytes - EllipsisBytes : 0;
	while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
		--end;
	return text.substr(0, end) + Ellipsis;
}

// UTF-16 units of well-formed UTF-8: 4-byte sequences are surrogate pairs.
inline size_t Utf16Units(const std::string &utf8)
{
	size_t units = 0;
	for (const char c : utf8) {
		const unsigned char byte = static_cast<unsigned char>(c);
		if ((byte & 0xC0) == 0x80)
			continue;
		units += byte >= 0xF0 ? 2 : 1;
	}
	return units;
}

// Units a quoted command line argument occupies: quote doubling and trailing backslash doubling follow
// crashpad's AppendCommandLineArgument.
inline size_t QuotedLength(const std::string &utf8)
{
	size_t extra = 0;
	size_t backslashes = 0;
	for (const char c : utf8) {
		if (c == '\\') {
			++backslashes;
			continue;
		}
		if (c == '"')
			extra += backslashes + 1;
		backslashes = 0;
	}
	return Utf16Units(utf8) + extra + backslashes;
}

inline std::string BuildLogAnnotation(const std::vector<std::string> &lines, size_t count)
{
	nlohmann::json result;
	for (size_t index = 0; index < count && index < lines.size(); ++index)
		result.push_back(lines[index]);
	return result.dump(4, ' ', false, nlohmann::json::error_handler_t::replace);
}

// Newest lines first. Returns how many leading lines fit in LogSoftBudget once escaped for the command line.
inline size_t LogLinesWithinSoftBudget(const std::vector<std::string> &lines, size_t budget = LogSoftBudget)
{
	constexpr size_t JsonLineOverhead = 6; // indent, comma, newline
	size_t used = 0;
	for (size_t index = 0; index < lines.size(); ++index) {
		const std::string dumped = nlohmann::json(lines[index]).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
		used += QuotedLength(dumped) + JsonLineOverhead;
		if (used > budget)
			return index;
	}
	return lines.size();
}

inline void ApplyTrimMarker(Annotations &annotations, const TrimReport &report)
{
	std::string detail = "log_lines_dropped=" + std::to_string(report.droppedLogLines) + " log_lines_truncated=" + std::to_string(report.truncatedLogLines);
	for (size_t index = 0; index < report.droppedAnnotations.size(); ++index)
		detail += (index ? "," : " dropped=") + report.droppedAnnotations[index];

	annotations[TrimmedTagAnnotation] = "true";
	annotations[TrimmedDetailAnnotation] = detail;
}

// Sets the log annotation from logLines (newest first) and trims until measure() fits in limit. Leaves
// annotations untouched, apart from the log, when nothing needs trimming.
inline TrimReport FitAnnotations(Annotations &annotations, std::vector<std::string> logLines, const MeasureFunction &measure,
				 size_t limit = MaxCommandLineLength - CommandLineMargin)
{
	TrimReport report;

	for (std::string &line : logLines) {
		if (line.size() <= MaxLogLineBytes)
			continue;
		line = TruncateUtf8(line, MaxLogLineBytes);
		++report.truncatedLogLines;
	}

	const size_t totalLines = logLines.size();
	size_t keep = LogLinesWithinSoftBudget(logLines);
	size_t nextDroppable = 0;

	for (int attempt = 0; attempt < MaxTrimAttempts; ++attempt) {
		report.droppedLogLines = totalLines - keep;
		annotations[LogAnnotation] = BuildLogAnnotation(logLines, keep);
		if (report.Trimmed())
			ApplyTrimMarker(annotations, report);

		const size_t length = measure(annotations);
		if (length == MeasureFailed || length <= limit)
			break;

		if (keep > 0) {
			keep /= 2;
		} else if (nextDroppable < std::size(DroppableAnnotations)) {
			const std::string name = DroppableAnnotations[nextDroppable++];
			if (annotations.erase(name))
				report.droppedAnnotations.push_back(name);
		} else {
			break;
		}
	}

	return report;
}

} // namespace util::crash_budget
