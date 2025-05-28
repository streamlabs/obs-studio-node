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
#include "osn-network.hpp"
#include "osn-error.hpp"
#include "shared.hpp"

void osn::INetwork::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("Network");

	cls->register_function(std::make_shared<ipc::function>("Create", std::vector<ipc::type>{}, Create));
	cls->register_function(std::make_shared<ipc::function>("Destroy", std::vector<ipc::type>{}, Destroy));

	cls->register_function(std::make_shared<ipc::function>("GetBindIP", std::vector<ipc::type>{ipc::type::UInt64}, GetBindIP));
	cls->register_function(std::make_shared<ipc::function>("SetBindIP", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetBindIP));
	cls->register_function(std::make_shared<ipc::function>("GetNetworkInterfaces", std::vector<ipc::type>{}, GetNetworkInterfaces));
	cls->register_function(std::make_shared<ipc::function>("GetEnableDynamicBitrate", std::vector<ipc::type>{ipc::type::UInt64}, GetEnableDynamicBitrate));
	cls->register_function(std::make_shared<ipc::function>("SetEnableDynamicBitrate", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32},
							       SetEnableDynamicBitrate));
	cls->register_function(std::make_shared<ipc::function>("GetEnableOptimizations", std::vector<ipc::type>{ipc::type::UInt64}, GetEnableOptimizations));
	cls->register_function(std::make_shared<ipc::function>("SetEnableOptimizations", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32},
							       SetEnableOptimizations));
	cls->register_function(std::make_shared<ipc::function>("GetEnableLowLatency", std::vector<ipc::type>{ipc::type::UInt64}, GetEnableLowLatency));
	cls->register_function(
		std::make_shared<ipc::function>("SetEnableLowLatency", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetEnableLowLatency));

	srv.register_collection(cls);
}

void osn::INetwork::Create(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	uint64_t uid = osn::INetwork::Manager::GetInstance().allocate(new Network());
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::INetwork::Destroy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	uint64_t uid = args[0].value_union.ui64;
	Network *network = osn::INetwork::Manager::GetInstance().find(uid);
	if (!network) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Network reference is not valid.");
	}

	osn::INetwork::Manager::GetInstance().free(uid);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::INetwork::GetBindIP(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Network *network = osn::INetwork::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!network) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Network reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(network->bindIP));
	AUTO_DEBUG;
}

void osn::INetwork::SetBindIP(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Network *network = osn::INetwork::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!network) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Network reference is not valid.");
	}

	network->bindIP = args[1].value_str;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::INetwork::GetNetworkInterfaces(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_properties_t *ppts = obs_get_output_properties("rtmp_output");
	obs_property_t *p = obs_properties_get(ppts, "bind_ip");

	size_t count = obs_property_list_item_count(p);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)count));

	for (size_t i = 0; i < count; i++) {
		rval.push_back(ipc::value(obs_property_list_item_name(p, i)));
		rval.push_back(ipc::value(obs_property_list_item_string(p, i)));
	}
}

void osn::INetwork::GetEnableDynamicBitrate(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Network *network = osn::INetwork::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!network) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Network reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(network->enableDynamicBitrate));
	AUTO_DEBUG;
}

void osn::INetwork::SetEnableDynamicBitrate(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Network *network = osn::INetwork::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!network) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Network reference is not valid.");
	}

	network->enableDynamicBitrate = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::INetwork::GetEnableOptimizations(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Network *network = osn::INetwork::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!network) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Network reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(network->enableOptimizations));
	AUTO_DEBUG;
}

void osn::INetwork::SetEnableOptimizations(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Network *network = osn::INetwork::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!network) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Network reference is not valid.");
	}

	network->enableOptimizations = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::INetwork::GetEnableLowLatency(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Network *network = osn::INetwork::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!network) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Network reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(network->enableLowLatency));
	AUTO_DEBUG;
}

void osn::INetwork::SetEnableLowLatency(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Network *network = osn::INetwork::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!network) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Network reference is not valid.");
	}

	network->enableLowLatency = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

osn::INetwork::Manager &osn::INetwork::Manager::GetInstance()
{
	static osn::INetwork::Manager _inst;
	return _inst;
}