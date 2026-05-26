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

#include "osn-simple-streaming.hpp"
#include "osn-audio-encoder.hpp"
#include "osn-service.hpp"
#include "osn-error.hpp"
#include "shared.hpp"
#include "nodeobs_audio_encoders.h"
#include "osn-encoders.hpp"

void osn::ISimpleStreaming::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("SimpleStreaming");
	cls->register_function(std::make_shared<ipc::function>("Create", std::vector<ipc::type>{}, Create));
	cls->register_function(std::make_shared<ipc::function>("Destroy", std::vector<ipc::type>{ipc::type::UInt64}, Destroy));
	cls->register_function(std::make_shared<ipc::function>("GetService", std::vector<ipc::type>{ipc::type::UInt64}, GetService));
	cls->register_function(std::make_shared<ipc::function>("SetService", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetService));
	cls->register_function(std::make_shared<ipc::function>("GetVideoEncoder", std::vector<ipc::type>{ipc::type::UInt64}, GetVideoEncoder));
	cls->register_function(
		std::make_shared<ipc::function>("SetVideoEncoder", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetVideoEncoder));
	cls->register_function(std::make_shared<ipc::function>("GetVideoCanvas", std::vector<ipc::type>{ipc::type::UInt64}, GetVideoCanvas));
	cls->register_function(std::make_shared<ipc::function>("SetVideoCanvas", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetVideoCanvas));
	cls->register_function(std::make_shared<ipc::function>("GetAudioEncoder", std::vector<ipc::type>{ipc::type::UInt64}, GetAudioEncoder));
	cls->register_function(
		std::make_shared<ipc::function>("SetAudioEncoder", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetAudioEncoder));
	cls->register_function(std::make_shared<ipc::function>("GetUseAdvanced", std::vector<ipc::type>{ipc::type::UInt64}, GetUseAdvanced));
	cls->register_function(std::make_shared<ipc::function>("SetUseAdvanced", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetUseAdvanced));
	cls->register_function(std::make_shared<ipc::function>("GetCustomEncSettings", std::vector<ipc::type>{ipc::type::UInt64}, GetCustomEncSettings));
	cls->register_function(
		std::make_shared<ipc::function>("SetCustomEncSettings", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::String}, SetCustomEncSettings));
	cls->register_function(std::make_shared<ipc::function>("GetEnforceServiceBirate", std::vector<ipc::type>{ipc::type::UInt64}, GetEnforceServiceBirate));
	cls->register_function(std::make_shared<ipc::function>("SetEnforceServiceBirate", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32},
							       SetEnforceServiceBirate));
	cls->register_function(std::make_shared<ipc::function>("GetEnableTwitchVOD", std::vector<ipc::type>{ipc::type::UInt64}, GetEnableTwitchVOD));
	cls->register_function(
		std::make_shared<ipc::function>("SetEnableTwitchVOD", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, SetEnableTwitchVOD));
	cls->register_function(std::make_shared<ipc::function>("GetDelay", std::vector<ipc::type>{ipc::type::UInt64}, GetDelay));
	cls->register_function(std::make_shared<ipc::function>("SetDelay", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetDelay));
	cls->register_function(std::make_shared<ipc::function>("GetReconnect", std::vector<ipc::type>{ipc::type::UInt64}, GetReconnect));
	cls->register_function(std::make_shared<ipc::function>("SetReconnect", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetReconnect));
	cls->register_function(std::make_shared<ipc::function>("GetNetwork", std::vector<ipc::type>{ipc::type::UInt64}, GetNetwork));
	cls->register_function(std::make_shared<ipc::function>("SetNetwork", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt64}, SetNetwork));
	cls->register_function(std::make_shared<ipc::function>("Start", std::vector<ipc::type>{ipc::type::UInt64}, Start));
	cls->register_function(std::make_shared<ipc::function>("Stop", std::vector<ipc::type>{ipc::type::UInt64, ipc::type::UInt32}, Stop));
	cls->register_function(std::make_shared<ipc::function>("Query", std::vector<ipc::type>{ipc::type::UInt64}, Query));
	cls->register_function(std::make_shared<ipc::function>("GetLegacySettings", std::vector<ipc::type>{}, GetLegacySettings));
	cls->register_function(std::make_shared<ipc::function>("SetLegacySettings", std::vector<ipc::type>{ipc::type::UInt64}, SetLegacySettings));
	cls->register_function(std::make_shared<ipc::function>("GetDroppedFrames", std::vector<ipc::type>{ipc::type::UInt64}, GetDroppedFrames));
	cls->register_function(std::make_shared<ipc::function>("GetTotalFrames", std::vector<ipc::type>{ipc::type::UInt64}, GetTotalFrames));
	cls->register_function(std::make_shared<ipc::function>("GetKBitsPerSec", std::vector<ipc::type>{ipc::type::UInt64}, GetKBitsPerSec));
	cls->register_function(std::make_shared<ipc::function>("GetDataOutput", std::vector<ipc::type>{ipc::type::UInt64}, GetDataOutput));
	cls->register_function(std::make_shared<ipc::function>("GetAvailableEncoders", std::vector<ipc::type>{ipc::type::UInt64}, GetAvailableEncoders));

	srv.register_collection(cls);
}

