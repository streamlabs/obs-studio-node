/******************************************************************************
    Copyright (C) 2016-2022 by Streamlabs (General Workings Inc)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

******************************************************************************/

#include "osn-file-output.hpp"
#include "osn-error.hpp"
#include "shared.hpp"
#include <osn-video.hpp>
#include <util/platform.h>
#include <algorithm>
#include <mutex>

// Serialises resolve-and-claim against the release in OnOutputStopped(). Start() runs on an IPC
// worker thread and the release on libobs' capture thread, and the check-then-set has to be atomic
// as a unit -- the manager's own lock is dropped when for_each returns, so it is not enough.
static std::mutex s_filenameClaimMutex;

// Windows paths are case-insensitive and accept either separator, so compare on a folded key while
// the claim itself keeps the exact string the muxer was given. Elsewhere the comparison is exact:
// on POSIX a backslash is an ordinary filename character, so folding it into a separator would make
// two different files look like one.
static std::string claim_key(const std::string &path)
{
#ifdef WIN32
	std::string key = path;
	std::replace(key.begin(), key.end(), '\\', '/');
	std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return (char)tolower(c); });
	return key;
#else
	return path;
#endif
}

// Caller must hold s_filenameClaimMutex.
static bool path_claimed_by_other(const std::string &candidate, const osn::FileOutput *owner)
{
	const std::string wanted = claim_key(candidate);
	bool claimed = false;

	osn::IFileOutput::Manager::GetInstance().for_each([&](osn::FileOutput *output) {
		// Idle outputs, replay buffers (which never claim) and GetLegacySettings' throwaway
		// objects all carry an empty claim.
		if (claimed || !output || output == owner || output->claimedFilePath.empty())
			return;

		if (claim_key(output->claimedFilePath) == wanted)
			claimed = true;
	});

	return claimed;
}

void osn::IFileOutput::FindBestFilename(std::string &strPath, bool noSpace, FileOutput *owner, bool allowOverwrite)
{
	std::lock_guard<std::mutex> lock(s_filenameClaimMutex);

	// Insert before the extension, or at the very end when there is none. Only the final component
	// counts: a dot in a directory name is not an extension. An extensionless name is reachable --
	// osn_generate_formatted_filename() appends the extension and then truncates the whole thing to
	// 255 bytes -- and giving up there would hand two live outputs the same path.
	const size_t sep = strPath.find_last_of("/\\");
	const size_t nameStart = (sep == std::string::npos) ? 0 : sep + 1;
	const size_t dot = strPath.find_last_of('.');
	const size_t insertAt = (dot != std::string::npos && dot > nameStart) ? dot : strPath.size();

	std::string candidate = strPath;
	int num = 2;
	bool blockedByLiveOutput = false;

	for (;;) {
		const bool onDisk = !allowOverwrite && os_file_exists(candidate.c_str());
		const bool heldByPeer = path_claimed_by_other(candidate, owner);

		if (!onDisk && !heldByPeer)
			break;
		if (heldByPeer)
			blockedByLiveOutput = true;

		std::string numStr = noSpace ? "_" : " (";
		numStr += std::to_string(num++);
		if (!noSpace)
			numStr += ")";

		candidate = strPath;
		candidate.insert(insertAt, numStr);
	}

	// Stepping over a file left on disk is ordinary and stays quiet. Stepping over a path another
	// running output holds is not: it means a client pointed two outputs at one file -- dual output
	// without distinct prefix/suffix, most likely -- and got a name it did not ask for.
	if (blockedByLiveOutput)
		blog(LOG_WARNING, "Recording path '%s' is in use by another active output, writing to '%s' instead.", strPath.c_str(), candidate.c_str());

	strPath = candidate;

	// Never move the claim of an output that is already running. A duplicate start() resolves a
	// fresh name -- the timestamp has moved on -- but obs_output_start() then refuses and the muxer
	// stays on its original file. Reassigning here would unclaim the file actually being written.
	if (owner && !obs_output_active(owner->GetOutput()))
		owner->claimedFilePath = strPath;
}

void osn::FileOutput::OnOutputStopped()
{
	std::lock_guard<std::mutex> lock(s_filenameClaimMutex);
	claimedFilePath.clear();
}

