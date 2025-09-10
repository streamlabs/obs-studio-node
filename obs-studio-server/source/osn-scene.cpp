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

#include "osn-scene.hpp"
#include <list>
#include "osn-error.hpp"
#include "osn-sceneitem.hpp"
#include "osn-video.hpp"
#include "shared.hpp"

void osn::Scene::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("Scene");
	cls->register_function(std::make_shared<ipc::function>("Create", std::vector<ipc::type>{ipc::type::String}, Create));
	cls->register_function(std::make_shared<ipc::function>("CreatePrivate", std::vector<ipc::type>{ipc::type::String}, CreatePrivate));
	cls->register_function(std::make_shared<ipc::function>("FromName", std::vector<ipc::type>{ipc::type::String}, FromName));

	cls->register_function(std::make_shared<ipc::function>("Release", std::vector<ipc::type>{ipc::type::UInt64}, Release));
	cls->register_function(std::make_shared<ipc::function>("Remove", std::vector<ipc::type>{ipc::type::UInt64}, Remove));

	cls->register_function(std::make_shared<ipc::function>("AsSource", std::vector<ipc::type>{ipc::type::UInt64}, AsSource));
	cls->register_function(
		std::make_shared<ipc::function>("Duplicate", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String, ipc::type::Int32}, Duplicate));

	cls->register_function(std::make_shared<ipc::function>("AsSource", std::vector<ipc::type>{ipc::type::UInt64}, AsSource));
	cls->register_function(
		std::make_shared<ipc::function>("AddSource", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64, ipc::type::UInt64}, AddSource));

	cls->register_function(std::make_shared<ipc::function>(
		"AddSourceWithTransform",
		std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64, ipc::type::Double, ipc::type::Double, ipc::type::Int32, ipc::type::Double,
				       ipc::type::Double, ipc::type::Double, ipc::type::Int64, ipc::type::Int64, ipc::type::Int64, ipc::type::Int64,
				       ipc::type::Int32, ipc::type::Int32, ipc::type::UInt32, ipc::type::UInt32, ipc::type::UInt32, ipc::type::UInt64},
		AddSource));

	cls->register_function(std::make_shared<ipc::function>("FindItemByName", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String}, FindItemByName));
	cls->register_function(std::make_shared<ipc::function>("FindItemById", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int64}, FindItemByItemId));
	cls->register_function(
		std::make_shared<ipc::function>("MoveItem", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int32, ipc::type::Int32}, MoveItem));
	cls->register_function(std::make_shared<ipc::function>("OrderItems", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Binary}, OrderItems));
	cls->register_function(std::make_shared<ipc::function>("GetItem", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int32}, GetItem));
	cls->register_function(std::make_shared<ipc::function>("GetItems", std::vector<ipc::type>{ipc::type::UInt64}, GetItems));
	cls->register_function(std::make_shared<ipc::function>("GetItemsInRange", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int32, ipc::type::Int32},
							       GetItemsInRange));

	cls->register_function(std::make_shared<ipc::function>("Connect", std::vector<ipc::type>{ipc::type::UInt64}, Connect));
	cls->register_function(std::make_shared<ipc::function>("Disconnect", std::vector<ipc::type>{ipc::type::UInt64}, Disconnect));
	srv.register_collection(cls);
}

void osn::Scene::Create(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_scene_t *scene = obs_scene_create(args[0].value_str.c_str());
	if (!scene) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to create scene.");
	}

	obs_source_t *source = obs_scene_get_source(scene);
	if (!source) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to get source from scene.");
	}

	uint64_t uid = osn::Source::Manager::GetInstance().find(source);
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	const char *sid = obs_source_get_unversioned_id(source);
	rval.push_back(ipc::value(sid ? sid : ""));
	AUTO_DEBUG;
}

void osn::Scene::CreatePrivate(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_scene_t *scene = obs_scene_create_private(args[0].value_str.c_str());
	if (!scene) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to create scene.");
	}

	obs_source_t *source = obs_scene_get_source(scene);
	if (!source) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to get source from scene.");
	}

	uint64_t uid = osn::Source::Manager::GetInstance().allocate(source);
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	const char *sid = obs_source_get_unversioned_id(source);
	rval.push_back(ipc::value(sid ? sid : ""));
	AUTO_DEBUG;
}

