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

#include "osn-source.hpp"
#include <ipc-class.hpp>
#include <ipc-function.hpp>
#include <ipc-server.hpp>
#include <ipc-value.hpp>
#include <map>
#include <memory>
#include <obs-data.h>
#include <obs.h>
#include <obs.hpp>
#include "osn-error.hpp"
#include "osn-common.hpp"
#include "shared.hpp"
#include "callback-manager.h"
#include "memory-manager.h"

void osn::Source::initialize_global_signals()
{
	signal_handler_t *sh = obs_get_signal_handler();
	signal_handler_connect(sh, "source_create", osn::Source::global_source_create_cb, nullptr);
	signal_handler_connect(sh, "source_activate", osn::Source::global_source_activate_cb, nullptr);
	signal_handler_connect(sh, "source_deactivate", osn::Source::global_source_deactivate_cb, nullptr);
}

void osn::Source::finalize_global_signals()
{
	signal_handler_t *sh = obs_get_signal_handler();
	signal_handler_disconnect(sh, "source_create", osn::Source::global_source_create_cb, nullptr);
}

void osn::Source::attach_source_signals(obs_source_t *src)
{
	signal_handler_t *sh = obs_source_get_signal_handler(src);
	if (!sh)
		return;
	signal_handler_connect(sh, "destroy", osn::Source::global_source_destroy_cb, nullptr);
	signal_handler_connect(sh, "remove", osn::Source::global_source_remove_cb, nullptr);
}

void osn::Source::detach_source_signals(obs_source_t *src)
{
	signal_handler_t *sh = obs_source_get_signal_handler(src);
	if (!sh)
		return;
	signal_handler_disconnect(sh, "remove", osn::Source::global_source_remove_cb, nullptr);
	signal_handler_disconnect(sh, "destroy", osn::Source::global_source_destroy_cb, nullptr);
}

void osn::Source::global_source_create_cb(void *ptr, calldata_t *cd)
{
	obs_source_t *source = nullptr;
	if (!calldata_get_ptr(cd, "source", &source)) {
		throw std::runtime_error("calldata did not contain source pointer");
	}

	osn::Source::Manager::GetInstance().allocate(source);
	osn::Source::attach_source_signals(source);
	CallbackManager::addSource(source);
	MemoryManager::GetInstance().registerSource(source);
}

void osn::Source::global_source_activate_cb(void *ptr, calldata_t *cd)
{
	obs_source_t *source = nullptr;
	if (!calldata_get_ptr(cd, "source", &source)) {
		throw std::runtime_error("calldata did not contain source pointer");
	}
	MemoryManager::GetInstance().updateSourceCache(source);
}

void osn::Source::global_source_deactivate_cb(void *ptr, calldata_t *cd)
{
	obs_source_t *source = nullptr;
	if (!calldata_get_ptr(cd, "source", &source)) {
		throw std::runtime_error("calldata did not contain source pointer");
	}
	MemoryManager::GetInstance().updateSourceCache(source);
}

void osn::Source::global_source_destroy_cb(void *ptr, calldata_t *cd)
{
	obs_source_t *source = nullptr;
	if (!calldata_get_ptr(cd, "source", &source)) {
		throw std::runtime_error("calldata did not contain source pointer");
	}

	CallbackManager::removeSource(source);
	detach_source_signals(source);
	osn::Source::Manager::GetInstance().free(source);
}

void osn::Source::global_source_remove_cb(void *ptr, calldata_t *cd)
{
	obs_source_t *source = nullptr;
	if (!calldata_get_ptr(cd, "source", &source)) {
		throw std::runtime_error("calldata did not contain source pointer");
	}

	MemoryManager::GetInstance().unregisterSource(source);
	obs_source_release(source);
}

