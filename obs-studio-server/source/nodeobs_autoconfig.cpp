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

#include "nodeobs_autoconfig.h"
#include "nodeobs_autoconfig_resource_sampler.h"
#include <algorithm>
#include <array>
#include <future>
#include <set>
#include <sstream>
#include "osn-error.hpp"
#include "shared.hpp"
#include "osn-encoders.hpp"
#include <util/platform.h>
#include <util/dstr.h>

#include "osn-service.hpp"
#include "osn-simple-streaming.hpp"
#include "osn-advanced-streaming.hpp"
#include "osn-streaming-helpers.hpp"
#include "osn-recording.hpp"
#include "osn-video.hpp"
#include <cmath>

enum class Type { Invalid, Streaming, Recording };

enum class Service { Twitch, Hitbox, Beam, YouTube, Other };

enum class Encoder { x264, NVENC, QSV, AMD, Apple, Stream };

// Forward decl — defined further down. Needed by GetAutoConfigSummary and the
// TestStreamEncoderThread encoder_detection event push, both of which sit above
// the definition site.
static inline const char *GetEncoderId(Encoder enc);

enum class Quality { Stream, High };

enum class FPSType : int { PreferHighFPS, PreferHighRes, UseCurrent, fps30, fps60 };

enum ThreadedTests : int { BandwidthTest, StreamEncoderTest, RecordingEncoderTest, SaveStreamSettings, SaveSettings, SetDefaultSettings, Count };

class AutoConfigInfo {
public:
	AutoConfigInfo(const std::string &a_event, const std::string &a_description, double a_percentage, const std::string &a_payload = "")
	{
		event = a_event;
		description = a_description;
		percentage = a_percentage;
		payload = a_payload;
	};
	~AutoConfigInfo(){};

	std::string event;
	std::string description;
	double percentage;
	// Optional JSON payload for new event types (bandwidth_result, selection_decision,
	// video_decision, encoder_detection). Legacy events leave it empty. Surfaced as
	// the 5th rval of Query() — legacy frontends read 4 fields and ignore it.
	std::string payload;
};

std::array<std::future<void>, ThreadedTests::Count> asyncTests;
std::mutex eventsMutex;
std::queue<AutoConfigInfo> events;

// Per-run context. One autoconfig run at a time. Targets are passed in by the
// frontend via InitializeAutoConfig; chosen values are stored here as each stage
// runs and are pushed to the live objects in applyResults().
struct AutoconfigRun {
	// Streaming targets the frontend asked us to run autoconfig against.
	// Populated from InitializeAutoConfig's argument and consumed by the
	// bandwidth test / apply phase. Empty means no targets were provided.
	std::vector<uint64_t> targetStreamingIds;

	// Inputs / options.
	Type type = Type::Streaming;
	FPSType fpsType = FPSType::PreferHighFPS;
	bool preferHardware = true;
	bool preferHighFPS = true;
	bool bandwidthTest = true;
	bool customServer = false;
	int specificFPSNum = 0;
	int specificFPSDen = 0;
	uint64_t baseResolutionCX = 1920;
	uint64_t baseResolutionCY = 1080;
	int startingBitrate = 4500;

	// Detected encoder availability (filled by TestHardwareEncoding).
	bool hardwareEncodingAvailable = false;
	bool nvencAvailable = false;
	bool qsvAvailable = false;
	bool vceAvailable = false;
	bool appleAvailable = false;
	bool softwareTested = false;

	// Chosen values (filled by each test stage; consumed by applyResults).
	Quality recordingQuality = Quality::Stream;
	Encoder recordingEncoder = Encoder::Stream;
	Encoder streamingEncoder = Encoder::x264;
	uint64_t idealBitrate = 4500;
	uint64_t idealResolutionCX = 1280;
	uint64_t idealResolutionCY = 720;
	int idealFPSNum = 60;
	int idealFPSDen = 1;
	std::string server;

	struct TargetResult {
		uint64_t streamingId = UINT64_MAX;
		uint64_t idealBitrate = 0;
		std::string server;
	};
	std::vector<TargetResult> targetResults;

	// Per-target bandwidth-test diagnostics. Captured in TestBandwidthThreadV2
	// and surfaced via the bandwidth_result event + GetAutoConfigSummary IPC.
	struct BandwidthDetail {
		uint64_t targetId = UINT64_MAX;
		int testBitrate = 0;
		int platformCapProbed = 0;
		uint64_t measuredKbps = 0;
		int droppedFrames = 0;
		int totalFrames = 0;
		uint64_t totalBytes = 0;
		int elapsedMs = 0;
		std::string serverTested;
	};
	std::vector<BandwidthDetail> bandwidthDetails;

	// Per-target selection breakdown. Captured in applyResults; surfaced via the
	// selection_decision event + summary IPC.
	struct SelectionDetail {
		uint64_t targetId = UINT64_MAX;
		int userBitrate = 0;
		uint64_t heuristic = 0;
		uint64_t choseBeforeCaps = 0;
		uint64_t afterMeasuredCap = 0;
		uint64_t afterPlatformCap = 0;
		uint64_t picked = 0;
		std::string bindingCap;
		std::string appliedServer;
		std::string currentEncoderId;
		std::string chosenEncoderId;
		bool encoderChanged = false;
	};
	std::vector<SelectionDetail> selectionDetails;

	// Per-phase resource samples (CPU%, process RAM, optionally GPU VRAM).
	// Captured by ResourceSampler around the bandwidth and encoder test phases;
	// surfaced via the resource_usage event + summary IPC. Pure telemetry — no
	// influence on the selection heuristics in this version.
	std::vector<autoConfig::ResourceWindow> resourceWindows;

	// Per-canvas video-context decision. Captured in applyResults.
	struct VideoDecision {
		void *contextPtr = nullptr;
		uint32_t cxBefore = 0, cyBefore = 0;
		uint32_t fpsNumBefore = 0, fpsDenBefore = 0;
		uint32_t cxAfter = 0, cyAfter = 0;
		uint32_t fpsNumAfter = 0, fpsDenAfter = 0;
		int obsSetVideoInfoRet = 0;
		bool skipped = false;
	};
	std::vector<VideoDecision> videoDecisions;

	// True once SaveSettings() finished. GetAutoConfigSummary uses it to set the
	// JSON's "complete" flag so the POC UI can tell whether the data is final.
	bool runComplete = false;
};

static AutoconfigRun runContext;

std::condition_variable cv;
std::mutex m;
bool cancel = false;
bool started = false;

struct ServerInfo {
	std::string name;
	std::string address;
	int bitrate = 0;
	int ms = -1;
	size_t targetIndex = 0;

	inline ServerInfo() {}

	inline ServerInfo(const char *name_, const char *address_) : name(name_), address(address_) {}
};
void autoConfig::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("AutoConfig");

	cls->register_function(std::make_shared<ipc::function>("InitializeAutoConfig", std::vector<ipc::type>{ipc::type::Binary}, autoConfig::InitializeAutoConfig));
	cls->register_function(std::make_shared<ipc::function>("StartBandwidthTest", std::vector<ipc::type>{}, autoConfig::StartBandwidthTest));
	cls->register_function(std::make_shared<ipc::function>("StartStreamEncoderTest", std::vector<ipc::type>{}, autoConfig::StartStreamEncoderTest));
	cls->register_function(std::make_shared<ipc::function>("StartRecordingEncoderTest", std::vector<ipc::type>{}, autoConfig::StartRecordingEncoderTest));
	cls->register_function(std::make_shared<ipc::function>("StartCheckSettings", std::vector<ipc::type>{}, autoConfig::StartCheckSettings));
	cls->register_function(std::make_shared<ipc::function>("StartSetDefaultSettings", std::vector<ipc::type>{}, autoConfig::StartSetDefaultSettings));
	cls->register_function(std::make_shared<ipc::function>("StartSaveStreamSettings", std::vector<ipc::type>{}, autoConfig::StartSaveStreamSettings));
	cls->register_function(std::make_shared<ipc::function>("StartSaveSettings", std::vector<ipc::type>{}, autoConfig::StartSaveSettings));
	cls->register_function(std::make_shared<ipc::function>("TerminateAutoConfig", std::vector<ipc::type>{}, autoConfig::TerminateAutoConfig));
	cls->register_function(std::make_shared<ipc::function>("Query", std::vector<ipc::type>{}, autoConfig::Query));
	cls->register_function(std::make_shared<ipc::function>("GetAutoConfigSummary", std::vector<ipc::type>{}, autoConfig::GetAutoConfigSummary));

	srv.register_collection(cls);
}

