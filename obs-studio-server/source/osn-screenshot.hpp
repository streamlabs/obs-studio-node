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

#include <obs.h>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Renders one or more canvases to PNG without blocking the IPC or graphics
// threads. A tick callback stages each canvas' main mix over two frames (queue
// the GPU copy, then map it once it has landed) and hands the pixels to a
// dedicated encoder thread; the IPC thread only submits jobs and polls their
// state. Mirrors OBS Studio's own multi-tick "Screenshot Output"
// (frontend/utility/ScreenshotObj.cpp), adapted to run off the Qt thread.
class ScreenshotManager {
public:
	enum class State : uint32_t { Pending = 0, Done = 1, Failed = 2 };

	struct Result {
		State state = State::Pending;
		std::string path;
		uint32_t width = 0;
		uint32_t height = 0;
		std::string error;
	};

	static ScreenshotManager &GetInstance();

	// Validates every canvas id and reserves a job for each, all picked up by
	// the same tick. Fails atomically (no jobs are created) if a canvas is
	// invalid or has no active mix, the directory is missing, or accepting
	// the whole batch would exceed the in-flight job cap.
	bool Submit(const std::vector<uint64_t> &canvasIds, const std::string &directory, const std::string &format, bool noSpace,
		    std::vector<uint64_t> &jobIds, std::string &error);

	// Returns the current state of a job. Once a terminal result (Done or
	// Failed) has been reported and its GPU resources (if any) are freed, the
	// job is erased; querying it again reports Failed with an "unknown job"
	// error, so each job must only be queried until it turns terminal.
	Result Query(uint64_t jobId);

	// Removes the tick callback, stops and joins the encoder thread, then
	// frees any GPU objects still owned by in-flight jobs. Call once, after
	// OBS_content_shutdownDisplays and before obs_shutdown(); not safe to
	// call from the graphics thread or the encoder thread.
	void Shutdown();

private:
	enum class RenderStage { NotStarted, Staged, Handed };

	struct Job {
		uint64_t canvasId = 0;
		obs_video_info *canvas = nullptr; // Pointer identity check only; never dereferenced outside a tick.
		uint32_t width = 0;
		uint32_t height = 0;
		std::string directory;
		std::string formattedName;
		bool noSpace = false;
		std::chrono::steady_clock::time_point deadline;

		State state = State::Pending;
		RenderStage renderStage = RenderStage::NotStarted;
		std::string path;
		std::string error;
		bool abandoned = false;
		bool tickBusy = false; // RunTick is working on this job without m_mutex; never erase it meanwhile.

		gs_texrender_t *texrender = nullptr;
		gs_stagesurf_t *stagesurf = nullptr;
	};

	struct EncodeTask {
		uint64_t jobId = 0;
		std::string directory;
		std::string formattedName;
		bool noSpace = false;
		uint32_t width = 0;
		uint32_t height = 0;
		std::vector<uint8_t> pixels;
	};

	ScreenshotManager() = default;
	ScreenshotManager(const ScreenshotManager &) = delete;
	ScreenshotManager &operator=(const ScreenshotManager &) = delete;

	// Drops jobs whose deadline is more than kJobGracePeriod behind, whether
	// pending or terminal, so a caller that stops polling (or fails partway
	// through a batch) can't hold job slots forever. Call under m_mutex.
	void PruneStaleJobsLocked();
	void EnsureEncoderStartedLocked();
	static void Tick(void *param, float seconds);
	void RunTick();
	void EncoderThreadMain();

	// Protects everything below. It is never held while in graphics: RunTick
	// and Shutdown collect work under it, release it, then enter graphics, and
	// only the tick creates or destroys a job's GPU objects.
	// obs_add_tick_callback/obs_remove_tick_callback must never be called
	// while holding m_mutex: a tick runs under libobs' draw_callbacks_mutex,
	// so doing so risks an ABBA deadlock against a tick that is itself
	// waiting on m_mutex.
	std::mutex m_mutex;
	std::map<uint64_t, Job> m_jobs;
	uint64_t m_nextJobId = 1;
	bool m_tickRegistered = false;

	std::thread m_encoderThread;
	std::condition_variable m_encoderCv;
	std::deque<EncodeTask> m_encodeQueue;
	bool m_encoderStop = false;
	size_t m_encodesInFlight = 0; // Queued plus currently encoding; counts toward the job cap.
};
