#include "osn-multitrack-video.hpp"

#include "osn-audio-bitrate.hpp"

#include "nodeobs_configManager.hpp"
#include <util/config-file.h>

namespace osn {

bool IsMultitrackVideoEnabled()
{
	return config_get_bool(ConfigManager::getInstance().getBasic(), "EnhancedBroadcasting", "EnableMultitrackVideo");
}

int GetMultitrackAudioBitrate()
{
	const char *audio_encoder_id = config_get_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "StreamAudioEncoder");
	const int bitrate = (int)config_get_uint(ConfigManager::getInstance().getBasic(), "SimpleOutput", "ABitrate");

	if (audio_encoder_id && strcmp(audio_encoder_id, "opus") == 0)
		return osn::FindClosestAvailableSimpleOpusBitrate(bitrate);

	return osn::FindClosestAvailableSimpleAACBitrate(bitrate);
}

}