void osn::Scene::FromName(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *source = obs_get_source_by_name(args[0].value_str.c_str());
	if (!source) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to get source from scene.");
	}

	uint64_t uid = osn::Source::Manager::GetInstance().find(source);
	obs_source_release(source);

	if (uid == UINT64_MAX) {

		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Source found but not indexed.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::Scene::Release(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *source = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!source) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not a scene.");
	}

	std::list<obs_sceneitem_t *> items;
	auto cb = [](obs_scene_t *scene, obs_sceneitem_t *item, void *data) {
		std::list<obs_sceneitem_t *> *items = reinterpret_cast<std::list<obs_sceneitem_t *> *>(data);
		obs_sceneitem_addref(item);
		items->push_back(item);
		return true;
	};
	obs_scene_enum_items(scene, cb, &items);

	for (auto item : items) {
		obs_sceneitem_remove(item);
		obs_sceneitem_release(item);
	}

	obs_source_release(source);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Scene::Remove(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *source = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!source) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not a scene.");
	}

	std::list<obs_sceneitem_t *> items;
	auto cb = [](obs_scene_t *scene, obs_sceneitem_t *item, void *data) {
		std::list<obs_sceneitem_t *> *items = reinterpret_cast<std::list<obs_sceneitem_t *> *>(data);
		obs_sceneitem_addref(item);
		items->push_back(item);
		return true;
	};
	obs_scene_enum_items(scene, cb, &items);

	for (auto item : items) {
		osn::SceneItem::Manager::GetInstance().free(item);
		obs_sceneitem_remove(item);
		obs_sceneitem_release(item);
	}

	obs_source_remove(source);
	osn::Source::Manager::GetInstance().free(args[0].value_union.ui64);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::Scene::AsSource(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Scenes are stored as such.
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(args[0].value_union.ui64));
	AUTO_DEBUG;
}

void osn::Scene::Duplicate(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *source = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!source) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not a scene.");
	}

	obs_scene_t *scene2 = obs_scene_duplicate(scene, args[1].value_str.c_str(), (obs_scene_duplicate_type)args[2].value_union.i32);
	if (!scene2) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to duplicate scene.");
	}

	obs_source_t *source2 = obs_scene_get_source(scene2);
	if (!source2) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to get source from duplicate scene.");
	}

	uint64_t uid = osn::Source::Manager::GetInstance().find(source2);
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::Scene::AddSource(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *source = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!source) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not a scene.");
	}

	obs_source_t *added_source = osn::Source::Manager::GetInstance().find(args[1].value_union.ui64);
	if (!added_source) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference to add is not valid.");
	}

	obs_sceneitem_t *item = obs_scene_add(scene, added_source);

	utility::unique_id::id_t uid = osn::SceneItem::Manager::GetInstance().allocate(item);
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	if (args.size() > 2) {
		vec2 scale;
		scale.x = static_cast<float>(args[2].value_union.fp64);
		scale.y = static_cast<float>(args[3].value_union.fp64);
		obs_sceneitem_set_scale(item, &scale);

		obs_sceneitem_set_visible(item, !!args[4].value_union.i32);

		vec2 pos;
		pos.x = static_cast<float>(args[5].value_union.fp64);
		pos.y = static_cast<float>(args[6].value_union.fp64);
		obs_sceneitem_set_pos(item, &pos);

		obs_sceneitem_set_rot(item, static_cast<float>(args[7].value_union.fp64));

		obs_sceneitem_crop crop;
		crop.left = static_cast<int>(args[8].value_union.i64);
		crop.top = static_cast<int>(args[9].value_union.i64);
		crop.right = static_cast<int>(args[10].value_union.i64);
		crop.bottom = static_cast<int>(args[11].value_union.i64);

		obs_sceneitem_set_crop(item, &crop);

		obs_sceneitem_set_stream_visible(item, !!args[12].value_union.i32);
		obs_sceneitem_set_recording_visible(item, !!args[13].value_union.i32);

		obs_sceneitem_set_scale_filter(item, (enum obs_scale_type)args[14].value_union.ui32);
		obs_sceneitem_set_blending_mode(item, (enum obs_blending_type)args[15].value_union.ui32);
		obs_sceneitem_set_blending_method(item, (enum obs_blending_method)args[16].value_union.ui32);

		if (args.size() >= 18) {
			obs_video_info *canvas = osn::Video::Manager::GetInstance().find(args[17].value_union.ui64);
			if (canvas)
				obs_sceneitem_set_canvas(item, canvas);
		}
	}

	obs_sceneitem_addref(item);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint64_t)uid));
	rval.push_back(ipc::value(obs_sceneitem_get_id(item)));

	AUTO_DEBUG;
}

