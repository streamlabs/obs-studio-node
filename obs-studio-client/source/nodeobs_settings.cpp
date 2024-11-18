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

#include "nodeobs_settings.hpp"
#include "controller.hpp"
#include "osn-error.hpp"
#include "utility-v8.hpp"

#include <node.h>
#include <sstream>
#include <string>
#include "shared.hpp"
#include "utility.hpp"

std::vector<settings::SubCategory> serializeCategory(uint32_t subCategoriesCount, uint32_t sizeStruct, std::vector<char> buffer)
{
	std::vector<settings::SubCategory> category;

	uint32_t indexData = 0;
	for (int i = 0; i < int(subCategoriesCount); i++) {
		settings::SubCategory sc;

		uint64_t *sizeMessage = reinterpret_cast<uint64_t *>(buffer.data() + indexData);
		indexData += sizeof(uint64_t);

		std::string name(buffer.data() + indexData, *sizeMessage);
		indexData += uint32_t(*sizeMessage);

		uint32_t *paramsCount = reinterpret_cast<uint32_t *>(buffer.data() + indexData);
		indexData += sizeof(uint32_t);

		settings::Parameter param;
		for (int j = 0; j < *paramsCount; j++) {
			uint64_t *sizeName = reinterpret_cast<uint64_t *>(buffer.data() + indexData);
			indexData += sizeof(uint64_t);

			std::string name(buffer.data() + indexData, *sizeName);
			indexData += uint32_t(*sizeName);

			uint64_t *sizeDescription = reinterpret_cast<uint64_t *>(buffer.data() + indexData);
			indexData += sizeof(uint64_t);

			std::string description(buffer.data() + indexData, *sizeDescription);
			indexData += uint32_t(*sizeDescription);

			uint64_t *sizeType = reinterpret_cast<uint64_t *>(buffer.data() + indexData);
			indexData += sizeof(uint64_t);

			std::string type(buffer.data() + indexData, *sizeType);
			indexData += uint32_t(*sizeType);

			uint64_t *sizeSubType = reinterpret_cast<uint64_t *>(buffer.data() + indexData);
			indexData += sizeof(uint64_t);

			std::string subType(buffer.data() + indexData, *sizeSubType);
			indexData += uint32_t(*sizeSubType);

			bool *enabled = reinterpret_cast<bool *>(buffer.data() + indexData);
			indexData += sizeof(bool);

			bool *masked = reinterpret_cast<bool *>(buffer.data() + indexData);
			indexData += sizeof(bool);

			bool *visible = reinterpret_cast<bool *>(buffer.data() + indexData);
			indexData += sizeof(bool);

			double *minVal = reinterpret_cast<double *>(buffer.data() + indexData);
			indexData += sizeof(double);

			double *maxVal = reinterpret_cast<double *>(buffer.data() + indexData);
			indexData += sizeof(double);

			double *stepVal = reinterpret_cast<double *>(buffer.data() + indexData);
			indexData += sizeof(double);

			uint64_t *sizeOfCurrentValue = reinterpret_cast<uint64_t *>(buffer.data() + indexData);
			indexData += sizeof(uint64_t);

			std::vector<char> currentValue;
			currentValue.resize(*sizeOfCurrentValue);
			memcpy(currentValue.data(), buffer.data() + indexData, *sizeOfCurrentValue);
			indexData += uint32_t(*sizeOfCurrentValue);

			uint64_t *sizeOfValues = reinterpret_cast<uint64_t *>(buffer.data() + indexData);
			indexData += sizeof(uint64_t);

			uint64_t *countValues = reinterpret_cast<uint64_t *>(buffer.data() + indexData);
			indexData += sizeof(uint64_t);

			std::vector<char> values;
			values.resize(*sizeOfValues);
			memcpy(values.data(), buffer.data() + indexData, *sizeOfValues);
			indexData += uint32_t(*sizeOfValues);

			param.name = name;
			param.description = description;
			param.type = type;
			param.subType = subType;
			param.enabled = *enabled;
			param.masked = *masked;
			param.visible = *visible;
			param.minVal = *minVal;
			param.maxVal = *maxVal;
			param.stepVal = *stepVal;
			param.currentValue = currentValue;
			param.values = values;
			param.countValues = *countValues;

			sc.params.push_back(param);
		}
		sc.name = name;
		sc.paramsCount = uint32_t(*paramsCount);
		category.push_back(sc);
	}
	return category;
}

