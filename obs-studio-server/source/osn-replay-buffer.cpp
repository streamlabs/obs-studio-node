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

#include "osn-replay-buffer.hpp"
#include "osn-video-encoder.hpp"
#include "osn-error.hpp"
#include "shared.hpp"
#include "util/platform.h"

osn::ReplayBuffer::~ReplayBuffer()
{
	deleteOutput();
}

void osn::IReplayBuffer::GetDuration(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	ReplayBuffer *replayBuffer = static_cast<ReplayBuffer *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!replayBuffer) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "ReplayBuffer reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(replayBuffer->duration));
	AUTO_DEBUG;
}

void osn::IReplayBuffer::SetDuration(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	ReplayBuffer *replayBuffer = static_cast<ReplayBuffer *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!replayBuffer) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "ReplayBuffer reference is not valid.");
	}

	replayBuffer->duration = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IReplayBuffer::GetPrefix(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	ReplayBuffer *replayBuffer = static_cast<ReplayBuffer *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!replayBuffer) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "ReplayBuffer reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(replayBuffer->prefix));
	AUTO_DEBUG;
}

void osn::IReplayBuffer::SetPrefix(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	ReplayBuffer *replayBuffer = static_cast<ReplayBuffer *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!replayBuffer) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "ReplayBuffer reference is not valid.");
	}

	replayBuffer->prefix = args[1].value_str;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IReplayBuffer::GetSuffix(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	ReplayBuffer *replayBuffer = static_cast<ReplayBuffer *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!replayBuffer) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "ReplayBuffer reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(replayBuffer->suffix));
	AUTO_DEBUG;
}

void osn::IReplayBuffer::SetSuffix(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	ReplayBuffer *replayBuffer = static_cast<ReplayBuffer *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!replayBuffer) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "ReplayBuffer reference is not valid.");
	}

	replayBuffer->suffix = args[1].value_str;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IReplayBuffer::GetUsesStream(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	ReplayBuffer *replayBuffer = static_cast<ReplayBuffer *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!replayBuffer) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "ReplayBuffer reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(replayBuffer->usesStream));
	AUTO_DEBUG;
}

void osn::IReplayBuffer::SetUsesStream(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	ReplayBuffer *replayBuffer = static_cast<ReplayBuffer *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!replayBuffer) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "ReplayBuffer reference is not valid.");
	}

	replayBuffer->usesStream = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IReplayBuffer::Query(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	ReplayBuffer *replayBuffer = static_cast<ReplayBuffer *>(osn::IFileOutput::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!replayBuffer) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "ReplayBuffer reference is not valid.");
	}

	auto signalOpt = replayBuffer->PopReceivedSignal();
	if (!signalOpt.has_value()) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		AUTO_DEBUG;
		return;
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value("replay-buffer"));
	rval.push_back(ipc::value(signalOpt.value().signal));
	rval.push_back(ipc::value(signalOpt.value().code));
	rval.push_back(ipc::value(signalOpt.value().errorMessage));

	AUTO_DEBUG;
}

void osn::IReplayBuffer::Save(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id id, obs_hotkey_t *key) {
			if (obs_hotkey_get_registerer_type(key) == OBS_HOTKEY_REGISTERER_OUTPUT) {
				std::string key_name = obs_hotkey_get_name(key);
				if (key_name.compare("ReplayBuffer.Save") == 0) {
					obs_hotkey_enable_callback_rerouting(true);
					obs_hotkey_trigger_routed_callback(id, true);
				}
			}
			return true;
		},
		nullptr);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
}