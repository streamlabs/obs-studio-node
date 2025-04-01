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

#include "scene.hpp"
#include <condition_variable>
#include <mutex>
#include <string>
#include "controller.hpp"
#include "osn-error.hpp"
#include "input.hpp"
#include "video.hpp"
#include "ipc-value.hpp"
#include "sceneitem.hpp"
#include "shared.hpp"
#include "utility.hpp"

Napi::FunctionReference osn::Scene::constructor;

Napi::Object osn::Scene::Init(Napi::Env env, Napi::Object exports)
{
	Napi::HandleScope scope(env);
	Napi::Function func = DefineClass(env, "Scene",
					  {
						  StaticMethod("create", &osn::Scene::Create),
						  StaticMethod("createPrivate", &osn::Scene::CreatePrivate),
						  StaticMethod("fromName", &osn::Scene::FromName),

						  InstanceAccessor("source", &osn::Scene::AsSource, nullptr),

						  InstanceMethod("duplicate", &osn::Scene::Duplicate),
						  InstanceMethod("add", &osn::Scene::AddSource),
						  InstanceMethod("findItem", &osn::Scene::FindItem),
						  InstanceMethod("moveItem", &osn::Scene::MoveItem),
						  InstanceMethod("orderItems", &osn::Scene::OrderItems),
						  InstanceMethod("getItemAtIdx", &osn::Scene::GetItemAtIndex),
						  InstanceMethod("getItems", &osn::Scene::GetItems),
						  InstanceMethod("getItemsInRange", &osn::Scene::GetItemsInRange),

						  InstanceAccessor("configurable", &osn::Scene::CallIsConfigurable, nullptr),
						  InstanceAccessor("properties", &osn::Scene::CallGetProperties, nullptr),
						  InstanceAccessor("settings", &osn::Scene::CallGetSettings, nullptr),
						  InstanceAccessor("slowUncachedSettings", &osn::Scene::CallGetSlowUncachedSettings, nullptr),
						  InstanceAccessor("type", &osn::Scene::CallGetType, nullptr),
						  InstanceAccessor("name", &osn::Scene::CallGetName, &osn::Scene::CallSetName),
						  InstanceAccessor("outputFlags", &osn::Scene::CallGetOutputFlags, nullptr),
						  InstanceAccessor("flags", &osn::Scene::CallGetFlags, &osn::Scene::CallSetFlags),
						  InstanceAccessor("status", &osn::Scene::CallGetStatus, nullptr),
						  InstanceAccessor("id", &osn::Scene::CallGetId, nullptr),
						  InstanceAccessor("muted", &osn::Scene::CallGetMuted, &osn::Scene::CallSetMuted),
						  InstanceAccessor("enabled", &osn::Scene::CallGetEnabled, &osn::Scene::CallSetEnabled),

						  InstanceMethod("release", &osn::Scene::CallRelease),
						  InstanceMethod("remove", &osn::Scene::CallRemove),
						  InstanceMethod("update", &osn::Scene::CallUpdate),
						  InstanceMethod("load", &osn::Scene::CallLoad),
						  InstanceMethod("save", &osn::Scene::CallSave),
						  InstanceMethod("sendMouseClick", &osn::Scene::CallSendMouseClick),
						  InstanceMethod("sendMouseMove", &osn::Scene::CallSendMouseMove),
						  InstanceMethod("sendMouseWheel", &osn::Scene::CallSendMouseWheel),
						  InstanceMethod("sendFocus", &osn::Scene::CallSendFocus),
						  InstanceMethod("sendKeyClick", &osn::Scene::CallSendKeyClick),
						  InstanceMethod("callHandler", &osn::Scene::CallCallHandler),
					  });
	exports.Set("Scene", func);
	osn::Scene::constructor = Napi::Persistent(func);
	osn::Scene::constructor.SuppressDestruct();
	return exports;
}