void osn::ISimpleStreaming::Create(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	uint64_t uid = osn::ISimpleStreaming::Manager::GetInstance().allocate(new SimpleStreaming());
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::ISimpleStreaming::Destroy(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleStreaming *streaming = static_cast<SimpleStreaming *>(osn::ISimpleStreaming::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	osn::ISimpleStreaming::Manager::GetInstance().free(streaming);
	delete streaming;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::ISimpleStreaming::GetAudioEncoder(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	blog(LOG_WARNING, "Function %s is deprecated", __func__);
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::ISimpleStreaming::SetAudioEncoder(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleStreaming *streaming = static_cast<SimpleStreaming *>(osn::ISimpleStreaming::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	if (args[1].value_union.ui64 == UINT64_MAX) {
		streaming->audioEncoder = nullptr;
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		AUTO_DEBUG;
		return;
	}

	obs_encoder_t *encoder = osn::AudioEncoder::Manager::GetInstance().find(args[1].value_union.ui64);
	if (!encoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Encoder reference is not valid.");
	}

	streaming->audioEncoder = encoder;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::ISimpleStreaming::GetUseAdvanced(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleStreaming *streaming = static_cast<SimpleStreaming *>(osn::ISimpleStreaming::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(streaming->useAdvanced));
	AUTO_DEBUG;
}

void osn::ISimpleStreaming::SetUseAdvanced(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleStreaming *streaming = static_cast<SimpleStreaming *>(osn::ISimpleStreaming::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	streaming->useAdvanced = args[1].value_union.ui32;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::ISimpleStreaming::GetCustomEncSettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleStreaming *streaming = static_cast<SimpleStreaming *>(osn::ISimpleStreaming::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(streaming->customEncSettings));
	AUTO_DEBUG;
}

void osn::ISimpleStreaming::SetCustomEncSettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleStreaming *streaming = static_cast<SimpleStreaming *>(osn::ISimpleStreaming::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	streaming->customEncSettings = args[1].value_str;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

static constexpr int kSoundtrackArchiveEncoderIdx = 1;
static constexpr int kSoundtrackArchiveTrackIdx = 5;

static uint32_t setMixer(obs_source_t *source, const int mixerIdx, const bool checked)
{
	uint32_t mixers = obs_source_get_audio_mixers(source);
	uint32_t new_mixers = mixers;
	if (checked) {
		new_mixers |= (1 << mixerIdx);
	} else {
		new_mixers &= ~(1 << mixerIdx);
	}
	obs_source_set_audio_mixers(source, new_mixers);
	return mixers;
}

static void SetupTwitchSoundtrackAudio(osn::SimpleStreaming *streaming)
{
	// These are magic ints provided by OBS for default sources:
	// 0 is the main scene/transition which you'd see on the main preview,
	// 1-2 are desktop audio 1 and 2 as you'd see in audio settings,
	// 2-4 are mic/aux 1-3 as you'd see in audio settings
	auto desktopSource1 = obs_get_output_source(1);
	auto desktopSource2 = obs_get_output_source(2);

	// Since our plugin duplicates all of the desktop sources, we want to ensure that both of the
	// default desktop sources, provided by OBS, are not set to mix on our custom encoder track.
	streaming->oldMixer_desktopSource1 = setMixer(desktopSource1, kSoundtrackArchiveTrackIdx, false);
	streaming->oldMixer_desktopSource2 = setMixer(desktopSource2, kSoundtrackArchiveTrackIdx, false);

	obs_source_release(desktopSource1);
	obs_source_release(desktopSource2);

	if (streaming->streamArchive && obs_encoder_active(streaming->streamArchive))
		return;

	if (!streaming->streamArchive) {
		streaming->streamArchive =
			obs_audio_encoder_create("ffmpeg_aac", "Soundtrack by Twitch Archive Encoder", nullptr, kSoundtrackArchiveTrackIdx, nullptr);
		obs_encoder_set_audio(streaming->streamArchive, obs_get_audio());
	}

	obs_output_set_audio_encoder(streaming->GetOutput(), streaming->streamArchive, kSoundtrackArchiveEncoderIdx);
	obs_encoder_set_video_mix(streaming->streamArchive, obs_video_mix_get(streaming->GetCanvas(), OBS_STREAMING_VIDEO_RENDERING));

	obs_data_t *settings = obs_data_create();

	obs_data_t *settingsEnc = obs_encoder_get_settings(streaming->audioEncoder);
	int64_t bitrate = obs_data_get_int(settingsEnc, "bitrate");
	obs_data_release(settingsEnc);

	obs_data_set_int(settings, "bitrate", bitrate);
	obs_encoder_update(streaming->streamArchive, settings);
	obs_data_release(settings);
}

static void StopTwitchSoundtrackAudio(osn::Streaming *streaming)
{
	if (streaming->streamArchive) {
		obs_encoder_release(streaming->streamArchive);
		streaming->streamArchive = nullptr;
	}

	auto desktopSource1 = obs_get_output_source(1);
	auto desktopSource2 = obs_get_output_source(2);

	obs_source_set_audio_mixers(desktopSource1, streaming->oldMixer_desktopSource1);
	obs_source_set_audio_mixers(desktopSource2, streaming->oldMixer_desktopSource2);

	obs_source_release(desktopSource1);
	obs_source_release(desktopSource2);
}

void UpdateStreamingSettings_amd(obs_data_t *settings, int bitrate)
{
	obs_data_set_string(settings, "profile", "high");
	obs_data_set_string(settings, "preset", "quality");
	obs_data_set_string(settings, "rate_control", "CBR");
	obs_data_set_int(settings, "bitrate", bitrate);
	obs_data_set_int(settings, "keyint_sec", 2);
	obs_data_set_int(settings, "cqp", 20);
	obs_data_set_int(settings, "bf", 3);
}

void osn::SimpleStreaming::UpdateEncoders()
{
	if (!videoEncoder || !audioEncoder)
		return;

	if (obs_encoder_active(videoEncoder))
		return;

	if (obs_encoder_active(audioEncoder))
		return;

	obs_data_t *videoEncSettings = obs_encoder_get_settings(videoEncoder);
	obs_data_t *audioEncSettings = obs_encoder_get_settings(audioEncoder);
	int vBitrate = static_cast<int>(obs_data_get_int(videoEncSettings, "bitrate"));
	int aBitrate = static_cast<int>(obs_data_get_int(audioEncSettings, "bitrate"));

	std::string id = obs_encoder_get_id(videoEncoder);
	if (id.compare(ADVANCED_ENCODER_AMD) == 0)
		UpdateStreamingSettings_amd(videoEncSettings, vBitrate);

	obs_data_set_string(videoEncSettings, "rate_control", "CBR");
	obs_data_set_int(videoEncSettings, "bitrate", vBitrate);

	if (useAdvanced) {
		obs_data_set_string(videoEncSettings, "x264opts", customEncSettings.c_str());
	}

	obs_data_set_string(audioEncSettings, "rate_control", "CBR");
	obs_data_set_int(audioEncSettings, "bitrate", aBitrate);

	obs_service_apply_encoder_settings(service, videoEncSettings, audioEncSettings);

	if (useAdvanced && !enforceServiceBitrate) {
		obs_data_set_int(videoEncSettings, "bitrate", vBitrate);
		obs_data_set_int(audioEncSettings, "bitrate", aBitrate);
	}

	video_t *video = this->GetCanvasVideo(obs_get_multiple_rendering() ? OBS_STREAMING_VIDEO_RENDERING
									  : OBS_MAIN_VIDEO_RENDERING);
	enum video_format format = video ? video_output_get_format(video) : VIDEO_FORMAT_NV12;

	switch (format) {
	case VIDEO_FORMAT_I420:
	case VIDEO_FORMAT_NV12:
		// case VIDEO_FORMAT_I010:
		// case VIDEO_FORMAT_P010:
		break;
	default:
		obs_encoder_set_preferred_video_format(videoEncoder, VIDEO_FORMAT_NV12);
	}

	if (obs_get_multiple_rendering()) {
		obs_encoder_set_video_mix(videoEncoder, obs_video_mix_get(this->GetCanvas(), OBS_STREAMING_VIDEO_RENDERING));
	} else {
		obs_encoder_set_video_mix(videoEncoder, obs_video_mix_get(this->GetCanvas(), OBS_MAIN_VIDEO_RENDERING));
	}

	obs_encoder_update(videoEncoder, videoEncSettings);
	obs_encoder_update(audioEncoder, audioEncSettings);

	obs_data_release(videoEncSettings);
	obs_data_release(audioEncSettings);
}

void osn::ISimpleStreaming::Start(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleStreaming *streaming = static_cast<SimpleStreaming *>(osn::ISimpleStreaming::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple streaming reference is not valid.");
	}

	if (!streaming->service) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid service.");
	}

	const char *type = OBS_service::getStreamOutputType(streaming->service);
	if (!type)
		type = "rtmp_output";

	if (!streaming->GetOutput() || strcmp(obs_output_get_id(streaming->GetOutput()), type) != 0)
		streaming->CreateOutput(type, "stream");

	if (!streaming->GetOutput()) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Error while creating the streaming output.");
	}

	if (!streaming->videoEncoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid video encoder.");
	}

	if (!streaming->audioEncoder) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid audio encoder.");
	}

	if (!streaming->GetCanvas()) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid main canvas.");
	}

	if (!streaming->GetCanvasVideo(obs_get_multiple_rendering() ? OBS_STREAMING_VIDEO_RENDERING : OBS_MAIN_VIDEO_RENDERING)) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError,
				    "Video pipeline not initialized (canvas has no video mix). "
				    "Graphics device may have been lost during startup. Restart the app.");
	}

	//verify the encoder is compatible before setting it - need config ID for simple mode in order to find correct settings
	const char *encID = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "StreamEncoder"));
	if (!osn::EncoderUtils::isEncoderCompatibleStreaming(streaming->service, encID, streaming->simple)) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "The provided encoder is not valid for the current service.");
	}

	streaming->UpdateEncoders();
	obs_encoder_set_audio(streaming->audioEncoder, obs_get_audio());
	obs_output_set_audio_encoder(streaming->GetOutput(), streaming->audioEncoder, 0);
	obs_encoder_set_video_mix(streaming->audioEncoder, obs_video_mix_get(streaming->GetCanvas(), OBS_STREAMING_VIDEO_RENDERING));

	obs_output_set_video_encoder(streaming->GetOutput(), streaming->videoEncoder);

	if (streaming->enableTwitchVOD) {
		streaming->twitchVODSupported = streaming->isTwitchVODSupported();
		if (streaming->twitchVODSupported)
			SetupTwitchSoundtrackAudio(streaming);
	}

	obs_output_set_service(streaming->GetOutput(), streaming->service);

	std::string outputSettingsError;
	if (!streaming->ApplyOutputSettings(streaming->GetOutput(), outputSettingsError)) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, outputSettingsError.c_str());
	}

	blog(LOG_INFO, "Start Streaming using %s encoder.", obs_encoder_get_id(streaming->videoEncoder));

	streaming->StartOutput();

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::ISimpleStreaming::Stop(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	Streaming *streaming = osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64);
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple streaming reference is not valid.");
	}

	if (!streaming->GetOutput()) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Invalid streaming output.");
	}

	bool force = args[1].value_union.ui32;

	if (force)
		obs_output_force_stop(streaming->GetOutput());
	else
		obs_output_stop(streaming->GetOutput());

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

