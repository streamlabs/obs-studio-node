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

#include "osn-reconnect.hpp"
#include "osn-error.hpp"
#include "shared.hpp"

void osn::IReconnect::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("Reconnect");

	cls->register_function(std::make_shared<ipc::function>("Create", std::vector<ipc::type>{}, Create));
	cls->register_function(std::make_shared<ipc::function>("Destroy", std::vector<ipc::type>{}, Destroy));

	cls->register_function(std::make_shared<ipc::function>("GetEnabled", std::vector<ipc::type>{ipc::type::UInt64}, GetEnabled));
	cls->register_function(std::make_shared<ipc::function>("SetEnabled", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetEnabled));
	cls->register_function(std::make_shared<ipc::function>("GetRetryDelay", std::vector<ipc::type>{ipc::type::UInt64}, GetRetryDelay));
	cls->register_function(std::make_shared<ipc::function>("SetRetryDelay", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetRetryDelay));
	cls->register_function(std::make_shared<ipc::function>("GetMaxRetries", std::vector<ipc::type>{ipc::type::UInt64}, GetMaxRetries));
	cls->register_function(std::make_shared<ipc::function>("SetMaxRetries", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetMaxRetries));

	srv.register_collection(cls);
}

void osn::IReconnect::Create(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	uint64_t uid = osn::IReconnect::Manager::GetInstance().allocate(new Reconnect());
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::IReconnect::Destroy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	uint64_t uid = args[0].value_union.ui64;
	Reconnect *reconnect = osn::IReconnect::Manager::GetInstance().find(uid);
	if (!reconnect) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Reconnect reference is not valid.");
	}

	osn::IReconnect::Manager::GetInstance().free(uid);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IReconnect::GetEnabled(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Reconnect *reconnect = osn::IReconnect::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!reconnect) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Reconnect reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(reconnect->enabled));
	AUTO_DEBUG;
}

void osn::IReconnect::SetEnabled(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Reconnect *reconnect = osn::IReconnect::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!reconnect) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Reconnect reference is not valid.");
	}

	reconnect->enabled = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IReconnect::GetRetryDelay(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Reconnect *reconnect = osn::IReconnect::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!reconnect) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Reconnect reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(reconnect->retryDelay));
	AUTO_DEBUG;
}

void osn::IReconnect::SetRetryDelay(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Reconnect *reconnect = osn::IReconnect::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!reconnect) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Reconnect reference is not valid.");
	}

	reconnect->retryDelay = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IReconnect::GetMaxRetries(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Reconnect *reconnect = osn::IReconnect::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!reconnect) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Reconnect reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(reconnect->maxRetries));
	AUTO_DEBUG;
}

void osn::IReconnect::SetMaxRetries(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Reconnect *reconnect = osn::IReconnect::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!reconnect) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Reconnect reference is not valid.");
	}

	reconnect->maxRetries = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

osn::IReconnect::Manager &osn::IReconnect::Manager::GetInstance()
{
	static osn::IReconnect::Manager _inst;
	return _inst;
}