osn::Scene::Scene(const Napi::CallbackInfo &info) : Napi::ObjectWrap<osn::Scene>(info)
{
	Napi::Env env = info.Env();
	Napi::HandleScope scope(env);
	size_t length = info.Length();
	this->sourceId = 0;

	if (length <= 0 || !info[0].IsNumber()) {
		Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
		return;
	}

	this->sourceId = (uint64_t)info[0].ToNumber().Int64Value();
}

Napi::Value osn::Scene::Create(const Napi::CallbackInfo &info)
{
	std::string name = info[0].ToString().Utf8Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Scene", "Create", std::vector<ipc::value>{ipc::value(name)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	uint64_t sourceId = response[1].value_union.ui64;

	SceneInfo *si = new SceneInfo();
	si->name = name;
	si->id = sourceId;
	SourceDataInfo *sdi = new SourceDataInfo;
	sdi->name = name;
	sdi->obs_sourceId = "scene";
	sdi->id = response[1].value_union.ui64;

	CacheManager<SourceDataInfo *>::getInstance().Store(sourceId, name, sdi);
	CacheManager<SceneInfo *>::getInstance().Store(sourceId, name, si);

	auto instance = osn::Scene::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

Napi::Value osn::Scene::CreatePrivate(const Napi::CallbackInfo &info)
{
	std::string name = info[0].ToString().Utf8Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Scene", "CreatePrivate", std::vector<ipc::value>{ipc::value(name)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	uint64_t sourceId = response[1].value_union.ui64;

	SceneInfo *si = new SceneInfo();
	si->name = name;
	si->id = sourceId;
	SourceDataInfo *sdi = new SourceDataInfo;
	sdi->name = name;
	sdi->obs_sourceId = "scene";
	sdi->id = response[1].value_union.ui64;

	CacheManager<SourceDataInfo *>::getInstance().Store(sourceId, name, sdi);
	CacheManager<SceneInfo *>::getInstance().Store(sourceId, name, si);

	auto instance = osn::Scene::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[1].value_union.ui64))});

	return instance;
}

Napi::Value osn::Scene::FromName(const Napi::CallbackInfo &info)
{
	std::string name = info[0].ToString().Utf8Value();
	SceneInfo *si = CacheManager<SceneInfo *>::getInstance().Retrieve(name);

	if (!si) {
		auto conn = GetConnection(info);
		if (!conn)
			return info.Env().Undefined();

		std::vector<ipc::value> response = conn->call_synchronous_helper("Scene", "FromName", {ipc::value(name)});

		if (!ValidateResponse(info, response))
			return info.Env().Undefined();

		si = new SceneInfo;
		si->id = response[1].value_union.ui64;
		si->name = name;
		CacheManager<SceneInfo *>::getInstance().Store(response[1].value_union.ui64, name, si);
	}
	auto instance = osn::Scene::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(si->id))});

	return instance;
}

Napi::Value osn::Scene::Release(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("Scene", "Release", std::vector<ipc::value>{ipc::value(this->sourceId)});
	return info.Env().Undefined();
}

Napi::Value osn::Scene::Remove(const Napi::CallbackInfo &info)
{
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	conn->call("Scene", "Remove", std::vector<ipc::value>{ipc::value(this->sourceId)});
	return info.Env().Undefined();
}

Napi::Value osn::Scene::AsSource(const Napi::CallbackInfo &info)
{
	auto instance = osn::Input::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(this->sourceId))});

	return instance;
}

Napi::Value osn::Scene::Duplicate(const Napi::CallbackInfo &info)
{
	std::string name = info[0].ToString().Utf8Value();
	int64_t duplicate_type = info[1].ToNumber().Int64Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper(
		"Scene", "Duplicate", std::vector<ipc::value>{ipc::value(this->sourceId), ipc::value(name), ipc::value(duplicate_type)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	const auto sourceId = response[1].value_union.ui64;

	auto *const sdi = new SourceDataInfo;
	sdi->name = name;
	sdi->obs_sourceId = "scene";
	sdi->id = sourceId;
	CacheManager<SourceDataInfo *>::getInstance().Store(sourceId, name, sdi);

	auto *const si = new SceneInfo;
	si->id = sourceId;
	CacheManager<SceneInfo *>::getInstance().Store(sourceId, name, si);

	auto instance = osn::Input::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(sourceId))});

	return instance;
}

