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

#include "utility-v8.hpp"
#include "properties.hpp"

struct SceneInfo {
	uint64_t id = 0;
	std::vector<std::pair<int64_t, uint64_t>> items;
	bool itemsOrderCached = false;
	std::string name;
};

struct SourceDataInfo {
	std::string obs_sourceId = "";
	std::string name = "";
	uint64_t id = UINT64_MAX;

	bool isMuted = false;
	bool mutedChanged = true;

	std::string setting = "";
	bool settingsChanged = true;

	osn::property_map_t properties;
	bool propertiesChanged = true;

	uint32_t audioMixers = UINT32_MAX;
	bool audioMixersChanged = true;

	std::vector<uint64_t> *filters = new std::vector<uint64_t>();
	bool filtersOrderChanged = true;

	uint32_t deinterlaceMode = 0;
	bool deinterlaceModeChanged = true;

	uint32_t deinterlaceFieldOrder = 0;
	bool deinterlaceFieldOrderChanged = true;
};

struct SceneItemData {
	int64_t obs_itemId = -1;
	uint64_t scene_id = UINT64_MAX;

	bool cached = false;
	bool isSelected = false;
	bool selectedChanged = false;

	float posX = 0;
	float posY = 0;
	bool posChanged = true;

	float scaleX = 1;
	float scaleY = 1;
	bool scaleChanged = true;

	bool isVisible = true;
	bool visibleChanged = true;

	int32_t cropLeft = 0;
	int32_t cropTop = 0;
	int32_t cropRight = 0;
	int32_t cropBottom = 0;
	bool cropChanged = true;

	float rotation = 0;
	bool rotationChanged = true;

	bool isStreamVisible = true;
	bool streamVisibleChanged = true;

	bool isRecordingVisible = true;
	bool recordingVisibleChanged = true;

	uint32_t scaleFilter = 0;
	bool scaleFilterChanged = true;

	uint32_t blendingMode = 0;
	bool blendingModeChanged = true;

	uint32_t blendingMethod = 0;
	bool blendingMethodChanged = true;
};

template<class T> class CacheManager {
public:
	static CacheManager &getInstance()
	{
		static CacheManager instance;
		return instance;
	}

private:
	CacheManager(){};

public:
	CacheManager(CacheManager const &) = delete;
	void operator=(CacheManager const &) = delete;

private:
	std::map<std::string, SceneInfo *> scenesByName;
	std::map<uint64_t, SceneInfo *> scenesById;
	std::map<std::string, SourceDataInfo *> sourcesByName;
	std::map<uint64_t, SourceDataInfo *> sourcesById;
	std::map<uint64_t, SceneItemData *> itemsData;

public:
	void Store(uint64_t id, std::string name, SceneInfo *si)
	{
		si->name = name;
		scenesByName.erase(name);
		scenesById.erase(id);
		scenesByName.emplace(name, si);
		scenesById.emplace(id, si);
	}
	void Store(uint64_t id, std::string name, SourceDataInfo *sdi)
	{
		sdi->name = name;
		sourcesByName.erase(name);
		sourcesById.erase(id);
		sourcesByName.emplace(name, sdi);
		sourcesById.emplace(id, sdi);
	}
	void Store(uint64_t id, SceneItemData *sid)
	{
		itemsData.erase(id);
		itemsData.emplace(id, sid);
	}
	T Retrieve(uint64_t id)
	{
		if (id != UINT64_MAX) {
			if (typeid(T) == typeid(SceneInfo *)) {
				auto it = scenesById.find(id);
				if (it != scenesById.end()) {
					return (T)it->second;
				}
			} else if (typeid(T) == typeid(SourceDataInfo *)) {
				auto it = sourcesById.find(id);
				if (it != sourcesById.end()) {
					return (T)it->second;
				}
			} else if (typeid(T) == typeid(SceneItemData *)) {
				auto it = itemsData.find(id);
				if (it != itemsData.end()) {
					return (T)it->second;
				}
			}
		}
		return nullptr;
	}
	T Retrieve(const std::string &name)
	{
		if (name.size() > 0) {
			if (typeid(T) == typeid(SceneInfo *)) {
				auto it = scenesByName.find(name);
				if (it != scenesByName.end()) {
					return (T)it->second;
				}
			} else if (typeid(T) == typeid(SourceDataInfo *)) {
				auto it = sourcesByName.find(name);
				if (it != sourcesByName.end()) {
					return (T)it->second;
				}
			}
		}
		return nullptr;
	}
	void Remove(uint64_t id)
	{
		if (id != UINT64_MAX) {
			if (typeid(T) == typeid(SceneInfo *)) {
				auto it = scenesById.find(id);
				if (it != scenesById.end()) {
					scenesByName.erase(it->second->name);
					scenesById.erase(id);
				}
			} else if (typeid(T) == typeid(SourceDataInfo *)) {
				auto it = sourcesById.find(id);
				if (it != sourcesById.end()) {
					sourcesByName.erase(it->second->name);
					sourcesById.erase(id);
				}
			} else if (typeid(T) == typeid(SceneItemData *)) {
				auto it = itemsData.find(id);
				if (it != itemsData.end()) {
					itemsData.erase(id);
				}
			}
		}
	}
};