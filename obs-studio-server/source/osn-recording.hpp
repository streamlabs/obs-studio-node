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

#pragma once
#include <obs.h>
#include "utility.hpp"
#include "osn-streaming.hpp"
#include "osn-file-output.hpp"

namespace osn {
enum SplitFileType : uint32_t { TIME, SIZE, MANUAL };

class Recording : public FileOutput {
public:
	Recording() : FileOutput({"start", "stop", "stopping", "wrote"})
	{
		videoEncoder = nullptr;
		//signals = {"start", "stop", "stopping", "wrote"};
		enableFileSplit = false;
		splitType = SplitFileType::TIME;
		splitTime = 15;
		splitSize = 2048;
		fileResetTimestamps = true;
		simple = true;
	}
	virtual ~Recording();

public:
	obs_encoder_t *videoEncoder;
	bool enableFileSplit;
	enum SplitFileType splitType;
	uint32_t splitTime;
	uint32_t splitSize;
	bool fileResetTimestamps;
	bool simple;

	void ConfigureRecFileSplitting();
};

class IRecording : public IFileOutput {
public:
	static void GetVideoEncoder(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetVideoEncoder(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);

	static void Query(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SplitFile(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetEnableFileSplit(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetEnableFileSplit(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetSplitType(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetSplitType(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetSplitTime(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetSplitTime(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetSplitSize(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetSplitSize(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void GetFileResetTimestamps(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
	static void SetFileResetTimestamps(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);

	static std::string GenerateSpecifiedFilename(const std::string &extension, bool noSpace, const std::string &format, int width, int height);
	static void FindBestFilename(std::string &strPath, bool noSpace);

	static obs_encoder_t *duplicate_encoder(obs_encoder_t *src, uint64_t trackIndex = 0);
};
}