void osn::Source::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("Source");
	cls->register_function(std::make_shared<ipc::function>("GetDefaults", std::vector<ipc::type>{ipc::type::String}, GetTypeDefaults));

	cls->register_function(std::make_shared<ipc::function>("CallHandler", std::vector<ipc::type>{ipc::type::String}, CallHandler));
	cls->register_function(std::make_shared<ipc::function>("Remove", std::vector<ipc::type>{ipc::type::UInt64}, Remove));
	cls->register_function(std::make_shared<ipc::function>("Release", std::vector<ipc::type>{ipc::type::UInt64}, Release));
	cls->register_function(std::make_shared<ipc::function>("IsConfigurable", std::vector<ipc::type>{ipc::type::UInt64}, IsConfigurable));
	cls->register_function(std::make_shared<ipc::function>("GetProperties", std::vector<ipc::type>{ipc::type::UInt64}, GetProperties));
	cls->register_function(std::make_shared<ipc::function>("GetSettings", std::vector<ipc::type>{ipc::type::UInt64}, GetSettings));
	cls->register_function(std::make_shared<ipc::function>("Load", std::vector<ipc::type>{ipc::type::UInt64}, Load));
	cls->register_function(std::make_shared<ipc::function>("Save", std::vector<ipc::type>{ipc::type::UInt64}, Save));
	cls->register_function(std::make_shared<ipc::function>("Update", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String}, Update));
	cls->register_function(std::make_shared<ipc::function>("GetType", std::vector<ipc::type>{ipc::type::UInt64}, GetType));
	cls->register_function(std::make_shared<ipc::function>("GetName", std::vector<ipc::type>{ipc::type::UInt64}, GetName));
	cls->register_function(std::make_shared<ipc::function>("SetName", std::vector<ipc::type>{ipc::type::UInt64}, SetName));
	cls->register_function(std::make_shared<ipc::function>("SendMessage", std::vector<ipc::type>{ipc::type::UInt64}, SendMessage));
	cls->register_function(std::make_shared<ipc::function>("GetOutputFlags", std::vector<ipc::type>{ipc::type::UInt64}, GetOutputFlags));
	cls->register_function(std::make_shared<ipc::function>("GetFlags", std::vector<ipc::type>{ipc::type::UInt64}, GetFlags));
	cls->register_function(std::make_shared<ipc::function>("SetFlags", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetFlags));
	cls->register_function(std::make_shared<ipc::function>("GetStatus", std::vector<ipc::type>{ipc::type::UInt64}, GetStatus));
	cls->register_function(std::make_shared<ipc::function>("GetId", std::vector<ipc::type>{ipc::type::UInt64}, GetId));
	cls->register_function(std::make_shared<ipc::function>("GetMuted", std::vector<ipc::type>{ipc::type::UInt64}, GetMuted));
	cls->register_function(std::make_shared<ipc::function>("SetMuted", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int32}, SetMuted));
	cls->register_function(std::make_shared<ipc::function>("GetEnabled", std::vector<ipc::type>{ipc::type::UInt64}, GetEnabled));
	cls->register_function(std::make_shared<ipc::function>("SetEnabled", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int32}, SetEnabled));

	cls->register_function(
		std::make_shared<ipc::function>("SendMouseClick",
						std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32, ipc::type::UInt32, ipc::type::UInt32,
								       ipc::type::UInt32, ipc::type::Int32, ipc::type::UInt32},
						SendMouseClick));
	cls->register_function(std::make_shared<ipc::function>(
		"SendMouseMove", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32, ipc::type::UInt32, ipc::type::UInt32, ipc::type::Int32},
		SendMouseMove));
	cls->register_function(std::make_shared<ipc::function>("SendMouseWheel",
							       std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32, ipc::type::UInt32,
										      ipc::type::UInt32, ipc::type::Int32, ipc::type::Int32},
							       SendMouseWheel));
	cls->register_function(std::make_shared<ipc::function>("SendFocus", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int32}, SendFocus));
	cls->register_function(
		std::make_shared<ipc::function>("SendKeyClick",
						std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32, ipc::type::String, ipc::type::UInt32,
								       ipc::type::UInt32, ipc::type::UInt32, ipc::type::Int32},
						SendKeyClick));

	srv.register_collection(cls);
}

void osn::Source::GetTypeDefaults(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AUTO_DEBUG;
	// Per Type Defaults (doesn't have an object)
	//obs_get_source_defaults();
}

void osn::Source::GetTypeOutputFlags(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AUTO_DEBUG;
	// Per Type Defaults (doesn't have an object)
	//obs_get_source_output_flags();
}

