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

#include "osn-sceneitem.hpp"
#include <osn-error.hpp>
#include "osn-source.hpp"
#include "shared.hpp"
#include <osn-video.hpp>

void osn::SceneItem::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("SceneItem");
	cls->register_function(std::make_shared<ipc::function>("GetSource", std::vector<ipc::type>{ipc::type::UInt64}, GetSource));
	cls->register_function(std::make_shared<ipc::function>("GetScene", std::vector<ipc::type>{ipc::type::UInt64}, GetScene));
	cls->register_function(std::make_shared<ipc::function>("Remove", std::vector<ipc::type>{ipc::type::UInt64}, Remove));
	cls->register_function(std::make_shared<ipc::function>("IsVisible", std::vector<ipc::type>{ipc::type::UInt64}, IsVisible));
	cls->register_function(std::make_shared<ipc::function>("SetVisible", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int32}, SetVisible));
	cls->register_function(std::make_shared<ipc::function>("IsSelected", std::vector<ipc::type>{ipc::type::UInt64}, IsSelected));
	cls->register_function(std::make_shared<ipc::function>("SetSelected", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int32}, SetSelected));
	cls->register_function(std::make_shared<ipc::function>("IsStreamVisible", std::vector<ipc::type>{ipc::type::UInt64}, IsStreamVisible));
	cls->register_function(
		std::make_shared<ipc::function>("SetStreamVisible", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int32}, SetStreamVisible));
	cls->register_function(std::make_shared<ipc::function>("IsRecordingVisible", std::vector<ipc::type>{ipc::type::UInt64}, IsRecordingVisible));
	cls->register_function(
		std::make_shared<ipc::function>("SetRecordingVisible", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int32}, SetRecordingVisible));
	cls->register_function(std::make_shared<ipc::function>("GetPosition", std::vector<ipc::type>{ipc::type::UInt64}, GetPosition));
	cls->register_function(
		std::make_shared<ipc::function>("SetPosition", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Float, ipc::type::Float}, SetPosition));
	cls->register_function(std::make_shared<ipc::function>("GetCanvas", std::vector<ipc::type>{ipc::type::UInt64}, GetCanvas));
	cls->register_function(std::make_shared<ipc::function>("SetCanvas", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetCanvas));
	cls->register_function(std::make_shared<ipc::function>("GetRotation", std::vector<ipc::type>{ipc::type::UInt64}, GetRotation));
	cls->register_function(std::make_shared<ipc::function>("SetRotation", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Float}, SetRotation));
	cls->register_function(std::make_shared<ipc::function>("GetScale", std::vector<ipc::type>{ipc::type::UInt64}, GetScale));
	cls->register_function(
		std::make_shared<ipc::function>("SetScale", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Float, ipc::type::Float}, SetScale));
	cls->register_function(std::make_shared<ipc::function>("GetScaleFilter", std::vector<ipc::type>{ipc::type::UInt64}, GetScaleFilter));
	cls->register_function(std::make_shared<ipc::function>("SetScaleFilter", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int32}, SetScaleFilter));
	cls->register_function(std::make_shared<ipc::function>("GetAlignment", std::vector<ipc::type>{ipc::type::UInt64}, GetAlignment));
	cls->register_function(std::make_shared<ipc::function>("SetAlignment", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetAlignment));
	cls->register_function(std::make_shared<ipc::function>("GetBounds", std::vector<ipc::type>{ipc::type::UInt64}, GetBounds));
	cls->register_function(
		std::make_shared<ipc::function>("SetBounds", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Float, ipc::type::Float}, SetBounds));
	cls->register_function(std::make_shared<ipc::function>("GetBoundsAlignment", std::vector<ipc::type>{ipc::type::UInt64}, GetBoundsAlignment));
	cls->register_function(
		std::make_shared<ipc::function>("SetBoundsAlignment", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetBoundsAlignment));
	cls->register_function(std::make_shared<ipc::function>("GetBoundsType", std::vector<ipc::type>{ipc::type::UInt64}, GetBoundsType));
	cls->register_function(std::make_shared<ipc::function>("SetBoundsType", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int32}, SetBoundsType));
	cls->register_function(std::make_shared<ipc::function>("GetCrop", std::vector<ipc::type>{ipc::type::UInt64}, GetCrop));
	cls->register_function(std::make_shared<ipc::function>(
		"SetCrop", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int32, ipc::type::Int32, ipc::type::Int32, ipc::type::Int32}, SetCrop));
	cls->register_function(std::make_shared<ipc::function>("GetTransformInfo",
							       std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Float, ipc::type::Float, ipc::type::Float,
										      ipc::type::Float, ipc::type::Float, ipc::type::UInt32, ipc::type::UInt32,
										      ipc::type::UInt32, ipc::type::Float, ipc::type::Float},
							       GetTransformInfo));
	cls->register_function(std::make_shared<ipc::function>("SetTransformInfo",
							       std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Float, ipc::type::Float, ipc::type::Float,
										      ipc::type::Float, ipc::type::Float, ipc::type::UInt32, ipc::type::UInt32,
										      ipc::type::UInt32, ipc::type::Float, ipc::type::Float},
							       SetTransformInfo));
	cls->register_function(std::make_shared<ipc::function>("GetId", std::vector<ipc::type>{ipc::type::UInt64}, GetId));
	cls->register_function(std::make_shared<ipc::function>("MoveUp", std::vector<ipc::type>{ipc::type::UInt64}, MoveUp));
	cls->register_function(std::make_shared<ipc::function>("MoveDown", std::vector<ipc::type>{ipc::type::UInt64}, MoveDown));
	cls->register_function(std::make_shared<ipc::function>("MoveTop", std::vector<ipc::type>{ipc::type::UInt64}, MoveTop));
	cls->register_function(std::make_shared<ipc::function>("MoveBottom", std::vector<ipc::type>{ipc::type::UInt64}, MoveBottom));
	cls->register_function(std::make_shared<ipc::function>("Move", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::Int32}, Move));
	cls->register_function(std::make_shared<ipc::function>("DeferUpdateBegin", std::vector<ipc::type>{ipc::type::UInt64}, DeferUpdateBegin));
	cls->register_function(std::make_shared<ipc::function>("DeferUpdateEnd", std::vector<ipc::type>{ipc::type::UInt64}, DeferUpdateEnd));
	cls->register_function(std::make_shared<ipc::function>("GetBlendingMethod", std::vector<ipc::type>{ipc::type::UInt64}, GetBlendingMethod));
	cls->register_function(
		std::make_shared<ipc::function>("SetBlendingMethod", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetBlendingMethod));
	cls->register_function(std::make_shared<ipc::function>("GetBlendingMode", std::vector<ipc::type>{ipc::type::UInt64}, GetBlendingMode));
	cls->register_function(
		std::make_shared<ipc::function>("SetBlendingMode", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetBlendingMode));
	srv.register_collection(cls);
}