Napi::Value settings::OBS_settings_getSettings(const Napi::CallbackInfo &info)
{
	std::string category = info[0].ToString().Utf8Value();
	std::vector<std::string> listSettings = getListCategories();
	std::vector<std::string>::iterator it = std::find(listSettings.begin(), listSettings.end(), category);

	if (it == listSettings.end())
		return Napi::Array::New(info.Env());

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Settings", "OBS_settings_getSettings", {ipc::value(category)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	Napi::Array array = Napi::Array::New(info.Env());
	Napi::Object settings = Napi::Object::New(info.Env());

	std::vector<settings::SubCategory> categorySettings =
		serializeCategory(uint32_t(response[1].value_union.ui64), uint32_t(response[2].value_union.ui64), response[3].value_bin);

	for (int i = 0; i < categorySettings.size(); i++) {
		Napi::Object subCategory = Napi::Object::New(info.Env());
		Napi::Array subCategoryParameters = Napi::Array::New(info.Env());
		std::vector<settings::Parameter> params = categorySettings.at(i).params;

		for (int j = 0; j < params.size(); j++) {
			Napi::Object parameter = Napi::Object::New(info.Env());

			parameter.Set("name", Napi::String::New(info.Env(), params.at(j).name));
			parameter.Set("type", Napi::String::New(info.Env(), params.at(j).type));
			parameter.Set("description", Napi::String::New(info.Env(), params.at(j).description));
			parameter.Set("subType", Napi::String::New(info.Env(), params.at(j).subType));

			if (params.at(j).currentValue.size() > 0) {
				if (params.at(j).type.compare("OBS_PROPERTY_EDIT_TEXT") == 0 || params.at(j).type.compare("OBS_PROPERTY_PATH") == 0 ||
				    params.at(j).type.compare("OBS_PROPERTY_TEXT") == 0 || params.at(j).type.compare("OBS_INPUT_RESOLUTION_LIST") == 0) {

					std::string value(params.at(j).currentValue.begin(), params.at(j).currentValue.end());
					parameter.Set("currentValue", Napi::String::New(info.Env(), value));
				} else if (params.at(j).type.compare("OBS_PROPERTY_INT") == 0) {
					int64_t value = *reinterpret_cast<int64_t *>(params.at(j).currentValue.data());
					parameter.Set("currentValue", Napi::Number::New(info.Env(), value));
					parameter.Set("minVal", Napi::Number::New(info.Env(), params.at(j).minVal));
					parameter.Set("maxVal", Napi::Number::New(info.Env(), params.at(j).maxVal));
					parameter.Set("stepVal", Napi::Number::New(info.Env(), params.at(j).stepVal));
				} else if (params.at(j).type.compare("OBS_PROPERTY_UINT") == 0 || params.at(j).type.compare("OBS_PROPERTY_BITMASK") == 0) {
					uint64_t value = *reinterpret_cast<uint64_t *>(params.at(j).currentValue.data());
					parameter.Set("currentValue", Napi::Number::New(info.Env(), value));
					parameter.Set("minVal", Napi::Number::New(info.Env(), params.at(j).minVal));
					parameter.Set("maxVal", Napi::Number::New(info.Env(), params.at(j).maxVal));
					parameter.Set("stepVal", Napi::Number::New(info.Env(), params.at(j).stepVal));
				} else if (params.at(j).type.compare("OBS_PROPERTY_BOOL") == 0) {
					bool value = *reinterpret_cast<bool *>(params.at(j).currentValue.data());
					parameter.Set("currentValue", Napi::Boolean::New(info.Env(), value));
				} else if (params.at(j).type.compare("OBS_PROPERTY_DOUBLE") == 0) {
					double value = *reinterpret_cast<double *>(params.at(j).currentValue.data());
					parameter.Set("currentValue", Napi::Number::New(info.Env(), value));
					parameter.Set("minVal", Napi::Number::New(info.Env(), params.at(j).minVal));
					parameter.Set("maxVal", Napi::Number::New(info.Env(), params.at(j).maxVal));
					parameter.Set("stepVal", Napi::Number::New(info.Env(), params.at(j).stepVal));
				} else if (params.at(j).type.compare("OBS_PROPERTY_LIST") == 0) {
					if (params.at(j).subType.compare("OBS_COMBO_FORMAT_INT") == 0) {
						int64_t value = *reinterpret_cast<int64_t *>(params.at(j).currentValue.data());
						parameter.Set("currentValue", Napi::Number::New(info.Env(), value));
						parameter.Set("minVal", Napi::Number::New(info.Env(), params.at(j).minVal));
						parameter.Set("maxVal", Napi::Number::New(info.Env(), params.at(j).maxVal));
						parameter.Set("stepVal", Napi::Number::New(info.Env(), params.at(j).stepVal));
					} else if (params.at(j).subType.compare("OBS_COMBO_FORMAT_FLOAT") == 0) {
						double value = *reinterpret_cast<double *>(params.at(j).currentValue.data());
						parameter.Set("currentValue", Napi::Number::New(info.Env(), value));
						parameter.Set("minVal", Napi::Number::New(info.Env(), params.at(j).minVal));
						parameter.Set("maxVal", Napi::Number::New(info.Env(), params.at(j).maxVal));
						parameter.Set("stepVal", Napi::Number::New(info.Env(), params.at(j).stepVal));
					} else if (params.at(j).subType.compare("OBS_COMBO_FORMAT_STRING") == 0) {
						std::string value(params.at(j).currentValue.begin(), params.at(j).currentValue.end());
						parameter.Set("currentValue", Napi::String::New(info.Env(), value));
					}
				}
			} else {
				parameter.Set("currentValue", Napi::String::New(info.Env(), ""));
			}

			// Values
			Napi::Array values = Napi::Array::New(info.Env());
			uint32_t indexData = 0;

			for (int k = 0; k < params.at(j).countValues; k++) {
				Napi::Object valueObject = Napi::Object::New(info.Env());

				if (params.at(j).subType.compare("OBS_COMBO_FORMAT_INT") == 0) {
					uint64_t *sizeName = reinterpret_cast<uint64_t *>(params.at(j).values.data() + indexData);
					indexData += sizeof(uint64_t);
					std::string name(params.at(j).values.data() + indexData, *sizeName);
					indexData += uint32_t(*sizeName);

					int64_t value = *reinterpret_cast<int64_t *>(params.at(j).values.data() + indexData);

					indexData += sizeof(int64_t);

					valueObject.Set(name, Napi::Number::New(info.Env(), value));
				} else if (params.at(j).subType.compare("OBS_COMBO_FORMAT_FLOAT") == 0) {
					uint64_t *sizeName = reinterpret_cast<uint64_t *>(params.at(j).values.data() + indexData);
					indexData += sizeof(uint64_t);
					std::string name(params.at(j).values.data() + indexData, *sizeName);
					indexData += uint32_t(*sizeName);

					double value = *reinterpret_cast<double *>(params.at(j).values.data() + indexData);

					indexData += sizeof(double);

					valueObject.Set(name, Napi::Number::New(info.Env(), value));
				} else {
					uint64_t *sizeName = reinterpret_cast<uint64_t *>(params.at(j).values.data() + indexData);
					indexData += sizeof(uint64_t);
					std::string name(params.at(j).values.data() + indexData, *sizeName);
					indexData += uint32_t(*sizeName);

					uint64_t *sizeValue = reinterpret_cast<uint64_t *>(params.at(j).values.data() + indexData);
					indexData += sizeof(uint64_t);
					std::string value(params.at(j).values.data() + indexData, *sizeValue);
					indexData += uint32_t(*sizeValue);

					valueObject.Set(name, Napi::String::New(info.Env(), value));
				}
				values.Set(k, valueObject);
			}
			if (params.at(j).countValues > 0 && params.at(j).currentValue.size() == 0 && params.at(j).type.compare("OBS_PROPERTY_LIST") == 0 &&
			    params.at(j).enabled) {
				uint32_t indexData = 0;
				uint64_t *sizeName = reinterpret_cast<uint64_t *>(params.at(j).values.data() + indexData);
				indexData += sizeof(uint64_t);
				std::string name(params.at(j).values.data() + indexData, *sizeName);
				indexData += uint32_t(*sizeName);

				uint64_t *sizeValue = reinterpret_cast<uint64_t *>(params.at(j).values.data() + indexData);
				indexData += sizeof(uint64_t);
				std::string value(params.at(j).values.data() + indexData, *sizeValue);
				indexData += uint32_t(*sizeValue);

				parameter.Set("currentValue", Napi::String::New(info.Env(), value));
			}
			parameter.Set("values", values);
			parameter.Set("visible", Napi::Boolean::New(info.Env(), params.at(j).visible));
			parameter.Set("enabled", Napi::Boolean::New(info.Env(), params.at(j).enabled));
			parameter.Set("masked", Napi::Boolean::New(info.Env(), params.at(j).masked));
			subCategoryParameters.Set(j, parameter);
		}
		subCategory.Set("nameSubCategory", Napi::String::New(info.Env(), categorySettings.at(i).name));
		subCategory.Set("parameters", subCategoryParameters);
		array.Set(i, subCategory);
		settings.Set("data", array);
		settings.Set("type", Napi::Number::New(info.Env(), response[4].value_union.ui32));
	}
	return settings;
}