obs_encoder_t *osn::ISimpleStreaming::CreateLegacyVideoEncoder()
{
	osn::EncoderUtils::convertOldJimNvencEncoder(ConfigManager::getInstance().getBasic(), "SimpleOutput", "StreamEncoder", "RecEncoder");

	const char *encId = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "StreamEncoder"));

	//check for missing/bad encoder ID and reset to x264 if needed
	if ((strlen(encId) == 0) || osn::EncoderUtils::isInvalidAppleEncoder(encId)) {
		blog(LOG_WARNING, "Invalid or missing encoder ID in basic.ini, defaulting to x264.");
		encId = SIMPLE_ENCODER_X264;
		config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "StreamEncoder", encId);
		config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);
	}

	obs_data_t *videoEncData = obs_data_create();
	obs_data_set_string(videoEncData, "rate_control", "CBR");
	obs_data_set_int(videoEncData, "bitrate", config_get_uint(ConfigManager::getInstance().getBasic(), "SimpleOutput", "VBitrate"));

	bool advanced = config_get_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "UseAdvanced");
	const char *custom = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "x264Settings"));

	const char *preset = nullptr;

	std::string presetType = osn::EncoderUtils::getEncoderPreset(encId);
	std::string encIdOBS = osn::EncoderUtils::getInternalEncoderFromSimple(encId);

	preset = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", presetType.c_str()));

	if (presetType == PRESET_NVENC) {
		if (strlen(preset) == 0) {
			const char *oldParamName = PRESET_NVENC_DEP;
			const char *oldValue = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", oldParamName));
			if (strlen(oldValue) != 0) {
				preset = osn::EncoderUtils::convertNvencSimplePreset(oldValue);
				blog(LOG_INFO, "NVENC preset converted from %s to %s", oldValue, preset);
			}
		}
	}

	if (advanced) {
		obs_data_set_string(videoEncData, "preset", preset);
		obs_data_set_string(videoEncData, "x264opts", custom);
	}

	bool enforceServiceBitrate = config_get_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "EnforceBitrate");

	if (advanced && !enforceServiceBitrate) {
		obs_data_set_int(videoEncData, "bitrate", config_get_uint(ConfigManager::getInstance().getBasic(), "SimpleOutput", "VBitrate"));
	}

	if (osn::EncoderUtils::getEncoderFamily(encId) == FAMILY_APPLE) {
		const char *profile = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "Profile"));
		if (profile)
			obs_data_set_string(videoEncData, "profile", profile);
	}

	obs_encoder_t *videoEncoder = obs_video_encoder_create(encIdOBS.c_str(), "video-encoder", videoEncData, nullptr);
	obs_data_release(videoEncData);

	osn::VideoEncoder::Manager::GetInstance().allocate(videoEncoder);

	return videoEncoder;
}

