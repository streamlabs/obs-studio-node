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

#include <nlohmann/json.hpp>

#include <optional>

/* From whatsnew.hpp */
#ifndef NLOHMANN_DEFINE_TYPE_INTRUSIVE
#define NLOHMANN_DEFINE_TYPE_INTRUSIVE(Type, ...)                                           \
	friend void to_json(nlohmann::json &nlohmann_json_j, const Type &nlohmann_json_t)   \
	{                                                                                   \
		NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO, __VA_ARGS__))    \
	}                                                                                   \
	friend void from_json(const nlohmann::json &nlohmann_json_j, Type &nlohmann_json_t) \
	{                                                                                   \
		NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM, __VA_ARGS__))  \
	}
#endif
#ifndef NLOHMANN_JSON_FROM_WITH_DEFAULT
#define NLOHMANN_JSON_FROM_WITH_DEFAULT(v1) nlohmann_json_t.v1 = nlohmann_json_j.value(#v1, nlohmann_json_default_obj.v1);
#endif
#ifndef NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT
#define NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Type, ...)                                          \
	friend void to_json(nlohmann::json &nlohmann_json_j, const Type &nlohmann_json_t)               \
	{                                                                                               \
		NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO, __VA_ARGS__))                \
	}                                                                                               \
	friend void from_json(const nlohmann::json &nlohmann_json_j, Type &nlohmann_json_t)             \
	{                                                                                               \
		Type nlohmann_json_default_obj;                                                         \
		NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM_WITH_DEFAULT, __VA_ARGS__)) \
	}
#endif

/*
 * Support for (de-)serialising std::optional
 * From https://github.com/nlohmann/json/issues/1749#issuecomment-1731266676
 * whatsnew.hpp's version doesn't seem to work here
 */
template<typename T> struct nlohmann::adl_serializer<std::optional<T>> {
	static void from_json(const nlohmann::json &j, std::optional<T> &opt)
	{
		if (j.is_null()) {
			opt = std::nullopt;
		} else {
			opt = j.get<T>();
		}
	}
	static void to_json(nlohmann::json &json, std::optional<T> t)
	{
		if (t) {
			json = *t;
		} else {
			json = nullptr;
		}
	}
};