void osn::Scene::FindItemByName(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *source = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!source) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not a scene.");
	}

	obs_sceneitem_t *item = obs_scene_find_source(scene, args[1].value_str.c_str());
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Source not found.");
	}

	utility::unique_id::id_t uid = osn::SceneItem::Manager::GetInstance().find(item);
	if (uid == UINT64_MAX) {
		uid = osn::SceneItem::Manager::GetInstance().allocate(item);
		if (uid == UINT64_MAX) {
			PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
		}
		obs_sceneitem_addref(item);
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint64_t)uid));
	AUTO_DEBUG;
}

void osn::Scene::FindItemByItemId(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *source = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!source) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not a scene.");
	}

	obs_sceneitem_t *item = obs_scene_find_sceneitem_by_id(scene, args[1].value_union.i64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Source not found.");
	}

	utility::unique_id::id_t uid = osn::SceneItem::Manager::GetInstance().find(item);
	if (uid == UINT64_MAX) {
		uid = osn::SceneItem::Manager::GetInstance().allocate(item);
		if (uid == UINT64_MAX) {
			PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
		}
		obs_sceneitem_addref(item);
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint64_t)uid));
	AUTO_DEBUG;
}

void osn::Scene::OrderItems(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *source = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!source) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not a scene.");
	}

	const std::vector<char> &new_items_order = args[1].value_bin;
	size_t items_count = new_items_order.size() / sizeof(int64_t);

	obs_scene_set_items_order(scene, (int64_t *)new_items_order.data(), static_cast<int>(items_count));

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

	std::list<obs_sceneitem_t *> items;
	auto cb_items = [](obs_scene_t *scene, obs_sceneitem_t *item, void *data) {
		std::list<obs_sceneitem_t *> *items = reinterpret_cast<std::list<obs_sceneitem_t *> *>(data);
		items->push_back(item);
		return true;
	};
	obs_scene_enum_items(scene, cb_items, &items);

	for (obs_sceneitem_t *item : items) {
		utility::unique_id::id_t uid = osn::SceneItem::Manager::GetInstance().find(item);
		if (uid == UINT64_MAX) {
			uid = osn::SceneItem::Manager::GetInstance().allocate(item);
			if (uid == UINT64_MAX) {
				PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
			}
			obs_sceneitem_addref(item);
		}
		rval.push_back(ipc::value((uint64_t)uid));
		rval.push_back(ipc::value(obs_sceneitem_get_id(item)));
	}
	AUTO_DEBUG;
}

void osn::Scene::MoveItem(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *source = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!source) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not a scene.");
	}

	// Listen up! This function does not work as you might expect it to (lowest index is furthest
	//  back), instead it works on the inverted order, so the lowest index is the furthest in front.
	// While this may be weird at first, this does have some advantages as you do not have to guess
	//  on what the best index for moving something in front is. Downside is that it is kinda weird
	//  to deal with when you're used to things being the other way around.

	size_t num_items = 0;
	auto cb_count = [](obs_scene_t *scene, obs_sceneitem_t *item, void *data) {
		(*(reinterpret_cast<size_t *>(data)))++;
		return true;
	};
	obs_scene_enum_items(scene, cb_count, &num_items);

	struct EnumData {
		obs_sceneitem_t *item = nullptr;
		int32_t findindex = 0;
		int32_t index = 0;
	} ed;
	ed.findindex = (int32_t(num_items) - 1) - args[1].value_union.i32;

	auto cb = [](obs_scene_t *scene, obs_sceneitem_t *item, void *data) {
		EnumData *items = reinterpret_cast<EnumData *>(data);
		if (items->index == items->findindex) {
			items->item = item;
			return false;
		}
		items->index++;
		return true;
	};
	obs_scene_enum_items(scene, cb, &ed);

	if (!ed.item) {
		PRETTY_ERROR_RETURN(ErrorCode::OutOfBounds, "Index not found in Scene.");
	}

	obs_sceneitem_set_order_position(ed.item, (int(num_items) - 1) - args[2].value_union.i32);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

	std::list<obs_sceneitem_t *> items;
	auto cb_items = [](obs_scene_t *scene, obs_sceneitem_t *item, void *data) {
		std::list<obs_sceneitem_t *> *items = reinterpret_cast<std::list<obs_sceneitem_t *> *>(data);
		items->push_back(item);
		return true;
	};
	obs_scene_enum_items(scene, cb_items, &items);

	for (obs_sceneitem_t *item : items) {
		utility::unique_id::id_t uid = osn::SceneItem::Manager::GetInstance().find(item);
		if (uid == UINT64_MAX) {
			uid = osn::SceneItem::Manager::GetInstance().allocate(item);
			if (uid == UINT64_MAX) {
				PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
			}
			obs_sceneitem_addref(item);
		}
		rval.push_back(ipc::value((uint64_t)uid));
		rval.push_back(ipc::value(obs_sceneitem_get_id(item)));
	}
	AUTO_DEBUG;
}