void osn::SceneItem::GetSource(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item does not contain a source.");
	}

	uint64_t uid = osn::Source::Manager::GetInstance().find(source);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint64_t)uid));
	AUTO_DEBUG;
}

void osn::SceneItem::GetScene(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_scene_t *scene = obs_sceneitem_get_scene(item);
	if (!scene) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Item does not belong to a scene.");
	}

	obs_source_t *source = obs_scene_get_source(scene);
	if (!source) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Scene is invalid.");
	}

	uint64_t uid = osn::Source::Manager::GetInstance().find(source);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint64_t)uid));
	AUTO_DEBUG;
}

void osn::SceneItem::Remove(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	osn::SceneItem::Manager::GetInstance().free(args[0].value_union.ui64);
	obs_sceneitem_remove(item);
	obs_sceneitem_release(item);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::SceneItem::IsVisible(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_sceneitem_visible(item)));
	AUTO_DEBUG;
}

void osn::SceneItem::SetVisible(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_sceneitem_set_visible(item, !!args[1].value_union.i32);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_sceneitem_visible(item)));
	AUTO_DEBUG;
}

void osn::SceneItem::IsSelected(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_sceneitem_selected(item)));
	AUTO_DEBUG;
}

void osn::SceneItem::SetSelected(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_sceneitem_select(item, !!args[1].value_union.i32);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_sceneitem_selected(item)));
	AUTO_DEBUG;
}

void osn::SceneItem::IsStreamVisible(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_sceneitem_stream_visible(item)));
	AUTO_DEBUG;
}

void osn::SceneItem::SetStreamVisible(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_sceneitem_set_stream_visible(item, !!args[1].value_union.i32);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_sceneitem_stream_visible(item)));
	AUTO_DEBUG;
}

void osn::SceneItem::IsRecordingVisible(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_sceneitem_recording_visible(item)));
	AUTO_DEBUG;
}

