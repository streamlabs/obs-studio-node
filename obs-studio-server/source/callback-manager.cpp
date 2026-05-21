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

#include "callback-manager.h"
#include "osn-source.hpp"
#ifdef WIN32
#include <windows.h>
#endif
#include "osn-error.hpp"
#include "shared.hpp"
#include "osn-source.hpp"
#include "osn-volmeter.hpp"

#include <memory>

std::mutex sources_mtx;
std::map<std::string, std::unique_ptr<SourceSizeInfo>> sources;

std::mutex transitions_mtx;
std::map<std::string, std::unique_ptr<TransitionInfo>> transitions;

void CallbackManager::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("CallbackManager");
	cls->register_function(std::make_shared<ipc::function>("GlobalQuery", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Binary}, GlobalQuery));
	srv.register_collection(cls);
}

void CallbackManager::GlobalQuery(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	const std::size_t source_size_idx = rval.size();
	rval.push_back(ipc::value((uint32_t)0));

	if (!sources.empty()) {
		sources_mtx.lock();
		uint32_t size = 0;

		for (auto &item : sources) {
			SourceSizeInfo *si = item.second.get();
			// See if width or height changed here
			uint32_t newWidth = obs_source_get_width(si->source);
			uint32_t newHeight = obs_source_get_height(si->source);
			uint32_t newFlags = obs_source_get_output_flags(si->source);

			if (si->width != newWidth || si->height != newHeight || si->flags != newFlags) {
				si->width = newWidth;
				si->height = newHeight;
				si->flags = newFlags;

				rval.push_back(ipc::value(obs_source_get_name(si->source)));
				rval.push_back(ipc::value(si->width));
				rval.push_back(ipc::value(si->height));
				rval.push_back(ipc::value(si->flags));

				size++;
			}
		}

		rval[source_size_idx] = size;
		sources_mtx.unlock();
	}

	const std::size_t transition_size_idx = rval.size();
	rval.push_back(ipc::value((uint32_t)0));

	if (!transitions.empty()) {
		transitions_mtx.lock();
		uint32_t size = 0;

		for (auto &item : transitions) {
			TransitionInfo *ti = item.second.get();

			for (const auto &e : ti->events) {
				rval.push_back(ipc::value(obs_source_get_name(ti->transition)));
				rval.push_back(ipc::value(e));
				size++;
			}

			ti->events.clear();
		}

		rval[transition_size_idx] = size;
		transitions_mtx.unlock();
	}

	const std::size_t messages_size_idx = rval.size();
	rval.push_back(ipc::value((uint32_t)0));

	if (!sources.empty()) {
		sources_mtx.lock();
		uint32_t size = 0;

		for (auto &item : sources) {
			SourceSizeInfo *si = item.second.get();
			obs_data_array_t *messages = obs_source_get_messages(si->source);
			if (messages != nullptr) {
				size_t count = obs_data_array_count(messages);
				for (size_t idx = 0; idx < count; ++idx) {
					obs_data_t *msg = obs_data_array_item(messages, idx);
					rval.push_back(ipc::value(obs_source_get_name(si->source)));
					rval.push_back(ipc::value(obs_data_get_string(msg, "message")));
					blog(LOG_INFO, "[BrowserMessage] %s message: %s", obs_source_get_name(si->source), obs_data_get_string(msg, "message"));
					size++;
				}
				obs_data_array_release(messages);
			}
		}

		rval[messages_size_idx] = size;
		sources_mtx.unlock();
	}

	uint64_t size_buffer = args[0].value_union.ui64;

	std::vector<char> buffer;
	buffer.resize(size_buffer);
	memcpy(buffer.data(), args[1].value_bin.data(), size_buffer);

	uint64_t nb_volmeters = size_buffer / sizeof(uint64_t);
	uint64_t index = 0;

	for (int i = 0; i < nb_volmeters; i++) {
		uint64_t id = *reinterpret_cast<uint64_t *>(buffer.data() + index);
		osn::Volmeter::getAudioData(id, rval);
		index += sizeof(uint64_t);
	}

	AUTO_DEBUG;
}

static void transition_start_handler(void *data, calldata_t *calldata)
{
	auto transitionInfo = reinterpret_cast<TransitionInfo *>(data);
	blog(LOG_INFO, "transition_start_handler - name: %s", obs_source_get_name(transitionInfo->transition));

	transitionInfo->events.push_back(TransitionInfo::START);
}

static void transition_stop_handler(void *data, calldata_t *calldata)
{
	auto transitionInfo = reinterpret_cast<TransitionInfo *>(data);
	blog(LOG_INFO, "transition_stop_handler - name: %s", obs_source_get_name(transitionInfo->transition));

	transitionInfo->events.push_back(TransitionInfo::STOP);
}

void CallbackManager::addSource(obs_source_t *source)
{
	if (!source)
		return;

	uint32_t flags = obs_source_get_output_flags(source);
	if ((flags & OBS_SOURCE_VIDEO) == 0)
		return;

	if (obs_source_get_type(source) == OBS_SOURCE_TYPE_FILTER || obs_source_get_type(source) == OBS_SOURCE_TYPE_SCENE)
		return;

	if (obs_source_get_type(source) == OBS_SOURCE_TYPE_TRANSITION) {
		std::unique_lock<std::mutex> ulock(transitions_mtx);

		const char *raw_name = obs_source_get_name(source);
		if (!raw_name)
			return;

		blog(LOG_INFO, "addSource - transition!: %s", raw_name);

		auto ti = std::make_unique<TransitionInfo>();
		ti->transition = source;

		auto result = transitions.try_emplace(std::string(raw_name), std::move(ti));
		if (!result.second) {
			blog(LOG_WARNING, "addSource - transition '%s' is already tracked; skipping", raw_name);
			return;
		}

		signal_handler_t *sh = obs_source_get_signal_handler(source);
		if (sh) {
			signal_handler_connect(sh, "transition_start", transition_start_handler, result.first->second.get());
			signal_handler_connect(sh, "transition_stop", transition_stop_handler, result.first->second.get());
		}
	} else {
		// Regular source
		std::unique_lock<std::mutex> ulock(sources_mtx);

		SourceSizeInfo *si = new SourceSizeInfo;
		si->source = source;
		si->width = obs_source_get_width(source);
		si->height = obs_source_get_height(source);

		sources.emplace(std::make_pair(std::string(obs_source_get_name(source)), si));
	}
}

void CallbackManager::removeSource(obs_source_t *source)
{
	if (!source)
		return;

	const char *name = obs_source_get_name(source);
	if (!name)
		return;

	if (obs_source_get_type(source) == OBS_SOURCE_TYPE_TRANSITION) {
		std::unique_lock<std::mutex> ulock(transitions_mtx);

		const auto it = transitions.find(name);
		if (it == transitions.end()) {
			return;
		}

		signal_handler_t *sh = obs_source_get_signal_handler(source);
		if (sh) {
			signal_handler_disconnect(sh, "transition_start", transition_start_handler, it->second.get());
			signal_handler_disconnect(sh, "transition_stop", transition_stop_handler, it->second.get());
		}

		transitions.erase(it);
	} else {
		// Regular source
		std::unique_lock<std::mutex> ulock(sources_mtx);
		sources.erase(name);
	}
}