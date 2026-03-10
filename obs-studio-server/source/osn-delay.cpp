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

#include "osn-delay.hpp"
#include "osn-error.hpp"
#include "shared.hpp"

void osn::IDelay::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("Delay");

	cls->register_function(std::make_shared<ipc::function>("Create", std::vector<ipc::type>{}, Create));
	cls->register_function(std::make_shared<ipc::function>("Destroy", std::vector<ipc::type>{}, Destroy));

	cls->register_function(std::make_shared<ipc::function>("GetEnabled", std::vector<ipc::type>{ipc::type::UInt64}, GetEnabled));
	cls->register_function(std::make_shared<ipc::function>("SetEnabled", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetEnabled));
	cls->register_function(std::make_shared<ipc::function>("GetDelaySec", std::vector<ipc::type>{ipc::type::UInt64}, GetDelaySec));
	cls->register_function(std::make_shared<ipc::function>("SetDelaySec", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetDelaySec));
	cls->register_function(std::make_shared<ipc::function>("GetPreserveDelay", std::vector<ipc::type>{ipc::type::UInt64}, GetPreserveDelay));
	cls->register_function(
		std::make_shared<ipc::function>("SetPreserveDelay", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetPreserveDelay));

	srv.register_collection(cls);
}

void osn::IDelay::Create(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	uint64_t uid = osn::IDelay::Manager::GetInstance().allocate(new Delay());
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::IDelay::Destroy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	uint64_t uid = args[0].value_union.ui64;
	Delay *delay = osn::IDelay::Manager::GetInstance().find(uid);
	if (!delay) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Delay reference is not valid.");
	}

	osn::IDelay::Manager::GetInstance().free(uid);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IDelay::GetEnabled(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Delay *delay = osn::IDelay::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!delay) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Delay reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(delay->enabled));
	AUTO_DEBUG;
}

void osn::IDelay::SetEnabled(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Delay *delay = osn::IDelay::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!delay) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Delay reference is not valid.");
	}

	delay->enabled = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IDelay::GetDelaySec(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Delay *delay = osn::IDelay::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!delay) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Delay reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(delay->delaySec));
	AUTO_DEBUG;
}

void osn::IDelay::SetDelaySec(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Delay *delay = osn::IDelay::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!delay) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Delay reference is not valid.");
	}

	delay->delaySec = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::IDelay::GetPreserveDelay(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Delay *delay = osn::IDelay::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!delay) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Delay reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(delay->preserveDelay));
	AUTO_DEBUG;
}

void osn::IDelay::SetPreserveDelay(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Delay *delay = osn::IDelay::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!delay) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Delay reference is not valid.");
	}

	delay->preserveDelay = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

osn::IDelay::Manager &osn::IDelay::Manager::GetInstance()
{
	static osn::IDelay::Manager _inst;
	return _inst;
}