Napi::Value osn::Scene::AddSource(const Napi::CallbackInfo &info)
{
	std::vector<ipc::value> params;
	osn::Input *input = Napi::ObjectWrap<osn::Input>::Unwrap(info[0].ToObject());

	params.push_back(ipc::value(this->sourceId));
	params.push_back(ipc::value(input->sourceId));
	Napi::Object transform = Napi::Object::New(info.Env());
	Napi::Object crop = Napi::Object::New(info.Env());

	if (info.Length() >= 2) {
		transform = info[1].ToObject();
		params.push_back(ipc::value(transform.Get("scaleX").ToNumber().DoubleValue()));
		params.push_back(ipc::value(transform.Get("scaleY").ToNumber().DoubleValue()));
		params.push_back(ipc::value(transform.Get("visible").ToBoolean().Value()));
		params.push_back(ipc::value(transform.Get("x").ToNumber().DoubleValue()));
		params.push_back(ipc::value(transform.Get("y").ToNumber().DoubleValue()));
		params.push_back(ipc::value(transform.Get("rotation").ToNumber().DoubleValue()));

		crop = transform.Get("crop").ToObject();
		params.push_back(ipc::value(crop.Get("left").ToNumber().Int64Value()));
		params.push_back(ipc::value(crop.Get("top").ToNumber().Int64Value()));
		params.push_back(ipc::value(crop.Get("right").ToNumber().Int64Value()));
		params.push_back(ipc::value(crop.Get("bottom").ToNumber().Int64Value()));

		params.push_back(ipc::value(transform.Get("streamVisible").ToBoolean().Value()));
		params.push_back(ipc::value(transform.Get("recordingVisible").ToBoolean().Value()));

		params.push_back(ipc::value(transform.Get("scaleFilter").ToNumber().Uint32Value()));
		params.push_back(ipc::value(transform.Get("blendingMode").ToNumber().Uint32Value()));
		params.push_back(ipc::value(transform.Get("blendingMethod").ToNumber().Uint32Value()));
	}
	if (info.Length() >= 3) {
		osn::Video *video = Napi::ObjectWrap<osn::Video>::Unwrap(info[2].ToObject());
		if (video)
			params.push_back(ipc::value(video->canvasId));
	}
	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Scene", info.Length() >= 2 ? "AddSourceWithTransform" : "AddSource", params);

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	uint64_t id = response[1].value_union.ui64;
	int64_t obs_id = response[2].value_union.i64;

	SceneInfo *si = CacheManager<SceneInfo *>::getInstance().Retrieve(this->sourceId);

	if (si) {
		si->items.push_back(std::make_pair(obs_id, id));
		si->itemsOrderCached = true;
	}

	SceneItemData *sid = new SceneItemData;
	sid->obs_itemId = obs_id;
	sid->scene_id = this->sourceId;

	if (info.Length() >= 2) {
		// Position
		sid->posX = transform.Get("x").ToNumber().FloatValue();
		sid->posY = transform.Get("y").ToNumber().FloatValue();
		sid->posChanged = false;

		// Scale
		sid->scaleX = transform.Get("scaleX").ToNumber().FloatValue();
		sid->scaleY = transform.Get("scaleY").ToNumber().FloatValue();
		sid->scaleChanged = false;

		// Visibility
		sid->isVisible = transform.Get("visible").ToBoolean().Value();
		sid->visibleChanged = false;

		// Crop
		sid->cropLeft = crop.Get("left").ToNumber().Int32Value();
		sid->cropTop = crop.Get("top").ToNumber().Int32Value();
		sid->cropRight = crop.Get("right").ToNumber().Int32Value();
		sid->cropBottom = crop.Get("bottom").ToNumber().Int32Value();
		sid->cropChanged = false;

		// Rotation
		sid->rotation = transform.Get("rotation").ToNumber().FloatValue();
		sid->rotationChanged = false;

		// Stream visible
		sid->isStreamVisible = transform.Get("streamVisible").ToBoolean().Value();
		sid->streamVisibleChanged = false;

		// Recording visible
		sid->isRecordingVisible = transform.Get("recordingVisible").ToBoolean().Value();
		sid->recordingVisibleChanged = false;

		// Scale Filter
		sid->scaleFilter = transform.Get("scaleFilter").ToNumber().Uint32Value();
		sid->scaleFilterChanged = false;

		// Blending mode
		sid->blendingMode = transform.Get("blendingMode").ToNumber().Uint32Value();
		sid->blendingModeChanged = false;

		// Recording visible
		sid->blendingMethod = transform.Get("blendingMethod").ToNumber().Uint32Value();
		sid->blendingMethodChanged = false;
	}

	CacheManager<SceneItemData *>::getInstance().Store(id, sid);

	auto instance = osn::SceneItem::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(id))});

	return instance;
}

