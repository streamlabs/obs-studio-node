/******************************************************************************
    Copyright (C) 2016-2019 by Streamlabs (General Workings Inc)

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
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <dxgi.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#endif

struct os_cpu_usage_info;

namespace autoConfig {

// Single resource snapshot. cpuPct is the per-process CPU% reported by libOBS's
// os_cpu_usage_info; procRamMB is the resident set size in MiB. GPU VRAM fields
// are populated only on Windows when DXGI bring-up succeeded.
struct ResourceSample {
	double cpuPct = 0.0;
	double procRamMB = 0.0;
	uint64_t gpuVramUsedMB = 0;
	uint64_t gpuVramBudgetMB = 0;
};

// Aggregated samples for one autoconfig phase. Each component is sorted
// independently across the window and reduced to two percentiles:
//   p50 — typical value during the test
//   p95 — sustained ceiling, ignoring single-sample spikes from unrelated
//         OS noise (e.g. a background process briefly using CPU)
// We deliberately do NOT surface min/max/avg: max is dominated by one-off
// spikes that aren't caused by autoconfig, and avg is hard to act on.
struct ResourceWindow {
	std::string phase;
	int sampleCount = 0;
	int durationMs = 0;
	ResourceSample p50Sample;
	ResourceSample p95Sample;
	bool gpuAvailable = false;
};

// Sampler owns one libOBS CPU info handle and (Windows only) one DXGI adapter
// reference. Two usage modes:
//
//   1. Manual:     start(phase);              sample(); ... ; stop();
//   2. Background: start(phase, interval);    [worker samples periodically]; stop();
//
// In background mode start() spawns a thread that calls sample() every `interval`
// until stop() joins it. The bandwidth test uses mode 1 because it already has a
// 250ms wait loop; the encoder tests use mode 2 because their wait happens deep
// inside helpers we don't want to instrument.
//
// Don't share an instance across threads — sample() is not internally synchronized
// against external callers (the worker thread is the only sampler in mode 2).
class ResourceSampler {
public:
	ResourceSampler();
	~ResourceSampler();

	ResourceSampler(const ResourceSampler &) = delete;
	ResourceSampler &operator=(const ResourceSampler &) = delete;

	void start(const std::string &phase, std::chrono::milliseconds interval = std::chrono::milliseconds(0));
	void sample();
	ResourceWindow stop();

	bool gpuAvailable() const { return gpuAvailable_; }

private:
	void workerLoop(std::chrono::milliseconds interval);

	std::string phase_;
	std::chrono::steady_clock::time_point startTime_;

	std::mutex aggMutex_;
	std::vector<ResourceSample> samples_;

	os_cpu_usage_info *cpuInfo_ = nullptr;
	bool started_ = false;

	std::thread worker_;
	std::atomic<bool> workerStop_{false};

	bool gpuAvailable_ = false;
#ifdef _WIN32
	Microsoft::WRL::ComPtr<IDXGIAdapter3> dxgiAdapter_;
#endif
};

} // namespace autoConfig
