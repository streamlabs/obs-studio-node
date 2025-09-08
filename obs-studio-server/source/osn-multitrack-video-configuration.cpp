#include "osn-multitrack-video-configuration.hpp"
#include "osn-multitrack-video-system-info.hpp"

#include "util-censor.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <vector>
#include <memory>
#include <sstream>

namespace osn {

static auto curl_deleter = [](CURL *curl) { curl_easy_cleanup(curl); };

using Curl = std::unique_ptr<CURL, decltype(curl_deleter)>;

#ifdef CURLSSLOPT_REVOKE_BEST_EFFORT
#define CURL_OBS_REVOKE_SETTING CURLSSLOPT_REVOKE_BEST_EFFORT
#else
#define CURL_OBS_REVOKE_SETTING CURLSSLOPT_NO_REVOKE
#endif

#define curl_obs_set_revoke_setting(handle) curl_easy_setopt(handle, CURLOPT_SSL_OPTIONS, CURL_OBS_REVOKE_SETTING)

static size_t string_write(char *ptr, size_t size, size_t nmemb, std::string &str)
{
	size_t total = size * nmemb;
	if (total)
		str.append(ptr, total);

	return total;
}

static size_t header_write(char *ptr, size_t size, size_t nmemb, std::vector<std::string> &list)
{
	std::string str;

	size_t total = size * nmemb;
	if (total)
		str.append(ptr, total);

	if (str.back() == '\n')
		str.resize(str.size() - 1);
	if (str.back() == '\r')
		str.resize(str.size() - 1);

	list.push_back(std::move(str));
	return total;
}

static std::string GetOBSVersionString()
{
	std::stringstream ver;

	ver << LIBOBS_API_MAJOR_VER << "." << LIBOBS_API_MINOR_VER << "." << LIBOBS_API_PATCH_VER;

	return ver.str();
}

static bool GetRemoteFile(const char *url, std::string &str, std::string &error, long *responseCode = nullptr, const char *contentType = nullptr,
			  std::string request_type = "", const char *postData = nullptr, std::vector<std::string> extraHeaders = std::vector<std::string>(),
			  std::string *signature = nullptr, int timeoutSec = 0, bool fail_on_error = true, int postDataSize = 0)
{

	std::vector<std::string> header_in_list;
	char error_in[CURL_ERROR_SIZE];
	CURLcode code = CURLE_FAILED_INIT;

	error_in[0] = 0;

	std::string versionString("User-Agent: obs-basic ");
	versionString += GetOBSVersionString();

	std::string contentTypeString;
	if (contentType) {
		contentTypeString += "Content-Type: ";
		contentTypeString += contentType;
	}

	Curl curl{curl_easy_init(), curl_deleter};
	if (curl) {
		struct curl_slist *header = nullptr;

		header = curl_slist_append(header, versionString.c_str());

		if (!contentTypeString.empty()) {
			header = curl_slist_append(header, contentTypeString.c_str());
		}

		for (std::string &h : extraHeaders)
			header = curl_slist_append(header, h.c_str());

		curl_easy_setopt(curl.get(), CURLOPT_URL, url);
		curl_easy_setopt(curl.get(), CURLOPT_ACCEPT_ENCODING, "");
		curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, header);
		curl_easy_setopt(curl.get(), CURLOPT_ERRORBUFFER, error_in);
		if (fail_on_error)
			curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
		curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, string_write);
		curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &str);
		curl_obs_set_revoke_setting(curl.get());

		if (signature) {
			curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, header_write);
			curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &header_in_list);
		}

		if (timeoutSec)
			curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, timeoutSec);

		if (!request_type.empty()) {
			if (request_type != "GET")
				curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, request_type.c_str());

			// Special case of "POST"
			if (request_type == "POST") {
				curl_easy_setopt(curl.get(), CURLOPT_POST, 1);
				if (!postData)
					curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, "{}");
			}
		}
		if (postData) {
			if (postDataSize > 0) {
				curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, (long)postDataSize);
			}
			curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, postData);
		}

		code = curl_easy_perform(curl.get());
		if (responseCode)
			curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, responseCode);

		if (code != CURLE_OK) {
			error = strlen(error_in) ? error_in : curl_easy_strerror(code);
		} else if (signature) {
			for (std::string &h : header_in_list) {
				std::string name = h.substr(0, 13);
				// HTTP headers are technically case-insensitive
				if (name == "X-Signature: " || name == "x-signature: ") {
					*signature = h.substr(13);
					break;
				}
			}
		}

		curl_slist_free_all(header);
	}

	return code == CURLE_OK;
}

