#pragma once

#include "osn-multitrack-video-data-model.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace osn {

struct EnhancedBroadcastingDisplayStats {
	double kbitsPerSec = 0;
	double dataOutput = 0;
};

struct EnhancedBroadcastingPerDisplayStats {
	EnhancedBroadcastingDisplayStats horizontal;
	EnhancedBroadcastingDisplayStats vertical;
};

void enhanced_broadcasting_display_stats_callback(obs_output_t *, encoder_packet *packet, encoder_packet_time *, void *param);

class EnhancedBroadcastingDisplayStatsTracker {
public:
	explicit EnhancedBroadcastingDisplayStatsTracker(const Config &config);

	void AddVideoPacketBytes(size_t trackIndex, size_t bytes);

	EnhancedBroadcastingPerDisplayStats CalculateStats();

private:
	struct DisplayCounter {
		std::atomic<uint64_t> totalBytes{0};
		std::atomic<uint64_t> lastBytesSent{0};
		std::atomic<uint64_t> lastBytesSentTime{0};
	};

	EnhancedBroadcastingDisplayStats CalculateDisplayStats(size_t canvasIndex);

	std::vector<uint32_t> trackCanvasIndices;
	std::array<DisplayCounter, 2> counters;
};

}
