#include "osn-enhanced-broadcasting-display-stats-tracker.hpp"

#include <limits>

#include <util/platform.h>

namespace osn {

void enhanced_broadcasting_display_stats_callback(obs_output_t *, encoder_packet *packet, encoder_packet_time *, void *param)
{
	if (!packet || packet->type != OBS_ENCODER_VIDEO)
		return;

	auto *tracker = static_cast<EnhancedBroadcastingDisplayStatsTracker *>(param);
	if (!tracker)
		return;

	tracker->AddVideoPacketBytes(packet->track_idx, packet->size);
}

EnhancedBroadcastingDisplayStatsTracker::EnhancedBroadcastingDisplayStatsTracker(const Config &config)
{
	trackCanvasIndices.reserve(config.encoder_configurations.size());
	for (const auto &encoderConfig : config.encoder_configurations) {
		trackCanvasIndices.push_back(encoderConfig.canvas_index);
	}

	const uint64_t now = os_gettime_ns();
	for (auto &counter : counters) {
		counter.lastBytesSentTime.store(now);
	}
}

void EnhancedBroadcastingDisplayStatsTracker::AddVideoPacketBytes(size_t trackIndex, size_t bytes)
{
	if (trackIndex >= trackCanvasIndices.size())
		return;

	const uint32_t canvasIndex = trackCanvasIndices[trackIndex];
	if (canvasIndex >= counters.size())
		return;

	counters[canvasIndex].totalBytes.fetch_add(bytes, std::memory_order_relaxed);
}

EnhancedBroadcastingPerDisplayStats EnhancedBroadcastingDisplayStatsTracker::CalculateStats()
{
	return {CalculateDisplayStats(0), CalculateDisplayStats(1)};
}

EnhancedBroadcastingDisplayStats EnhancedBroadcastingDisplayStatsTracker::CalculateDisplayStats(size_t canvasIndex)
{
	EnhancedBroadcastingDisplayStats result;
	if (canvasIndex >= counters.size())
		return result;

	auto &counter = counters[canvasIndex];
	const uint64_t bytesSent = counter.totalBytes.load(std::memory_order_relaxed);
	const uint64_t bytesSentTime = os_gettime_ns();
	const uint64_t lastBytesSent = counter.lastBytesSent.exchange(bytesSent);
	const uint64_t lastBytesSentTime = counter.lastBytesSentTime.exchange(bytesSentTime);

	if (bytesSent >= lastBytesSent && lastBytesSentTime != 0) {
		const uint64_t bitsBetween = (bytesSent - lastBytesSent) * 8;
		const double timePassed = double(bytesSentTime - lastBytesSentTime) / 1000000000.0;
		if (timePassed > std::numeric_limits<double>::epsilon())
			result.kbitsPerSec = double(bitsBetween) / timePassed / 1000.0;
	}

	result.dataOutput = bytesSent / (1024.0 * 1024.0);
	return result;
}

}
