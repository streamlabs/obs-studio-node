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

#include "utility.hpp"
#include "obs-property.hpp"

std::string utility::osn_current_version(const std::string &_version)
{
	static std::string current_version = "";
	if (_version != "")
		current_version = _version;

	return current_version;
}

utility::unique_id::unique_id() {}

utility::unique_id::~unique_id() {}

utility::unique_id::id_t utility::unique_id::allocate()
{
	if (allocated.size() > 0) {
		for (auto &v : allocated) {
			if (v.first > 0) {
				utility::unique_id::id_t v2 = v.first - 1;
				mark_used(v2);
				return v2;
			} else if (v.second < std::numeric_limits<utility::unique_id::id_t>::max()) {
				utility::unique_id::id_t v2 = v.second + 1;
				mark_used(v2);
				return v2;
			}
		}
	} else {
		mark_used(0);
		return 0;
	}

	// No more free indexes. However that has happened.
	return std::numeric_limits<utility::unique_id::id_t>::max();
}

void utility::unique_id::free(utility::unique_id::id_t v)
{
	mark_free(v);
}

bool utility::unique_id::is_allocated(utility::unique_id::id_t v)
{
	for (auto &v2 : allocated) {
		if ((v >= v2.first) && (v <= v2.second))
			return true;
	}
	return false;
}

utility::unique_id::id_t utility::unique_id::count(bool count_free)
{
	id_t count = 0;
	for (auto &v : allocated) {
		count += (v.second - v.first);
	}
	return count_free ? (std::numeric_limits<id_t>::max() - count) : count;
}

bool utility::unique_id::mark_used(utility::unique_id::id_t v)
{
	// If no elements have been assigned, simply insert v as used.
	if (allocated.size() == 0) {
		range_t r;
		r.first = r.second = v;
		allocated.push_back(r);
		return true;
	}

	// Otherwise, attempt to find the best fitting element.
	bool lastWasSmaller = false;
	for (auto iter = allocated.begin(); iter != allocated.end(); iter++) {
		auto fiter = std::list<range_t>::iterator(iter);
		auto riter = std::list<range_t>::reverse_iterator(iter);
		if ((iter->first > 0) && (v == (iter->first - 1))) {
			// If the minimum of the selected element is > 0 and v is equal to
			//  (minimum - 1), decrease the minimum.
			iter->first--;

			// Then test if the previous elements maximum is equal to (v - 1),
			//  if so merge the two since they are now continuous.
			riter--;
			if ((riter != allocated.rend()) && (riter->second == (v - 1))) {
				riter->second = iter->second;
				allocated.erase(iter);
			}

			return true;
		} else if ((iter->second < std::numeric_limits<utility::unique_id::id_t>::max()) && (v == (iter->second + 1))) {
			// If the maximum of the selected element is < UINT_MAX and v is
			//  equal to (maximum + 1), increase the maximum.
			iter->second++;

			// Then test if the next elements minimum is equal to (v + 1),
			//  if so merge the two since they are now continuous.
			fiter++;
			if ((fiter != allocated.end()) && (fiter->first == (v + 1))) {
				iter->second = fiter->second;
				allocated.erase(fiter);
			}

			return true;
		} else if (lastWasSmaller && (v < iter->first)) {
			// If we are between two ranges that are smaller and larger than v
			//  insert a new element before the larger range containing only v.
			allocated.insert(iter, {v, v});
			return true;
		} else if ((fiter++) == allocated.end()) {
			// Otherwise if we reached the end of the list, append v.
			allocated.insert(fiter, {v, v});
			return true;
		}
		lastWasSmaller = (v > iter->second);
	}
	return false;
}

void utility::unique_id::mark_used_range(utility::unique_id::id_t min, utility::unique_id::id_t max)
{
	for (utility::unique_id::id_t v = min; v < max; v++) {
		mark_used(v);
	}
}

bool utility::unique_id::mark_free(utility::unique_id::id_t v)
{
	for (auto iter = allocated.begin(); iter != allocated.end(); iter++) {
		// Is v inside this range?
		if ((v >= iter->first) && (v <= iter->second)) {
			if (v == iter->first) {
				// If v is simply the beginning of the range, increase the
				//  minimum and test if the range is now no longer valid.
				iter->first++;
				if (iter->first > iter->second) {
					// If the range is no longer valid, just erase it.
					allocated.erase(iter);
				}
				return true;
			} else if (v == iter->second) {
				// If v is simply the end of the range, decrease the maximum
				//  and test if the range is now no longer valid.
				iter->second--;
				if (iter->second < iter->first) {
					// If the range is no longer valid, just erase it.
					allocated.erase(iter);
				}
				return true;
			} else {
				// Otherwise, since v is inside the range, split the range at
				// v and insert a new element.
				range_t x;
				x.first = iter->first;
				x.second = v - 1;
				iter->first = v + 1;
				allocated.insert(iter, x);
				return true;
			}
		}
	}
	return false;
}

