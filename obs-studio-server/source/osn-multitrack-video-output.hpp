#pragma once

#include "osn-multitrack-video-data-model.hpp"

#include <obs.hpp>

#include <optional>
#include <vector>
#include <string>
#include <memory>

namespace osn {

OBSOutputAutoRelease SetupOBSOutput(const std::string &multitrack_video_name, const Config &go_live_config, std::vector<OBSEncoderAutoRelease> &audio_encoders,
				    std::shared_ptr<obs_encoder_group_t> &video_encoder_group, const char *audio_encoder_id, size_t main_audio_mixer,
				    std::optional<size_t> vod_track_mixer);

OBSServiceAutoRelease create_service(const Config &go_live_config, const std::optional<std::string> &rtmp_url, const std::string &in_stream_key);

}