void autoConfig::WaitPendingTests(double timeout)
{
	clock_t start_time = clock();
	while ((float(clock() - start_time) / CLOCKS_PER_SEC) < timeout) {

		bool all_finished = true;
		for (auto &async_test : asyncTests) {
			if (async_test.valid()) {
				auto status = async_test.wait_for(std::chrono::milliseconds(0));
				if (status != std::future_status::ready) {
					all_finished = false;
				}
			}
		}

		if (all_finished)
			break;

		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
}

// Serialize a ResourceWindow to JSON and emit a resource_usage event. The window
// is also pushed onto runContext.resourceWindows so GetAutoConfigSummary can
// re-emit it later. Frontends can consume either the event stream or the summary.
static std::string resourceWindowToJson(const autoConfig::ResourceWindow &w)
{
	obs_data_t *root = obs_data_create();
	obs_data_set_string(root, "phase", w.phase.c_str());
	obs_data_set_int(root, "sampleCount", w.sampleCount);
	obs_data_set_int(root, "durationMs", w.durationMs);

	// p50 is the typical value during the window; p95 is the sustained ceiling
	// after dropping single-sample outliers (a background process briefly using
	// CPU shouldn't dominate the report).
	auto putPct = [&](const char *key, double p50, double p95) {
		obs_data_t *o = obs_data_create();
		obs_data_set_double(o, "p50", p50);
		obs_data_set_double(o, "p95", p95);
		obs_data_set_obj(root, key, o);
		obs_data_release(o);
	};
	auto putPctInt = [&](obs_data_t *parent, const char *key, uint64_t p50, uint64_t p95) {
		obs_data_t *o = obs_data_create();
		obs_data_set_int(o, "p50", (long long)p50);
		obs_data_set_int(o, "p95", (long long)p95);
		obs_data_set_obj(parent, key, o);
		obs_data_release(o);
	};

	putPct("cpuPct", w.p50Sample.cpuPct, w.p95Sample.cpuPct);
	putPct("procRamMB", w.p50Sample.procRamMB, w.p95Sample.procRamMB);

	obs_data_t *gpu = obs_data_create();
	obs_data_set_bool(gpu, "available", w.gpuAvailable);
	if (w.gpuAvailable) {
		putPctInt(gpu, "vramUsedMB", w.p50Sample.gpuVramUsedMB, w.p95Sample.gpuVramUsedMB);
		// Budget is platform-driven and effectively constant across a window —
		// surface a single number rather than a percentile pair.
		obs_data_set_int(gpu, "vramBudgetMB", (long long)w.p95Sample.gpuVramBudgetMB);
	}
	obs_data_set_obj(root, "gpu", gpu);
	obs_data_release(gpu);

	std::string json = obs_data_get_json(root);
	obs_data_release(root);
	return json;
}

static void recordResourceWindow(const autoConfig::ResourceWindow &w)
{
	if (w.sampleCount <= 0)
		return;

	runContext.resourceWindows.push_back(w);

	std::string payload = resourceWindowToJson(w);
	std::lock_guard<std::mutex> lock(eventsMutex);
	events.push(AutoConfigInfo("resource_usage", w.phase, 100, payload));
}

void autoConfig::TestHardwareEncoding(void)
{
	size_t idx = 0;
	const char *id;
	while (obs_enum_encoder_types(idx++, &id)) {
		if (strcmp(id, ADVANCED_ENCODER_NVENC) == 0)
			runContext.hardwareEncodingAvailable = runContext.nvencAvailable = true;
		else if (strcmp(id, ADVANCED_ENCODER_QSV) == 0)
			runContext.hardwareEncodingAvailable = runContext.qsvAvailable = true;
		else if (strcmp(id, ADVANCED_ENCODER_AMD) == 0)
			runContext.hardwareEncodingAvailable = runContext.vceAvailable = true;
#ifdef __APPLE__
		else if (strcmp(id, APPLE_HARDWARE_VIDEO_ENCODER_M1) == 0
#ifndef __aarch64__
			 && os_get_emulation_status() == true
#endif
		)
			if (__builtin_available(macOS 13.0, *))
				runContext.hardwareEncodingAvailable = runContext.appleAvailable = true;
#endif
	}
}

static inline void string_depad_key(std::string &key)
{
	while (!key.empty()) {
		char ch = key.back();
		if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
			key.pop_back();
		else
			break;
	}
}

// bool autoConfig::CanTestServer(const char *server)
// {
// 	if (!testRegions || (regionNA && regionSA && regionEU && regionAS && regionOC))
// 		return true;

// 	if (serviceSelected == Service::Twitch) {
// 		if (astrcmp_n(server, "NA:", 3) == 0 || astrcmp_n(server, "US West:", 8) == 0 || astrcmp_n(server, "US East:", 8) == 0 ||
// 		    astrcmp_n(server, "US Central:", 11) == 0) {
// 			return regionNA;
// 		} else if (astrcmp_n(server, "South America:", 14) == 0) {
// 			return regionSA;
// 		} else if (astrcmp_n(server, "EU:", 3) == 0) {
// 			return regionEU;
// 		} else if (astrcmp_n(server, "Asia:", 5) == 0) {
// 			return regionAS;
// 		} else if (astrcmp_n(server, "Australia:", 10) == 0) {
// 			return regionOC;
// 		} else {
// 			return true;
// 		}
// 	} else if (serviceSelected == Service::Hitbox) {
// 		if (strcmp(server, "Default") == 0) {
// 			return true;
// 		} else if (astrcmp_n(server, "US-West:", 8) == 0 || astrcmp_n(server, "US-East:", 8) == 0) {
// 			return regionNA;
// 		} else if (astrcmp_n(server, "South America:", 14) == 0) {
// 			return regionSA;
// 		} else if (astrcmp_n(server, "EU-", 3) == 0) {
// 			return regionEU;
// 		} else if (astrcmp_n(server, "South Korea:", 12) == 0 || astrcmp_n(server, "Asia:", 5) == 0 || astrcmp_n(server, "China:", 6) == 0) {
// 			return regionAS;
// 		} else if (astrcmp_n(server, "Oceania:", 8) == 0) {
// 			return regionOC;
// 		} else {
// 			return true;
// 		}
// 	} else if (serviceSelected == Service::Beam) {
// 		if (astrcmp_n(server, "US:", 3) == 0 || astrcmp_n(server, "Canada:", 7) || astrcmp_n(server, "Mexico:", 7)) {
// 			return regionNA;
// 		} else if (astrcmp_n(server, "Brazil:", 7) == 0) {
// 			return regionSA;
// 		} else if (astrcmp_n(server, "EU:", 3) == 0) {
// 			return regionEU;
// 		} else if (astrcmp_n(server, "South Korea:", 12) == 0 || astrcmp_n(server, "Asia:", 5) == 0 || astrcmp_n(server, "India:", 6) == 0) {
// 			return regionAS;
// 		} else if (astrcmp_n(server, "Australia:", 10) == 0) {
// 			return regionOC;
// 		} else {
// 			return true;
// 		}
// 	} else {
// 		return true;
// 	}

// 	return false;
// }

// void GetServers(std::vector<ServerInfo> &servers)
// {
// 	OBSData settings = obs_data_create();
// 	obs_data_release(settings);
// 	// obs_data_set_string(settings, "service", wiz->serviceName.c_str());
// 	//FIX ME
// 	obs_data_set_string(settings, "service", serviceName.c_str());

// 	obs_properties_t *ppts = obs_get_service_properties("rtmp_common");
// 	obs_property_t *p = obs_properties_get(ppts, "service");
// 	obs_property_modified(p, settings);

// 	p = obs_properties_get(ppts, "server");
// 	size_t count = obs_property_list_item_count(p);
// 	servers.reserve(count);

// 	for (size_t i = 0; i < count; i++) {
// 		const char *name = obs_property_list_item_name(p, i);
// 		const char *server = obs_property_list_item_string(p, i);

// 		if (autoConfig::CanTestServer(name)) {
// 			ServerInfo info(name, server);
// 			servers.push_back(info);
// 		}
// 	}

// 	obs_properties_destroy(ppts);
// }

void start_next_step(void (*task)(), std::string event, std::string description, int percentage)
{
	/*eventCallbackQueue.work_queue.push_back({cb, event, description, percentage});
    eventCallbackQueue.Signal();

    if(task)
    	std::thread(*task).detach();*/
}

void autoConfig::TerminateAutoConfig(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	StopThread();
	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void autoConfig::Query(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	std::unique_lock<std::mutex> ulock(eventsMutex);
	if (events.empty()) {
		rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
		AUTO_DEBUG;
		return;
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));

	rval.push_back(ipc::value(events.front().event));
	rval.push_back(ipc::value(events.front().description));
	rval.push_back(ipc::value(events.front().percentage));
	// 5th field: optional JSON payload for new event types. Empty for legacy
	// events. Legacy frontends read 4 fields and ignore this one.
	rval.push_back(ipc::value(events.front().payload));

	events.pop();

	AUTO_DEBUG;
}

void autoConfig::GetAutoConfigSummary(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	// Build a structured JSON summary of the most recent autoconfig run for the
	// new POC UI. Safe to call before `done` — `complete` flag indicates whether
	// the data is final. Reset to empty on InitializeAutoConfig.
	obs_data_t *root = obs_data_create();
	obs_data_set_bool(root, "complete", runContext.runComplete);

	// encoderDetection
	{
		const char *chosenStream = GetEncoderId(runContext.streamingEncoder);
		const char *chosenRecording = GetEncoderId(runContext.recordingEncoder);
		obs_data_t *enc = obs_data_create();
		obs_data_set_bool(enc, "hardwareEncodingAvailable", runContext.hardwareEncodingAvailable);
		obs_data_set_bool(enc, "nvenc", runContext.nvencAvailable);
		obs_data_set_bool(enc, "qsv", runContext.qsvAvailable);
		obs_data_set_bool(enc, "vce", runContext.vceAvailable);
		obs_data_set_bool(enc, "apple", runContext.appleAvailable);
		obs_data_set_bool(enc, "softwareTested", runContext.softwareTested);
		obs_data_set_string(enc, "chosenStreamingEncoder", chosenStream ? chosenStream : "");
		obs_data_set_string(enc, "chosenRecordingEncoder", chosenRecording ? chosenRecording : "");
		obs_data_set_string(enc, "recordingQuality", runContext.recordingQuality == Quality::High ? "High" : "Stream");
		obs_data_set_obj(root, "encoderDetection", enc);
		obs_data_release(enc);
	}

	// videoDecision
	{
		obs_data_t *video = obs_data_create();
		obs_data_t *chosen = obs_data_create();
		obs_data_set_int(chosen, "cx", (long long)runContext.idealResolutionCX);
		obs_data_set_int(chosen, "cy", (long long)runContext.idealResolutionCY);
		obs_data_set_int(chosen, "fpsNum", runContext.idealFPSNum);
		obs_data_set_int(chosen, "fpsDen", runContext.idealFPSDen);
		obs_data_set_obj(video, "chosen", chosen);
		obs_data_release(chosen);

		obs_data_array_t *perCanvas = obs_data_array_create();
		for (auto &vd : runContext.videoDecisions) {
			obs_data_t *item = obs_data_create();
			std::ostringstream ptrOss;
			ptrOss << "0x" << std::hex << reinterpret_cast<uintptr_t>(vd.contextPtr);
			obs_data_set_string(item, "contextPtr", ptrOss.str().c_str());

			obs_data_t *before = obs_data_create();
			obs_data_set_int(before, "cx", vd.cxBefore);
			obs_data_set_int(before, "cy", vd.cyBefore);
			obs_data_set_int(before, "fpsNum", vd.fpsNumBefore);
			obs_data_set_int(before, "fpsDen", vd.fpsDenBefore);
			obs_data_set_obj(item, "before", before);
			obs_data_release(before);

			obs_data_t *after = obs_data_create();
			obs_data_set_int(after, "cx", vd.cxAfter);
			obs_data_set_int(after, "cy", vd.cyAfter);
			obs_data_set_int(after, "fpsNum", vd.fpsNumAfter);
			obs_data_set_int(after, "fpsDen", vd.fpsDenAfter);
			obs_data_set_obj(item, "after", after);
			obs_data_release(after);

			obs_data_set_int(item, "obsSetVideoInfoRet", vd.obsSetVideoInfoRet);
			obs_data_set_bool(item, "skipped", vd.skipped);
			obs_data_array_push_back(perCanvas, item);
			obs_data_release(item);
		}
		obs_data_set_array(video, "perCanvas", perCanvas);
		obs_data_array_release(perCanvas);

		obs_data_set_obj(root, "videoDecision", video);
		obs_data_release(video);
	}

	// bandwidthTest
	{
		obs_data_t *bw = obs_data_create();
		obs_data_array_t *perTarget = obs_data_array_create();
		for (auto &bd : runContext.bandwidthDetails) {
			obs_data_t *item = obs_data_create();
			obs_data_set_int(item, "targetId", (long long)bd.targetId);
			obs_data_set_int(item, "testBitrate", bd.testBitrate);
			obs_data_set_int(item, "platformCapProbed", bd.platformCapProbed);
			obs_data_set_int(item, "measuredKbps", (long long)bd.measuredKbps);
			obs_data_set_int(item, "droppedFrames", bd.droppedFrames);
			obs_data_set_int(item, "totalFrames", bd.totalFrames);
			obs_data_set_int(item, "totalBytes", (long long)bd.totalBytes);
			obs_data_set_int(item, "elapsedMs", bd.elapsedMs);
			obs_data_set_string(item, "serverTested", bd.serverTested.c_str());
			obs_data_array_push_back(perTarget, item);
			obs_data_release(item);
		}
		obs_data_set_array(bw, "perTarget", perTarget);
		obs_data_array_release(perTarget);

		obs_data_set_obj(root, "bandwidthTest", bw);
		obs_data_release(bw);
	}

	// selection
	{
		obs_data_t *sel = obs_data_create();
		obs_data_array_t *perTarget = obs_data_array_create();
		for (auto &sd : runContext.selectionDetails) {
			obs_data_t *item = obs_data_create();
			obs_data_set_int(item, "targetId", (long long)sd.targetId);
			obs_data_set_int(item, "userBitrate", sd.userBitrate);
			obs_data_set_int(item, "heuristic", (long long)sd.heuristic);
			obs_data_set_int(item, "choseBeforeCaps", (long long)sd.choseBeforeCaps);
			obs_data_set_int(item, "afterMeasuredCap", (long long)sd.afterMeasuredCap);
			obs_data_set_int(item, "afterPlatformCap", (long long)sd.afterPlatformCap);
			obs_data_set_int(item, "picked", (long long)sd.picked);
			obs_data_set_string(item, "bindingCap", sd.bindingCap.c_str());
			obs_data_set_string(item, "appliedServer", sd.appliedServer.c_str());
			obs_data_set_string(item, "currentEncoderId", sd.currentEncoderId.c_str());
			obs_data_set_string(item, "chosenEncoderId", sd.chosenEncoderId.c_str());
			obs_data_set_bool(item, "encoderChanged", sd.encoderChanged);
			obs_data_array_push_back(perTarget, item);
			obs_data_release(item);
		}
		obs_data_set_array(sel, "perTarget", perTarget);
		obs_data_array_release(perTarget);

		obs_data_set_obj(root, "selection", sel);
		obs_data_release(sel);
	}

	// resourceUsage — per-phase CPU/RAM (and Windows-only GPU VRAM) samples
	// captured during the bandwidth and encoder test phases. Same JSON shape
	// as the resource_usage event payload.
	{
		obs_data_array_t *windows = obs_data_array_create();
		for (auto &w : runContext.resourceWindows) {
			std::string s = resourceWindowToJson(w);
			obs_data_t *item = obs_data_create_from_json(s.c_str());
			if (item) {
				obs_data_array_push_back(windows, item);
				obs_data_release(item);
			}
		}
		obs_data_set_array(root, "resourceUsage", windows);
		obs_data_array_release(windows);
	}

	std::string json = obs_data_get_json_pretty(root);
	obs_data_release(root);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value(json));
	AUTO_DEBUG;
}