void osn::IFileOutput::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("FileOutput");
	cls->register_function(std::make_shared<ipc::function>("GetPath", std::vector<ipc::type>{ipc::type::UInt64}, GetPath));
	cls->register_function(std::make_shared<ipc::function>("SetPath", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String}, SetPath));
	cls->register_function(std::make_shared<ipc::function>("GetFormat", std::vector<ipc::type>{ipc::type::UInt64}, GetFormat));
	cls->register_function(std::make_shared<ipc::function>("SetFormat", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String}, SetFormat));
	cls->register_function(std::make_shared<ipc::function>("GetMuxerSettings", std::vector<ipc::type>{ipc::type::UInt64}, GetMuxerSettings));
	cls->register_function(
		std::make_shared<ipc::function>("SetMuxerSettings", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String}, SetMuxerSettings));
	cls->register_function(std::make_shared<ipc::function>("GetFileFormat", std::vector<ipc::type>{ipc::type::UInt64}, GetFileFormat));
	cls->register_function(std::make_shared<ipc::function>("SetFileFormat", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetFileFormat));
	cls->register_function(std::make_shared<ipc::function>("GetOverwrite", std::vector<ipc::type>{ipc::type::UInt64}, GetOverwrite));
	cls->register_function(std::make_shared<ipc::function>("SetOverwrite", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetOverwrite));
	cls->register_function(std::make_shared<ipc::function>("GetNoSpace", std::vector<ipc::type>{ipc::type::UInt64}, GetNoSpace));
	cls->register_function(std::make_shared<ipc::function>("SetNoSpace", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetNoSpace));
	cls->register_function(std::make_shared<ipc::function>("GetLastFile", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, GetLastFile));
	cls->register_function(std::make_shared<ipc::function>("GetVideoCanvas", std::vector<ipc::type>{ipc::type::UInt64}, GetVideoCanvas));
	cls->register_function(std::make_shared<ipc::function>("SetVideoCanvas", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetVideoCanvas));
	srv.register_collection(cls);
}

void osn::IFileOutput::GetPath(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	FileOutput *fileOutput = osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!fileOutput) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "File output reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(fileOutput->path));
	AUTO_DEBUG;
}

void osn::IFileOutput::SetPath(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	FileOutput *fileOutput = osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!fileOutput) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "File output reference is not valid.");
	}

	fileOutput->path = args[1].value_str;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IFileOutput::GetVideoCanvas(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	FileOutput *fileOutput = osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!fileOutput) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "File output reference is not valid.");
	}

	uint64_t uid = osn::Video::Manager::GetInstance().find(fileOutput->GetCanvas());

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::IFileOutput::SetVideoCanvas(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	FileOutput *fileOutput = osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!fileOutput) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "File output reference is not valid.");
	}

	obs_video_info *canvas = osn::Video::Manager::GetInstance().find(args[1].value_union.ui64);
	if (!canvas) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Canvas reference is not valid.");
	}

	fileOutput->SetCanvas(canvas);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IFileOutput::GetFormat(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	FileOutput *fileOutput = osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!fileOutput) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "File output reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(fileOutput->format));
	AUTO_DEBUG;
}

void osn::IFileOutput::SetFormat(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	FileOutput *fileOutput = osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!fileOutput) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "File output reference is not valid.");
	}

	fileOutput->format = args[1].value_str;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IFileOutput::GetMuxerSettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	FileOutput *fileOutput = osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!fileOutput) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "File output reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(fileOutput->muxerSettings));
	AUTO_DEBUG;
}

void osn::IFileOutput::SetMuxerSettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	FileOutput *fileOutput = osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!fileOutput) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "File output reference is not valid.");
	}

	fileOutput->muxerSettings = args[1].value_str;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IFileOutput::GetFileFormat(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	FileOutput *fileOutput = osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!fileOutput) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "ReplayBuffer reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(fileOutput->fileFormat));
	AUTO_DEBUG;
}

void osn::IFileOutput::SetFileFormat(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	FileOutput *fileOutput = osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!fileOutput) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "ReplayBuffer reference is not valid.");
	}

	fileOutput->fileFormat = args[1].value_str;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IFileOutput::GetOverwrite(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	FileOutput *fileOutput = osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!fileOutput) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "ReplayBuffer reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(fileOutput->overwrite));
	AUTO_DEBUG;
}

void osn::IFileOutput::SetOverwrite(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	FileOutput *fileOutput = osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!fileOutput) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "ReplayBuffer reference is not valid.");
	}

	fileOutput->overwrite = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IFileOutput::GetNoSpace(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	FileOutput *fileOutput = osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!fileOutput) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "ReplayBuffer reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(fileOutput->noSpace));
	AUTO_DEBUG;
}

void osn::IFileOutput::SetNoSpace(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	FileOutput *fileOutput = osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!fileOutput) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "ReplayBuffer reference is not valid.");
	}

	fileOutput->noSpace = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IFileOutput::GetLastFile(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	FileOutput *fileOutput = osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!fileOutput) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "File output reference is not valid.");
	}

	calldata_t cd = {0};
	proc_handler_t *ph = obs_output_get_proc_handler(fileOutput->GetOutput());
	proc_handler_call(ph, "get_last_file", &cd);
	const char *path = calldata_string(&cd, "path");
	path = path ? path : "";

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(path));
	calldata_free(&cd);
	AUTO_DEBUG;
}

osn::IFileOutput::Manager &osn::IFileOutput::Manager::GetInstance()
{
	static osn::IFileOutput::Manager _inst;
	return _inst;
}