Napi::Value osn::Scene::FindItem(const Napi::CallbackInfo &info)
{
	bool haveName = false;
	std::string name;
	int64_t position;

	if (info[0].IsNumber()) {
		haveName = false;
		position = info[0].ToNumber().Int64Value();
	} else if (info[0].IsString()) {
		haveName = true;
		name = info[0].ToString().Utf8Value();
	} else {
		Napi::TypeError::New(info.Env(), "Expected string or number").ThrowAsJavaScriptException();
		return info.Env().Undefined();
	}

	SceneInfo *si = CacheManager<SceneInfo *>::getInstance().Retrieve(this->sourceId);

	if (si && !haveName) {
		auto find = [position](const std::pair<int64_t, uint64_t> &item) { return item.first == position; };

		auto itemIt = std::find_if(si->items.begin(), si->items.end(), find);
		if (itemIt != si->items.end()) {
			auto instance = osn::SceneItem::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(itemIt->second))});

			return instance;
		}
	}

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response;
	if (haveName) {
		response = conn->call_synchronous_helper("Scene", "FindItemByName", std::vector<ipc::value>{ipc::value(this->sourceId), ipc::value(name)});
	} else {
		response = conn->call_synchronous_helper("Scene", "FindItemById", std::vector<ipc::value>{ipc::value(this->sourceId), ipc::value(position)});
	}

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	uint64_t id = response[1].value_union.ui64;

	auto instance = osn::SceneItem::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(id))});

	return instance;
}