Config DownloadGoLiveConfig(std::string url, const PostData &post_data)
{
	nlohmann::json post_data_json = post_data;
	blog(LOG_INFO, "Go live POST data: %s", util::censoredJson(post_data_json).c_str());

	if (url.empty()) {
		throw std::runtime_error("Failed to start stream. Missing config URL");
	}

	std::string encodeConfigText;
	std::string libraryError;

	std::vector<std::string> headers;
	headers.push_back("Content-Type: application/json");
	bool encodeConfigDownloadedOk = GetRemoteFile(url.c_str(), encodeConfigText,
						      libraryError, // out params
						      nullptr,
						      nullptr, // out params (response code and content type)
						      "POST", post_data_json.dump().c_str(), headers,
						      nullptr, // signature
						      5);      // timeout in seconds

	if (!encodeConfigDownloadedOk) {
		throw std::runtime_error("Failed to start stream. Config request failed");
	}

	try {
		auto data = nlohmann::json::parse(encodeConfigText);
		blog(LOG_INFO, "Go live response data: %s", util::censoredJson(data, true).c_str());
		Config config = data;

		if (config.status) {
			const auto &status = *config.status;

			switch (status.result) {
			case StatusResult::Unknown:
				throw std::runtime_error("Failed to start stream. The reason is unknown");
			case StatusResult::Warning:
			case StatusResult::Error:
				throw std::runtime_error("Failed to start stream." + (status.html_en_us ? (" " + *status.html_en_us) : ""));
			default:
			case StatusResult::Success:
				// do nothing
				break;
			}
		}

		return config;

	} catch (const nlohmann::json::exception &e) {
		throw std::runtime_error("Failed to parse go live config: " + std::string(e.what()));
	}

	// This is actually an error state and should never happen
	return {};
}

std::string MultitrackVideoAutoConfigURL(obs_service_t *service)
{
	OBSDataAutoRelease settings = obs_service_get_settings(service);
	auto url = obs_data_get_string(settings, "multitrack_video_configuration_url");
	blog(LOG_INFO, "Go live URL: %s", url);
	return url;
}

PostData constructGoLivePost(std::string streamKey, const std::optional<uint64_t> &maximum_aggregate_bitrate,
			     const std::optional<uint32_t> &maximum_video_tracks, bool vod_track_enabled)
{
	PostData post_data{};
	post_data.service = "IVS";
	post_data.schema_version = "2025-01-25";
	post_data.authentication = streamKey;

	system_info(post_data.capabilities);

	auto &client = post_data.client;

	client.name = "obs-studio";
	client.version = obs_get_version_string();

	auto add_codec = [&](const char *codec) {
		auto it = std::find(std::begin(client.supported_codecs), std::end(client.supported_codecs), codec);
		if (it != std::end(client.supported_codecs))
			return;

		client.supported_codecs.push_back(codec);
	};

	const char *encoder_id = nullptr;
	for (size_t i = 0; obs_enum_encoder_types(i, &encoder_id); i++) {
		auto codec = obs_get_encoder_codec(encoder_id);
		if (!codec)
			continue;

		if (strcmp(codec, "h264") == 0) {
			add_codec("h264");
#ifdef ENABLE_HEVC
		} else if (qstricmp(codec, "hevc")) {
			add_codec("h265");
#endif
		} else if (strcmp(codec, "av1")) {
			add_codec("av1");
		}
	}

	auto &preferences = post_data.preferences;
	preferences.vod_track_audio = vod_track_enabled;

	obs_video_info ovi;
	if (obs_get_video_info(&ovi))
		preferences.composition_gpu_index = ovi.adapter;

	const size_t contexts = obs_get_video_info_count();
	for (size_t i = 0; i < contexts; i++) {
		if (obs_get_video_info_by_index(i, &ovi)) {
			preferences.canvases.emplace_back(Canvas{ovi.output_width,
									    ovi.output_height,
									    ovi.base_width,
									    ovi.base_height,
									    {ovi.fps_num, ovi.fps_den}});
		}
	}

	obs_audio_info2 oai2;
	if (obs_get_audio_info2(&oai2)) {
		preferences.audio_samples_per_sec = oai2.samples_per_sec;
		preferences.audio_channels = get_audio_channels(oai2.speakers);
		preferences.audio_fixed_buffering = oai2.fixed_buffering;
		preferences.audio_max_buffering_ms = oai2.max_buffering_ms;
	}

	if (maximum_aggregate_bitrate.has_value())
		preferences.maximum_aggregate_bitrate = maximum_aggregate_bitrate.value();

	if (maximum_video_tracks.has_value()) {
		/* Cap to maximum supported number of output encoders. */
		preferences.maximum_video_tracks =
			std::min(maximum_video_tracks.value(), static_cast<uint32_t>(MAX_OUTPUT_VIDEO_ENCODERS));
	}

	return post_data;
}

}