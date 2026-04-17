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

#pragma once
#include <obs.h>

#include <optional>
#include <string>
#include <vector>

namespace osn {

template<typename BaseStreaming> class EnhancedBroadcasting : public BaseStreaming {
public:
	EnhancedBroadcasting() : BaseStreaming() {}

	virtual ~EnhancedBroadcasting() {}

	void StartEnhancedBroadcastingStream(std::optional<size_t> vod_track_mixer = std::nullopt)
	{
		blog(LOG_INFO, "StartEnhancedBroadcastingStream - service id: %s", obs_service_get_id(this->service));

		if (vod_track_mixer.has_value()) {
			blog(LOG_INFO, "vod_track_mixer: %d", vod_track_mixer.value());
		}

		const bool is_custom = strncmp("rtmp_custom", obs_service_get_type(this->service), 11) == 0;

		OBSDataAutoRelease settings = obs_service_get_settings(this->service);
		const std::string key = obs_data_get_string(settings, "key");

		const char *service_name = "<unknown>";
		if (is_custom && obs_data_has_user_value(settings, "service_name")) {
			service_name = obs_data_get_string(settings, "service_name");
		} else if (!is_custom) {
			service_name = obs_data_get_string(settings, "service");
		}

		std::optional<std::string> custom_rtmp_url;
		auto server = obs_data_get_string(settings, "server");
		if (strcmp(server, "auto") != 0) {
			custom_rtmp_url = server;
		}

		auto service_custom_server = obs_data_get_bool(settings, "using_custom_server");
		if (custom_rtmp_url.has_value()) {
			blog(LOG_INFO, "Using %s server '%s'", service_custom_server ? "custom " : "", custom_rtmp_url->c_str());
		}

		auto auto_config_url = osn::MultitrackVideoAutoConfigURL(this->service);
		blog(LOG_INFO, "Auto config URL: %s", auto_config_url.c_str());

		std::vector<obs_video_info *> canvases{this->GetCanvas()};
		if (this->GetAdditionalVideoCanvas()) {
			canvases.push_back(this->GetAdditionalVideoCanvas());
		}

		auto go_live_post = osn::constructGoLivePost(canvases, key, std::nullopt, std::nullopt, vod_track_mixer.has_value());
		std::optional<osn::Config> go_live_config = osn::DownloadGoLiveConfig(auto_config_url, go_live_post);
		if (!go_live_config.has_value()) {
			throw std::runtime_error("startStreaming - go live config is empty");
		}

		const auto audio_bitrate = osn::GetMultitrackAudioBitrate();
		const auto audio_encoder_id = osn::GetSimpleAACEncoderForBitrate(audio_bitrate);

		std::vector<OBSEncoderAutoRelease> audio_encoders;
		std::shared_ptr<obs_encoder_group_t> video_encoder_group;
		auto output = osn::SetupOBSOutput("Enhanced Broadcasting", go_live_config.value(), audio_encoders, video_encoder_group, audio_encoder_id, 0,
						  vod_track_mixer, canvases);
		if (!output) {
			throw std::runtime_error("startStreaming - failed to create multitrack output");
		}

		// Stream key is defined by config from Twitch
		auto multitrack_video_service = osn::create_service(*go_live_config, std::nullopt, "");
		if (!multitrack_video_service) {
			throw std::runtime_error("startStreaming - failed to create multitrack video service");
		}

		this->SetOutput(output.Get());
		obs_output_set_service(output, multitrack_video_service);

		std::string outputSettingsError;
		if (!this->ApplyOutputSettings(output.Get(), outputSettingsError)) {
			throw std::runtime_error(outputSettingsError);
		}

		// Register the BPM (Broadcast Performance Metrics) callback
		obs_output_add_packet_callback(output, bpm_inject, NULL);

		this->StartOutput();

		enhancedBroadcastingContext.emplace(EnhancedBroadcastOutputObjects{
			std::move(output),
			video_encoder_group,
			std::move(audio_encoders),
			std::move(multitrack_video_service),
		});
	}

	void StopEnhancedBroadcastingStream()
	{
		blog(LOG_INFO, "StopEnhancedBroadcastingStream - service id: %s", obs_service_get_id(this->service));

		auto output = this->GetOutput();
		if (!output) {
			blog(LOG_WARNING, "StopEnhancedBroadcastingStream - empty output");
			return;
		}

		obs_output_remove_packet_callback(output, bpm_inject, NULL);
		bpm_destroy(output);
	}

	obs_video_info *GetAdditionalVideoCanvas() { return additionalVideoContext; }

	void SetAdditionalVideoCanvas(obs_video_info *video) { additionalVideoContext = video; }

	void DeleteOutput() override
	{
		BaseStreaming::DeleteOutput();
		enhancedBroadcastingContext.reset();
	}

private:
	std::optional<osn::EnhancedBroadcastOutputObjects> enhancedBroadcastingContext;

	// If set, this context makes enhanced broadcasting stream the dual streaming modde
	obs_video_info *additionalVideoContext = nullptr;
};

}
