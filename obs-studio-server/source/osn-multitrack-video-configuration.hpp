#pragma once

#include "osn-multitrack-video-data-model.hpp"
#include "nodeobs_service.h"

#include <obs.hpp>

#include <optional>
#include <string>
#include <vector>

namespace osn {

std::string MultitrackVideoAutoConfigURL(obs_service_t *service);

Config DownloadGoLiveConfig(std::string url, const PostData &post_data);

PostData constructGoLivePost(std::vector<obs_video_info *> canvases, std::string streamKey, const std::optional<uint64_t> &maximum_aggregate_bitrate,
			     const std::optional<uint32_t> &maximum_video_tracks, bool vod_track_enabled);

}