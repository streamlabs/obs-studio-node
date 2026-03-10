#pragma once

#include "osn-multitrack-video-configuration.hpp"
#include "osn-audio-bitrate.hpp"
#include "osn-multitrack-video-output.hpp"

#include "obs.hpp"

namespace osn {

struct EnhancedBroadcastOutputObjects {
	OBSOutputAutoRelease obsOutput;
	std::shared_ptr<obs_encoder_group_t> videoEncoderGroup;
	std::vector<OBSEncoderAutoRelease> audioEncoders;
	OBSServiceAutoRelease multitrackVideoService;
};

bool IsMultitrackVideoEnabled();

int GetMultitrackAudioBitrate();

}