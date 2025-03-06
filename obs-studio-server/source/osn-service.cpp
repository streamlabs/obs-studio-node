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

#include "osn-service.hpp"
#include <osn-error.hpp>
#include "shared.hpp"
#include "nodeobs_service.h"

#include "nodeobs_configManager.hpp"

void osn::Service::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("Service");
	cls->register_function(std::make_shared<ipc::function>("GetTypes", std::vector<ipc::type>{}, GetTypes));
	cls->register_function(std::make_shared<ipc::function>("Create", std::vector<ipc::type>{ipc::type::String, ipc::type::String}, Create));
	cls->register_function(
		std::make_shared<ipc::function>("Create", std::vector<ipc::type>{ipc::type::String, ipc::type::String, ipc::type::String}, Create));
	cls->register_function(std::make_shared<ipc::function>(
		"Create", std::vector<ipc::type>{ipc::type::String, ipc::type::String, ipc::type::String, ipc::type::String}, Create));
	cls->register_function(std::make_shared<ipc::function>("Destroy", std::vector<ipc::type>{ipc::type::UInt64}, Destroy));
	cls->register_function(std::make_shared<ipc::function>("CreatePrivate", std::vector<ipc::type>{ipc::type::String, ipc::type::String}, CreatePrivate));
	cls->register_function(std::make_shared<ipc::function>("CreatePrivate", std::vector<ipc::type>{ipc::type::String, ipc::type::String, ipc::type::String},
							       CreatePrivate));
	cls->register_function(std::make_shared<ipc::function>("GetName", std::vector<ipc::type>{ipc::type::UInt64}, GetName));
	cls->register_function(std::make_shared<ipc::function>("GetProperties", std::vector<ipc::type>{ipc::type::UInt64}, GetProperties));
	cls->register_function(std::make_shared<ipc::function>("Update", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String}, Update));
	cls->register_function(std::make_shared<ipc::function>("GetSettings", std::vector<ipc::type>{ipc::type::UInt64}, GetSettings));
	cls->register_function(std::make_shared<ipc::function>("GetLegacySettings", std::vector<ipc::type>{}, GetLegacySettings));
	cls->register_function(std::make_shared<ipc::function>("SetLegacySettings", std::vector<ipc::type>{}, SetLegacySettings));

	srv.register_collection(cls);
}

void osn::Service::GetTypes(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

	size_t index = 0;
	const char *type = "";
	while (obs_enum_service_types(index++, &type)) {
		if (type)
			rval.push_back(ipc::value(type));
	}
	AUTO_DEBUG;
}

void osn::Service::Create(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	std::string serviceId, name;
	obs_data_t *settings = nullptr, *hotkeys = nullptr;

	switch (args.size()) {
	case 4:
		hotkeys = obs_data_create_from_json(args[3].value_str.c_str());
	case 3:
		settings = obs_data_create_from_json(args[2].value_str.c_str());
	case 2:
		name = args[1].value_str;
		serviceId = args[0].value_str;
		break;
	}

	obs_service_t *service = obs_service_create(serviceId.c_str(), name.c_str(), settings, hotkeys);
	if (!service) {
		if (settings)
			obs_data_release(settings);
		if (hotkeys)
			obs_data_release(hotkeys);
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to create service.");
	}

	uint64_t uid = osn::Service::Manager::GetInstance().allocate(service);
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::Service::Destroy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_service_t *service = static_cast<obs_service_t *>(osn::Service::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!service) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Service reference is not valid.");
	}

	osn::Service::Manager::GetInstance().free(service);

	obs_service_release(service);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Service::CreatePrivate(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	std::string serviceId, name;
	obs_data_t *settings = nullptr;

	switch (args.size()) {
	case 3:
		settings = obs_data_create_from_json(args[2].value_str.c_str());
	case 2:
		name = args[1].value_str;
		serviceId = args[0].value_str;
		break;
	}

	obs_service_t *service = obs_service_create_private(serviceId.c_str(), name.c_str(), settings);
	if (!service) {
		if (settings)
			obs_data_release(settings);
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to create service.");
	}

	uint64_t uid = osn::Service::Manager::GetInstance().allocate(service);
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::Service::GetName(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_service_t *service = osn::Service::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!service) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Service reference is not valid.");
	}

	const char *name = obs_service_get_name(service);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(name ? name : ""));
	AUTO_DEBUG;
}

void osn::Service::GetProperties(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_service_t *service = osn::Service::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!service) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Service reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

	obs_properties_t *prp = obs_service_properties(service);
	obs_data *settings = obs_service_get_settings(service);

	utility::ProcessProperties(prp, settings, rval);

	obs_properties_destroy(prp);

	obs_data_release(settings);
	AUTO_DEBUG;
}

void osn::Service::Update(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_service_t *service = osn::Service::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!service) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Service reference is not valid.");
	}

	obs_data_t *settings = obs_data_create_from_json(args[1].value_str.c_str());
	obs_service_update(service, settings);
	obs_data_release(settings);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Service::GetSettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_service_t *service = osn::Service::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!service) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Service reference is not valid.");
	}

	obs_data_t *settings = obs_service_get_settings(service);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_data_get_json_pretty(settings)));
	obs_data_release(settings);
	AUTO_DEBUG;
}

obs_service_t *osn::Service::GetLegacyServiceSettings()
{
	obs_data_t *serviceData = obs_data_create_from_json_file_safe(ConfigManager::getInstance().getService(0).c_str(), "bak");

	std::string type = obs_data_get_string(serviceData, "type");
	obs_data_t *settings = obs_data_get_obj(serviceData, "settings");

	obs_service_t *service = obs_service_create(type.c_str(), "service", settings, nullptr);
	obs_data_release(settings);
	obs_data_release(serviceData);

	return service;
}

void osn::Service::GetLegacySettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_service_t *service = GetLegacyServiceSettings();
	if (!service) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to create service.");
	}

	uint64_t uid = osn::Service::Manager::GetInstance().allocate(service);
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::Service::SetLegacyServiceSettings(obs_service_t *service)
{
	if (!service)
		return;

	obs_data_t *settings = obs_service_get_settings(service);
	obs_data_t *serviceData = obs_data_create();
	obs_data_set_string(serviceData, "type", obs_service_get_type(service));
	obs_data_set_obj(serviceData, "settings", settings);

	if (!obs_data_save_json_safe(serviceData, ConfigManager::getInstance().getService(0).c_str(), "tmp", "bak")) {
		blog(LOG_WARNING, "Failed to save service");
	}

	obs_data_release(settings);
	obs_data_release(serviceData);
}

void osn::Service::SetLegacySettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_service_t *service = osn::Service::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!service) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Service reference is not valid.");
	}

	SetLegacyServiceSettings(service);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

osn::Service::Manager &osn::Service::Manager::GetInstance()
{
	static osn::Service::Manager _inst;
	return _inst;
}