void osn::Source::Remove(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Attempt to find the source asked to load.
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	obs_source_remove(src);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Source::Release(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Attempt to find the source asked to load.
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	if (obs_source_get_type(src) == OBS_SOURCE_TYPE_TRANSITION) {
		obs_source_release(src);
	} else if (obs_source_get_type(src) == OBS_SOURCE_TYPE_SCENE) {
		blog(LOG_INFO, "Releasing scene %s", obs_source_get_name(src));
		std::list<obs_sceneitem_t *> items;
		auto cb = [](obs_scene_t *scene, obs_sceneitem_t *item, void *data) {
			if (item) {
				std::list<obs_sceneitem_t *> *items = reinterpret_cast<std::list<obs_sceneitem_t *> *>(data);
				obs_sceneitem_addref(item);
				items->push_back(item);
			}
			return true;
		};
		obs_scene_t *scene = obs_scene_from_source(src);
		if (scene)
			obs_scene_enum_items(scene, cb, &items);

		for (auto item : items) {
			obs_sceneitem_remove(item);
			obs_sceneitem_release(item);
		}

		obs_source_release(src);
	} else {
		obs_source_remove(src);
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Source::IsConfigurable(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Attempt to find the source asked to load.
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_configurable(src)));
	AUTO_DEBUG;
}

void osn::Source::GetProperties(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Atomically find and acquire a strong reference under the manager lock,
	// preventing the source from being destroyed between find() and obs_source_get_ref().
	OBSSourceAutoRelease src = osn::Source::Manager::GetInstance().findAndRef(args[0].value_union.ui64);
	if (!src) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

	obs_properties_t *prp = obs_source_properties(src);
	obs_data *settings = obs_source_get_settings(src);

	utility::ProcessProperties(prp, settings, rval);

	obs_properties_destroy(prp);

	obs_source_update(src, settings);

	obs_data_release(settings);
	AUTO_DEBUG;
}

void osn::Source::CallHandler(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Attempt to find the source asked to load.
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	std::string function_name = args[1].value_str.c_str();
	std::string function_input = args[2].value_str.c_str();

	calldata_t cd;
	calldata_init(&cd);
	calldata_set_string(&cd, "input", function_input.c_str());

	auto procHandler = obs_source_get_proc_handler(src);

	if (procHandler == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::NotFound));
		rval.push_back(ipc::value("obs_source_get_proc_handler returned null"));
	}

	// Call function by name
	else if (proc_handler_call(procHandler, function_name.c_str(), &cd)) {
		std::string result;

		if (const char *str = calldata_string(&cd, "output"))
			result = str;

		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		rval.push_back(ipc::value(result));
	} else {
		rval.push_back(ipc::value((uint64_t)ErrorCode::NotFound));
		rval.push_back(ipc::value("proc_handler_call failed, function_name: " + function_name + ", function_input: " + function_input));
	}

	calldata_free(&cd);
	AUTO_DEBUG;
}

void osn::Source::GetSettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Attempt to find the source asked to load.
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	obs_data_t *sets = obs_source_get_settings(src);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_data_get_json_pretty(sets)));
	obs_data_release(sets);
	AUTO_DEBUG;
}

void osn::Source::Update(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	blog(LOG_INFO, "[BrowserMessage] Update called with source id: %llu, %s", args[0].value_union.ui64, args[1].value_str.c_str());
	// Attempt to find the source asked to load.
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	obs_data_t *sets = obs_data_create_from_json(args[1].value_str.c_str());

	std::string_view unversioned_id(obs_source_get_unversioned_id(src));
	// Check for both "macos_avcapture" and "macos_avcapture_fast"
	if (unversioned_id.find("macos_avcapture") != std::string_view::npos) {
		const char *frame_rate_string = obs_data_get_string(sets, "frame_rate");
		if (frame_rate_string && strcmp(frame_rate_string, "") != 0) {
			nlohmann::json fps = nlohmann::json::parse(frame_rate_string);
			media_frames_per_second obs_fps = {};
			obs_fps.numerator = fps["numerator"];
			obs_fps.denominator = fps["denominator"];
			obs_data_set_frames_per_second(sets, "frame_rate", obs_fps, nullptr);
		}
	}

	obs_source_update(src, sets);
	MemoryManager::GetInstance().updateSourceCache(src);
	obs_data_release(sets);

	obs_data_t *updatedSettings = obs_source_get_settings(src);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_data_get_json_pretty(updatedSettings)));
	obs_data_release(updatedSettings);
	AUTO_DEBUG;
}

void osn::Source::Load(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Attempt to find the source asked to load.
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	obs_source_load(src);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Source::Save(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Attempt to find the source asked to load.
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	obs_source_save(src);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Source::GetType(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Attempt to find the source asked to load.
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_type(src)));
	AUTO_DEBUG;
}