Napi::Value osn::Scene::MoveItem(const Napi::CallbackInfo &info)
{
	int64_t from = info[0].ToNumber().Int64Value();
	int64_t to = info[1].ToNumber().Int64Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response =
		conn->call_synchronous_helper("Scene", "MoveItem", std::vector<ipc::value>{ipc::value(this->sourceId), ipc::value(from), ipc::value(to)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	SceneInfo *si = CacheManager<SceneInfo *>::getInstance().Retrieve(this->sourceId);

	if (si && response.size() > 2) {
		si->items.clear();

		for (size_t i = 1; i < response.size(); i += 2) {
			si->items.push_back(std::make_pair(response[i + 1].value_union.i64, response[i].value_union.ui64));
		}
		si->itemsOrderCached = true;
	}
	return info.Env().Undefined();
}

Napi::Value osn::Scene::OrderItems(const Napi::CallbackInfo &info)
{
	std::vector<int64_t> order;
	std::vector<char> order_char;

	Napi::Array array = info[0].As<Napi::Array>();

	order.resize(array.Length());
	for (unsigned int i = 0; i < array.Length(); i++) {
		if (array.Has(i))
			order[i] = array.Get(i).ToNumber().Int64Value();
	}
	order_char.resize(order.size() * sizeof(int64_t));
	memcpy(order_char.data(), order.data(), order_char.size());

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response =
		conn->call_synchronous_helper("Scene", "OrderItems", std::vector<ipc::value>{ipc::value(this->sourceId), ipc::value(order_char)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	SceneInfo *si = CacheManager<SceneInfo *>::getInstance().Retrieve(this->sourceId);

	if (si && response.size() > 2) {
		si->items.clear();

		for (size_t i = 1; i < response.size(); i += 2) {
			si->items.push_back(std::make_pair(response[i + 1].value_union.i64, response[i].value_union.ui64));
		}
		si->itemsOrderCached = true;
	}

	return info.Env().Undefined();
}

Napi::Value osn::Scene::GetItemAtIndex(const Napi::CallbackInfo &info)
{
	int32_t index = info[0].ToNumber().Int32Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response =
		conn->call_synchronous_helper("Scene", "GetItem", std::vector<ipc::value>{ipc::value(this->sourceId), ipc::value(index)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	uint64_t id = response[1].value_union.ui64;

	auto instance = osn::SceneItem::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(id))});

	return instance;
}

Napi::Value osn::Scene::GetItems(const Napi::CallbackInfo &info)
{
	SceneInfo *si = CacheManager<SceneInfo *>::getInstance().Retrieve(this->sourceId);

	if (si && si->itemsOrderCached) {
		Napi::Array array = Napi::Array::New(info.Env(), int(si->items.size()) - 1);
		size_t index = 0;
		bool itemRemoved = false;

		for (auto item : si->items) {
			SceneItemData *sid = CacheManager<SceneItemData *>::getInstance().Retrieve(item.first);
			if (!sid) {
				itemRemoved = true;
				break;
			}
			auto instance = osn::SceneItem::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(item.second))});
			array.Set(uint32_t(index++), instance);
		}
		if (!itemRemoved) {
			return array;
		}
	}

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Scene", "GetItems", std::vector<ipc::value>{ipc::value(this->sourceId)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	Napi::Array array = Napi::Array::New(info.Env(), int((response.size()) - 1) / 2);
	size_t index = 0;
	for (size_t i = 1; i < response.size(); i++) {
		auto instance = osn::SceneItem::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[i++].value_union.ui64))});
		array.Set(uint32_t(index++), instance);
	}

	if (si) {
		si->items.clear();

		for (size_t i = 1; i < response.size(); i += 2) {
			si->items.push_back(std::make_pair(response[i + 1].value_union.i64, response[i].value_union.ui64));
		}

		si->itemsOrderCached = true;
	}

	return array;
}

Napi::Value osn::Scene::GetItemsInRange(const Napi::CallbackInfo &info)
{
	int32_t from = info[0].ToNumber().Int32Value();
	int32_t to = info[1].ToNumber().Int32Value();

	auto conn = GetConnection(info);
	if (!conn)
		return info.Env().Undefined();

	std::vector<ipc::value> response = conn->call_synchronous_helper("Scene", "GetItemsInRange",
									 std::vector<ipc::value>{ipc::value(this->sourceId), ipc::value(from), ipc::value(to)});

	if (!ValidateResponse(info, response))
		return info.Env().Undefined();

	Napi::Array array = Napi::Array::New(info.Env(), int(response.size() - 1));
	for (size_t i = 1; i < response.size(); i++) {
		auto instance = osn::SceneItem::constructor.New({Napi::Number::New(info.Env(), static_cast<double>(response[i].value_union.ui64))});
		array.Set(uint32_t(i - 1), instance);
	}

	return array;
}

Napi::Value osn::Scene::CallIsConfigurable(const Napi::CallbackInfo &info)
{
	return osn::ISource::IsConfigurable(info, this->sourceId);
}

Napi::Value osn::Scene::CallGetProperties(const Napi::CallbackInfo &info)
{
	return osn::ISource::GetProperties(info, this->sourceId);
}

Napi::Value osn::Scene::CallGetSettings(const Napi::CallbackInfo &info)
{
	return osn::ISource::GetSettings(info, this->sourceId);
}

Napi::Value osn::Scene::CallGetSlowUncachedSettings(const Napi::CallbackInfo &info)
{
	return osn::ISource::GetSlowUncachedSettings(info, this->sourceId);
}