std::vector<char> deserializeCategory(uint32_t *subCategoriesCount, uint32_t *sizeStruct, Napi::Array settings)
{
	std::vector<char> buffer;
	std::vector<settings::SubCategory> sucCategories;

	for (int i = 0; i < int(settings.Length()); i++) {
		settings::SubCategory sc;

		Napi::Object subCategoryObject = settings.Get(i).ToObject();

		sc.name = subCategoryObject.Get("nameSubCategory").ToString().Utf8Value();
		Napi::Array parameters = subCategoryObject.Get("parameters").As<Napi::Array>();

		sc.paramsCount = parameters.Length();
		for (int j = 0; j < sc.paramsCount; j++) {
			settings::Parameter param;
			Napi::Object parameterObject = parameters.Get(j).ToObject();

			param.name = parameterObject.Get("name").ToString().Utf8Value();
			param.type = parameterObject.Get("type").ToString().Utf8Value();
			param.subType = parameterObject.Get("subType").ToString().Utf8Value();

			if (param.type.compare("OBS_PROPERTY_EDIT_TEXT") == 0 || param.type.compare("OBS_PROPERTY_PATH") == 0 ||
			    param.type.compare("OBS_PROPERTY_TEXT") == 0 || param.type.compare("OBS_INPUT_RESOLUTION_LIST") == 0) {
				std::string value = parameterObject.Get("currentValue").ToString().Utf8Value();

				param.sizeOfCurrentValue = value.length();
				param.currentValue.resize(param.sizeOfCurrentValue);
				memcpy(param.currentValue.data(), value.c_str(), param.sizeOfCurrentValue);
			} else if (param.type.compare("OBS_PROPERTY_INT") == 0) {
				int64_t value = parameterObject.Get("currentValue").ToNumber().Int64Value();

				param.sizeOfCurrentValue = sizeof(value);
				param.currentValue.resize(sizeof(value));
				memcpy(param.currentValue.data(), &value, sizeof(value));
			} else if (param.type.compare("OBS_PROPERTY_UINT") == 0 || param.type.compare("OBS_PROPERTY_BITMASK") == 0) {
				uint64_t value = uint64_t(parameterObject.Get("currentValue").ToNumber().Uint32Value());

				param.sizeOfCurrentValue = sizeof(value);
				param.currentValue.resize(sizeof(value));
				memcpy(param.currentValue.data(), &value, sizeof(value));
			} else if (param.type.compare("OBS_PROPERTY_BOOL") == 0) {
				bool value = parameterObject.Get("currentValue").ToBoolean().Value();

				param.sizeOfCurrentValue = sizeof(value);
				param.currentValue.resize(sizeof(value));
				memcpy(param.currentValue.data(), &value, sizeof(value));
			} else if (param.type.compare("OBS_PROPERTY_DOUBLE") == 0) {
				double value = parameterObject.Get("currentValue").ToNumber().DoubleValue();

				param.sizeOfCurrentValue = sizeof(value);
				param.currentValue.resize(sizeof(value));
				memcpy(param.currentValue.data(), &value, sizeof(value));
			} else if (param.type.compare("OBS_PROPERTY_LIST") == 0) {
				std::string subType = parameterObject.Get("subType").ToString().Utf8Value();

				if (subType.compare("OBS_COMBO_FORMAT_INT") == 0) {
					int64_t value = parameterObject.Get("currentValue").ToNumber().Int64Value();

					param.sizeOfCurrentValue = sizeof(value);
					param.currentValue.resize(sizeof(value));
					memcpy(param.currentValue.data(), &value, sizeof(value));
				} else if (subType.compare("OBS_COMBO_FORMAT_FLOAT") == 0) {
					double value = parameterObject.Get("currentValue").ToNumber().DoubleValue();

					param.sizeOfCurrentValue = sizeof(value);
					param.currentValue.resize(sizeof(value));
					memcpy(param.currentValue.data(), &value, sizeof(value));
				} else if (subType.compare("OBS_COMBO_FORMAT_STRING") == 0) {
					std::string value = parameterObject.Get("currentValue").ToString().Utf8Value();

					param.sizeOfCurrentValue = value.length();
					param.currentValue.resize(param.sizeOfCurrentValue);
					memcpy(param.currentValue.data(), value.c_str(), param.sizeOfCurrentValue);
				}
			}
			sc.params.push_back(param);
		}
		sucCategories.push_back(sc);
	}

	for (int i = 0; i < sucCategories.size(); i++) {
		std::vector<char> serializedBuf = sucCategories.at(i).serialize();

		buffer.insert(buffer.end(), serializedBuf.begin(), serializedBuf.end());
	}

	*subCategoriesCount = uint32_t(sucCategories.size());
	*sizeStruct = uint32_t(buffer.size());

	return buffer;
}