void osn::Source::GetName(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Attempt to find the source asked to load.
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_name(src)));
	AUTO_DEBUG;
}

void osn::Source::SetName(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Attempt to find the source asked to load.
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	obs_source_set_name(src, args[1].value_str.c_str());

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_name(src)));
	AUTO_DEBUG;
}

void osn::Source::GetOutputFlags(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Attempt to find the source asked to load.
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_output_flags(src)));
	AUTO_DEBUG;
}

void osn::Source::GetFlags(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Attempt to find the source asked to load.
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_flags(src)));
	AUTO_DEBUG;
}

void osn::Source::SetFlags(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Attempt to find the source asked to load.
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	obs_source_set_flags(src, args[1].value_union.ui32);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_get_flags(src)));
	AUTO_DEBUG;
}

void osn::Source::GetStatus(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Attempt to find the source asked to load.
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t) true));
	AUTO_DEBUG;
}

void osn::Source::GetId(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Attempt to find the source asked to load.
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	const char *sid = obs_source_get_unversioned_id(src);
	rval.push_back(ipc::value(sid ? sid : ""));
	AUTO_DEBUG;
}

void osn::Source::SendMessage(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	blog(LOG_INFO, "[BrowserMessage] SendMessage called with source id: %llu, %s", args[0].value_union.ui64, args[1].value_str.c_str());
	// Attempt to find the source asked to load.
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	obs_data_t *sets = obs_data_create_from_json(args[1].value_str.c_str());

	obs_source_send_message(src, sets);
	obs_data_release(sets);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Source::GetMuted(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Attempt to find the source asked to load.
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_muted(src)));
	AUTO_DEBUG;
}

void osn::Source::SetMuted(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Attempt to find the source asked to load.
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	obs_source_set_muted(src, !!args[1].value_union.i32);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_muted(src)));
	AUTO_DEBUG;
}

void osn::Source::GetEnabled(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Attempt to find the source asked to load.
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_enabled(src)));
	AUTO_DEBUG;
}

void osn::Source::SetEnabled(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Attempt to find the source asked to load.
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (src == nullptr) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	obs_source_set_enabled(src, !!args[1].value_union.i32);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_source_enabled(src)));
	AUTO_DEBUG;
}

void osn::Source::SendMouseClick(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);

	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	obs_mouse_event event = {
		args[1].value_union.ui32,
		(int32_t)args[2].value_union.ui32,
		(int32_t)args[3].value_union.ui32,
	};

	obs_source_send_mouse_click(src, &event, args[4].value_union.ui32, args[5].value_union.i32, args[6].value_union.ui32);

	AUTO_DEBUG;
}

void osn::Source::SendMouseMove(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);

	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	obs_mouse_event event = {args[1].value_union.ui32, (int32_t)args[2].value_union.ui32, (int32_t)args[3].value_union.ui32};

	obs_source_send_mouse_move(src, &event, args[4].value_union.i32);

	AUTO_DEBUG;
}

void osn::Source::SendMouseWheel(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);

	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	obs_mouse_event event = {
		args[1].value_union.ui32,
		(int32_t)args[2].value_union.ui32,
		(int32_t)args[3].value_union.ui32,
	};

	obs_source_send_mouse_wheel(src, &event, args[4].value_union.i32, args[5].value_union.i32);

	AUTO_DEBUG;
}

void osn::Source::SendFocus(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);

	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	obs_source_send_focus(src, args[1].value_union.i32);

	AUTO_DEBUG;
}

void osn::Source::SendKeyClick(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *src = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);

	if (src == nullptr) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::InvalidReference));
		rval.push_back(ipc::value("Source reference is not valid."));
		AUTO_DEBUG;
		return;
	}

	char *text = new char[args[2].value_str.size() + 1];
	strcpy(text, args[2].value_str.c_str());

	obs_key_event event = {args[1].value_union.ui32, text, args[3].value_union.ui32, args[4].value_union.ui32, args[5].value_union.ui32};

	obs_source_send_key_click(src, &event, args[6].value_union.i32);

	delete[] text;

	AUTO_DEBUG;
}

osn::Source::Manager &osn::Source::Manager::GetInstance()
{
	static osn::Source::Manager _inst;
	return _inst;
}