void osn::SceneItem::SetRecordingVisible(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_sceneitem_set_recording_visible(item, !!args[1].value_union.i32);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_sceneitem_recording_visible(item)));
	AUTO_DEBUG;
}

void osn::SceneItem::GetPosition(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	vec2 pos;
	obs_sceneitem_get_pos(item, &pos);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(pos.x));
	rval.push_back(ipc::value(pos.y));
	AUTO_DEBUG;
}

void osn::SceneItem::SetPosition(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	vec2 pos;
	pos.x = args[1].value_union.fp32;
	pos.y = args[2].value_union.fp32;

	obs_sceneitem_set_pos(item, &pos);
	obs_sceneitem_get_pos(item, &pos);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(pos.x));
	rval.push_back(ipc::value(pos.y));
	AUTO_DEBUG;
}

void osn::SceneItem::GetCanvas(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_video_info *canvas = obs_sceneitem_get_canvas(item);

	uint64_t uid = osn::Video::Manager::GetInstance().find(canvas);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint64_t)uid));
	AUTO_DEBUG;
}

void osn::SceneItem::SetCanvas(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_video_info *canvas = osn::Video::Manager::GetInstance().find(args[1].value_union.ui64);
	if (!canvas) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Canvas reference is not valid.");
	}

	obs_sceneitem_set_canvas(item, canvas);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::SceneItem::GetRotation(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_sceneitem_get_rot(item)));
	AUTO_DEBUG;
}

void osn::SceneItem::SetRotation(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_sceneitem_set_rot(item, args[1].value_union.fp32);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_sceneitem_get_rot(item)));
	AUTO_DEBUG;
}

void osn::SceneItem::GetScale(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	vec2 scale;
	obs_sceneitem_get_scale(item, &scale);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(scale.x));
	rval.push_back(ipc::value(scale.y));
	AUTO_DEBUG;
}

void osn::SceneItem::SetScale(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	vec2 scale;
	scale.x = args[1].value_union.fp32;
	scale.y = args[2].value_union.fp32;

	obs_sceneitem_set_scale(item, &scale);
	obs_sceneitem_get_scale(item, &scale);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(scale.x));
	rval.push_back(ipc::value(scale.y));
	AUTO_DEBUG;
}

void osn::SceneItem::GetScaleFilter(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_scale_type type = obs_sceneitem_get_scale_filter(item);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(type));
	AUTO_DEBUG;
}

void osn::SceneItem::SetScaleFilter(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_sceneitem_set_scale_filter(item, (obs_scale_type)args[1].value_union.i32);
	obs_scale_type type = obs_sceneitem_get_scale_filter(item);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(type));
	AUTO_DEBUG;
}

void osn::SceneItem::GetAlignment(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	uint32_t align = obs_sceneitem_get_alignment(item);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(align));
	AUTO_DEBUG;
}

void osn::SceneItem::SetAlignment(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_sceneitem_set_alignment(item, args[1].value_union.ui32);
	uint32_t align = obs_sceneitem_get_alignment(item);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(align));
	AUTO_DEBUG;
}

void osn::SceneItem::GetBounds(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	vec2 bounds;
	obs_sceneitem_get_bounds(item, &bounds);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(bounds.x));
	rval.push_back(ipc::value(bounds.y));
	AUTO_DEBUG;
}

void osn::SceneItem::SetBounds(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	vec2 bounds;
	bounds.x = args[1].value_union.fp32;
	bounds.y = args[2].value_union.fp32;

	obs_sceneitem_get_bounds(item, &bounds);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(bounds.x));
	rval.push_back(ipc::value(bounds.y));
	AUTO_DEBUG;
}

void osn::SceneItem::GetBoundsAlignment(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	uint32_t align = obs_sceneitem_get_bounds_alignment(item);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(align));
	AUTO_DEBUG;
}

void osn::SceneItem::SetBoundsAlignment(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_sceneitem_set_bounds_alignment(item, args[1].value_union.ui32);
	uint32_t align = obs_sceneitem_get_bounds_alignment(item);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(align));
	AUTO_DEBUG;
}

void osn::SceneItem::GetBoundsType(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_bounds_type bounds = obs_sceneitem_get_bounds_type(item);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(bounds));
	AUTO_DEBUG;
}