void settings::OBS_settings_saveSettings(const Napi::CallbackInfo &info)
{
	std::string category = info[0].ToString().Utf8Value();
	Napi::Array settings = info[1].As<Napi::Array>();

	uint32_t subCategoriesCount, sizeStruct;

	std::vector<char> buffer = deserializeCategory(&subCategoriesCount, &sizeStruct, settings);

	auto conn = GetConnection(info);
	if (!conn)
		return;

	std::vector<ipc::value> response = conn->call_synchronous_helper(
		"Settings", "OBS_settings_saveSettings", {ipc::value(category), ipc::value(subCategoriesCount), ipc::value(sizeStruct), ipc::value(buffer)});

	if (!ValidateResponse(info, response))
		return;
}

std::vector<std::string> settings::getListCategories(void)
{
	std::vector<std::string> categories;

	categories.push_back("General");
	categories.push_back("Stream");
	categories.push_back("StreamSecond");
	categories.push_back("Output");
	categories.push_back("Audio");
	categories.push_back("Video");
	categories.push_back("Hotkeys");
	categories.push_back("Advanced");
	categories.push_back("Virtual Webcam");

	return categories;
}

Napi::Value settings::OBS_settings_getListCategories(const Napi::CallbackInfo &info)
{
	Napi::Array categories = Napi::Array::New(info.Env());
	std::vector<std::string> settings = getListCategories();

	size_t index = 0;
	for (auto &category : settings)
		categories.Set(index++, Napi::String::New(info.Env(), category));

	return categories;
}