obs_encoder_t *osn::ISimpleStreaming::CreateLegacyAudioEncoder()
{
	obs_data_t *audioEncData = obs_data_create();
	obs_data_set_string(audioEncData, "rate_control", "CBR");
	int bitrate = (static_cast<int>(config_get_uint(ConfigManager::getInstance().getBasic(), "SimpleOutput", "ABitrate")));
	obs_data_set_int(audioEncData, "bitrate", FindClosestAvailableAACBitrate(bitrate));

	bool advanced = config_get_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "UseAdvanced");

	bool enforceServiceBitrate = config_get_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "EnforceBitrate");

	if (advanced && !enforceServiceBitrate) {
		obs_data_set_int(audioEncData, "bitrate", config_get_uint(ConfigManager::getInstance().getBasic(), "SimpleOutput", "ABitrate"));
	}

	obs_encoder_t *audioEncoder = obs_audio_encoder_create("ffmpeg_aac", "audio", audioEncData, 0, nullptr);
	obs_data_release(audioEncData);

	osn::AudioEncoder::Manager::GetInstance().allocate(audioEncoder);

	return audioEncoder;
}

void osn::ISimpleStreaming::GetLegacySettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	osn::SimpleStreaming *streaming = new osn::SimpleStreaming();

	streaming->videoEncoder = CreateLegacyVideoEncoder();
	streaming->audioEncoder = CreateLegacyAudioEncoder();

	streaming->useAdvanced = config_get_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "UseAdvanced");
	streaming->enableTwitchVOD = config_get_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "VodTrackEnabled");
	streaming->enforceServiceBitrate = config_get_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "EnforceBitrate");
	streaming->customEncSettings = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "x264Settings"));

	streaming->getDelayLegacySettings();
	streaming->getReconnectLegacySettings();
	streaming->getNetworkLegacySettings();

	streaming->service = osn::Service::GetLegacyServiceSettings();
	osn::Service::Manager::GetInstance().allocate(streaming->service);

	uint64_t uid = osn::ISimpleStreaming::Manager::GetInstance().allocate(streaming);
	if (uid == UINT64_MAX) {
		PRETTY_ERROR_RETURN(ErrorCode::CriticalError, "Index list is full.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(uid));
	AUTO_DEBUG;
}

void osn::ISimpleStreaming::SetLegacyVideoEncoderSettings(obs_encoder_t *encoder)
{
	const char *encIdOBS = obs_encoder_get_id(encoder);

	obs_data_t *settings = obs_encoder_get_settings(encoder);
	uint64_t bitrate = obs_data_get_int(settings, "bitrate");
	config_set_uint(ConfigManager::getInstance().getBasic(), "SimpleOutput", "VBitrate", bitrate);

	const char *custom = utility::GetSafeString(config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "x264Settings"));

	const char *preset = nullptr;

	std::string presetType = osn::EncoderUtils::getEncoderPreset(encIdOBS);
	std::string encId = osn::EncoderUtils::getSimpleEncoderFromInternal(encIdOBS);

	config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "StreamEncoder", encId.c_str());

	preset = obs_data_get_string(settings, "preset");
	config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", presetType.c_str(), preset);

	obs_data_release(settings);
}

