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

#pragma once
#include <functional>
#include <inttypes.h>
#include <queue>
#include <sstream>
#include <vector>
#include "ipc-value.hpp"
#include "util-osx.hpp"

#ifndef __FUNCTION_NAME__
#if defined(_WIN32) || defined(_WIN64) //WINDOWS
#define __FUNCTION_NAME__ __FUNCTION__
#else //*NIX
#define __FUNCTION_NAME__ __func__
#endif
#endif

#ifdef __APPLE__
extern UtilInt *g_util_osx;
#endif

static inline std::string StringFromIPCValueVector(std::vector<ipc::value> const &val)
{
	std::stringstream mystream;
	bool prevParam = false;
	for (auto &v : val) {
		if (prevParam) {
			mystream << ", ";
		}
		switch (v.type) {
		case ipc::type::Null:
			mystream << "void";
			break;
		case ipc::type::Float:
			mystream << v.value_union.fp32;
			break;
		case ipc::type::Double:
			mystream << v.value_union.fp64;
			break;
		case ipc::type::Int32:
			mystream << v.value_union.i32;
			break;
		case ipc::type::Int64:
			mystream << v.value_union.i64;
			break;
		case ipc::type::UInt32:
			mystream << v.value_union.ui32;
			break;
		case ipc::type::UInt64:
			mystream << v.value_union.ui64;
			break;
		case ipc::type::String:
			mystream << '"' << v.value_str << '"';
			break;
		case ipc::type::Binary:
			mystream << "binary";
			//mystream << '"' << v.value_str << '"';
			break;
		}
		prevParam = true;
	}

	return mystream.str();
}

#define EXTENDED_DEBUG_LOG
#if defined(EXTENDED_DEBUG_LOG)
#define AUTO_DEBUG blog(LOG_INFO, "%s(%s) = %s", __FUNCTION_NAME__, StringFromIPCValueVector(args).c_str(), StringFromIPCValueVector(rval).c_str());
#else
#define AUTO_DEBUG
#endif
