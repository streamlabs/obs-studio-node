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

#include <napi.h>

namespace settings {
struct Parameter {
	std::string name;
	std::string description;
	std::string type;
	std::string subType;
	bool enabled = false;
	bool masked = false;
	bool visible = false;
	double minVal = 0;
	double maxVal = 0;
	double stepVal = 0;
	uint64_t sizeOfCurrentValue = 0;
	std::vector<char> currentValue;
	uint64_t sizeOfValues = 0;
	uint64_t countValues = 0;
	std::vector<char> values;

	std::vector<char> serialize()
	{
		std::vector<char> buffer;
		uint32_t indexBuffer = 0;

		size_t sizeStruct = name.length() + description.length() + type.length() + subType.length() + sizeof(uint64_t) * 7 + sizeof(bool) * 3 +
				    sizeof(double) * 3 + sizeOfCurrentValue + sizeOfValues;
		buffer.resize(sizeStruct);

		*reinterpret_cast<uint64_t *>(buffer.data() + indexBuffer) = name.length();
		indexBuffer += sizeof(uint64_t);
		memcpy(buffer.data() + indexBuffer, name.data(), name.length());
		indexBuffer += uint32_t(name.length());

		*reinterpret_cast<uint64_t *>(buffer.data() + indexBuffer) = description.length();
		indexBuffer += sizeof(uint64_t);
		memcpy(buffer.data() + indexBuffer, description.data(), description.length());
		indexBuffer += uint32_t(description.length());

		*reinterpret_cast<uint64_t *>(buffer.data() + indexBuffer) = type.length();
		indexBuffer += sizeof(uint64_t);
		memcpy(buffer.data() + indexBuffer, type.data(), type.length());
		indexBuffer += uint32_t(type.length());

		*reinterpret_cast<uint64_t *>(buffer.data() + indexBuffer) = subType.length();
		indexBuffer += sizeof(uint64_t);
		memcpy(buffer.data() + indexBuffer, subType.data(), subType.length());
		indexBuffer += uint32_t(subType.length());

		*reinterpret_cast<bool *>(buffer.data() + indexBuffer) = enabled;
		indexBuffer += sizeof(bool);
		*reinterpret_cast<bool *>(buffer.data() + indexBuffer) = masked;
		indexBuffer += sizeof(bool);
		*reinterpret_cast<bool *>(buffer.data() + indexBuffer) = visible;
		indexBuffer += sizeof(bool);

		*reinterpret_cast<double *>(buffer.data() + indexBuffer) = minVal;
		indexBuffer += sizeof(double);
		*reinterpret_cast<double *>(buffer.data() + indexBuffer) = maxVal;
		indexBuffer += sizeof(double);
		*reinterpret_cast<double *>(buffer.data() + indexBuffer) = stepVal;
		indexBuffer += sizeof(double);

		*reinterpret_cast<uint64_t *>(buffer.data() + indexBuffer) = sizeOfCurrentValue;
		indexBuffer += sizeof(uint64_t);

		memcpy(buffer.data() + indexBuffer, currentValue.data(), sizeOfCurrentValue);
		indexBuffer += uint32_t(sizeOfCurrentValue);

		*reinterpret_cast<uint64_t *>(buffer.data() + indexBuffer) = sizeOfValues;
		indexBuffer += sizeof(uint64_t);

		*reinterpret_cast<uint64_t *>(buffer.data() + indexBuffer) = countValues;
		indexBuffer += sizeof(uint64_t);

		memcpy(buffer.data() + indexBuffer, values.data(), sizeOfValues);
		indexBuffer += uint32_t(sizeOfValues);

		return buffer;
	}
};

struct SubCategory {
	std::string name;
	uint32_t paramsCount = 0;
	std::vector<Parameter> params;

	std::vector<char> serialize()
	{
		std::vector<char> buffer;
		uint64_t indexBuffer = 0;

		size_t sizeStruct = name.length() + sizeof(uint64_t) + sizeof(uint32_t);
		buffer.resize(sizeStruct);

		*reinterpret_cast<uint64_t *>(buffer.data()) = name.length();
		indexBuffer += sizeof(uint64_t);
		memcpy(buffer.data() + indexBuffer, name.data(), name.length());
		indexBuffer += name.length();

		*reinterpret_cast<uint32_t *>(buffer.data() + indexBuffer) = paramsCount;
		indexBuffer += sizeof(uint32_t);

		for (int i = 0; i < params.size(); i++) {
			std::vector<char> serializedBuf = params.at(i).serialize();

			buffer.insert(buffer.end(), serializedBuf.begin(), serializedBuf.end());
		}

		return buffer;
	}
};

void Init(Napi::Env env, Napi::Object exports);

Napi::Value OBS_settings_getSettings(const Napi::CallbackInfo &info);
void OBS_settings_saveSettings(const Napi::CallbackInfo &info);
Napi::Value OBS_settings_getListCategories(const Napi::CallbackInfo &info);

Napi::Value OBS_settings_getInputAudioDevices(const Napi::CallbackInfo &info);
Napi::Value OBS_settings_getOutputAudioDevices(const Napi::CallbackInfo &info);
Napi::Value OBS_settings_getVideoDevices(const Napi::CallbackInfo &info);

Napi::Value OBS_settings_isEnhancedBroadcasting(const Napi::CallbackInfo &info);
void OBS_settings_setEnhancedBroadcasting(const Napi::CallbackInfo &info);

static std::vector<std::string> getListCategories(void);
}