void autoConfig::StopThread(void)
{
	std::unique_lock<std::mutex> ul(m);
	cancel = true;
	cv.notify_one();
}

void autoConfig::InitializeAutoConfig(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	runContext = AutoconfigRun{};
	cancel = false;

	// Drain leftover events from a prior run. Otherwise a stopping_step queued
	// by an aborted bandwidth thread (e.g. after TerminateAutoConfig) leaks into
	// the next session's first drainUntil() and confuses callers.
	{
		std::lock_guard<std::mutex> lock(eventsMutex);
		while (!events.empty())
			events.pop();
	}

	if (!args.empty()) {
		const std::vector<char> &bin = args[0].value_bin;
		size_t n = bin.size() / sizeof(uint64_t);
		runContext.targetStreamingIds.resize(n);
		if (n > 0)
			memcpy(runContext.targetStreamingIds.data(), bin.data(), n * sizeof(uint64_t));
	}

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void autoConfig::StartBandwidthTest(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	if (asyncTests[ThreadedTests::BandwidthTest].valid())
		asyncTests[ThreadedTests::BandwidthTest].wait();

	{
		std::lock_guard<std::mutex> lock(eventsMutex);
		while (!events.empty())
			events.pop();
	}

	asyncTests[ThreadedTests::BandwidthTest] = std::async(std::launch::async, TestBandwidthThreadV2);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void autoConfig::StartStreamEncoderTest(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	asyncTests[ThreadedTests::StreamEncoderTest] = std::async(std::launch::async, TestStreamEncoderThread);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void autoConfig::StartRecordingEncoderTest(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	asyncTests[ThreadedTests::RecordingEncoderTest] = std::async(std::launch::async, TestRecordingEncoderThread);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void autoConfig::StartSaveStreamSettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	asyncTests[ThreadedTests::SaveStreamSettings] = std::async(std::launch::async, SaveStreamSettings);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void autoConfig::StartSaveSettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	asyncTests[ThreadedTests::SaveSettings] = std::async(std::launch::async, SaveSettings);

	cancel = false;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void autoConfig::StartCheckSettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	bool sucess = CheckSettings();

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	rval.push_back(ipc::value((uint32_t)sucess));
	AUTO_DEBUG;
}

void autoConfig::StartSetDefaultSettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	asyncTests[ThreadedTests::SetDefaultSettings] = std::async(std::launch::async, SetDefaultSettings);

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

int EvaluateBandwidth(ServerInfo &server, bool &connected, bool &stopped, bool &success, bool &errorOnStop, OBSData &service_settings, OBSService &service,
		      OBSOutput &output, OBSData &vencoder_settings)
{
	connected = false;
	stopped = false;
	errorOnStop = false;

	obs_data_set_string(service_settings, "server", server.address.c_str());
	obs_service_update(service, service_settings);

	if (!obs_output_start(output))
		return -1;

	std::unique_lock<std::mutex> ul(m);
	if (cancel) {
		ul.unlock();
		obs_output_force_stop(output);
		return -1;
	}
	if (!stopped && !connected)
		cv.wait(ul);
	if (cancel) {
		ul.unlock();
		obs_output_force_stop(output);
		return -1;
	}
	if (!connected) {
		return -1;
	}

	uint64_t t_start = os_gettime_ns();

	//wait for start signal from output
	cv.wait_for(ul, std::chrono::seconds(10));
	if (stopped)
		return -1;
	if (cancel) {
		ul.unlock();
		obs_output_force_stop(output);
		return -1;
	}

	obs_output_stop(output);

	while (!obs_output_active(output)) {
		if (errorOnStop) {
			ul.unlock();
			obs_output_force_stop(output);
			return -1;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}

	//wait for stop signal from output
	cv.wait(ul);

	uint64_t total_time = os_gettime_ns() - t_start;
	int total_bytes = (int)obs_output_get_total_bytes(output);
	uint64_t bitrate = 0;

	if (total_time > 0) {
		bitrate = (uint64_t)total_bytes * 8U * 1000000000U / total_time / 1000U;
	}

	runContext.startingBitrate = (int)obs_data_get_int(vencoder_settings, "bitrate");
	if (obs_output_get_frames_dropped(output) || (int)bitrate < (runContext.startingBitrate * 75 / 100)) {
		server.bitrate = (int)bitrate * 70 / 100;
	} else {
		server.bitrate = runContext.startingBitrate;
	}

	server.ms = obs_output_get_connect_time_ms(output);
	success = true;

	//wait for deactivate signal from output
	cv.wait(ul);

	return 0;
}

void sendErrorMessage(const std::string &message)
{
	eventsMutex.lock();
	events.push(AutoConfigInfo("error", message, 0));
	eventsMutex.unlock();
}

int autoConfig::GetStartingBitrate(const std::string &serviceName)
{
	OBSData service_settings = obs_data_create();
	obs_data_release(service_settings);

	obs_data_set_string(service_settings, "service", serviceName.c_str());

	OBSService service = obs_service_create("rtmp_common", "temp_service", service_settings, nullptr);
	obs_service_release(service);

	int bitrate = 10000;

	OBSData settings = obs_data_create();
	obs_data_release(settings);
	obs_data_set_int(settings, "bitrate", bitrate);
	obs_service_apply_encoder_settings(service, settings, nullptr);

	int startingBitrate = (int)obs_data_get_int(settings, "bitrate");
	return startingBitrate;
}

void autoConfig::TestBandwidthThreadV2(void)
{
	bool gotError = false;
	std::vector<ServerInfo> testResults;

	{
		std::lock_guard<std::mutex> lock(eventsMutex);
		events.push(AutoConfigInfo("starting_step", "bandwidth_test", 0));
	}

	// Resolve the streaming targets the frontend passed to InitializeAutoConfig.
	// Skip ids that no longer resolve (object was destroyed between Initialize
	// and the bandwidth test) or that have no service set.
	std::vector<osn::Streaming *> targets;
	std::vector<uint64_t> targetIds;

	for (uint64_t uid : runContext.targetStreamingIds) {
		osn::Streaming *s = osn::IStreaming::Manager::GetInstance().find(uid);
		if (s && s->service) {
			targets.push_back(s);
			targetIds.push_back(uid);
		}
	}

	if (targets.empty()) {
		sendErrorMessage("no_streaming_targets_provided");
		gotError = true;
	}

	if (!gotError) {
		std::vector<osn::Streaming *> testingServices;
		std::vector<size_t> testingServiceTargetIdx;

		for (size_t i = 0; i < targets.size(); i++) {
			const char *type = osn::streaming_helpers::getStreamOutputType(targets[i]->service);
			if (!type)
				type = "rtmp_output";
			std::string outputName = "autoconfig_bw_" + std::to_string(i);
			targets[i]->CreateOutput(type, outputName);

			// Pick a high test bitrate so the measurement reflects the link's
			// real ceiling rather than whatever low value the user has set.
			//   - user's current bitrate (might already be high)
			//   - platform cap probed via obs_service_apply_encoder_settings
			//     (Twitch returns 6000 for non-partners; partners higher)
			//   - 6000 fallback when no platform hook exists (custom RTMP, etc.)
			int userBitrate = 0;
			if (targets[i]->videoEncoder) {
				obs_data_t *s = obs_encoder_get_settings(targets[i]->videoEncoder);
				userBitrate = (int)obs_data_get_int(s, "bitrate");
				obs_data_release(s);
			}
			int platformCap = 0;
			if (targets[i]->service) {
				obs_data_t *probe = obs_data_create();
				obs_data_set_int(probe, "bitrate", 50000);
				obs_service_apply_encoder_settings(targets[i]->service, probe, nullptr);
				int capped = (int)obs_data_get_int(probe, "bitrate");
				if (capped > 0 && capped < 50000)
					platformCap = capped;
				obs_data_release(probe);
			}
			int testBitrate = std::max({userBitrate, platformCap, 6000});
			blog(LOG_INFO, "TestBandwidthV2: target %zu test bitrate %d (user=%d, platformCap=%d)", i, testBitrate, userBitrate, platformCap);

			// Pre-record the test setup; measurement fields filled below.
			AutoconfigRun::BandwidthDetail bd;
			bd.targetId = targetIds[i];
			bd.testBitrate = testBitrate;
			bd.platformCapProbed = platformCap;
			runContext.bandwidthDetails.push_back(bd);

			targets[i]->testBandwidth(gotError, testBitrate);

			if (!gotError && targets[i]->GetOutput()) {
				testingServices.push_back(targets[i]);
				testingServiceTargetIdx.push_back(i);
			} else if (targets[i]->GetOutput() && obs_output_active(targets[i]->GetOutput())) {
				obs_output_stop(targets[i]->GetOutput());
			}
		}

		if (!gotError && !testingServices.empty()) {
			auto startTime = std::chrono::steady_clock::now();
			bool allConnected = false;

			// Wait up to 10 seconds for all services to connect or fail.
			while (!allConnected && !gotError && std::chrono::steady_clock::now() - startTime < std::chrono::seconds(10)) {
				allConnected = true;

				for (auto *streaming : testingServices) {
					// Surface a connection failure immediately; drained signals are
					// only supplemental to the real output state checked below.
					if (streaming->testQuery() == "error") {
						gotError = true;
						break;
					}

					// Primary readiness check: a target counts as connected only once
					// libobs reports the output active. Keying off drained signals alone
					// would treat a target that hasn't emitted one yet as ready, exit
					// the wait early, and measure totalBytes == 0.
					obs_output_t *output = streaming->GetOutput();
					if (!output || !obs_output_active(output))
						allConnected = false;
				}

				std::unique_lock<std::mutex> ul(m);
				if (cancel) {
					gotError = true;
					break;
				}
				ul.unlock();

				if (!allConnected && !gotError) {
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
				}
			}

			// Let the outputs stream for a few seconds so data accumulates
			// before we sample obs_output_get_total_bytes(). Without this
			// the loop above exits as soon as signals are drained (often
			// < 100 ms after the RTMP connection opens) and totalBytes is 0.
			int dataWaitMs = 0;
			autoConfig::ResourceSampler sampler;
			if (!gotError && allConnected) {
				const int targetWaitMs = 5000;
				auto dataStart = std::chrono::steady_clock::now();
				sampler.start("bandwidth");
				while (std::chrono::steady_clock::now() - dataStart < std::chrono::milliseconds(targetWaitMs)) {
					std::unique_lock<std::mutex> ul(m);
					if (cancel) {
						gotError = true;
						break;
					}
					ul.unlock();
					std::this_thread::sleep_for(std::chrono::milliseconds(250));
					sampler.sample();
				}
				dataWaitMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - dataStart).count();
				recordResourceWindow(sampler.stop());
			}

			if (!gotError) {
				for (size_t si = 0; si < testingServices.size(); si++) {
					auto *streaming = testingServices[si];
					if (streaming->GetOutput() && obs_output_active(streaming->GetOutput())) {
						uint64_t totalBytes = obs_output_get_total_bytes(streaming->GetOutput());
						int connectTimeMs = obs_output_get_connect_time_ms(streaming->GetOutput());
						int droppedFrames = obs_output_get_frames_dropped(streaming->GetOutput());
						int totalFrames = obs_output_get_total_frames(streaming->GetOutput());

						obs_output_stop(streaming->GetOutput());

						if (totalBytes > 0) {
							// connectTimeMs may be 0 for localhost; fall back
							// to the data-wait duration for bitrate estimation.
							int elapsedMs = connectTimeMs > 0 ? connectTimeMs : std::max(dataWaitMs, 1);
							uint64_t bitrate = (totalBytes * 8ULL * 1000ULL) / static_cast<uint64_t>(elapsedMs) / 1000ULL;

							std::string serverAddress;
							if (streaming->service) {
								serverAddress =
									obs_service_get_connect_info(streaming->service, OBS_SERVICE_CONNECT_INFO_SERVER_URL);
							}

							ServerInfo result;
							result.address = serverAddress;
							result.ms = elapsedMs;
							result.targetIndex = testingServiceTargetIdx[si];

							// Use the per-target test bitrate (still active on the encoder
							// until CleanTestMode runs below) as the reference, not the
							// global runContext.startingBitrate which doesn't reflect the
							// per-target ceiling-search override.
							int testBitrateRef = 0;
							if (streaming->videoEncoder) {
								obs_data_t *encSettings = obs_encoder_get_settings(streaming->videoEncoder);
								testBitrateRef = (int)obs_data_get_int(encSettings, "bitrate");
								obs_data_release(encSettings);
							}
							if (testBitrateRef <= 0)
								testBitrateRef = runContext.startingBitrate;

							if (droppedFrames > 0 || (int)bitrate < (testBitrateRef * 75 / 100)) {
								result.bitrate = (int)bitrate * 70 / 100;
							} else {
								result.bitrate = testBitrateRef;
							}

							testResults.push_back(result);

							// Fill the measurement side of the per-target detail record
							// (setup side was filled before testBandwidth) and emit the
							// bandwidth_result event with a JSON payload for the new UI.
							size_t targetIdx = testingServiceTargetIdx[si];
							if (targetIdx < runContext.bandwidthDetails.size()) {
								auto &bd = runContext.bandwidthDetails[targetIdx];
								bd.measuredKbps = bitrate;
								bd.droppedFrames = droppedFrames;
								bd.totalFrames = totalFrames;
								bd.totalBytes = totalBytes;
								bd.elapsedMs = elapsedMs;
								bd.serverTested = serverAddress;

								obs_data_t *p = obs_data_create();
								obs_data_set_int(p, "targetId", (long long)bd.targetId);
								obs_data_set_int(p, "testBitrate", bd.testBitrate);
								obs_data_set_int(p, "platformCapProbed", bd.platformCapProbed);
								obs_data_set_int(p, "measuredKbps", (long long)bd.measuredKbps);
								obs_data_set_int(p, "droppedFrames", bd.droppedFrames);
								obs_data_set_int(p, "totalFrames", bd.totalFrames);
								obs_data_set_int(p, "totalBytes", (long long)bd.totalBytes);
								obs_data_set_int(p, "elapsedMs", bd.elapsedMs);
								obs_data_set_string(p, "serverTested", bd.serverTested.c_str());
								std::string payload = obs_data_get_json(p);
								obs_data_release(p);

								std::lock_guard<std::mutex> lock(eventsMutex);
								events.push(AutoConfigInfo("bandwidth_result", "target_" + std::to_string(bd.targetId), 100,
											   payload));
							}
						}
					}
				}
			}
		}

		for (auto *streaming : targets) {
			streaming->CleanTestMode();
		}
	}

	if (!gotError) {
		if (testResults.empty()) {
			sendErrorMessage("no_valid_bandwidth_results");
			gotError = true;
		} else {
			// Build per-target results. Each target picks its best server
			// (highest bitrate, lowest latency).
			runContext.targetResults.clear();
			for (size_t ti = 0; ti < targetIds.size(); ti++) {
				std::vector<ServerInfo> targetSpecific;
				for (auto &r : testResults) {
					if (r.targetIndex == ti)
						targetSpecific.push_back(r);
				}
				if (targetSpecific.empty())
					continue;

				std::sort(targetSpecific.begin(), targetSpecific.end(), [](const ServerInfo &a, const ServerInfo &b) {
					return (a.bitrate > b.bitrate) || (a.bitrate == b.bitrate && a.ms < b.ms);
				});

				AutoconfigRun::TargetResult tr;
				tr.streamingId = targetIds[ti];
				tr.idealBitrate = targetSpecific.front().bitrate;
				tr.server = targetSpecific.front().address;
				runContext.targetResults.push_back(tr);
			}

			// Global idealBitrate = minimum across all targets (conservative
			// for shared canvas resolution/FPS selection).
			if (!runContext.targetResults.empty()) {
				uint64_t minBitrate = UINT64_MAX;
				for (auto &tr : runContext.targetResults) {
					if (tr.idealBitrate < minBitrate)
						minBitrate = tr.idealBitrate;
				}
				runContext.idealBitrate = minBitrate;
				runContext.server = runContext.targetResults[0].server;
			}
		}
	}

	{
		std::lock_guard<std::mutex> lock(eventsMutex);
		events.push(AutoConfigInfo("stopping_step", "bandwidth_test", 100));
	}
}

// Old implementation - commented out as it has compilation errors
// This will be removed after V2 development is finished
#ifdef OLD_BANDWIDTH_TEST //deprecated
void autoConfig::TestBandwidthThread(void)
{
	eventsMutex.lock();
	events.push(AutoConfigInfo("starting_step", "bandwidth_test", 0));
	eventsMutex.unlock();

	bool connected = false;
	bool stopped = false;
	bool errorOnStop = false;
	bool gotError = false;

	const char *serverType = "rtmp_common";

	OBSEncoder vencoder = obs_video_encoder_create(ADVANCED_ENCODER_X264, "test_x264", nullptr, nullptr);
	OBSEncoder aencoder = obs_audio_encoder_create("ffmpeg_aac", "test_aac", nullptr, 0, nullptr);
	OBSOutput output = obs_output_create("rtmp_output", "test_stream", nullptr, nullptr);

	/* -----------------------------------*/
	/* configure settings                 */

	// service: "service", "server", "key"
	// vencoder: "bitrate", "rate_control",
	//           obs_service_apply_encoder_settings
	// aencoder: "bitrate"
	// output: "bind_ip" via main config -> "Output", "BindIP"
	//         obs_output_set_service

	OBSData vencoder_settings = obs_data_create();
	OBSData aencoder_settings = obs_data_create();
	OBSData output_settings = obs_data_create();
	obs_data_release(vencoder_settings);
	obs_data_release(aencoder_settings);
	obs_data_release(output_settings);

	osn::Service::Manager::GetInstance().for_each([&gotError](obs_service_t *service) {
		Service serviceType = Service::Other;
		std::string key;
		std::string keyToEvaluate;
		std::string serviceName;
		OBSData service_settings = obs_data_create();
		obs_data_release(service_settings);

		if (service) {
			obs_service_t *currentService = service;
			if (currentService) {
				obs_data_t *currentServiceSettings = obs_service_get_settings(currentService);
				if (currentServiceSettings) {
					serviceName = obs_data_get_string(currentServiceSettings, "service");

					key = obs_service_get_connect_info(currentService, OBS_SERVICE_CONNECT_INFO_STREAM_KEY);
					if (key.empty()) {
						sendErrorMessage("invalid_stream_settings");
						gotError = true;
					}
				} else {
					sendErrorMessage("invalid_stream_settings");
					gotError = true;
				}
			} else {
				sendErrorMessage("invalid_stream_settings");
				gotError = true;
			}
			if (gotError) {
				return;
			}

			if (serviceName == "Twitch")
				serviceType = Service::Twitch;
			else if (serviceName == "hitbox.tv")
				serviceType = Service::Hitbox;
			else if (serviceName == "beam.pro")
				serviceType = Service::Beam;
			else if (serviceName.find("YouTube") != std::string::npos)
				serviceType = Service::YouTube;
			else
				serviceType = Service::Other;

			keyToEvaluate = key;

			if (serviceType == Service::Twitch) {
				string_depad_key(key);
				keyToEvaluate += "?bandwidthtest";
			}

			// todo - will it work without making it custom server?
			// if (serviceType == Service::YouTube) {
			// 	serverName = "Stream URL";
			// 	server = obs_service_get_connect_info(currentService, OBS_SERVICE_CONNECT_INFO_SERVER_URL);
			// }

			obs_data_set_string(service_settings, "service", serviceName.c_str());
			obs_data_set_string(service_settings, "key", keyToEvaluate.c_str());

			int awstartingBitrate = GetStartingBitrate(serviceName);
		}
	});

	if (gotError) {
		obs_output_release(output);
		obs_encoder_release(vencoder);
		obs_encoder_release(aencoder);
		return;
	}

	// if (!customServer) {
	// 	if (serviceName == "Twitch")
	// 		serviceSelected = Service::Twitch;
	// 	else if (serviceName == "hitbox.tv")
	// 		serviceSelected = Service::Hitbox;
	// 	else if (serviceName == "beam.pro")
	// 		serviceSelected = Service::Beam;
	// 	else if (serviceName.find("YouTube") != std::string::npos)
	// 		serviceSelected = Service::YouTube;
	// 	else
	// 		serviceSelected = Service::Other;
	// } else {
	// 	serviceSelected = Service::Other;
	// }
	//std::string keyToEvaluate = key;

	// if (serviceSelected == Service::Twitch) {
	// 	string_depad_key(key);
	// 	keyToEvaluate += "?bandwidthtest";
	// }

	// if (serviceSelected == Service::YouTube) {
	// 	serverName = "Stream URL";
	// 	server = obs_service_get_connect_info(currentService, OBS_SERVICE_CONNECT_INFO_SERVER_URL);
	// }

	// obs_data_set_string(service_settings, "service", serviceName.c_str());
	// obs_data_set_string(service_settings, "key", keyToEvaluate.c_str());

	//Setting starting bitrate
	// OBSData service_settingsawd = obs_data_create();
	// obs_data_release(service_settingsawd);

	// obs_data_set_string(service_settingsawd, "service", serviceName.c_str());

	// OBSService servicewad = obs_service_create(serverType, "temp_service", service_settingsawd, nullptr);
	// obs_service_release(servicewad);

	// int bitrate = 10000;

	// OBSData settings = obs_data_create();
	// obs_data_release(settings);
	// obs_data_set_int(settings, "bitrate", bitrate);
	// obs_service_apply_encoder_settings(servicewad, settings, nullptr);

	// int awstartingBitrate = (int)obs_data_get_int(settings, "bitrate");

	obs_data_set_int(vencoder_settings, "bitrate", awstartingBitrate);
	obs_data_set_string(vencoder_settings, "rate_control", "CBR");
	obs_data_set_string(vencoder_settings, "preset", "veryfast");
	obs_data_set_int(vencoder_settings, "keyint_sec", 2);

	obs_data_set_int(aencoder_settings, "bitrate", 32);

	//todo get bind if from new api
	const char *bind_ip = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "BindIP");
	obs_data_set_string(output_settings, "bind_ip", bind_ip);

	/* -----------------------------------*/
	/* determine which servers to test    */

	// std::vector<ServerInfo> servers;
	// if (customServer)
	// 	servers.emplace_back(server.c_str(), server.c_str());
	//	else
	//		GetServers(servers);

	/* just use the first server if it only has one alternate server */
	// if (servers.size() < 3)
	// 	servers.resize(1);

	/* -----------------------------------*/
	/* apply settings                     */

	obs_service_update(service, service_settings);
	obs_service_apply_encoder_settings(service, vencoder_settings, aencoder_settings);

	obs_encoder_update(vencoder, vencoder_settings);
	obs_encoder_update(aencoder, aencoder_settings);

	obs_encoder_set_video_mix(vencoder, obs_video_mix_get(ovi, OBS_MAIN_VIDEO_RENDERING));
	obs_encoder_set_audio(aencoder, obs_get_audio());

	/* -----------------------------------*/
	/* connect encoders/services/outputs  */

	obs_output_set_video_encoder(output, vencoder);
	obs_output_set_audio_encoder(output, aencoder, 0);

	obs_output_update(output, output_settings);

	obs_output_set_service(output, service);

	/* -----------------------------------*/
	/* connect signals                    */

	auto on_started = [&]() {
		std::unique_lock<std::mutex> lock(m);
		connected = true;
		stopped = false;
		cv.notify_one();
	};

	auto on_stopped = [&]() {
		const char *output_error = obs_output_get_last_error(output);

		if (output_error == nullptr) {
			std::unique_lock<std::mutex> lock(m);
			connected = false;
			stopped = true;
			cv.notify_one();
		} else {
			errorOnStop = true;
		}
	};

	auto on_deactivate = [&]() { cv.notify_one(); };

	using on_started_t = decltype(on_started);
	using on_stopped_t = decltype(on_stopped);
	using on_deactivate_t = decltype(on_deactivate);

	auto pre_on_started = [](void *data, calldata_t *) {
		on_started_t &on_started = *reinterpret_cast<on_started_t *>(data);
		on_started();
	};

	auto pre_on_stopped = [](void *data, calldata_t *) {
		on_stopped_t &on_stopped = *reinterpret_cast<on_stopped_t *>(data);
		on_stopped();
	};

	auto pre_on_deactivate = [](void *data, calldata_t *) {
		on_deactivate_t &on_deactivate = *reinterpret_cast<on_deactivate_t *>(data);
		on_deactivate();
	};

	signal_handler *sh = obs_output_get_signal_handler(output);
	signal_handler_connect(sh, "start", pre_on_started, &on_started);
	signal_handler_connect(sh, "stop", pre_on_stopped, &on_stopped);
	signal_handler_connect(sh, "deactivate", pre_on_deactivate, &on_deactivate);

	/* -----------------------------------*/
	/* test servers                       */

	int bestBitrate = 0;
	int bestMS = 0x7FFFFFFF;
	std::string bestServer;
	std::string bestServerName;
	bool success = false;

	if (serverName.compare("") != 0) {
		ServerInfo info(serverName.c_str(), server.c_str());

		if (EvaluateBandwidth(info, connected, stopped, success, errorOnStop, service_settings, service, output, vencoder_settings) < 0) {
			eventsMutex.lock();
			events.push(AutoConfigInfo("error", "invalid_stream_settings", 0));
			eventsMutex.unlock();
			gotError = true;
		} else {
			bestServer = info.address;
			bestServerName = info.name;
			bestBitrate = info.bitrate;

			eventsMutex.lock();
			events.push(AutoConfigInfo("progress", "bandwidth_test", 100));
			eventsMutex.unlock();
		}
		// } else {
		// 	for (size_t i = 0; i < servers.size(); i++) {
		// 		EvaluateBandwidth(servers[i], connected, stopped, success, errorOnStop, service_settings, service, output, vencoder_settings);
		// 		eventsMutex.lock();
		// 		events.push(AutoConfigInfo("progress", "bandwidth_test", (double)(i + 1) * 100 / servers.size()));
		// 		eventsMutex.unlock();
		// 	}
	}

	if (!success && !gotError) {
		eventsMutex.lock();
		events.push(AutoConfigInfo("error", "invalid_stream_settings", 0));
		eventsMutex.unlock();
		gotError = true;
	}

	if (!gotError) {
		// for (auto &server : servers) {
		// 	bool close = abs(server.bitrate - bestBitrate) < 400;

		// 	if ((!close && server.bitrate > bestBitrate) || (close && server.ms < bestMS)) {
		// 		bestServer = server.address;
		// 		bestServerName = server.name;
		// 		bestBitrate = server.bitrate;
		// 		bestMS = server.ms;
		// 	}
		// }
		runContext.server = bestServer;
		serverName = bestServerName;
		runContext.idealBitrate = bestBitrate;
	}

	obs_output_release(output);
	obs_encoder_release(vencoder);
	obs_encoder_release(aencoder);

	if (!gotError) {
		eventsMutex.lock();
		events.push(AutoConfigInfo("stopping_step", "bandwidth_test", 100));
		eventsMutex.unlock();
	}
}
#endif //deprecated

/* this is used to estimate the lower bitrate limit for a given
 * resolution/fps.  yes, it is a totally arbitrary equation that gets
 * the closest to the expected values */
static long double EstimateBitrateVal(int cx, int cy, int fps_num, int fps_den)
{
	long fps = long((long double)fps_num / (long double)fps_den);
	long double areaVal = pow((long double)(cx * cy), 0.85l);
	return areaVal * sqrt(pow(fps, 1.1l));
}

static long double EstimateMinBitrate(int cx, int cy, int fps_num, int fps_den)
{
	long double val = EstimateBitrateVal((int)runContext.baseResolutionCX, (int)runContext.baseResolutionCY, 60, 1) / 5800.0l;
	if (val < std::numeric_limits<double>::epsilon() && val > -std::numeric_limits<double>::epsilon()) {
		return 0.0;
	}

	return EstimateBitrateVal(cx, cy, fps_num, fps_den) / val;
}

static long double EstimateUpperBitrate(int cx, int cy, int fps_num, int fps_den)
{
	long double val = EstimateBitrateVal(1280, 720, 30, 1) / 3000.0l;
	if (val < std::numeric_limits<double>::epsilon() && val > -std::numeric_limits<double>::epsilon()) {
		return 0.0;
	}

	return EstimateBitrateVal(cx, cy, fps_num, fps_den) / val;
}

struct Result {
	int cx;
	int cy;
	int fps_num;
	int fps_den;

	inline Result(int cx_, int cy_, int fps_num_, int fps_den_) : cx(cx_), cy(cy_), fps_num(fps_num_), fps_den(fps_den_) {}
};

void autoConfig::FindIdealHardwareResolution()
{
	int baseCX = (int)runContext.baseResolutionCX;
	int baseCY = (int)runContext.baseResolutionCY;

	std::vector<Result> results;

	int pcores = os_get_physical_cores();
	int maxDataRate;
	if (pcores >= 4) {
		maxDataRate = int(runContext.baseResolutionCX * runContext.baseResolutionCY * 60 + 1000);
	} else {
		maxDataRate = 1280 * 720 * 30 + 1000;
	}

	auto testRes = [&](long double div, int fps_num, int fps_den, bool force) {
		if (results.size() >= 3)
			return;

		if (!fps_num || !fps_den) {
			fps_num = runContext.specificFPSNum;
			fps_den = runContext.specificFPSDen;
		}

		long double fps = ((long double)fps_num / (long double)fps_den);

		int cx = int((long double)baseCX / div);
		int cy = int((long double)baseCY / div);

		long double rate = (long double)cx * (long double)cy * fps;
		if (!force && rate > maxDataRate)
			return;

		int minBitrate = int(EstimateMinBitrate(cx, cy, fps_num, fps_den) * 114 / 100);
		if (runContext.type == Type::Recording)
			force = true;
		if (force || runContext.idealBitrate >= minBitrate)
			results.emplace_back(cx, cy, fps_num, fps_den);
	};

	if (runContext.specificFPSNum && runContext.specificFPSDen) {
		testRes(1.0, 0, 0, false);
		testRes(1.5, 0, 0, false);
		testRes(1.0 / 0.6, 0, 0, false);
		testRes(2.0, 0, 0, false);
		testRes(2.25, 0, 0, true);
	} else {
		testRes(1.0, 60, 1, false);
		testRes(1.0, 30, 1, false);
		testRes(1.5, 60, 1, false);
		testRes(1.5, 30, 1, false);
		testRes(1.0 / 0.6, 60, 1, false);
		testRes(1.0 / 0.6, 30, 1, false);
		testRes(2.0, 60, 1, false);
		testRes(2.0, 30, 1, false);
		testRes(2.25, 60, 1, false);
		testRes(2.25, 30, 1, true);
	}

	int minArea = 960 * 540 + 1000;

	if (!runContext.specificFPSNum && runContext.preferHighFPS && results.size() > 1) {
		Result &result1 = results[0];
		Result &result2 = results[1];

		if (result1.fps_num == 30 && result2.fps_num == 60) {
			int nextArea = result2.cx * result2.cy;
			if (nextArea >= minArea)
				results.erase(results.begin());
		}
	}

	Result result = results.front();
	runContext.idealResolutionCX = result.cx;
	runContext.idealResolutionCY = result.cy;

	runContext.idealFPSNum = result.fps_num;
	runContext.idealFPSDen = result.fps_den;
}

bool autoConfig::TestSoftwareEncoding()
{
	OBSEncoder vencoder = obs_video_encoder_create(ADVANCED_ENCODER_X264, "test_x264", nullptr, nullptr);
	OBSEncoder aencoder = obs_audio_encoder_create("ffmpeg_aac", "test_aac", nullptr, 0, nullptr);
	OBSOutput output = obs_output_create("null_output", "null", nullptr, nullptr);

	/* -----------------------------------*/
	/* configure settings                 */

	OBSData aencoder_settings = obs_data_create();
	OBSData vencoder_settings = obs_data_create();
	obs_data_release(aencoder_settings);
	obs_data_release(vencoder_settings);
	obs_data_set_int(aencoder_settings, "bitrate", 32);

	if (runContext.type != Type::Recording) {
		obs_data_set_int(vencoder_settings, "keyint_sec", 2);
		obs_data_set_int(vencoder_settings, "bitrate", runContext.idealBitrate);
		obs_data_set_string(vencoder_settings, "rate_control", "CBR");
		obs_data_set_string(vencoder_settings, "profile", "main");
		obs_data_set_string(vencoder_settings, "preset", "veryfast");
	} else {
		obs_data_set_int(vencoder_settings, "crf", 20);
		obs_data_set_string(vencoder_settings, "rate_control", "CRF");
		obs_data_set_string(vencoder_settings, "profile", "high");
		obs_data_set_string(vencoder_settings, "preset", "veryfast");
	}

	/* -----------------------------------*/
	/* apply settings                     */

	obs_encoder_update(vencoder, vencoder_settings);
	obs_encoder_update(aencoder, aencoder_settings);

	/* -----------------------------------*/
	/* connect encoders/services/outputs  */

	obs_output_set_video_encoder(output, vencoder);
	obs_output_set_audio_encoder(output, aencoder, 0);

	/* -----------------------------------*/
	/* connect signals                    */

	auto on_stopped = [&]() {
		std::unique_lock<std::mutex> lock(m);
		cv.notify_one();
	};

	using on_stopped_t = decltype(on_stopped);

	auto pre_on_stopped = [](void *data, calldata_t *) {
		on_stopped_t &on_stopped = *reinterpret_cast<on_stopped_t *>(data);
		on_stopped();
	};

	signal_handler *sh = obs_output_get_signal_handler(output);
	signal_handler_connect(sh, "deactivate", pre_on_stopped, &on_stopped);

	/* -----------------------------------*/
	/* calculate starting resolution      */

	int baseCX = int(runContext.baseResolutionCX);
	int baseCY = int(runContext.baseResolutionCY);

	/* -----------------------------------*/
	/* calculate starting test rates      */

	int pcores = os_get_physical_cores();
	int lcores = os_get_logical_cores();
	int maxDataRate;
	if (lcores > 8 || pcores > 4) {
		/* superb */
		maxDataRate = int(runContext.baseResolutionCX * runContext.baseResolutionCY * 60 + 1000);

	} else if (lcores > 4 && pcores == 4) {
		/* great */
		maxDataRate = int(runContext.baseResolutionCX * runContext.baseResolutionCY * 60 + 1000);

	} else if (pcores == 4) {
		/* okay */
		maxDataRate = int(runContext.baseResolutionCX * runContext.baseResolutionCY * 30 + 1000);

	} else {
		/* toaster */
		maxDataRate = 960 * 540 * 30 + 1000;
	}

	/* -----------------------------------*/
	/* perform tests                      */

	std::vector<Result> results;
	int i = 0;
	int count = 1;

	obs_video_info *ovi = obs_create_video_info();

	auto testRes = [&](long double div, int fps_num, int fps_den, bool force) {
		int per = ++i * 100 / count;

		/* no need for more than 3 tests max */
		if (results.size() >= 3)
			return true;

		if (!fps_num || !fps_den) {
			fps_num = runContext.specificFPSNum;
			fps_den = runContext.specificFPSDen;
		}

		long double fps = ((long double)fps_num / (long double)fps_den);

		int cx = int((long double)baseCX / div);
		int cy = int((long double)baseCY / div);

		if (!force && runContext.type != Type::Recording) {
			int est = int(EstimateMinBitrate(cx, cy, fps_num, fps_den));
			if (est > runContext.idealBitrate)
				return true;
		}

		long double rate = (long double)cx * (long double)cy * fps;
		if (!force && rate > maxDataRate)
			return true;

		obs_video_info video = *ovi;
		video.base_width = 1280;
		video.base_height = 720;
		video.output_width = cx;
		video.output_height = cy;
		video.output_format = VIDEO_FORMAT_NV12;
		video.fps_num = fps_num;
		video.fps_den = fps_den;
		video.initialized = true;
		int ret = obs_set_video_info(ovi, &video);
		if (ret != OBS_VIDEO_SUCCESS) {
			blog(LOG_ERROR, "[VIDEO_CANVAS] Failed to update video info %08X", ovi);
			return false;
		}

		obs_encoder_set_audio(aencoder, obs_get_audio());

		obs_encoder_update(vencoder, vencoder_settings);
		obs_encoder_set_video_mix(vencoder, obs_video_mix_get(ovi, OBS_MAIN_VIDEO_RENDERING));

		obs_output_set_audio_encoder(output, aencoder, 0);
		obs_output_set_video_encoder(output, vencoder);

		std::unique_lock<std::mutex> ul(m);
		if (cancel)
			return false;

		if (!obs_output_start(output)) {
			return false;
		}

		cv.wait_for(ul, std::chrono::seconds(5));

		obs_output_stop(output);
		cv.wait(ul);

		int skipped = (int)video_output_get_skipped_frames(obs_get_video());
		if (force || skipped <= 10)
			results.emplace_back(cx, cy, fps_num, fps_den);

		return !cancel;
	};

	if (runContext.specificFPSNum && runContext.specificFPSDen) {
		count = 5;
		if (!testRes(1.0, 0, 0, false))
			return false;
		if (!testRes(1.5, 0, 0, false))
			return false;
		if (!testRes(1.0 / 0.6, 0, 0, false))
			return false;
		if (!testRes(2.0, 0, 0, false))
			return false;
		if (!testRes(2.25, 0, 0, true))
			return false;
	} else {
		count = 10;
		if (!testRes(1.0, 60, 1, false))
			return false;
		if (!testRes(1.0, 30, 1, false))
			return false;
		if (!testRes(1.5, 60, 1, false))
			return false;
		if (!testRes(1.5, 30, 1, false))
			return false;
		if (!testRes(1.0 / 0.6, 60, 1, false))
			return false;
		if (!testRes(1.0 / 0.6, 30, 1, false))
			return false;
		if (!testRes(2.0, 60, 1, false))
			return false;
		if (!testRes(2.0, 30, 1, false))
			return false;
		if (!testRes(2.25, 60, 1, false))
			return false;
		if (!testRes(2.25, 30, 1, true))
			return false;
	}

	/* -----------------------------------*/
	/* find preferred settings            */

	int minArea = 960 * 540 + 1000;

	if (!runContext.specificFPSNum && runContext.preferHighFPS && results.size() > 1) {
		Result &result1 = results[0];
		Result &result2 = results[1];

		if (result1.fps_num == 30 && result2.fps_num == 60) {
			int nextArea = result2.cx * result2.cy;
			if (nextArea >= minArea)
				results.erase(results.begin());
		}
	}

	Result result = results.front();
	runContext.idealResolutionCX = result.cx;
	runContext.idealResolutionCY = result.cy;

	runContext.idealFPSNum = result.fps_num;
	runContext.idealFPSDen = result.fps_den;

	long double fUpperBitrate = EstimateUpperBitrate(result.cx, result.cy, result.fps_num, result.fps_den);

	int upperBitrate = int(floor(fUpperBitrate / 50.0l) * 50.0l);

	if (runContext.streamingEncoder != Encoder::x264) {
		upperBitrate *= 114;
		upperBitrate /= 100;
	}

	if (runContext.idealBitrate > upperBitrate)
		runContext.idealBitrate = upperBitrate;

	obs_output_release(output);
	obs_encoder_release(vencoder);
	obs_encoder_release(aencoder);

	int ret = obs_remove_video_info(ovi);
	if (ret != OBS_VIDEO_SUCCESS) {
		blog(LOG_ERROR, "[VIDEO_CANVAS] Failed to remove video info after TestSoftwareEncoding, %08X", ovi);
	}

	runContext.softwareTested = true;
	return true;
}

void autoConfig::TestStreamEncoderThread()
{
	eventsMutex.lock();
	events.push(AutoConfigInfo("starting_step", "runContext.streamingEncoder_test", 0));
	eventsMutex.unlock();

	autoConfig::ResourceSampler sampler;
	sampler.start("stream_encoder", std::chrono::milliseconds(250));

	TestHardwareEncoding();

	if (!runContext.softwareTested) {
		if (!runContext.preferHardware || !runContext.hardwareEncodingAvailable) {
			if (!TestSoftwareEncoding()) {
				return;
			}
		}
	}

	if (runContext.preferHardware && !runContext.softwareTested && runContext.hardwareEncodingAvailable)
		FindIdealHardwareResolution();

	if (!runContext.softwareTested) {
		if (runContext.nvencAvailable)
			runContext.streamingEncoder = Encoder::NVENC;
		else if (runContext.qsvAvailable)
			runContext.streamingEncoder = Encoder::QSV;
		else if (runContext.vceAvailable)
			runContext.streamingEncoder = Encoder::AMD;
		// HW encoding seems to not be stable on Mac
		// else if (appleHWAvailable)
		// 	runContext.streamingEncoder = Encoder::appleHW;
	} else {
		runContext.streamingEncoder = Encoder::x264;
	}

	// Surface encoder detection + chosen streaming encoder for the new POC UI.
	{
		const char *chosenId = GetEncoderId(runContext.streamingEncoder);
		obs_data_t *p = obs_data_create();
		obs_data_set_bool(p, "hardwareEncodingAvailable", runContext.hardwareEncodingAvailable);
		obs_data_set_bool(p, "nvenc", runContext.nvencAvailable);
		obs_data_set_bool(p, "qsv", runContext.qsvAvailable);
		obs_data_set_bool(p, "vce", runContext.vceAvailable);
		obs_data_set_bool(p, "apple", runContext.appleAvailable);
		obs_data_set_bool(p, "softwareTested", runContext.softwareTested);
		obs_data_set_string(p, "chosenStreamingEncoder", chosenId ? chosenId : "");
		std::string payload = obs_data_get_json(p);
		obs_data_release(p);

		std::lock_guard<std::mutex> lock(eventsMutex);
		events.push(AutoConfigInfo("encoder_detection", "summary", 100, payload));
	}

	recordResourceWindow(sampler.stop());

	eventsMutex.lock();
	events.push(AutoConfigInfo("stopping_step", "runContext.streamingEncoder_test", 100));
	eventsMutex.unlock();
}

void autoConfig::TestRecordingEncoderThread()
{
	eventsMutex.lock();
	events.push(AutoConfigInfo("starting_step", "runContext.recordingEncoder_test", 0));
	eventsMutex.unlock();

	autoConfig::ResourceSampler sampler;
	sampler.start("recording_encoder", std::chrono::milliseconds(250));

	TestHardwareEncoding();

	if (!runContext.hardwareEncodingAvailable && !runContext.softwareTested) {
		if (!TestSoftwareEncoding()) {
			return;
		}
	}

	if (runContext.type == Type::Recording && runContext.hardwareEncodingAvailable)
		FindIdealHardwareResolution();

	runContext.recordingQuality = Quality::High;

	bool recordingOnly = runContext.type == Type::Recording;

	if (runContext.hardwareEncodingAvailable) {
		if (runContext.nvencAvailable)
			runContext.recordingEncoder = Encoder::NVENC;
		else if (runContext.qsvAvailable)
			runContext.recordingEncoder = Encoder::QSV;
		else if (runContext.vceAvailable)
			runContext.recordingEncoder = Encoder::AMD;
		// HW encoding seems to not be stable on Mac
		// else if (appleHWAvailable)
		// 	runContext.recordingEncoder = Encoder::appleHW;
	} else {
		runContext.recordingEncoder = Encoder::x264;
	}

	if (runContext.recordingEncoder != Encoder::NVENC) {
		if (!recordingOnly) {
			runContext.recordingEncoder = Encoder::Stream;
			runContext.recordingQuality = Quality::Stream;
		}
	}

	recordResourceWindow(sampler.stop());

	eventsMutex.lock();
	events.push(AutoConfigInfo("stopping_step", "runContext.recordingEncoder_test", 100));
	eventsMutex.unlock();
}

inline const char *GetEncoderId(Encoder enc)
{
	switch (enc) {
	case Encoder::NVENC:
		return SIMPLE_ENCODER_NVENC;
	case Encoder::QSV:
		return SIMPLE_ENCODER_QSV;
	case Encoder::AMD:
		return SIMPLE_ENCODER_AMD;
	case Encoder::Apple:
		return SIMPLE_ENCODER_APPLE_H264;
	default:
		return SIMPLE_ENCODER_X264;
	}
};

bool autoConfig::CheckSettings(void)
{
#ifdef OLD_BANDWIDTH_TEST //deprecated
	OBSData settings = obs_data_create();

	obs_data_set_string(settings, "service", serviceName.c_str());
	obs_data_set_string(settings, "server", server.c_str());

	std::string testKey = key;

	if (serviceName.compare("Twitch") == 0) {
		testKey += "?bandwidthtest";
	}

	obs_data_set_string(settings, "key", testKey.c_str());

	OBSService service = obs_service_create("rtmp_common", "serviceTest", settings, NULL);

	if (!service) {
		eventsMutex.lock();
		events.push(AutoConfigInfo("error", "invalid_service", 100));
		eventsMutex.unlock();
		return false;
	}

	obs_video_info video = {0};
	bool have_users_info = obs_get_video_info(&video);

	obs_video_info *ovi = obs_create_video_info();

	if (!have_users_info) {
		video = *ovi;
	}

	video.base_width = 1280;
	video.base_height = 720;
	video.output_width = (uint32_t)runContext.idealResolutionCX;
	video.output_height = (uint32_t)runContext.idealResolutionCY;
	video.fps_num = runContext.idealFPSNum;
	video.fps_den = 1;
	video.initialized = true;
	int ret = obs_set_video_info(ovi, &video);
	if (ret != OBS_VIDEO_SUCCESS) {
		eventsMutex.lock();
		events.push(AutoConfigInfo("error", "invalid_video_settings", 100));
		eventsMutex.unlock();
		obs_remove_video_info(ovi);
		return false;
	}

	OBSEncoder vencoder = obs_video_encoder_create(GetEncoderId(runContext.streamingEncoder), "test_encoder", nullptr, nullptr);
	OBSEncoder aencoder = obs_audio_encoder_create("ffmpeg_aac", "test_aac", nullptr, 0, nullptr);
	OBSOutput output = obs_output_create("rtmp_output", "test_stream", nullptr, nullptr);

	OBSData service_settings = obs_data_create();
	OBSData vencoder_settings = obs_data_create();
	OBSData aencoder_settings = obs_data_create();
	OBSData output_settings = obs_data_create();
	obs_data_release(service_settings);
	obs_data_release(vencoder_settings);
	obs_data_release(aencoder_settings);
	obs_data_release(output_settings);

	obs_data_set_int(vencoder_settings, "bitrate", runContext.idealBitrate);
	obs_data_set_string(vencoder_settings, "rate_control", "CBR");
	obs_data_set_string(vencoder_settings, "preset", "veryfast");
	obs_data_set_int(vencoder_settings, "keyint_sec", 2);

	obs_data_set_int(aencoder_settings, "bitrate", 32);

	/* -----------------------------------*/
	/* apply settings                     */

	obs_service_apply_encoder_settings(service, vencoder_settings, aencoder_settings);

	obs_encoder_update(vencoder, vencoder_settings);
	obs_encoder_update(aencoder, aencoder_settings);

	obs_encoder_set_video_mix(vencoder, obs_video_mix_get(ovi, OBS_MAIN_VIDEO_RENDERING));
	obs_encoder_set_audio(aencoder, obs_get_audio());

	/* -----------------------------------*/
	/* connect encoders/services/outputs  */

	obs_output_set_video_encoder(output, vencoder);
	obs_output_set_audio_encoder(output, aencoder, 0);

	obs_output_update(output, output_settings);

	obs_output_set_service(output, service);

	/* -----------------------------------*/
	/* connect signals                    */
	bool success = true;

	auto on_started = [&]() {
		std::unique_lock<std::mutex> lock(m);
		success = true;
		cv.notify_one();
	};

	auto on_stopped = [&]() {
		std::unique_lock<std::mutex> lock(m);
		cv.notify_one();
	};

	auto on_deactivate = [&]() { cv.notify_one(); };

	using on_started_t = decltype(on_started);
	using on_stopped_t = decltype(on_stopped);
	using on_deactivate_t = decltype(on_deactivate);

	auto pre_on_started = [](void *data, calldata_t *) {
		on_started_t &on_started = *reinterpret_cast<on_started_t *>(data);
		on_started();
	};

	auto pre_on_stopped = [](void *data, calldata_t *) {
		on_stopped_t &on_stopped = *reinterpret_cast<on_stopped_t *>(data);
		on_stopped();
	};

	auto pre_on_deactivate = [](void *data, calldata_t *) {
		on_deactivate_t &on_deactivate = *reinterpret_cast<on_deactivate_t *>(data);
		on_deactivate();
	};

	signal_handler *sh = obs_output_get_signal_handler(output);
	signal_handler_connect(sh, "start", pre_on_started, &on_started);
	signal_handler_connect(sh, "stop", pre_on_stopped, &on_stopped);
	signal_handler_connect(sh, "deactivate", pre_on_deactivate, &on_deactivate);

	std::unique_lock<std::mutex> ul(m);
	if (!cancel) {
		/* -----------------------------------*/
		/* start and wait to stop             */

		if (!obs_output_start(output)) {
		} else {
			cv.wait_for(ul, std::chrono::seconds(4));

			obs_output_stop(output);
			//wait for the output to stop
			cv.wait(ul);
			//wait for the output to deactivate
			cv.wait(ul);
		}
	} else {
		success = false;
	}

	obs_output_release(output);
	obs_encoder_release(vencoder);
	obs_encoder_release(aencoder);
	obs_service_release(service);

	ret = obs_remove_video_info(ovi);
	if (ret != OBS_VIDEO_SUCCESS) {
		blog(LOG_ERROR, "[VIDEO_CANVAS] Failed to remove video info after CheckSettings, %08X", ovi);
	}
	return success;
#endif //deprecated old api bandwidth test
	return true;
}

void autoConfig::SetDefaultSettings(void)
{
	eventsMutex.lock();
	events.push(AutoConfigInfo("starting_step", "setting_default_settings", 0));
	eventsMutex.unlock();

	runContext.idealResolutionCX = 1280;
	runContext.idealResolutionCY = 720;
	runContext.idealFPSNum = 30;
	runContext.recordingQuality = Quality::High;
	runContext.idealBitrate = 4500;
	runContext.streamingEncoder = Encoder::x264;
	runContext.recordingEncoder = Encoder::Stream;

	eventsMutex.lock();
	events.push(AutoConfigInfo("stopping_step", "setting_default_settings", 100));
	eventsMutex.unlock();
}

// Push the chosen values from runContext into the streaming targets the
// frontend passed to InitializeAutoConfig. No basic.ini writes — APIv2 contract
// is that the frontend re-fetches via Get*() after seeing the "done" event.
//
// Order matters: obs_set_video_info fails while any output is still active, so we
// stop test outputs first, then mutate the video context, then service / encoders.
static void applyResults()
{
	// Resolve the streaming targets stored in runContext. Skip ids that no
	// longer resolve (object was destroyed mid-run).
	struct StreamingTarget {
		osn::Streaming *streaming;
		uint64_t id;
	};
	std::vector<StreamingTarget> streamingTargets;

	for (uint64_t uid : runContext.targetStreamingIds) {
		osn::Streaming *s = osn::IStreaming::Manager::GetInstance().find(uid);
		if (s)
			streamingTargets.push_back({s, uid});
	}

	// Collect video contexts referenced by the streaming targets (deduplicated).
	std::set<obs_video_info *> videoContexts;
	for (auto &st : streamingTargets) {
		obs_video_info *v = st.streaming->GetCanvas();
		if (v)
			videoContexts.insert(v);
	}

	// Defensive — stop all streaming outputs before touching video context.
	for (auto &st : streamingTargets) {
		if (st.streaming->GetOutput() && obs_output_active(st.streaming->GetOutput()))
			obs_output_stop(st.streaming->GetOutput());
	}

	// obs_output_stop() is asynchronous; wait for outputs to fully deactivate
	// before mutating the video context, or obs_set_video_info() below can return
	// OBS_VIDEO_CURRENTLY_ACTIVE (the same race Streaming::CleanTestMode guards).
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
		for (auto &st : streamingTargets) {
			obs_output_t *output = st.streaming->GetOutput();
			while (output && obs_output_active(output) && std::chrono::steady_clock::now() < deadline) {
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
		}
	}

	// 1. Resolution / FPS — applied to each video context referenced by a streaming
	//    target. Must run before encoders (encoder video-mix indices are tied to the
	//    video context).
	for (auto *video : videoContexts) {
		AutoconfigRun::VideoDecision vd;
		vd.contextPtr = video;
		vd.cxBefore = video->output_width;
		vd.cyBefore = video->output_height;
		vd.fpsNumBefore = video->fps_num;
		vd.fpsDenBefore = video->fps_den;

		obs_video_info v = *video;
		v.fps_num = (uint32_t)runContext.idealFPSNum;
		v.fps_den = (uint32_t)(runContext.idealFPSDen ? runContext.idealFPSDen : 1);
		v.output_width = ((uint32_t)runContext.idealResolutionCX) & 0xFFFFFFFC;
		v.output_height = ((uint32_t)runContext.idealResolutionCY) & 0xFFFFFFFE;

		vd.cxAfter = v.output_width;
		vd.cyAfter = v.output_height;
		vd.fpsNumAfter = v.fps_num;
		vd.fpsDenAfter = v.fps_den;

		blog(LOG_INFO, "applyResults: ctx=%p current=%ux%u@%u/%u requested=%ux%u@%u/%u", video, video->output_width, video->output_height,
		     video->fps_num, video->fps_den, v.output_width, v.output_height, v.fps_num, v.fps_den);

		// Skip the libobs call when nothing changes — the common case where
		// autoconfig picks the resolution/FPS the canvas is already running at.
		// obs_set_video_info would otherwise return OBS_VIDEO_CURRENTLY_ACTIVE
		// for any active video context (e.g. running preview).
		if (video->fps_num == v.fps_num && video->fps_den == v.fps_den && video->output_width == v.output_width &&
		    video->output_height == v.output_height) {
			blog(LOG_INFO, "applyResults: ctx=%p no change, skipping obs_set_video_info", video);
			vd.skipped = true;
			vd.obsSetVideoInfoRet = OBS_VIDEO_SUCCESS;
		} else {
			int ret = obs_set_video_info(video, &v);
			vd.obsSetVideoInfoRet = ret;
			blog(ret == OBS_VIDEO_SUCCESS ? LOG_INFO : LOG_WARNING, "applyResults: ctx=%p obs_set_video_info returned %d", video, ret);
			if (ret != OBS_VIDEO_SUCCESS) {
				std::lock_guard<std::mutex> lock(eventsMutex);
				events.push(AutoConfigInfo("error", "video_failed_ret_" + std::to_string(ret), 0));
			}
		}

		runContext.videoDecisions.push_back(vd);

		// Emit video_decision event for the new POC UI.
		std::ostringstream ptrOss;
		ptrOss << "0x" << std::hex << reinterpret_cast<uintptr_t>(video);
		std::string ptrStr = ptrOss.str();

		obs_data_t *p = obs_data_create();
		obs_data_set_string(p, "contextPtr", ptrStr.c_str());
		obs_data_t *before = obs_data_create();
		obs_data_set_int(before, "cx", vd.cxBefore);
		obs_data_set_int(before, "cy", vd.cyBefore);
		obs_data_set_int(before, "fpsNum", vd.fpsNumBefore);
		obs_data_set_int(before, "fpsDen", vd.fpsDenBefore);
		obs_data_set_obj(p, "before", before);
		obs_data_release(before);
		obs_data_t *after = obs_data_create();
		obs_data_set_int(after, "cx", vd.cxAfter);
		obs_data_set_int(after, "cy", vd.cyAfter);
		obs_data_set_int(after, "fpsNum", vd.fpsNumAfter);
		obs_data_set_int(after, "fpsDen", vd.fpsDenAfter);
		obs_data_set_obj(p, "after", after);
		obs_data_release(after);
		obs_data_set_int(p, "ret", vd.obsSetVideoInfoRet);
		obs_data_set_bool(p, "skipped", vd.skipped);
		std::string payload = obs_data_get_json(p);
		obs_data_release(p);

		std::lock_guard<std::mutex> lock(eventsMutex);
		events.push(AutoConfigInfo("video_decision", "ctx_" + ptrStr, 100, payload));
	}

	// 2. Per-target service URL + bitrate. Each target gets its own per-target
	//    result when the bandwidth test ran; otherwise falls back to the global values.
	for (auto &st : streamingTargets) {
		uint64_t targetBitrate = runContext.idealBitrate;
		std::string targetServer = runContext.server;
		for (auto &tr : runContext.targetResults) {
			if (tr.streamingId == st.id) {
				targetBitrate = tr.idealBitrate;
				targetServer = tr.server;
				break;
			}
		}

		// Service URL
		if (st.streaming->service && !targetServer.empty()) {
			obs_data_t *settings = obs_data_create();
			obs_data_set_string(settings, "server", targetServer.c_str());
			obs_service_update(st.streaming->service, settings);
			obs_data_release(settings);
			blog(LOG_INFO, "applyResults: target %llu: applied service server '%s'", st.id, targetServer.c_str());
		}

		// Streaming bitrate selection (Option 2 — heuristic as floor, not ceiling):
		//   1. Read user's current bitrate (CleanTestMode restored it before this).
		//   2. Compute OBS quality heuristic for the chosen res/FPS.
		//   3. Pick max(user, heuristic) — respect user when above the heuristic;
		//      otherwise use the heuristic as the recommendation.
		//   4. Cap by what the bandwidth test actually delivered (targetBitrate).
		//   5. Cap by the per-platform service cap (Twitch etc.).
		if (st.streaming->videoEncoder && targetBitrate > 0) {
			int userBitrate = 0;
			{
				obs_data_t *s = obs_encoder_get_settings(st.streaming->videoEncoder);
				userBitrate = (int)obs_data_get_int(s, "bitrate");
				obs_data_release(s);
			}

			long double upperBitrate_d = EstimateUpperBitrate((int)runContext.idealResolutionCX, (int)runContext.idealResolutionCY,
									  runContext.idealFPSNum, runContext.idealFPSDen ? runContext.idealFPSDen : 1);
			uint64_t heuristic = (uint64_t)(std::floor(upperBitrate_d / 50.0L) * 50.0L);
			if (runContext.streamingEncoder != Encoder::x264 && heuristic > 0) {
				heuristic = heuristic * 114ULL / 100ULL;
			}

			uint64_t choseBeforeCaps = ((uint64_t)userBitrate > heuristic) ? (uint64_t)userBitrate : heuristic;
			uint64_t afterMeasuredCap = choseBeforeCaps;
			if (afterMeasuredCap > targetBitrate)
				afterMeasuredCap = targetBitrate;

			uint64_t afterPlatformCap = afterMeasuredCap;
			if (st.streaming->service) {
				obs_data_t *capSettings = obs_data_create();
				obs_data_set_int(capSettings, "bitrate", (long long)afterMeasuredCap);
				obs_service_apply_encoder_settings(st.streaming->service, capSettings, nullptr);
				uint64_t platformReturned = (uint64_t)obs_data_get_int(capSettings, "bitrate");
				if (platformReturned > 0 && platformReturned < afterMeasuredCap)
					afterPlatformCap = platformReturned;
				obs_data_release(capSettings);
			}

			uint64_t finalBitrate = afterPlatformCap;

			// Determine which cap was binding (in increasing-binding order).
			std::string bindingCap;
			if (afterPlatformCap < afterMeasuredCap)
				bindingCap = "platform";
			else if (afterMeasuredCap < choseBeforeCaps)
				bindingCap = "measured";
			else if ((uint64_t)userBitrate > heuristic)
				bindingCap = "user";
			else
				bindingCap = "heuristic";

			blog(LOG_INFO,
			     "applyResults: target %llu picked %llu (user=%d heuristic=%llu choseBeforeCaps=%llu measured=%llu afterPlatform=%llu binding=%s)",
			     st.id, finalBitrate, userBitrate, heuristic, choseBeforeCaps, targetBitrate, afterPlatformCap, bindingCap.c_str());

			obs_data_t *encSettings = obs_data_create();
			obs_data_set_int(encSettings, "bitrate", (long long)finalBitrate);
			obs_encoder_update(st.streaming->videoEncoder, encSettings);
			obs_data_release(encSettings);
			blog(LOG_INFO, "applyResults: target %llu: applied video encoder bitrate %llu", st.id, finalBitrate);

			// Encoder id snapshot for the selection record + change log.
			std::string currentEncoderId, chosenEncoderId;
			bool encoderChanged = false;
			if (st.streaming->videoEncoder) {
				const char *cur = obs_encoder_get_id(st.streaming->videoEncoder);
				const char *cho = GetEncoderId(runContext.streamingEncoder);
				if (cur)
					currentEncoderId = cur;
				if (cho)
					chosenEncoderId = cho;
				if (cur && cho && strcmp(cur, cho) != 0) {
					encoderChanged = true;
					blog(LOG_INFO, "applyResults: target %llu: chosen encoder '%s' differs from current '%s'", st.id, cho, cur);
				}
			}

			AutoconfigRun::SelectionDetail sd;
			sd.targetId = st.id;
			sd.userBitrate = userBitrate;
			sd.heuristic = heuristic;
			sd.choseBeforeCaps = choseBeforeCaps;
			sd.afterMeasuredCap = afterMeasuredCap;
			sd.afterPlatformCap = afterPlatformCap;
			sd.picked = finalBitrate;
			sd.bindingCap = bindingCap;
			sd.appliedServer = targetServer;
			sd.currentEncoderId = currentEncoderId;
			sd.chosenEncoderId = chosenEncoderId;
			sd.encoderChanged = encoderChanged;
			runContext.selectionDetails.push_back(sd);

			obs_data_t *p = obs_data_create();
			obs_data_set_int(p, "targetId", (long long)sd.targetId);
			obs_data_set_int(p, "userBitrate", sd.userBitrate);
			obs_data_set_int(p, "heuristic", (long long)sd.heuristic);
			obs_data_set_int(p, "choseBeforeCaps", (long long)sd.choseBeforeCaps);
			obs_data_set_int(p, "afterMeasuredCap", (long long)sd.afterMeasuredCap);
			obs_data_set_int(p, "afterPlatformCap", (long long)sd.afterPlatformCap);
			obs_data_set_int(p, "picked", (long long)sd.picked);
			obs_data_set_string(p, "bindingCap", sd.bindingCap.c_str());
			obs_data_set_string(p, "appliedServer", sd.appliedServer.c_str());
			obs_data_set_string(p, "currentEncoderId", sd.currentEncoderId.c_str());
			obs_data_set_string(p, "chosenEncoderId", sd.chosenEncoderId.c_str());
			obs_data_set_bool(p, "encoderChanged", sd.encoderChanged);
			std::string payload = obs_data_get_json(p);
			obs_data_release(p);

			std::lock_guard<std::mutex> lock(eventsMutex);
			events.push(AutoConfigInfo("selection_decision", "target_" + std::to_string(sd.targetId), 100, payload));
		}
	}

	// 3. Recording bitrate — discover all recording targets, apply the global
	//    idealBitrate (minimum across all streaming targets) to each.
	if (runContext.idealBitrate > 0) {
		osn::IRecording::Manager::GetInstance().for_each([&](osn::FileOutput *fileOutput) {
			auto *recording = static_cast<osn::Recording *>(fileOutput);
			if (recording && recording->videoEncoder) {
				obs_data_t *encSettings = obs_data_create();
				obs_data_set_int(encSettings, "bitrate", (long long)runContext.idealBitrate);
				obs_encoder_update(recording->videoEncoder, encSettings);
				obs_data_release(encSettings);
				blog(LOG_INFO, "applyResults: applied recording video encoder bitrate %llu", runContext.idealBitrate);
			}
		});
	}
}

void autoConfig::SaveStreamSettings()
{
	// Legacy IPC stage. The actual apply happens in SaveSettings (the terminal
	// stage in the legacy frontend contract). Kept here as a no-op so callers that
	// invoke both don't error.
	std::lock_guard<std::mutex> lock(eventsMutex);
	events.push(AutoConfigInfo("stopping_step", "saving_service", 100));
}

void autoConfig::SaveSettings()
{
	{
		std::lock_guard<std::mutex> lock(eventsMutex);
		events.push(AutoConfigInfo("starting_step", "applying_settings", 0));
	}

	applyResults();

	runContext.runComplete = true;

	{
		std::lock_guard<std::mutex> lock(eventsMutex);
		events.push(AutoConfigInfo("stopping_step", "applying_settings", 100));
		events.push(AutoConfigInfo("done", "", 100));
	}
}
