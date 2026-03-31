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

#include "osn-module.hpp"
#include "osn-error.hpp"
#include "shared.hpp"

void osn::Module::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("Module");

	cls->register_function(std::make_shared<ipc::function>("Open", std::vector<ipc::type>{ipc::type::String, ipc::type::String}, Open));
	cls->register_function(std::make_shared<ipc::function>("Modules", std::vector<ipc::type>{}, Modules));
	cls->register_function(std::make_shared<ipc::function>("Initialize", std::vector<ipc::type>{ipc::type::UInt64}, Initialize));
	cls->register_function(std::make_shared<ipc::function>("GetName", std::vector<ipc::type>{ipc::type::UInt64}, GetName));
	cls->register_function(std::make_shared<ipc::function>("GetFileName", std::vector<ipc::type>{ipc::type::UInt64}, GetFileName));
	cls->register_function(std::make_shared<ipc::function>("GetAuthor", std::vector<ipc::type>{ipc::type::UInt64}, GetAuthor));
	cls->register_function(std::make_shared<ipc::function>("GetDescription", std::vector<ipc::type>{ipc::type::UInt64}, GetDescription));
	cls->register_function(std::make_shared<ipc::function>("GetBinaryPath", std::vector<ipc::type>{ipc::type::UInt64}, GetBinaryPath));
	cls->register_function(std::make_shared<ipc::function>("GetDataPath", std::vector<ipc::type>{ipc::type::UInt64}, GetDataPath));
	cls->register_function(std::make_shared<ipc::function>("GetDataPath", std::vector<ipc::type>{ipc::type::UInt64}, GetDataPath));

	srv.register_collection(cls);
}

void osn::Module::Open(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_module_t *module;

	const std::string bin_path = args[0].value_str.c_str();
	const std::string data_path = args[1].value_str.c_str();

	int64_t result = obs_open_module(&module, bin_path.c_str(), data_path.c_str());

	if (result == MODULE_SUCCESS) {
		uint64_t uid = osn::Module::Manager::GetInstance().allocate(module);

		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		rval.push_back(ipc::value(uid));
	} else {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to create module.");
	}

	AUTO_DEBUG;
}

void osn::Module::Modules(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	std::vector<std::string> modules;

	obs_enum_modules(
		[](void *param, obs_module_t *module) {
			std::vector<std::string> &modules = *static_cast<std::vector<std::string> *>(param);
			const char *name = obs_get_module_file_name(module);
			if (name)
				modules.push_back(name);
		},
		&modules);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint64_t)modules.size()));

	for (size_t i = 0; i < modules.size(); i++) {
		rval.push_back(ipc::value(modules.at(i).c_str()));
	}

	AUTO_DEBUG;
}
void osn::Module::Initialize(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_module_t *module = osn::Module::Manager::GetInstance().find(args[0].value_union.ui64);

	if (!module) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Module reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_init_module(module)));
	AUTO_DEBUG;
}

void osn::Module::GetName(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_module_t *module = osn::Module::Manager::GetInstance().find(args[0].value_union.ui64);

	if (!module) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Module reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_get_module_name(module)));
	AUTO_DEBUG;
}

void osn::Module::GetFileName(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_module_t *module = osn::Module::Manager::GetInstance().find(args[0].value_union.ui64);

	if (!module) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Module reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_get_module_file_name(module)));
	AUTO_DEBUG;
}

void osn::Module::GetAuthor(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_module_t *module = osn::Module::Manager::GetInstance().find(args[0].value_union.ui64);

	if (!module) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Module reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_get_module_author(module)));
	AUTO_DEBUG;
}

void osn::Module::GetDescription(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_module_t *module = osn::Module::Manager::GetInstance().find(args[0].value_union.ui64);

	if (!module) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Module reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_get_module_description(module)));
	AUTO_DEBUG;
}

void osn::Module::GetBinaryPath(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_module_t *module = osn::Module::Manager::GetInstance().find(args[0].value_union.ui64);

	if (!module) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Module reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_get_module_binary_path(module)));
	AUTO_DEBUG;
}

void osn::Module::GetDataPath(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_module_t *module = osn::Module::Manager::GetInstance().find(args[0].value_union.ui64);

	if (!module) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Module reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_get_module_data_path(module)));
	AUTO_DEBUG;
}

void osn::Module::GetFilePath(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AUTO_DEBUG;
}

void osn::Module::GetConfigFilePath(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AUTO_DEBUG;
}

osn::Module::Manager &osn::Module::Manager::GetInstance()
{
	static osn::Module::Manager _inst;
	return _inst;
}