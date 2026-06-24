#pragma once

#include "osn-multitrack-video-configuration.hpp"
#include "osn-audio-bitrate.hpp"
#include "osn-multitrack-video-output.hpp"

#include "obs.hpp"

#include <memory>
#include <vector>

namespace osn {

class EnhancedBroadcastingDisplayStatsTracker;

struct EnhancedBroadcastOutputObjects {
	OBSOutputAutoRelease obsOutput;
	std::shared_ptr<obs_encoder_group_t> videoEncoderGroup;
	std::vector<OBSEncoderAutoRelease> audioEncoders;
	OBSServiceAutoRelease multitrackVideoService;
	std::shared_ptr<EnhancedBroadcastingDisplayStatsTracker> displayStatsTracker;
};

bool IsMultitrackVideoEnabled();

int GetMultitrackAudioBitrate();

}