Napi::Value osn::Scene::CallGetType(const Napi::CallbackInfo &info)
{
	return osn::ISource::GetType(info, this->sourceId);
}

Napi::Value osn::Scene::CallGetName(const Napi::CallbackInfo &info)
{
	return osn::ISource::GetName(info, this->sourceId);
}

void osn::Scene::CallSetName(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::ISource::SetName(info, value, this->sourceId);
}

Napi::Value osn::Scene::CallGetOutputFlags(const Napi::CallbackInfo &info)
{
	return osn::ISource::GetOutputFlags(info, this->sourceId);
}

Napi::Value osn::Scene::CallGetFlags(const Napi::CallbackInfo &info)
{
	return osn::ISource::GetFlags(info, this->sourceId);
}

void osn::Scene::CallSetFlags(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::ISource::SetFlags(info, value, this->sourceId);
}

Napi::Value osn::Scene::CallGetStatus(const Napi::CallbackInfo &info)
{
	return osn::ISource::GetStatus(info, this->sourceId);
}

Napi::Value osn::Scene::CallGetId(const Napi::CallbackInfo &info)
{
	return osn::ISource::GetId(info, this->sourceId);
}

Napi::Value osn::Scene::CallGetMuted(const Napi::CallbackInfo &info)
{
	return osn::ISource::GetMuted(info, this->sourceId);
}

void osn::Scene::CallSetMuted(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::ISource::SetMuted(info, value, this->sourceId);
}

Napi::Value osn::Scene::CallGetEnabled(const Napi::CallbackInfo &info)
{
	return osn::ISource::GetEnabled(info, this->sourceId);
}

void osn::Scene::CallSetEnabled(const Napi::CallbackInfo &info, const Napi::Value &value)
{
	osn::ISource::SetEnabled(info, value, this->sourceId);
}

Napi::Value osn::Scene::CallRelease(const Napi::CallbackInfo &info)
{
	osn::ISource::Release(info, this->sourceId);

	return info.Env().Undefined();
}

Napi::Value osn::Scene::CallCallHandler(const Napi::CallbackInfo &info)
{
	return osn::ISource::CallHandler(info, this->sourceId);
}

Napi::Value osn::Scene::CallRemove(const Napi::CallbackInfo &info)
{
	osn::ISource::Remove(info, this->sourceId);
	this->sourceId = UINT64_MAX;

	return info.Env().Undefined();
}

Napi::Value osn::Scene::CallUpdate(const Napi::CallbackInfo &info)
{
	osn::ISource::Update(info, this->sourceId);

	return info.Env().Undefined();
}

Napi::Value osn::Scene::CallLoad(const Napi::CallbackInfo &info)
{
	osn::ISource::Load(info, this->sourceId);

	return info.Env().Undefined();
}

Napi::Value osn::Scene::CallSave(const Napi::CallbackInfo &info)
{
	osn::ISource::Save(info, this->sourceId);

	return info.Env().Undefined();
}

Napi::Value osn::Scene::CallSendMouseClick(const Napi::CallbackInfo &info)
{
	osn::ISource::SendMouseClick(info, this->sourceId);

	return info.Env().Undefined();
}

Napi::Value osn::Scene::CallSendMouseMove(const Napi::CallbackInfo &info)
{
	osn::ISource::SendMouseMove(info, this->sourceId);

	return info.Env().Undefined();
}

Napi::Value osn::Scene::CallSendMouseWheel(const Napi::CallbackInfo &info)
{
	osn::ISource::SendMouseWheel(info, this->sourceId);

	return info.Env().Undefined();
}

Napi::Value osn::Scene::CallSendFocus(const Napi::CallbackInfo &info)
{
	osn::ISource::SendFocus(info, this->sourceId);

	return info.Env().Undefined();
}

Napi::Value osn::Scene::CallSendKeyClick(const Napi::CallbackInfo &info)
{
	osn::ISource::SendKeyClick(info, this->sourceId);

	return info.Env().Undefined();
}