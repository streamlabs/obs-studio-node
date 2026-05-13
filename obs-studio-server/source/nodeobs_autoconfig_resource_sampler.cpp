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

#include "nodeobs_autoconfig_resource_sampler.h"

#include <obs.h>
#include <util/platform.h>

#include <algorithm>
#include <cmath>

namespace autoConfig {

namespace {
constexpr uint64_t kMiB = 1024ULL * 1024ULL;
}

ResourceSampler::ResourceSampler()
{
	cpuInfo_ = os_cpu_usage_info_start();

#ifdef _WIN32
	// Pick the discrete GPU when present by iterating adapters and choosing the
	// one with the largest dedicated VRAM. EnumAdapterByGpuPreference would be
	// cleaner but lives on IDXGIFactory6 (dxgi1_6.h); the manual scan avoids
	// the SDK version dependency.
	Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
		return;

	Microsoft::WRL::ComPtr<IDXGIAdapter1> best;
	SIZE_T bestVram = 0;
	for (UINT i = 0;; ++i) {
		Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
		if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND)
			break;
		DXGI_ADAPTER_DESC1 desc{};
		if (FAILED(adapter->GetDesc1(&desc)))
			continue;
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			continue;
		if (desc.DedicatedVideoMemory > bestVram) {
			bestVram = desc.DedicatedVideoMemory;
			best = adapter;
		}
	}
	if (!best)
		return;

	Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
	if (FAILED(best.As(&adapter3)))
		return;
	dxgiAdapter_ = adapter3;
	gpuAvailable_ = true;
#endif
}

ResourceSampler::~ResourceSampler()
{
	if (worker_.joinable()) {
		workerStop_.store(true, std::memory_order_relaxed);
		worker_.join();
	}
	if (cpuInfo_) {
		os_cpu_usage_info_destroy(cpuInfo_);
		cpuInfo_ = nullptr;
	}
}

void ResourceSampler::start(const std::string &phase, std::chrono::milliseconds interval)
{
	phase_ = phase;
	startTime_ = std::chrono::steady_clock::now();
	{
		std::lock_guard<std::mutex> lk(aggMutex_);
		samples_.clear();
		// Pre-reserve enough for typical phase durations (5s bandwidth + buffer)
		// at the 250ms cadence we use for background sampling.
		samples_.reserve(32);
	}
	started_ = true;

	// First query after start returns no useful delta — discard it so the
	// first real sample() call is meaningful.
	if (cpuInfo_)
		(void)os_cpu_usage_info_query(cpuInfo_);

	if (interval.count() > 0) {
		// Take one sample synchronously before spawning the worker so a phase
		// that completes faster than the worker's first scheduling slice still
		// produces at least one data point.
		sample();
		workerStop_.store(false, std::memory_order_relaxed);
		worker_ = std::thread(&ResourceSampler::workerLoop, this, interval);
	}
}

void ResourceSampler::sample()
{
	if (!started_)
		return;

	ResourceSample s;
	// os_cpu_usage_info_query returns NaN when called too soon after start()
	// (zero time delta) — treat that as 0% so downstream JSON stays numeric.
	double cpu = cpuInfo_ ? os_cpu_usage_info_query(cpuInfo_) : 0.0;
	s.cpuPct = (std::isnan(cpu) || std::isinf(cpu) || cpu < 0.0) ? 0.0 : cpu;
	s.procRamMB = static_cast<double>(os_get_proc_resident_size()) / static_cast<double>(kMiB);

#ifdef _WIN32
	if (dxgiAdapter_) {
		DXGI_QUERY_VIDEO_MEMORY_INFO info{};
		if (SUCCEEDED(dxgiAdapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
			s.gpuVramUsedMB = info.CurrentUsage / kMiB;
			s.gpuVramBudgetMB = info.Budget / kMiB;
		}
	}
#endif

	std::lock_guard<std::mutex> lk(aggMutex_);
	samples_.push_back(s);
}

void ResourceSampler::workerLoop(std::chrono::milliseconds interval)
{
	while (!workerStop_.load(std::memory_order_relaxed)) {
		sample();
		// Sleep in small slices so stop() returns promptly when the test ends.
		auto end = std::chrono::steady_clock::now() + interval;
		while (std::chrono::steady_clock::now() < end) {
			if (workerStop_.load(std::memory_order_relaxed))
				return;
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
	}
}

// Nearest-rank percentile on a sorted series. p is in [0, 100]. Returns the
// element at position ceil(p/100 * N) - 1, clamped — so p95 of 20 samples
// returns the 19th-of-20 sample, dropping a single top outlier.
template<typename T> static T percentileNearestRank(std::vector<T> sorted, double p)
{
	if (sorted.empty())
		return T{};
	std::sort(sorted.begin(), sorted.end());
	size_t n = sorted.size();
	double rank = (p / 100.0) * static_cast<double>(n);
	size_t idx = rank <= 0.0 ? 0 : static_cast<size_t>(std::ceil(rank)) - 1;
	if (idx >= n)
		idx = n - 1;
	return sorted[idx];
}

ResourceWindow ResourceSampler::stop()
{
	if (worker_.joinable()) {
		workerStop_.store(true, std::memory_order_relaxed);
		worker_.join();
	}

	ResourceWindow w;
	w.phase = phase_;
	w.durationMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime_).count());
	w.gpuAvailable = gpuAvailable_;

	std::lock_guard<std::mutex> lk(aggMutex_);
	w.sampleCount = static_cast<int>(samples_.size());
	if (!samples_.empty()) {
		// Sort each component independently — CPU's p95 sample is rarely the
		// same physical sample as RAM's p95.
		std::vector<double> cpu, ram;
		std::vector<uint64_t> gpuUsed, gpuBudget;
		cpu.reserve(samples_.size());
		ram.reserve(samples_.size());
		gpuUsed.reserve(samples_.size());
		gpuBudget.reserve(samples_.size());
		for (auto &s : samples_) {
			cpu.push_back(s.cpuPct);
			ram.push_back(s.procRamMB);
			gpuUsed.push_back(s.gpuVramUsedMB);
			gpuBudget.push_back(s.gpuVramBudgetMB);
		}
		w.p50Sample.cpuPct = percentileNearestRank(cpu, 50.0);
		w.p50Sample.procRamMB = percentileNearestRank(ram, 50.0);
		w.p50Sample.gpuVramUsedMB = percentileNearestRank(gpuUsed, 50.0);
		w.p50Sample.gpuVramBudgetMB = percentileNearestRank(gpuBudget, 50.0);
		w.p95Sample.cpuPct = percentileNearestRank(cpu, 95.0);
		w.p95Sample.procRamMB = percentileNearestRank(ram, 95.0);
		w.p95Sample.gpuVramUsedMB = percentileNearestRank(gpuUsed, 95.0);
		w.p95Sample.gpuVramBudgetMB = percentileNearestRank(gpuBudget, 95.0);
	}

	started_ = false;
	return w;
}

} // namespace autoConfig
