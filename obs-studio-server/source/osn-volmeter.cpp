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

#include "osn-volmeter.hpp"
#include "osn-error.hpp"
#include "obs.h"
#include "osn-source.hpp"
#include "shared.hpp"
#include "utility.hpp"
#include <cmath>

std::mutex mtx;

osn::Volmeter::Manager &osn::Volmeter::Manager::GetInstance()
{
	static Manager _inst;
	return _inst;
}

osn::Volmeter::Volmeter(obs_fader_type type)
{
	self = obs_volmeter_create(type);
	if (!self)
		throw std::exception();
}

osn::Volmeter::~Volmeter()
{
	obs_volmeter_destroy(self);
}

void osn::Volmeter::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("Volmeter");
	cls->register_function(std::make_shared<ipc::function>("Create", std::vector<ipc::type>{ipc::type::Int32}, Create));
	cls->register_function(std::make_shared<ipc::function>("Destroy", std::vector<ipc::type>{ipc::type::UInt64}, Destroy));
	cls->register_function(std::make_shared<ipc::function>("Attach", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, Attach));
	cls->register_function(std::make_shared<ipc::function>("Detach", std::vector<ipc::type>{ipc::type::UInt64}, Detach));
	srv.register_collection(cls);
}

void osn::Volmeter::ClearVolmeters()
{
	Manager::GetInstance().for_each(
		[](const std::shared_ptr<osn::Volmeter> &volmeter) { obs_volmeter_remove_callback(volmeter->self, OBSCallback, &volmeter->id); });

	Manager::GetInstance().clear();
}

void osn::Volmeter::Create(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_fader_type type = (obs_fader_type)args[0].value_union.i32;
	std::shared_ptr<Volmeter> meter;

	std::unique_lock<std::mutex> ulock(mtx);
	try {
		meter = std::make_shared<osn::Volmeter>(type);
	} catch (...) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to create Meter.");
	}

	meter->id = Manager::GetInstance().allocate(meter);
	if (meter->id == INVALID_ID) {
		meter.reset();
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Failed to allocate unique id for Meter.");
	}

	obs_volmeter_add_callback(meter->self, OBSCallback, &meter->id);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(meter->id));
	rval.push_back(ipc::value(obs_volmeter_get_update_interval(meter->self)));
	AUTO_DEBUG;
}

void osn::Volmeter::Destroy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	auto uid = args[0].value_union.ui64;

	std::unique_lock<std::mutex> ulock(mtx);
	auto meter = Manager::GetInstance().find(uid);
	if (!meter) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid Meter reference.");
	}

	obs_volmeter_remove_callback(meter->self, OBSCallback, &meter->id);

	Manager::GetInstance().free(uid);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Volmeter::Attach(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	auto uid_fader = args[0].value_union.ui64;
	auto uid_source = args[1].value_union.ui64;

	std::unique_lock<std::mutex> ulock(mtx);
	auto meter = Manager::GetInstance().find(uid_fader);
	if (!meter) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid Meter reference.");
	}

	auto source = osn::Source::Manager::GetInstance().find(uid_source);
	if (!source) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid Source reference.");
	}

	if (!obs_volmeter_attach_source(meter->self, source)) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Error attaching source.");
	}

	meter->uid_source = uid_source;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Volmeter::Detach(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	auto uid = args[0].value_union.ui64;

	std::unique_lock<std::mutex> ulock(mtx);
	auto meter = Manager::GetInstance().find(uid);
	if (!meter) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid Meter reference.");
	}

	meter->uid_source = INVALID_ID;
	obs_volmeter_detach_source(meter->self);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Volmeter::OBSCallback(void *param, const float magnitude[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS],
				const float input_peak[MAX_AUDIO_CHANNELS])
{
	std::unique_lock<std::mutex> ulockMutex(mtx);
	auto meter = Manager::GetInstance().find(*reinterpret_cast<uint64_t *>(param));
	if (!meter) {
		return;
	}

	const auto source = osn::Source::Manager::GetInstance().find(meter->uid_source);
	if (!source) {
		return;
	}

	std::unique_lock<std::mutex> ulock(meter->current_data_mtx);
	meter->current_data.ch = obs_volmeter_get_nr_channels(meter->self);

	const bool isMuted = obs_source_muted(source);
	if (isMuted) {
		return;
	}

	meter->current_data.lastUpdateTime = GetTime();

#define MAKE_FLOAT_SANE(db) (std::isfinite(db) ? db : (db > 0 ? 0.0f : -65535.0f))
#define PREVIOUS_FRAME_WEIGHT

	for (size_t ch = 0; ch < meter->current_data.ch; ch++) {
		meter->current_data.magnitude[ch] = MAKE_FLOAT_SANE(magnitude[ch]);
		meter->current_data.peak[ch] = MAKE_FLOAT_SANE(peak[ch]);
		meter->current_data.input_peak[ch] = MAKE_FLOAT_SANE(input_peak[ch]);
	}

#undef MAKE_FLOAT_SANE
}

std::chrono::milliseconds osn::Volmeter::GetTime()
{
	auto currentTime = std::chrono::high_resolution_clock::now();
	auto currentTimeMs = std::chrono::time_point_cast<std::chrono::milliseconds>(currentTime);
	return currentTimeMs.time_since_epoch();
}

bool osn::Volmeter::CheckIdle(std::chrono::milliseconds currentTime, std::chrono::milliseconds lastUpdateTime)
{
	auto idleTime = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastUpdateTime);

	if (idleTime > std::chrono::milliseconds(300)) {
		return true;
	}

	return false;
}

void osn::Volmeter::getAudioData(uint64_t id, std::vector<ipc::value> &rval)
{
	std::unique_lock<std::mutex> ulockMutex(mtx);

	// Client expects that we send at least some minimal data related to the volmeter back
	const auto createMinimalResponse = [&]() {
		rval.push_back(ipc::value(""));
		rval.push_back(ipc::value(2));
		rval.push_back(ipc::value(true));
	};

	auto meter = Manager::GetInstance().find(id);
	if (!meter) {
		createMinimalResponse();
		blog(LOG_ERROR, "Invalid AudioMeter reference: %" PRIu64, id);
		return;
	}

	std::unique_lock<std::mutex> ulock(meter->current_data_mtx);

	const auto source = osn::Source::Manager::GetInstance().find(meter->uid_source);
	if (!source) {
		createMinimalResponse();
		blog(LOG_ERROR, "Volmeter source not found.: %" PRIu64, meter->uid_source);
		return;
	}

	bool isMuted = obs_source_muted(source);

	if (CheckIdle(GetTime(), meter->current_data.lastUpdateTime)) {
		meter->current_data.resetData();
		meter->current_data.lastUpdateTime = GetTime();

		// isMuted flag only tells UI if it needs process and visualize volmeters bars.
		// It does not responsible for any audio processing or source elements state.
		// Thus, even if volmeter does not recieve any data, we periodically send updates
		// to let UI present volmeters properly in case of UI state changes.
		// For example, enable/disable of performance mode.
		isMuted = false;
	}

	rval.push_back(ipc::value(obs_source_get_name(source)));
	rval.push_back(ipc::value(meter->current_data.ch));
	rval.push_back(ipc::value(isMuted));

	if (isMuted)
		return;

	for (size_t ch = 0; ch < meter->current_data.ch; ch++) {
		rval.push_back(ipc::value(meter->current_data.magnitude[ch]));
		rval.push_back(ipc::value(meter->current_data.peak[ch]));
		rval.push_back(ipc::value(meter->current_data.input_peak[ch]));
	}
}