void osn::SceneItem::SetBoundsType(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_sceneitem_set_bounds_type(item, (obs_bounds_type)args[1].value_union.i32);
	obs_bounds_type bounds = obs_sceneitem_get_bounds_type(item);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(bounds));
	AUTO_DEBUG;
}

void osn::SceneItem::GetCrop(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(item, &crop);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(crop.left));
	rval.push_back(ipc::value(crop.top));
	rval.push_back(ipc::value(crop.right));
	rval.push_back(ipc::value(crop.bottom));
	AUTO_DEBUG;
}

void osn::SceneItem::SetCrop(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_sceneitem_crop crop;
	crop.left = args[1].value_union.i32;
	crop.top = args[2].value_union.i32;
	crop.right = args[3].value_union.i32;
	crop.bottom = args[4].value_union.i32;

	obs_sceneitem_set_crop(item, &crop);
	obs_sceneitem_get_crop(item, &crop);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(crop.left));
	rval.push_back(ipc::value(crop.top));
	rval.push_back(ipc::value(crop.right));
	rval.push_back(ipc::value(crop.bottom));
	AUTO_DEBUG;
}

void osn::SceneItem::GetTransformInfo(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_transform_info info;
	obs_sceneitem_get_info2(item, &info);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(info.pos.x));
	rval.push_back(ipc::value(info.pos.y));
	rval.push_back(ipc::value(info.rot));
	rval.push_back(ipc::value(info.scale.x));
	rval.push_back(ipc::value(info.scale.y));
	rval.push_back(ipc::value(info.alignment));
	rval.push_back(ipc::value(info.bounds_type));
	rval.push_back(ipc::value(info.bounds_alignment));
	rval.push_back(ipc::value(info.bounds.x));
	rval.push_back(ipc::value(info.bounds.y));

	AUTO_DEBUG;
}

void osn::SceneItem::SetTransformInfo(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_transform_info info;
	info.pos.x = args[1].value_union.fp32;
	info.pos.y = args[2].value_union.fp32;
	info.rot = args[3].value_union.fp32;
	info.scale.x = args[4].value_union.fp32;
	info.scale.y = args[5].value_union.fp32;
	info.alignment = args[6].value_union.ui32;
	info.bounds_type = static_cast<obs_bounds_type>(args[7].value_union.ui32);
	info.bounds_alignment = args[8].value_union.ui32;
	info.bounds.x = args[9].value_union.fp32;
	info.bounds.y = args[10].value_union.fp32;
	obs_sceneitem_set_info2(item, &info);

	AUTO_DEBUG;
}

void osn::SceneItem::GetId(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_sceneitem_get_id(item)));
	AUTO_DEBUG;
}

void osn::SceneItem::MoveUp(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_sceneitem_set_order(item, OBS_ORDER_MOVE_UP);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::SceneItem::MoveDown(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_sceneitem_set_order(item, OBS_ORDER_MOVE_DOWN);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::SceneItem::MoveTop(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::SceneItem::MoveBottom(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_sceneitem_set_order(item, OBS_ORDER_MOVE_BOTTOM);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::SceneItem::Move(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_sceneitem_set_order_position(item, args[1].value_union.i32);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::SceneItem::DeferUpdateBegin(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_sceneitem_defer_update_begin(item);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::SceneItem::DeferUpdateEnd(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_sceneitem_defer_update_end(item);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

osn::SceneItem::Manager &osn::SceneItem::Manager::GetInstance()
{
	// Thread Safe since C++13 (Visual Studio 2015, GCC 4.3).
	static Manager instance;
	return instance;
}

void osn::SceneItem::GetBlendingMethod(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_blending_method method = obs_sceneitem_get_blending_method(item);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)method));
	AUTO_DEBUG;
}

void osn::SceneItem::SetBlendingMethod(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_sceneitem_set_blending_method(item, (obs_blending_method)args[1].value_union.ui32);
	obs_blending_method method = obs_sceneitem_get_blending_method(item);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(method));
	AUTO_DEBUG;
}

void osn::SceneItem::GetBlendingMode(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_blending_type type = obs_sceneitem_get_blending_mode(item);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)type));
	AUTO_DEBUG;
}

void osn::SceneItem::SetBlendingMode(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_sceneitem_t *item = osn::SceneItem::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!item) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Item reference is not valid.");
	}

	obs_sceneitem_set_blending_mode(item, (obs_blending_type)args[1].value_union.ui32);
	obs_blending_type type = obs_sceneitem_get_blending_mode(item);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(type));
	AUTO_DEBUG;
}