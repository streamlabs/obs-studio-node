/******************************************************************************
    Copyright (C) 2016-2022 by Streamlabs (General Workings Inc)

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

#include "osn-video-encoder.hpp"
#include "osn-error.hpp"
#include "shared.hpp"
#include "osn-encoders.hpp"

void osn::VideoEncoder::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("VideoEncoder");
	cls->register_function(
		std::make_shared<ipc::function>("Create", std::vector<ipc::type>{ipc::type::String, ipc::type::String, ipc::type::String}, Create));
	cls->register_function(std::make_shared<ipc::function>("GetTypes", std::vector<ipc::type>{}, GetTypes));
	cls->register_function(std::make_shared<ipc::function>("GetName", std::vector<ipc::type>{ipc::type::UInt64}, GetName));
	cls->register_function(std::make_shared<ipc::function>("SetName", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String}, SetName));
	cls->register_function(std::make_shared<ipc::function>("GetType", std::vector<ipc::type>{ipc::type::UInt64}, GetType));
	cls->register_function(std::make_shared<ipc::function>("GetActive", std::vector<ipc::type>{ipc::type::UInt64}, GetActive));
	cls->register_function(std::make_shared<ipc::function>("GetId", std::vector<ipc::type>{ipc::type::UInt64}, GetId));
	cls->register_function(std::make_shared<ipc::function>("GetLastError", std::vector<ipc::type>{ipc::type::UInt64}, GetLastError));
	cls->register_function(std::make_shared<ipc::function>("Release", std::vector<ipc::type>{ipc::type::UInt64}, Release));
	cls->register_function(std::make_shared<ipc::function>("Update", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String}, Update));
	cls->register_function(std::make_shared<ipc::function>("GetProperties", std::vector<ipc::type>{ipc::type::UInt64}, GetProperties));
	cls->register_function(std::make_shared<ipc::function>("GetSettings", std::vector<ipc::type>{ipc::type::UInt64}, GetSettings));
	srv.register_collection(cls);
}

void osn::VideoEncoder::Create(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	std::string encoderId = args[0].value_str;
	std::string name = args[1].value_str;

	std::string settingsJson = args[2].value_str;
	if (settingsJson.empty())
		settingsJson = "{}";
	obs_data_t *settings = obs_data_create_from_json(settingsJson.c_str());

	//for simple need to get internal name
	//TODO do we need to check low CPU here before we convert?
	std::string convertedEncoderId = encoderId; 
	const char *mode = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "Output", "Mode"));
	if (strcmp(mode, "Simple") == 0) {
		convertedEncoderId = osn::EncoderUtils::getInternalEncoderFromSimple(encoderId.c_str());
	}	

	obs_encoder_t *encoder = obs_video_encoder_create(convertedEncoderId.c_str(), name.c_str(), settings, nullptr);
	obs_data_release(settings);

	if (!encoder) {
		PRETTY_ERROR_RETURN(ErrorCode::Error, "Failed to create encoder.");
	}

	uint64_t uid = osn::VideoEncoder::Manager::GetInstance().allocate(encoder);
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::VideoEncoder::GetTypes(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	const char *typeId = nullptr;
	for (size_t idx = 0; obs_enum_encoder_types(idx, &typeId); idx++) {
		if (obs_get_encoder_type(typeId) == OBS_ENCODER_VIDEO)
			rval.push_back(ipc::value(typeId ? typeId : ""));
	}
	AUTO_DEBUG;
}

void osn::VideoEncoder::GetName(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_encoder_t *encoder = osn::VideoEncoder::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!encoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Video encoder reference is not valid.");
	}

	const char *name = obs_encoder_get_name(encoder);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(name ? name : ""));
	AUTO_DEBUG;
}

void osn::VideoEncoder::SetName(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_encoder_t *encoder = osn::VideoEncoder::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!encoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Video encoder reference is not valid.");
	}

	std::string name = args[1].value_str;
	obs_encoder_set_name(encoder, name.c_str());
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::VideoEncoder::GetType(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_encoder_t *encoder = osn::VideoEncoder::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!encoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Video encoder reference is not valid.");
	}

	uint32_t type = (uint32_t)obs_encoder_get_type(encoder);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(type));
	AUTO_DEBUG;
}

void osn::VideoEncoder::GetActive(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_encoder_t *encoder = osn::VideoEncoder::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!encoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Video encoder reference is not valid.");
	}

	bool active = obs_encoder_active(encoder);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(active));
	AUTO_DEBUG;
}

void osn::VideoEncoder::GetId(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_encoder_t *encoder = osn::VideoEncoder::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!encoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Video encoder reference is not valid.");
	}

	const char *encoderId = obs_encoder_get_id(encoder);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(encoderId ? encoderId : ""));
	AUTO_DEBUG;
}

void osn::VideoEncoder::GetLastError(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_encoder_t *encoder = osn::VideoEncoder::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!encoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Video encoder reference is not valid.");
	}

	const char *lastError = obs_encoder_get_last_error(encoder);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(lastError ? lastError : ""));
	AUTO_DEBUG;
}

void osn::VideoEncoder::Release(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_encoder_t *encoder = osn::VideoEncoder::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!encoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Video encoder reference is not valid.");
	}

	obs_encoder_release(encoder);
	encoder = nullptr;
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::VideoEncoder::Update(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_encoder_t *encoder = osn::VideoEncoder::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!encoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Video encoder reference is not valid.");
	}

	obs_data_t *settings = obs_data_create_from_json(args[1].value_str.c_str());
	obs_encoder_update(encoder, settings);
	obs_data_release(settings);
	AUTO_DEBUG;
}

void osn::VideoEncoder::GetProperties(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_encoder_t *encoder = osn::VideoEncoder::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!encoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Video encoder reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

	obs_properties_t *prp = obs_encoder_properties(encoder);
	obs_data *settings = obs_encoder_get_settings(encoder);

	utility::ProcessProperties(prp, settings, rval);

	obs_properties_destroy(prp);

	obs_data_release(settings);
	AUTO_DEBUG;
}

void osn::VideoEncoder::GetSettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	obs_encoder_t *encoder = osn::VideoEncoder::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!encoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Video encoder reference is not valid.");
	}

	obs_data_t *settings = obs_encoder_get_settings(encoder);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(obs_data_get_json_pretty(settings)));
	obs_data_release(settings);
	AUTO_DEBUG;
}

osn::VideoEncoder::Manager &osn::VideoEncoder::Manager::GetInstance()
{
	static osn::VideoEncoder::Manager _inst;
	return _inst;
}