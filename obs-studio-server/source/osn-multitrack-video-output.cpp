#include "osn-multitrack-video-output.hpp"

#include "osn-multitrack-video-data-model.hpp"

#include <obs.hpp>
#include <util/dstr.hpp>

#include <inttypes.h>

// Codec profile strings
static const char *h264_main = "Main";
static const char *h264_high = "High";
static const char *h264_cb = "Constrained Baseline";
static const char *hevc_main = "Main";
static const char *hevc_main10 = "Main 10";
static const char *av1_main = "Main";

///////////////////////////////////////////////////////////////////////////////////
// Note: this code block represents essential constants from the libav library;
//       it was added to not add the libav as a dependency of OBS studio node.
#define AV_PROFILE_UNKNOWN -99

#define AV_PROFILE_H264_CONSTRAINED (1 << 9) // 8+1; constraint_set1_flag
#define AV_PROFILE_H264_INTRA (1 << 11)      // 8+3; constraint_set3_flag
#define AV_PROFILE_H264_MAIN 77
#define AV_PROFILE_H264_HIGH 100
#define AV_PROFILE_H264_CONSTRAINED_BASELINE (66 | AV_PROFILE_H264_CONSTRAINED)

#define AV_PROFILE_HEVC_MAIN 1
#define AV_PROFILE_HEVC_MAIN_10 2

#define AV_PROFILE_AV1_MAIN 0
///////////////////////////////////////////////////////////////////////////////////

#ifdef _MSC_VER
// not #if defined(_WIN32) || defined(_WIN64) because we have strncasecmp in mingw
#define strncasecmp strnicmp
#endif

