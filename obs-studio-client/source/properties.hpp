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
#include <inttypes.h>
#include <map>
#include <math.h>
#include <napi.h>
#include <unordered_map>
#include "utility-v8.hpp"
#include "obs-property.hpp"

namespace osn {
struct Property {
	enum class Type {
		INVALID,
		BOOL,
		INT,
		FLOAT,
		TEXT,
		PATH,
		LIST,
		COLOR,
		BUTTON,
		FONT,
		EDITABLELIST,
		FRAMERATE,
	};

	std::string name;
	std::string description;
	std::string long_description;

	bool enabled = false;
	bool visible = false;

	Type type;
};

struct NumberProperty : Property {
	enum class Type {
		SCROLLER,
		SLIDER,
	};

	union {
		struct {
			int64_t min, max, step, value;
		} int_value;
		struct {
			double_t min, max, step, value;
		} float_value;
		struct {
			bool value;
		} bool_value;
	};

	Type field_type;
};

struct TextProperty : Property {
	obs::TextProperty::TextType field_type;
	obs::TextProperty::InfoType info_type;
	std::string value;
};

struct PathProperty : Property {
	enum class Type { FILE, FILE_SAVE, PATH_DIRECTORY };

	Type field_type;
	std::string filter;
	std::string default_path;
	std::string value;
};

struct ListProperty : Property {
	enum class Type {
		INVALID,
		EDITABLE,
		LIST,
	};
	enum class Format { INVALID, INT, FLOAT, STRING, BOOL };

	struct Item {
		std::string name;
		bool disabled = false;

		union {
			int64_t value_int = 0;
			double_t value_float;
		};
		std::string value_str;
	};

	union {
		int64_t current_value_int = 0;
		double_t current_value_float;
	};
	std::string current_value_str;

	Type field_type;
	Format item_format;
	std::list<Item> items;
};

struct FontProperty : Property {
	std::string face;
	std::string style;
	std::string path;
	int64_t sizeF = 0;
	uint32_t flags = 0;
};

// Contrary to the name, not compatible with the ListProperty. Actually more comparable to PathProperty.
struct EditableListProperty : Property {
	enum class Type {
		STRINGS,
		FILES,
		FILES_AND_URLS,
	};

	Type field_type;
	std::string filter;
	std::string default_path;
	std::list<std::string> values;
};

struct FrameRateProperty : Property {
	struct FrameRate {
		uint32_t numerator;
		uint32_t denominator;
	};
	struct Option {
		std::string name;
		std::string description;
	};

	std::list<std::pair<FrameRate, FrameRate>> ranges; // minmax range
	std::list<Option> options;                         // Could also be a list, but this allows fast indexing by name.
};

// This is a class that basically implements the OBS behavior.
typedef std::map<size_t, std::shared_ptr<Property>> property_map_t;

// The actual classes that work with JavaScript
class Properties : public Napi::ObjectWrap<osn::Properties> {
public:
	std::shared_ptr<property_map_t> properties;
	uint64_t sourceId;

public:
	std::shared_ptr<property_map_t> GetProperties();
	static Napi::FunctionReference constructor;
	static Napi::Object Init(Napi::Env env, Napi::Object exports);
	Properties(const Napi::CallbackInfo &info);

	Napi::Value Count(const Napi::CallbackInfo &info);
	Napi::Value First(const Napi::CallbackInfo &info);
	Napi::Value Last(const Napi::CallbackInfo &info);
	Napi::Value Get(const Napi::CallbackInfo &info);
};

class PropertyObject : public Napi::ObjectWrap<osn::PropertyObject> {
	osn::Properties *parent;
	uint32_t index;

public:
	static Napi::FunctionReference constructor;
	static Napi::Object Init(Napi::Env env, Napi::Object exports);
	PropertyObject(const Napi::CallbackInfo &info);

	Napi::Value Previous(const Napi::CallbackInfo &info);
	Napi::Value Next(const Napi::CallbackInfo &info);
	Napi::Value IsFirst(const Napi::CallbackInfo &info);
	Napi::Value IsLast(const Napi::CallbackInfo &info);

	Napi::Value GetValue(const Napi::CallbackInfo &info);

	Napi::Value GetName(const Napi::CallbackInfo &info);
	Napi::Value GetDescription(const Napi::CallbackInfo &info);
	Napi::Value GetLongDescription(const Napi::CallbackInfo &info);
	Napi::Value IsEnabled(const Napi::CallbackInfo &info);
	Napi::Value IsVisible(const Napi::CallbackInfo &info);
	Napi::Value GetType(const Napi::CallbackInfo &info);

	Napi::Value GetDetails(const Napi::CallbackInfo &info);

	Napi::Value Modified(const Napi::CallbackInfo &info);
	Napi::Value ButtonClicked(const Napi::CallbackInfo &info);
};

property_map_t ProcessProperties(const std::vector<ipc::value> &data, size_t index);
}