Napi::Array devices_to_js(const Napi::CallbackInfo &info, const std::vector<ipc::value> &response)
{
	Napi::Array devices = Napi::Array::New(info.Env());

	uint32_t js_array_index = 0;
	uint64_t items = response[1].value_union.ui32;
	if (items > 0) {
		for (uint64_t idx = 2; idx < items * 2 + 2; idx += 2) {
			Napi::Object device = Napi::Object::New(info.Env());
			device.Set("description", Napi::String::New(info.Env(), response[idx].value_str.c_str()));
			device.Set("id", Napi::String::New(info.Env(), response[idx + 1].value_str.c_str()));
			devices.Set(js_array_index++, device);
		}
	}

	return devices;
}

Napi::Value settings::OBS_settings_getInputAudioDevices(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Settings", "OBS_settings_getInputAudioDevices", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return devices_to_js(info, response);
}

Napi::Value settings::OBS_settings_getOutputAudioDevices(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Settings", "OBS_settings_getOutputAudioDevices", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return devices_to_js(info, response);
}

Napi::Value settings::OBS_settings_getVideoDevices(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Settings", "OBS_settings_getVideoDevices", {});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	return devices_to_js(info, response);
}

void settings::Init(Napi::Env env, Napi::Object exports)
{
	exports.Set(Napi::String::New(env, "OBS_settings_getSettings"), Napi::Function::New(env, settings::OBS_settings_getSettings));
	exports.Set(Napi::String::New(env, "OBS_settings_saveSettings"), Napi::Function::New(env, settings::OBS_settings_saveSettings));
	exports.Set(Napi::String::New(env, "OBS_settings_getListCategories"), Napi::Function::New(env, settings::OBS_settings_getListCategories));
	exports.Set(Napi::String::New(env, "OBS_settings_getInputAudioDevices"), Napi::Function::New(env, settings::OBS_settings_getInputAudioDevices));
	exports.Set(Napi::String::New(env, "OBS_settings_getOutputAudioDevices"), Napi::Function::New(env, settings::OBS_settings_getOutputAudioDevices));
	exports.Set(Napi::String::New(env, "OBS_settings_getVideoDevices"), Napi::Function::New(env, settings::OBS_settings_getVideoDevices));
}