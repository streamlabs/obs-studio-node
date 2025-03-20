#pragma once

#include "osn-multitrack-video-data-model.hpp"

#include <obs.hpp>

#include <optional>
#include <string>

namespace osn {

std::string MultitrackVideoAutoConfigURL(obs_service_t *service);

Config DownloadGoLiveConfig(std::string url, const PostData &post_data);

PostData constructGoLivePost(std::string streamKey, const std::optional<uint64_t> &maximum_aggregate_bitrate,
			     const std::optional<uint32_t> &maximum_video_tracks, bool vod_track_enabled);

}