void utility::unique_id::mark_free_range(utility::unique_id::id_t min, utility::unique_id::id_t max)
{
	for (utility::unique_id::id_t v = min; v < max; v++) {
		mark_free(v);
	}
}

void utility::ProcessProperties(obs_properties_t *prp, obs_data *settings, std::vector<ipc::value> &rval)
{
	const char *buf = nullptr;
	for (obs_property_t *p = obs_properties_first(prp); (p != nullptr); obs_property_next(&p)) {
		std::shared_ptr<obs::Property> prop;
		const char *name = obs_property_name(p);

		switch (obs_property_get_type(p)) {
		case OBS_PROPERTY_BOOL: {
			auto prop2 = std::make_shared<obs::BooleanProperty>();
			prop2->value = obs_data_get_bool(settings, name);
			prop = prop2;
			break;
		}
		case OBS_PROPERTY_INT: {
			auto prop2 = std::make_shared<obs::IntegerProperty>();
			prop2->field_type = obs::NumberProperty::NumberType(obs_property_int_type(p));
			prop2->minimum = obs_property_int_min(p);
			prop2->maximum = obs_property_int_max(p);
			prop2->step = obs_property_int_step(p);
			prop2->value = (int)obs_data_get_int(settings, name);
			prop = prop2;
			break;
		}
		case OBS_PROPERTY_FLOAT: {
			auto prop2 = std::make_shared<obs::FloatProperty>();
			prop2->field_type = obs::NumberProperty::NumberType(obs_property_float_type(p));
			prop2->minimum = obs_property_float_min(p);
			prop2->maximum = obs_property_float_max(p);
			prop2->step = obs_property_float_step(p);
			prop2->value = obs_data_get_double(settings, name);
			prop = prop2;
			break;
		}
		case OBS_PROPERTY_TEXT: {
			auto prop2 = std::make_shared<obs::TextProperty>();
			prop2->field_type = obs::TextProperty::TextType(obs_property_text_type(p));
			prop2->info_type = obs::TextProperty::InfoType(obs_property_text_info_type(p));
			prop2->value = (buf = obs_data_get_string(settings, name)) != nullptr ? buf : "";
			prop = prop2;
			break;
		}
		case OBS_PROPERTY_PATH: {
			auto prop2 = std::make_shared<obs::PathProperty>();
			prop2->field_type = obs::PathProperty::PathType(obs_property_path_type(p));
			prop2->filter = (buf = obs_property_path_filter(p)) != nullptr ? buf : "";
			prop2->default_path = (buf = obs_property_path_default_path(p)) != nullptr ? buf : "";
			prop2->value = (buf = obs_data_get_string(settings, name)) != nullptr ? buf : "";
			prop = prop2;
			break;
		}
		case OBS_PROPERTY_LIST: {
			auto prop2 = std::make_shared<obs::ListProperty>();
			prop2->field_type = obs::ListProperty::ListType(obs_property_list_type(p));
			prop2->format = obs::ListProperty::Format(obs_property_list_format(p));
			size_t items = obs_property_list_item_count(p);
			for (size_t idx = 0; idx < items; ++idx) {
				obs::ListProperty::Item entry;
				entry.name = (buf = obs_property_list_item_name(p, idx)) != nullptr ? buf : "";
				entry.enabled = !obs_property_list_item_disabled(p, idx);
				switch (prop2->format) {
				case obs::ListProperty::Format::Integer:
					entry.value_int = obs_property_list_item_int(p, idx);
					break;
				case obs::ListProperty::Format::Float:
					entry.value_float = obs_property_list_item_float(p, idx);
					break;
				case obs::ListProperty::Format::Bool:
					entry.value_int = obs_property_list_item_bool(p, idx);
					break;
				case obs::ListProperty::Format::String:
					entry.value_string = (buf = obs_property_list_item_string(p, idx)) != nullptr ? buf : "";
					break;
				}
				prop2->items.push_back(std::move(entry));
			}
			switch (prop2->format) {
			case obs::ListProperty::Format::Integer: {
				prop2->current_value_int = (int)obs_data_get_int(settings, name);
				break;
			}
			case obs::ListProperty::Format::Float: {
				prop2->current_value_float = obs_data_get_double(settings, name);
				break;
			}
			case obs::ListProperty::Format::Bool: {
				prop2->current_value_int = (int)obs_data_get_bool(settings, name);
				break;
			}
			case obs::ListProperty::Format::String: {
				prop2->current_value_str = (buf = obs_data_get_string(settings, name)) != nullptr ? buf : "";
				break;
			}
			}
			prop = prop2;
			break;
		}
		case OBS_PROPERTY_COLOR_ALPHA:
		case OBS_PROPERTY_COLOR: {
			auto prop2 = std::make_shared<obs::ColorProperty>();
			prop2->field_type = obs::NumberProperty::NumberType(obs_property_int_type(p));
			prop2->value = (int)obs_data_get_int(settings, name);
			prop = prop2;
			break;
		}
		case OBS_PROPERTY_CAPTURE: {
			auto prop2 = std::make_shared<obs::CaptureProperty>();
			prop2->field_type = obs::NumberProperty::NumberType(obs_property_int_type(p));
			prop2->value = (int)obs_data_get_int(settings, name);
			prop = prop2;
			break;
		}
		case OBS_PROPERTY_BUTTON:
			prop = std::make_shared<obs::ButtonProperty>();
			break;
		case OBS_PROPERTY_FONT: {
			auto prop2 = std::make_shared<obs::FontProperty>();
			obs_data_t *font_obj = obs_data_get_obj(settings, name);
			prop2->face = (buf = obs_data_get_string(font_obj, "face")) != nullptr ? buf : "";
			prop2->style = (buf = obs_data_get_string(font_obj, "style")) != nullptr ? buf : "";
			prop2->path = (buf = obs_data_get_string(font_obj, "path")) != nullptr ? buf : "";
			prop2->sizeF = (int)obs_data_get_int(font_obj, "size");
			prop2->flags = (uint32_t)obs_data_get_int(font_obj, "flags");
			prop = prop2;
			break;
		}
		case OBS_PROPERTY_EDITABLE_LIST: {
			auto prop2 = std::make_shared<obs::EditableListProperty>();
			prop2->field_type = obs::EditableListProperty::ListType(obs_property_editable_list_type(p));
			prop2->filter = (buf = obs_property_editable_list_filter(p)) != nullptr ? buf : "";
			prop2->default_path = (buf = obs_property_editable_list_default_path(p)) != nullptr ? buf : "";

			obs_data_array_t *array = obs_data_get_array(settings, name);
			size_t count = obs_data_array_count(array);

			for (size_t idx = 0; idx < count; ++idx) {
				obs_data_t *item = obs_data_array_item(array, idx);
				prop2->values.push_back((buf = obs_data_get_string(item, "value")) != nullptr ? buf : "");
			}

			prop = prop2;
			break;
		}
		case OBS_PROPERTY_FRAME_RATE: {
			auto prop2 = std::make_shared<obs::FrameRateProperty>();
			size_t num_ranges = obs_property_frame_rate_fps_ranges_count(p);
			for (size_t idx = 0; idx < num_ranges; idx++) {
				auto min = obs_property_frame_rate_fps_range_min(p, idx), max = obs_property_frame_rate_fps_range_max(p, idx);

				obs::FrameRateProperty::Range range;
				range.minimum.first = min.numerator;
				range.minimum.second = min.denominator;
				range.maximum.first = max.numerator;
				range.maximum.second = max.denominator;

				prop2->ranges.push_back(std::move(range));
			}

			size_t num_options = obs_property_frame_rate_options_count(p);
			for (size_t idx = 0; idx < num_options; idx++) {
				auto min = obs_property_frame_rate_fps_range_min(p, idx), max = obs_property_frame_rate_fps_range_max(p, idx);

				obs::FrameRateProperty::Option option;
				option.name = (buf = obs_property_frame_rate_option_name(p, idx)) != nullptr ? buf : "";
				option.description = (buf = obs_property_frame_rate_option_description(p, idx)) != nullptr ? buf : "";

				prop2->options.push_back(std::move(option));
			}

			media_frames_per_second fps = {};
			if (obs_data_get_frames_per_second(settings, name, &fps, nullptr)) {
				prop2->current_numerator = fps.numerator;
				prop2->current_denominator = fps.denominator;
			}

			prop = prop2;
			break;
		}
		case OBS_PROPERTY_GROUP: {
			auto grp = obs_property_group_content(p);
			ProcessProperties(grp, settings, rval);
			prop = nullptr;
			break;
		}
		}

		if (!prop) {
			continue;
		}

		prop->name = obs_property_name(p);
		prop->description = obs_property_description(p) ? obs_property_description(p) : "";
		prop->long_description = obs_property_long_description(p) ? obs_property_long_description(p) : "";
		prop->enabled = obs_property_enabled(p);
		prop->visible = obs_property_visible(p);

		std::vector<char> buf(prop->size());
		if (prop->serialize(buf)) {
			rval.push_back(ipc::value(buf));
		}
	}
}

const char *utility::GetSafeString(const char *str)
{
	return str ? str : "";
}
