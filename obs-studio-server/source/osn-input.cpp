/******************************************************************************
    Copyright (C) 2016-2019 by Streamlabs (General Workings Inc)

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

#include "osn-Input.hpp"
#include <iostream>
#include <ipc-server.hpp>
#include <memory>
#include <obs.h>
#include "osn-error.hpp"
#include "osn-source.hpp"
#include "shared.hpp"

void osn::Input::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("Input");
	cls->register_function(std::make_shared<ipc::function>("Types", std::vector<ipc::type>{}, Types));
	cls->register_function(std::make_shared<ipc::function>("Create", std::vector<ipc::type>{ipc::type::String, ipc::type::String}, Create));
	cls->register_function(
		std::make_shared<ipc::function>("Create", std::vector<ipc::type>{ipc::type::String, ipc::type::String, ipc::type::String}, Create));
	cls->register_function(std::make_shared<ipc::function>(
		"Create", std::vector<ipc::type>{ipc::type::String, ipc::type::String, ipc::type::String, ipc::type::String}, Create));
	cls->register_function(std::make_shared<ipc::function>("CreatePrivate", std::vector<ipc::type>{ipc::type::String, ipc::type::String}, CreatePrivate));
	cls->register_function(std::make_shared<ipc::function>("CreatePrivate", std::vector<ipc::type>{ipc::type::String, ipc::type::String, ipc::type::String},
							       CreatePrivate));
	cls->register_function(std::make_shared<ipc::function>("FromName", std::vector<ipc::type>{ipc::type::String}, FromName));
	cls->register_function(std::make_shared<ipc::function>("GetPublicSources", std::vector<ipc::type>{}, GetPublicSources));

	cls->register_function(std::make_shared<ipc::function>("Duplicate", std::vector<ipc::type>{ipc::type::UInt64}, Duplicate));
	cls->register_function(std::make_shared<ipc::function>("Duplicate", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String}, Duplicate));
	cls->register_function(
		std::make_shared<ipc::function>("Duplicate", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String, ipc::type::Int32}, Duplicate));
	cls->register_function(std::make_shared<ipc::function>("GetActive", std::vector<ipc::type>{ipc::type::UInt64}, GetActive));
	cls->register_function(std::make_shared<ipc::function>("GetShowing", std::vector<ipc::type>{ipc::type::UInt64}, GetShowing));
	cls->register_function(std::make_shared<ipc::function>("GetWidth", std::vector<ipc::type>{ipc::type::UInt64}, GetWidth));
	cls->register_function(std::make_shared<ipc::function>("GetHeight", std::vector<ipc::type>{ipc::type::UInt64}, GetHeight));
	cls->register_function(std::make_shared<ipc::function>("GetVolume", std::vector<ipc::type>{ipc::type::UInt64}, GetVolume));
	cls->register_function(std::make_shared<ipc::function>("SetVolume", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Float}, SetVolume));
	cls->register_function(std::make_shared<ipc::function>("GetSyncOffset", std::vector<ipc::type>{ipc::type::UInt64}, GetSyncOffset));
	cls->register_function(std::make_shared<ipc::function>("SetSyncOffset", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int64}, SetSyncOffset));
	cls->register_function(std::make_shared<ipc::function>("GetAudioMixers", std::vector<ipc::type>{ipc::type::UInt64}, GetAudioMixers));
	cls->register_function(std::make_shared<ipc::function>("SetAudioMixers", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetAudioMixers));
	cls->register_function(std::make_shared<ipc::function>("GetMonitoringType", std::vector<ipc::type>{ipc::type::UInt64}, GetMonitoringType));
	cls->register_function(
		std::make_shared<ipc::function>("SetMonitoringType", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int32}, SetMonitoringType));
	cls->register_function(
		std::make_shared<ipc::function>("GetDeInterlaceFieldOrder", std::vector<ipc::type>{ipc::type::UInt64}, GetDeInterlaceFieldOrder));
	cls->register_function(std::make_shared<ipc::function>("SetDeInterlaceFieldOrder", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int32},
							       SetDeInterlaceFieldOrder));
	cls->register_function(std::make_shared<ipc::function>("GetDeInterlaceMode", std::vector<ipc::type>{ipc::type::UInt64}, GetDeInterlaceMode));
	cls->register_function(
		std::make_shared<ipc::function>("SetDeInterlaceMode", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetDeInterlaceMode));

	cls->register_function(std::make_shared<ipc::function>("GetFilters", std::vector<ipc::type>{ipc::type::UInt64}, GetFilters));
	cls->register_function(std::make_shared<ipc::function>("AddFilter", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, AddFilter));
	cls->register_function(std::make_shared<ipc::function>("RemoveFilter", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, RemoveFilter));
	cls->register_function(
		std::make_shared<ipc::function>("MoveFilter", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64, ipc::type::UInt32}, MoveFilter));
	cls->register_function(std::make_shared<ipc::function>("FindFilter", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String}, FindFilter));
	cls->register_function(std::make_shared<ipc::function>("CopyFiltersTo", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, CopyFiltersTo));

	cls->register_function(std::make_shared<ipc::function>("GetDuration", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int64}, GetDuration));
	cls->register_function(std::make_shared<ipc::function>("GetTime", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int64}, GetTime));
	cls->register_function(std::make_shared<ipc::function>("SetTime", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int64}, SetTime));
	cls->register_function(std::make_shared<ipc::function>("Play", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int64}, Play));
	cls->register_function(std::make_shared<ipc::function>("Pause", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int64}, Pause));
	cls->register_function(std::make_shared<ipc::function>("Restart", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int64}, Restart));
	cls->register_function(std::make_shared<ipc::function>("Stop", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int64}, Stop));
	cls->register_function(std::make_shared<ipc::function>("GetMediaState", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int64}, GetMediaState));

	srv.register_collection(cls);
}

void osn::Input::Types(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	const char *typeId = nullptr;
	for (size_t idx = 0; obs_enum_input_types(idx, &typeId); idx++) {
		rval.push_back(ipc::value(typeId ? typeId : ""));
	}
	AUTO_DEBUG;
}

void osn::Input::Create(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	std::string sourceId, name;
	obs_data_t *settings = nullptr, *hotkeys = nullptr;

	switch (args.size()) {
	case 4:
		hotkeys = obs_data_create_from_json(args[3].value_str.c_str());
	case 3:
		settings = obs_data_create_from_json(args[2].value_str.c_str());
	case 2:
		name = args[1].value_str;
		sourceId = args[0].value_str;
		break;
	}

	obs_source_t *source = obs_source_create(sourceId.c_str(), name.c_str(), settings, hotkeys);
	obs_data_release(hotkeys);
	obs_data_release(settings);

	if (!source) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to create input.");
	}

	uint64_t uid = osn::Source::Manager::GetInstance().find(source);
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}
	obs_data_t *settingsSource = obs_source_get_settings(source);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	rval.push_back(ipc::value(obs_data_get_json_pretty(settingsSource)));
	rval.push_back(ipc::value(obs_source_get_audio_mixers(source)));
	rval.push_back(ipc::value((uint32_t)obs_source_get_deinterlace_mode(source)));
	rval.push_back(ipc::value((uint32_t)obs_source_get_deinterlace_field_order(source)));

	obs_data_release(settingsSource);
	AUTO_DEBUG;
}

void osn::Input::CreatePrivate(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	std::string sourceId, name;
	obs_data_t *settings = nullptr;

	switch (args.size()) {
	case 3:
		settings = obs_data_create_from_json(args[2].value_str.c_str());
	case 2:
		name = args[1].value_str;
		sourceId = args[0].value_str;
		break;
	}

	obs_source_t *source = obs_source_create_private(sourceId.c_str(), name.c_str(), settings);
	if (!source) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to create input.");
	}

	obs_data_release(settings);

	uint64_t uid = osn::Source::Manager::GetInstance().allocate(source);
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}
	osn::Source::attach_source_signals(source);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::Input::Duplicate(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *filter = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!filter) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	bool isPrivate = false;
	const char *nameOverride = nullptr;

	obs_source_t *source = nullptr;
	source = obs_source_duplicate(filter, nameOverride, isPrivate);

	if (!source) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to duplicate input.");
	}

	if (source != filter) {
		uint64_t uid = osn::Source::Manager::GetInstance().allocate(source);
		if (uid == UINT64_MAX) {
			PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
		}

		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		rval.push_back(ipc::value(uid));
	} else {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		rval.push_back(ipc::value(args[0].value_union.ui64));
	}
	AUTO_DEBUG;
}

void osn::Input::FromName(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *source = obs_get_source_by_name(args[0].value_str.c_str());
	if (!source) {
		PRETTY_ERROR_RETURN(ErrorCode::NotFound, "Named input could not be found.");
	}

	uint64_t uid = osn::Source::Manager::GetInstance().find(source);
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Source found but not indexed.");
	}

	obs_source_release(source);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::Input::GetPublicSources(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	std::list<uint64_t> inputs;

	// !FIXME! Optimize for zero-copy operation, can directly write to rval.
	auto enum_cb = [](void *data, obs_source_t *source) {
		uint64_t uid = osn::Source::Manager::GetInstance().find(source);
		if (uid != UINT64_MAX) {
			static_cast<std::list<uint64_t> *>(data)->push_back(uid);
		}
		return true;
	};

	obs_enum_sources(enum_cb, &inputs);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	for (uint64_t uid : inputs) {
		rval.push_back(uid);
	}
	AUTO_DEBUG;
}

void osn::Input::GetActive(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_active(input)));
	AUTO_DEBUG;
}

void osn::Input::GetShowing(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_showing(input)));
	AUTO_DEBUG;
}

void osn::Input::GetVolume(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_volume(input)));
	AUTO_DEBUG;
}

void osn::Input::SetVolume(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	obs_source_set_volume(input, (float_t)args[1].value_union.fp64);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_volume(input)));
	AUTO_DEBUG;
}

void osn::Input::GetSyncOffset(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_sync_offset(input)));
	AUTO_DEBUG;
}

void osn::Input::SetSyncOffset(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	obs_source_set_sync_offset(input, args[1].value_union.i64);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_sync_offset(input)));
	AUTO_DEBUG;
}

void osn::Input::GetAudioMixers(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_audio_mixers(input)));
	AUTO_DEBUG;
}

void osn::Input::SetAudioMixers(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	obs_source_set_audio_mixers(input, (obs_monitoring_type)args[1].value_union.ui32);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_audio_mixers(input)));
	AUTO_DEBUG;
}

void osn::Input::GetMonitoringType(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_monitoring_type(input)));
	AUTO_DEBUG;
}

void osn::Input::SetMonitoringType(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	obs_source_set_monitoring_type(input, (obs_monitoring_type)args[1].value_union.ui32);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_monitoring_type(input)));
	AUTO_DEBUG;
}

void osn::Input::GetWidth(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_width(input)));
	AUTO_DEBUG;
}

void osn::Input::GetHeight(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_height(input)));
	AUTO_DEBUG;
}

void osn::Input::GetDeInterlaceFieldOrder(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_deinterlace_field_order(input)));
	AUTO_DEBUG;
}

void osn::Input::SetDeInterlaceFieldOrder(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	obs_source_set_deinterlace_field_order(input, (obs_deinterlace_field_order)args[1].value_union.ui32);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_deinterlace_field_order(input)));
	AUTO_DEBUG;
}

void osn::Input::GetDeInterlaceMode(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_deinterlace_mode(input)));
	AUTO_DEBUG;
}

void osn::Input::SetDeInterlaceMode(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	obs_source_set_deinterlace_mode(input, (obs_deinterlace_mode)args[1].value_union.ui32);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_deinterlace_mode(input)));
	AUTO_DEBUG;
}

void osn::Input::AddFilter(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	obs_source_t *filter = osn::Source::Manager::GetInstance().find(args[1].value_union.ui64);
	if (!filter) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Filter reference is not valid.");
	}

	obs_source_filter_add(input, filter);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Input::RemoveFilter(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	obs_source_t *filter = osn::Source::Manager::GetInstance().find(args[1].value_union.ui64);
	if (!filter) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Filter reference is not valid.");
	}

	obs_source_filter_remove(input, filter);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Input::MoveFilter(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	obs_source_t *filter = osn::Source::Manager::GetInstance().find(args[1].value_union.ui64);
	if (!filter) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Filter reference is not valid.");
	}

	obs_order_movement movement = (obs_order_movement)args[2].value_union.ui32;

	obs_source_filter_set_order(input, filter, movement);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Input::FindFilter(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	obs_source_t *filter = obs_source_get_filter_by_name(input, args[1].value_str.c_str());
	if (!filter) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		AUTO_DEBUG;
		return;
	}
	obs_source_release(filter);

	uint64_t uid = osn::Source::Manager::GetInstance().find(filter);
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Filter found but not indexed.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::Input::GetFilters(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	auto enum_cb = [](obs_source_t *parent, obs_source_t *filter, void *data) {
		std::vector<ipc::value> *rval = reinterpret_cast<std::vector<ipc::value> *>(data);

		uint64_t id = osn::Source::Manager::GetInstance().find(filter);
		if (id != UINT64_MAX) {
			rval->push_back(id);
		}
	};

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	obs_source_enum_filters(input, enum_cb, &rval);
	AUTO_DEBUG;
}

void osn::Input::CopyFiltersTo(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input_from = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input_from) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "1st Input reference is not valid.");
	}

	obs_source_t *input_to = osn::Source::Manager::GetInstance().find(args[1].value_union.ui64);
	if (!input_to) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "2nd Input reference is not valid.");
	}

	obs_source_copy_filters(input_to, input_from);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Input::GetDuration(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_media_get_duration(input)));
	AUTO_DEBUG;
}

void osn::Input::GetTime(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_media_get_time(input)));
	AUTO_DEBUG;
}

void osn::Input::SetTime(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	obs_source_media_set_time(input, args[1].value_union.i64);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_media_get_time(input)));
	AUTO_DEBUG;
}

void osn::Input::Play(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);

	if (!input)
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");

	obs_source_media_play_pause(input, false);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Input::Pause(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);

	if (!input)
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");

	obs_source_media_play_pause(input, true);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Input::Restart(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);

	if (!input)
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");

	obs_source_media_restart(input);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Input::Stop(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);

	if (!input)
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");

	obs_source_media_stop(input);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Input::GetMediaState(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *input = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!input) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Input reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_media_get_state(input)));
	AUTO_DEBUG;
}