namespace osn {

static bool encoder_available(const char *type)
{
	const char *id = nullptr;

	for (size_t idx = 0; obs_enum_encoder_types(idx, &id); idx++) {
		if (strcmp(id, type) == 0)
			return true;
	}

	return false;
}

static void adjust_video_encoder_scaling(const obs_video_info &ovi, obs_encoder_t *video_encoder, const VideoEncoderConfiguration &encoder_config,
					 size_t encoder_index)
{
	auto requested_width = encoder_config.width;
	auto requested_height = encoder_config.height;

	if (ovi.base_width < requested_width || ovi.base_height < requested_height) {
		blog(LOG_WARNING, "Requested resolution exceeds canvas/available resolution for encoder %zu: %" PRIu32 "x%" PRIu32 " > %" PRIu32 "x%" PRIu32,
		     encoder_index, requested_width, requested_height, ovi.base_width, ovi.base_height);
	}

	obs_encoder_set_scaled_size(video_encoder, requested_width, requested_height);
	obs_encoder_set_gpu_scale_type(video_encoder, encoder_config.gpu_scale_type.value_or(OBS_SCALE_BICUBIC));
	obs_encoder_set_preferred_video_format(video_encoder, encoder_config.format.value_or(VIDEO_FORMAT_NV12));
	obs_encoder_set_preferred_color_space(video_encoder, encoder_config.colorspace.value_or(VIDEO_CS_709));
	obs_encoder_set_preferred_range(video_encoder, encoder_config.range.value_or(VIDEO_RANGE_PARTIAL));
}

static uint32_t closest_divisor(const obs_video_info &ovi, const media_frames_per_second &target_fps)
{
	auto target = (uint64_t)target_fps.numerator * ovi.fps_den;
	auto source = (uint64_t)ovi.fps_num * target_fps.denominator;
	return std::max(1u, static_cast<uint32_t>(source / target));
}

static void adjust_encoder_frame_rate_divisor(const obs_video_info &ovi, obs_encoder_t *video_encoder, const VideoEncoderConfiguration &encoder_config,
					      const size_t encoder_index)
{
	if (!encoder_config.framerate) {
		blog(LOG_WARNING, "`framerate` not specified for encoder %zu", encoder_index);
		return;
	}
	media_frames_per_second requested_fps = *encoder_config.framerate;

	if (ovi.fps_num == requested_fps.numerator && ovi.fps_den == requested_fps.denominator)
		return;

	auto divisor = closest_divisor(ovi, requested_fps);
	if (divisor <= 1)
		return;

	blog(LOG_INFO, "Setting frame rate divisor to %u for encoder %zu", divisor, encoder_index);
	obs_encoder_set_frame_rate_divisor(video_encoder, divisor);
}

static OBSEncoderAutoRelease create_video_encoder(DStr &name_buffer, std::size_t encoder_index, const VideoEncoderConfiguration &encoder_config,
						  obs_video_info *canvas_ovi)
{
	auto encoder_type = encoder_config.type.c_str();
	if (!encoder_available(encoder_type)) {
		blog(LOG_ERROR, "Encoder type '%s' not available", encoder_type);
		throw std::runtime_error("Failed to start stream. Encoder '" + std::string(encoder_type) + "' not available");
	}

	dstr_printf(name_buffer, "multitrack video video encoder %zu", encoder_index);

	OBSDataAutoRelease encoder_settings = obs_data_create_from_json(encoder_config.settings.dump().c_str());

	/* VAAPI-based encoders unfortunately use an integer for "profile". Until a string-based "profile" can be used with
	 * VAAPI, find the corresponding integer value and update the settings with an integer-based "profile".
	 */
	if (strstr(encoder_type, "vaapi")) {
		// Move the "profile" string to "profile_str".
		const char *profile_str = obs_data_get_string(encoder_settings, "profile");
		obs_data_set_string(encoder_settings, "profile_str", profile_str);
		obs_data_item_t *profile_item = obs_data_item_byname(encoder_settings, "profile");
		obs_data_item_remove(&profile_item);
		obs_data_item_release(&profile_item);

		// Find the vaapi_profile integer based on codec type and "profile" string.
		int vaapi_profile;
		const char *codec = obs_get_encoder_codec(encoder_type);
		if (strcmp(codec, "h264") == 0) {
			if (astrcmpi(profile_str, h264_main) == 0) {
				vaapi_profile = AV_PROFILE_H264_MAIN;
			} else if (astrcmpi(profile_str, h264_high) == 0) {
				vaapi_profile = AV_PROFILE_H264_HIGH;
			} else if (astrcmpi(profile_str, h264_cb) == 0) {
				vaapi_profile = AV_PROFILE_H264_CONSTRAINED_BASELINE;
			} else {
				blog(LOG_WARNING, "Unsupported H264 profile '%s', setting to Main profile", profile_str);
				vaapi_profile = AV_PROFILE_H264_MAIN;
			}
		} else if (strcmp(codec, "hevc") == 0) {
			if (astrcmpi(profile_str, hevc_main) == 0) {
				vaapi_profile = AV_PROFILE_HEVC_MAIN;
			} else if (astrcmpi(profile_str, hevc_main10) == 0) {
				vaapi_profile = AV_PROFILE_HEVC_MAIN_10;
			} else {
				blog(LOG_WARNING, "Unsupported HEVC profile '%s', setting to Main profile", profile_str);
				vaapi_profile = AV_PROFILE_HEVC_MAIN;
			}
		} else if (strcmp(codec, "av1") == 0) {
			if (astrcmpi(profile_str, av1_main) == 0) {
				vaapi_profile = AV_PROFILE_AV1_MAIN;
			} else {
				blog(LOG_WARNING, "Unsupported AV1 profile '%s', setting to Main profile", profile_str);
				vaapi_profile = AV_PROFILE_AV1_MAIN;
			}
		} else {
			vaapi_profile = AV_PROFILE_UNKNOWN;
			blog(LOG_WARNING, "Unsupported codec '%s', setting profile to unknown", codec);
		}
		obs_data_set_int(encoder_settings, "profile", vaapi_profile);
	}
	obs_data_set_bool(encoder_settings, "disable_scenecut", true);

	OBSEncoderAutoRelease video_encoder = obs_video_encoder_create(encoder_type, name_buffer, encoder_settings, nullptr);
	if (!video_encoder) {
		blog(LOG_ERROR, "Failed to create video encoder '%s'", name_buffer->array);
		throw std::runtime_error("Failed to start stream. Failed to create video encoder");
	}

	obs_encoder_set_video_mix(video_encoder, obs_video_mix_get(canvas_ovi, OBS_MAIN_VIDEO_RENDERING));

	adjust_video_encoder_scaling(*canvas_ovi, video_encoder, encoder_config, encoder_index);
	adjust_encoder_frame_rate_divisor(*canvas_ovi, video_encoder, encoder_config, encoder_index);

	return video_encoder;
}

static bool create_video_encoders(const Config &go_live_config, std::shared_ptr<obs_encoder_group_t> &video_encoder_group, obs_output_t *output,
				  const std::vector<obs_video_info *> &canvases)
{
	DStr video_encoder_name_buffer;
	if (go_live_config.encoder_configurations.empty()) {
		blog(LOG_ERROR, "create_video_encoders - Missing video encoder configurations");
		return false;
	}

	std::shared_ptr<obs_encoder_group_t> encoder_group(obs_encoder_group_create(), obs_encoder_group_destroy);
	if (!encoder_group) {
		blog(LOG_ERROR, "create_video_encoders - failed to create encoder group");
		return false;
	}

	for (size_t i = 0; i < go_live_config.encoder_configurations.size(); i++) {
		auto &config = go_live_config.encoder_configurations[i];
		if (config.canvas_index >= canvases.size()) {
			blog(LOG_ERROR, "create_video_encoders - canvas_index %d out of range (canvases.size()=%zu) - encoder: %zu", config.canvas_index,
			     canvases.size(), i);
			return false;
		}

		obs_video_info *ovi = canvases[config.canvas_index];
		if (!ovi) {
			blog(LOG_ERROR, "create_video_encoders - null canvas at index %d - encoder: %zu", config.canvas_index, i);
			return false;
		}

		auto encoder = create_video_encoder(video_encoder_name_buffer, i, config, ovi);
		if (!encoder) {
			blog(LOG_ERROR, "create_video_encoders - failed to create video encoder - i: %d", config.canvas_index);
			return false;
		}

		if (obs_get_multiple_rendering()) {
			obs_encoder_set_video_mix(encoder, obs_video_mix_get(ovi, OBS_STREAMING_VIDEO_RENDERING));
		}

		if (!obs_encoder_set_group(encoder, encoder_group.get())) {
			blog(LOG_ERROR, "create_video_encoders - failed to set video encoder group - i: %d", config.canvas_index);
			return false;
		}

		obs_output_set_video_encoder2(output, encoder, i);
	}

	video_encoder_group = encoder_group;
	return true;
}

static OBSEncoderAutoRelease create_audio_encoder(const char *name, const char *audio_encoder_id, obs_data_t *settings, size_t mixer_idx)
{
	OBSEncoderAutoRelease audio_encoder = obs_audio_encoder_create(audio_encoder_id, name, settings, mixer_idx, nullptr);

	if (!audio_encoder) {
		blog(LOG_ERROR, "Failed to create audio encoder");
		throw std::runtime_error("Failed to start stream. Failed to create audio encoder");
	}

	obs_encoder_set_audio(audio_encoder, obs_get_audio());
	return audio_encoder;
}

static void create_audio_encoders(const Config &go_live_config, std::vector<OBSEncoderAutoRelease> &audio_encoders, obs_output_t *output,
				  const char *audio_encoder_id, size_t main_audio_mixer, std::optional<size_t> vod_track_mixer,
				  std::vector<speaker_layout> &speaker_layouts, speaker_layout &current_layout)
{
	speaker_layout speakers = SPEAKERS_UNKNOWN;
	obs_audio_info oai = {};
	if (obs_get_audio_info(&oai))
		speakers = oai.speakers;

	current_layout = speakers;

	auto sanitize_audio_channels = [&](obs_encoder_t *encoder, uint32_t channels) {
		speaker_layout target_speakers = SPEAKERS_UNKNOWN;
		for (size_t i = 0; i <= (size_t)SPEAKERS_7POINT1; i++) {
			if (get_audio_channels((speaker_layout)i) != channels)
				continue;

			target_speakers = (speaker_layout)i;
			break;
		}
		if (target_speakers == SPEAKERS_UNKNOWN) {
			blog(LOG_WARNING,
			     "MultitrackVideoOutput: Could not find "
			     "speaker layout for %" PRIu32 "channels "
			     "while configuring encoder '%s'",
			     channels, obs_encoder_get_name(encoder));
			return;
		}
		if (speakers != SPEAKERS_UNKNOWN && (channels > get_audio_channels(speakers) || speakers == target_speakers))
			return;

		auto it = std::find(std::begin(speaker_layouts), std::end(speaker_layouts), target_speakers);
		if (it == std::end(speaker_layouts))
			speaker_layouts.push_back(target_speakers);
	};

	using encoder_configs_type = decltype(go_live_config.audio_configurations.live);
	DStr encoder_name_buffer;
	size_t output_encoder_index = 0;

	auto create_encoders = [&](const char *name_prefix, const encoder_configs_type &configs, size_t mixer_idx) {
		if (configs.empty()) {
			blog(LOG_WARNING, "MultitrackVideoOutput: Missing audio encoder configurations (for '%s')", name_prefix);
			throw std::runtime_error("Failed to start stream. Missing audio encoder configurations (for '" + std::string(name_prefix) + "')");
		}

		for (size_t i = 0; i < configs.size(); i++) {
			dstr_printf(encoder_name_buffer, "%s %zu", name_prefix, i);
			OBSDataAutoRelease settings = obs_data_create_from_json(configs[i].settings.dump().c_str());
			OBSEncoderAutoRelease audio_encoder = create_audio_encoder(encoder_name_buffer->array, audio_encoder_id, settings, mixer_idx);

			sanitize_audio_channels(audio_encoder, configs[i].channels);

			obs_output_set_audio_encoder(output, audio_encoder, output_encoder_index);
			output_encoder_index += 1;
			audio_encoders.emplace_back(std::move(audio_encoder));
		}
	};

	create_encoders("multitrack video live audio", go_live_config.audio_configurations.live, main_audio_mixer);

	if (!vod_track_mixer.has_value())
		return;

	// we already check for empty inside of `create_encoders`
	encoder_configs_type empty = {};
	create_encoders("multitrack video vod audio", go_live_config.audio_configurations.vod.value_or(empty), *vod_track_mixer);

	return;
}

static OBSOutputAutoRelease create_output()
{
	OBSOutputAutoRelease output = obs_output_create("rtmp_output", "rtmp multitrack video", nullptr, nullptr);

	if (!output) {
		blog(LOG_ERROR, "Failed to create multitrack video rtmp output");
		throw std::runtime_error("Failed to start stream. Failed to create multitrack video rtmp output");
	}

	return output;
}

OBSOutputAutoRelease SetupOBSOutput(const std::string &multitrack_video_name, const Config &go_live_config, std::vector<OBSEncoderAutoRelease> &audio_encoders,
				    std::shared_ptr<obs_encoder_group_t> &video_encoder_group, const char *audio_encoder_id, size_t main_audio_mixer,
				    std::optional<size_t> vod_track_mixer, const std::vector<obs_video_info *> &canvases)
{

	auto output = create_output();

	if (!create_video_encoders(go_live_config, video_encoder_group, output, canvases))
		return nullptr;

	std::vector<speaker_layout> requested_speaker_layouts;
	speaker_layout current_layout = SPEAKERS_UNKNOWN;
	create_audio_encoders(go_live_config, audio_encoders, output, audio_encoder_id, main_audio_mixer, vod_track_mixer, requested_speaker_layouts,
			      current_layout);

	// TODO: in case of issues we need to port this from OBS
	// handle_speaker_layout_issues(parent, multitrack_video_name, requested_speaker_layouts, current_layout);

	return output;
}

OBSServiceAutoRelease create_service(const Config &go_live_config, const std::optional<std::string> &rtmp_url, const std::string &in_stream_key)
{
	const char *url = nullptr;
	auto stream_key = in_stream_key;

	const auto &ingest_endpoints = go_live_config.ingest_endpoints;

	for (auto &endpoint : ingest_endpoints) {
		if (strncasecmp("RTMP", endpoint.protocol.c_str(), 4))
			continue;

		url = endpoint.url_template.c_str();
		if (endpoint.authentication && !endpoint.authentication->empty()) {
			blog(LOG_INFO, "Using stream key supplied by autoconfig");
			stream_key = *endpoint.authentication;
		}
		break;
	}

	if (rtmp_url.has_value()) {
		// Despite being set by user, it was set to a ""
		if (rtmp_url->empty()) {
			throw std::runtime_error("Failed to start stream. Custom RTMP URL is empty");
		}

		url = rtmp_url->c_str();
		blog(LOG_INFO, "Using custom RTMP URL: '%s'", url);
	} else {
		if (!url) {
			blog(LOG_ERROR, "No RTMP URL in go live config");
			throw std::runtime_error("Failed to start stream. No RTMP URL in go live config");
		}

		blog(LOG_INFO, "Using URL template: '%s'", url);
	}

	DStr str;
	dstr_cat(str, url);

	// dstr_find does not protect against null, and dstr_cat will
	// not initialize str if cat'ing with a null url
	if (!dstr_is_empty(str)) {
		auto found = dstr_find(str, "/{stream_key}");
		if (found)
			dstr_remove(str, found - str->array, str->len - (found - str->array));
	}

	std::string final_url = str->array;
	if (!go_live_config.meta.config_id.empty()) {
		final_url += "?";
		final_url += "clientConfigId=";
		final_url += go_live_config.meta.config_id;
	}

	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "server", final_url.c_str());
	obs_data_set_string(settings, "key", stream_key.c_str());

	return obs_service_create("rtmp_custom", "multitrack video service", settings, nullptr);
	;
}

}