void osn::Scene::GetItem(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *source = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!source) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not a scene.");
	}

	struct EnumData {
		obs_sceneitem_t *item = nullptr;
		size_t findindex = 0;
		size_t index = 0;
	} ed;
	ed.findindex = args[1].value_union.ui64;

	auto cb = [](obs_scene_t *scene, obs_sceneitem_t *item, void *data) {
		EnumData *items = reinterpret_cast<EnumData *>(data);
		if (items->index == items->findindex) {
			items->item = item;
			return false;
		}
		items->index++;
		return true;
	};
	obs_scene_enum_items(scene, cb, &ed);

	if (!ed.item) {
		PRETTY_ERROR_RETURN(ErrorCode::OutOfBounds, "Index not found in Scene.");
	}

	utility::unique_id::id_t uid = osn::SceneItem::Manager::GetInstance().find(ed.item);
	if (uid == UINT64_MAX) {
		uid = osn::SceneItem::Manager::GetInstance().allocate(ed.item);
		if (uid == UINT64_MAX) {
			PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
		}
		obs_sceneitem_addref(ed.item);
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint64_t)uid));
	AUTO_DEBUG;
}

void osn::Scene::GetItems(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *source = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!source) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not a scene.");
	}

	std::list<obs_sceneitem_t *> items;
	auto cb = [](obs_scene_t *scene, obs_sceneitem_t *item, void *data) {
		std::list<obs_sceneitem_t *> *items = reinterpret_cast<std::list<obs_sceneitem_t *> *>(data);
		items->push_back(item);
		return true;
	};
	obs_scene_enum_items(scene, cb, &items);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	for (obs_sceneitem_t *item : items) {
		utility::unique_id::id_t uid = osn::SceneItem::Manager::GetInstance().find(item);
		if (uid == UINT64_MAX) {
			uid = osn::SceneItem::Manager::GetInstance().allocate(item);
			if (uid == UINT64_MAX) {
				PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
			}
			obs_sceneitem_addref(item);
		}
		rval.push_back(ipc::value((uint64_t)uid));
		rval.push_back(ipc::value(obs_sceneitem_get_id(item)));
	}
	AUTO_DEBUG;
}

void osn::Scene::GetItemsInRange(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_source_t *source = osn::Source::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!source) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not valid.");
	}

	obs_scene_t *scene = obs_scene_from_source(source);
	if (!scene) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Source reference is not a scene.");
	}

	struct EnumData {
		std::list<obs_sceneitem_t *> items;
		size_t index_from = 0, index_to = 0;
		size_t index = 0;
	} ed;
	ed.index_from = args[1].value_union.ui64;
	ed.index_to = args[1].value_union.ui64;

	auto cb = [](obs_scene_t *scene, obs_sceneitem_t *item, void *data) {
		EnumData *ed = reinterpret_cast<EnumData *>(data);
		if ((ed->index >= ed->index_from) && (ed->index <= ed->index_to)) {
			ed->items.push_back(item);
		} else if (ed->index > ed->index_to) {
			return false;
		}
		ed->index++;
		return true;
	};
	obs_scene_enum_items(scene, cb, &ed);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	for (obs_sceneitem_t *item : ed.items) {
		utility::unique_id::id_t uid = osn::SceneItem::Manager::GetInstance().find(item);
		if (uid == UINT64_MAX) {
			uid = osn::SceneItem::Manager::GetInstance().allocate(item);
			if (uid == UINT64_MAX) {
				PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
			}
			obs_sceneitem_addref(item);
		}
		rval.push_back(ipc::value((uint64_t)uid));
	}
	AUTO_DEBUG;
}

void osn::Scene::Connect(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AUTO_DEBUG;
	// !FIXME! Signals
}

void osn::Scene::Disconnect(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	AUTO_DEBUG;
	// !FIXME! Signals
}