void osn::ISimpleStreaming::SetLegacyAudioEncoderSettings(obs_encoder_t *encoder)
{
	obs_data_t *settings = obs_encoder_get_settings(encoder);
	uint64_t bitrate = obs_data_get_int(settings, "bitrate");
	config_set_uint(ConfigManager::getInstance().getBasic(), "SimpleOutput", "ABitrate", bitrate);

	obs_data_release(settings);
}

void osn::ISimpleStreaming::SetLegacySettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleStreaming *streaming = static_cast<SimpleStreaming *>(osn::ISimpleStreaming::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Streaming reference is not valid.");
	}

	streaming->setDelayLegacySettings();
	streaming->setReconnectLegacySettings();
	streaming->setNetworkLegacySettings();

	osn::Service::SetLegacyServiceSettings(streaming->service);

	config_set_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "VodTrackEnabled", streaming->enableTwitchVOD);
	config_set_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "EnforceBitrate", streaming->enforceServiceBitrate);
	config_set_bool(ConfigManager::getInstance().getBasic(), "SimpleOutput", "UseAdvanced", streaming->useAdvanced);
	config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "x264Settings", streaming->customEncSettings.c_str());

	SetLegacyVideoEncoderSettings(streaming->videoEncoder);
	SetLegacyAudioEncoderSettings(streaming->audioEncoder);

	config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void osn::ISimpleStreaming::GetAvailableEncoders(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	SimpleStreaming *streaming = static_cast<SimpleStreaming *>(osn::IStreaming::Manager::GetInstance().find(args[0].value_union.ui64));
	if (!streaming) {
		PRETTY_ERROR_RETURN(ErrorCode::InvalidReference, "Simple streaming reference is not valid.");
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	osn::EncoderUtils::getAvailableEncoders(rval, streaming->service, true, false, "");
	AUTO_DEBUG;
}
