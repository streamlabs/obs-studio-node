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

#include "osn-screenshot.hpp"
#include "osn-video.hpp"

#include <graphics/vec4.h>
#include <util/platform.h>
#include <cstring>

namespace {
constexpr size_t kMaxActiveJobs = 4;
constexpr auto kJobDeadline = std::chrono::seconds(10);
constexpr auto kJobGracePeriod = std::chrono::seconds(5);

std::string JoinPath(const std::string &directory, const std::string &file)
{
	if (directory.empty())
		return file;
	const char last = directory.back();
	if (last == '/' || last == '\\')
		return directory + file;
	return directory + "/" + file;
}

std::string DirName(const std::string &path)
{
	const size_t pos = path.find_last_of("/\\");
	return pos == std::string::npos ? std::string() : path.substr(0, pos);
}

/* Same behaviour as OBS Studio's FindBestFilename: append " (2)", " (3)"...
 * or "_2", "_3"... before the extension until the name is free. */
void FindBestScreenshotFilename(std::string &strPath, bool noSpace)
{
	int num = 2;

	if (!os_file_exists(strPath.c_str()))
		return;

	const char *ext = strrchr(strPath.c_str(), '.');
	if (!ext)
		return;

	int extStart = int(ext - strPath.c_str());
	for (;;) {
		std::string testPath = strPath;
		std::string numStr = noSpace ? "_" : " (";
		numStr += std::to_string(num++);
		if (!noSpace)
			numStr += ")";

		testPath.insert(extStart, numStr);

		if (!os_file_exists(testPath.c_str())) {
			strPath = testPath;
			break;
		}
	}
}

/* OBS: GetOutputFilename(rec_path, "png", noSpace, overwrite = false,
 * GetFormatString(format, "Screenshot", nullptr)). The prefix is inserted in
 * front of the format; os_generate_formatted_filename(space = false) then
 * turns every space into '_', which is why noSpace yields "Screenshot_...". */
bool GenerateFormattedName(obs_video_info *canvas, const std::string &format, bool noSpace, bool multiCanvas, std::string &outName, std::string &error)
{
	const std::string fullFormat = "Screenshot " + format;
	char *filename = os_generate_formatted_filename("png", !noSpace, fullFormat.c_str(), canvas);
	if (!filename || !*filename) {
		bfree(filename);
		error = "Failed to generate a screenshot filename from format '" + format + "'.";
		return false;
	}

	outName = filename;
	bfree(filename);

	if (multiCanvas) {
		std::string suffix = noSpace ? "_" : " ";
		suffix += std::to_string(canvas->base_width) + "x" + std::to_string(canvas->base_height);
		const size_t ext = outName.rfind(".png");
		outName.insert(ext == std::string::npos ? outName.size() : ext, suffix);
	}
	return true;
}
} // namespace

ScreenshotManager &ScreenshotManager::GetInstance()
{
	static ScreenshotManager instance;
	return instance;
}

bool ScreenshotManager::Submit(const std::vector<uint64_t> &canvasIds, const std::string &directory, const std::string &format, bool noSpace,
			       std::vector<uint64_t> &jobIds, std::string &error)
{
	if (canvasIds.empty()) {
		error = "takeScreenshot requires at least one canvas id.";
		return false;
	}
	if (directory.empty() || !os_file_exists(directory.c_str())) {
		error = "Screenshot directory does not exist: " + directory;
		return false;
	}

	const bool multiCanvas = canvasIds.size() > 1;

	struct Prepared {
		uint64_t canvasId;
		obs_video_info *canvas;
		std::string name;
	};
	std::vector<Prepared> prepared;
	prepared.reserve(canvasIds.size());

	for (uint64_t canvasId : canvasIds) {
		obs_video_info *canvas = osn::Video::Manager::GetInstance().find(canvasId);
		if (!canvas) {
			error = "Invalid canvas id provided to takeScreenshot: " + std::to_string(canvasId);
			return false;
		}
		if (canvas->base_width == 0 || canvas->base_height == 0) {
			error = "Canvas " + std::to_string(canvasId) + " has no base resolution.";
			return false;
		}
		if (!obs_video_mix_get(canvas, OBS_MAIN_VIDEO_RENDERING)) {
			error = "Canvas " + std::to_string(canvasId) + " has no active video mix; video must be running to take a screenshot.";
			return false;
		}

		std::string name;
		if (!GenerateFormattedName(canvas, format, noSpace, multiCanvas, name, error))
			return false;

		prepared.push_back({canvasId, canvas, std::move(name)});
	}

	bool needsTickCallback = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		PruneStaleJobsLocked();

		size_t activeCount = m_encodesInFlight;
		for (auto &item : m_jobs) {
			if (!item.second.abandoned && item.second.renderStage != RenderStage::Handed)
				++activeCount;
		}
		if (activeCount + prepared.size() > kMaxActiveJobs) {
			error = "busy";
			return false;
		}

		jobIds.clear();
		jobIds.reserve(prepared.size());
		const auto deadline = std::chrono::steady_clock::now() + kJobDeadline;
		for (auto &p : prepared) {
			const uint64_t jobId = m_nextJobId++;
			Job &job = m_jobs[jobId];
			job.canvasId = p.canvasId;
			job.canvas = p.canvas;
			job.width = p.canvas->base_width;
			job.height = p.canvas->base_height;
			job.directory = directory;
			job.formattedName = p.name;
			job.noSpace = noSpace;
			job.deadline = deadline;
			jobIds.push_back(jobId);
		}

		needsTickCallback = !m_tickRegistered;
		m_tickRegistered = true;
		EnsureEncoderStartedLocked();
	}

	// obs_add_tick_callback must run outside m_mutex; see the lock-order note
	// on m_mutex in the header.
	if (needsTickCallback)
		obs_add_tick_callback(&ScreenshotManager::Tick, this);

	return true;
}

ScreenshotManager::Result ScreenshotManager::Query(uint64_t jobId)
{
	Result result;
	std::lock_guard<std::mutex> lock(m_mutex);

	auto it = m_jobs.find(jobId);
	if (it == m_jobs.end()) {
		result.state = State::Failed;
		result.error = "Unknown screenshot job.";
		return result;
	}

	Job &job = it->second;
	if (job.state == State::Pending && std::chrono::steady_clock::now() >= job.deadline) {
		job.state = State::Failed;
		job.error = "Screenshot timed out.";
		job.abandoned = true;
	}

	result.state = job.state;
	result.path = job.path;
	result.width = job.width;
	result.height = job.height;
	result.error = job.error;

	/* Once GPU resources are gone this job has nothing left to report; a job
	 * that timed out while still staged is left for RunTick to free and erase. */
	if (job.state != State::Pending && !job.texrender && !job.stagesurf && !job.tickBusy)
		m_jobs.erase(it);

	return result;
}

void ScreenshotManager::PruneStaleJobsLocked()
{
	const auto now = std::chrono::steady_clock::now();
	for (auto it = m_jobs.begin(); it != m_jobs.end();) {
		Job &job = it->second;
		if (now < job.deadline + kJobGracePeriod) {
			++it;
			continue;
		}

		if (job.state == State::Pending) {
			job.state = State::Failed;
			job.error = "Screenshot timed out.";
		}

		/* Still owns GPU objects, or RunTick is working on it: only RunTick may free those. */
		if (job.texrender || job.stagesurf || job.tickBusy) {
			job.abandoned = true;
			++it;
		} else {
			it = m_jobs.erase(it);
		}
	}
}

void ScreenshotManager::EnsureEncoderStartedLocked()
{
	if (!m_encoderThread.joinable()) {
		m_encoderStop = false;
		m_encoderThread = std::thread(&ScreenshotManager::EncoderThreadMain, this);
	}
}

void ScreenshotManager::Tick(void *param, float)
{
	static_cast<ScreenshotManager *>(param)->RunTick();
}

void ScreenshotManager::RunTick()
{
	struct StageWork {
		uint64_t jobId;
		uint64_t canvasId;
		obs_video_info *canvas;
		uint32_t width;
		uint32_t height;
		gs_texrender_t *texrender = nullptr;
		gs_stagesurf_t *stagesurf = nullptr;
		std::string error;
	};
	struct MapWork {
		uint64_t jobId;
		uint32_t width;
		uint32_t height;
		gs_texrender_t *texrender;
		gs_stagesurf_t *stagesurf;
		std::vector<uint8_t> pixels;
		bool mapped = false;
	};
	struct GpuObjects {
		gs_texrender_t *texrender;
		gs_stagesurf_t *stagesurf;
	};

	auto destroyGpuObjects = [](const std::vector<GpuObjects> &objects) {
		if (objects.empty())
			return;
		obs_enter_graphics();
		for (const GpuObjects &item : objects) {
			if (item.stagesurf)
				gs_stagesurface_destroy(item.stagesurf);
			if (item.texrender)
				gs_texrender_destroy(item.texrender);
		}
		obs_leave_graphics();
	};

	std::vector<StageWork> stageWork;
	std::vector<MapWork> mapWork;
	std::vector<GpuObjects> abandonedObjects;

	/* Jobs handed to the work vectors are marked tickBusy so Query and
	 * PruneStaleJobsLocked only flag them abandoned while the tick works on
	 * them without m_mutex. */
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (auto it = m_jobs.begin(); it != m_jobs.end();) {
			Job &job = it->second;

			if (job.abandoned) {
				if (job.texrender || job.stagesurf)
					abandonedObjects.push_back({job.texrender, job.stagesurf});
				it = m_jobs.erase(it);
				continue;
			}

			if (job.state == State::Pending && job.renderStage == RenderStage::NotStarted) {
				job.tickBusy = true;
				stageWork.push_back({it->first, job.canvasId, job.canvas, job.width, job.height});
			} else if (job.state == State::Pending && job.renderStage == RenderStage::Staged) {
				job.tickBusy = true;
				mapWork.push_back({it->first, job.width, job.height, job.texrender, job.stagesurf, {}});
			}
			++it;
		}
	}

	if (stageWork.empty() && mapWork.empty() && abandonedObjects.empty())
		return;

	obs_enter_graphics();

	for (GpuObjects &item : abandonedObjects) {
		if (item.stagesurf)
			gs_stagesurface_destroy(item.stagesurf);
		if (item.texrender)
			gs_texrender_destroy(item.texrender);
	}
	abandonedObjects.clear();

	for (StageWork &work : stageWork) {
		/* Frame N: stage the render, queueing a GPU copy that lands by
		 * next tick instead of stalling here to wait for it. */
		obs_video_info *current = osn::Video::Manager::GetInstance().find(work.canvasId);
		if (!current || current != work.canvas) {
			work.error = "Canvas removed before the screenshot could be rendered.";
			continue;
		}

		gs_texrender_t *texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		if (!texrender) {
			work.error = "Failed to create texture renderer.";
			continue;
		}
		if (!gs_texrender_begin_with_color_space(texrender, work.width, work.height, GS_CS_SRGB)) {
			gs_texrender_destroy(texrender);
			work.error = "Failed to begin texture render.";
			continue;
		}

		vec4 black;
		vec4_set(&black, 0.0f, 0.0f, 0.0f, 1.0f);
		gs_clear(GS_CLEAR_COLOR, &black, 0.0f, 0);

		gs_viewport_push();
		gs_projection_push();
		gs_ortho(0.0f, float(work.width), 0.0f, float(work.height), -100.0f, 100.0f);
		gs_set_viewport(0, 0, int(work.width), int(work.height));

		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

		obs_render_texture(work.canvas, OBS_MAIN_VIDEO_RENDERING);

		gs_blend_state_pop();
		gs_projection_pop();
		gs_viewport_pop();
		gs_texrender_end(texrender);

		gs_texture_t *tex = gs_texrender_get_texture(texrender);
		gs_stagesurf_t *stagesurf = tex ? gs_stagesurface_create(work.width, work.height, GS_RGBA) : nullptr;
		if (!stagesurf) {
			gs_texrender_destroy(texrender);
			work.error = "Failed to create staging surface.";
			continue;
		}

		gs_stage_texture(stagesurf, tex);
		work.texrender = texrender;
		work.stagesurf = stagesurf;
	}

	/* Frame N+1: the staged copy has landed, so this map does not stall. */
	for (MapWork &work : mapWork) {
		uint8_t *data = nullptr;
		uint32_t linesize = 0;
		if (gs_stagesurface_map(work.stagesurf, &data, &linesize)) {
			const size_t rowBytes = size_t(work.width) * 4;
			work.pixels.resize(rowBytes * work.height);
			for (uint32_t y = 0; y < work.height; ++y) {
				const uint8_t *src = data + size_t(y) * linesize;
				uint8_t *dst = work.pixels.data() + size_t(y) * rowBytes;
				memcpy(dst, src, rowBytes);
				/* Force alpha opaque, matching OBS's QImage::Format_RGBX8888 save path. */
				for (size_t x = 3; x < rowBytes; x += 4)
					dst[x] = 0xFF;
			}
			gs_stagesurface_unmap(work.stagesurf);
			work.mapped = true;
		}

		gs_stagesurface_destroy(work.stagesurf);
		gs_texrender_destroy(work.texrender);
		work.stagesurf = nullptr;
		work.texrender = nullptr;
	}

	obs_leave_graphics();

	std::vector<GpuObjects> orphanedObjects;
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		for (StageWork &work : stageWork) {
			auto it = m_jobs.find(work.jobId);
			if (it == m_jobs.end()) {
				orphanedObjects.push_back({work.texrender, work.stagesurf});
				continue;
			}

			Job &job = it->second;
			job.tickBusy = false;
			if (job.abandoned) {
				orphanedObjects.push_back({work.texrender, work.stagesurf});
				m_jobs.erase(it);
			} else if (!work.error.empty()) {
				job.state = State::Failed;
				job.error = work.error;
			} else {
				job.texrender = work.texrender;
				job.stagesurf = work.stagesurf;
				job.renderStage = RenderStage::Staged;
			}
		}

		for (MapWork &work : mapWork) {
			auto it = m_jobs.find(work.jobId);
			if (it == m_jobs.end())
				continue;

			Job &job = it->second;
			job.tickBusy = false;
			job.texrender = nullptr;
			job.stagesurf = nullptr;
			job.renderStage = RenderStage::Handed;

			if (job.abandoned) {
				m_jobs.erase(it);
			} else if (!work.mapped) {
				job.state = State::Failed;
				job.error = "Failed to map staging surface.";
			} else {
				EncodeTask task;
				task.jobId = work.jobId;
				task.directory = job.directory;
				task.formattedName = job.formattedName;
				task.noSpace = job.noSpace;
				task.width = work.width;
				task.height = work.height;
				task.pixels = std::move(work.pixels);
				m_encodeQueue.push_back(std::move(task));
				++m_encodesInFlight;
				m_encoderCv.notify_one();
			}
		}
	}

	destroyGpuObjects(orphanedObjects);
}

void ScreenshotManager::EncoderThreadMain()
{
	std::unique_lock<std::mutex> lock(m_mutex);
	for (;;) {
		m_encoderCv.wait(lock, [this] { return m_encoderStop || !m_encodeQueue.empty(); });
		if (m_encoderStop)
			return;

		EncodeTask task = std::move(m_encodeQueue.front());
		m_encodeQueue.pop_front();
		lock.unlock();

		std::string path = JoinPath(task.directory, task.formattedName);
		std::string error;
		bool ok = true;

		const std::string dir = DirName(path);
		if (!dir.empty() && os_mkdirs(dir.c_str()) == MKDIR_ERROR) {
			error = "Failed to create screenshot directory '" + dir + "'.";
			ok = false;
		}

		if (ok) {
			FindBestScreenshotFilename(path, task.noSpace);
			if (!gs_save_png_file(path.c_str(), task.pixels.data(), GS_RGBA, task.width, task.height, task.width * 4)) {
				error = "Failed to write PNG '" + path + "'.";
				ok = false;
			}
		}

		if (ok)
			blog(LOG_INFO, "[SCREENSHOT] Saved %ux%u screenshot to '%s'", task.width, task.height, path.c_str());
		else
			blog(LOG_ERROR, "[SCREENSHOT] %s", error.c_str());

		lock.lock();
		--m_encodesInFlight;
		auto it = m_jobs.find(task.jobId);
		if (it != m_jobs.end()) {
			it->second.state = ok ? State::Done : State::Failed;
			it->second.path = path;
			it->second.error = error;
		}
	}
}

void ScreenshotManager::Shutdown()
{
	bool tickWasRegistered = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		tickWasRegistered = m_tickRegistered;
		m_tickRegistered = false;
	}
	// obs_remove_tick_callback blocks until any tick in progress finishes, and
	// that tick needs m_mutex, so it must not be held here.
	if (tickWasRegistered)
		obs_remove_tick_callback(&ScreenshotManager::Tick, this);

	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_encoderStop = true;
	}
	m_encoderCv.notify_all();
	if (m_encoderThread.joinable())
		m_encoderThread.join();
	m_encoderThread = std::thread();

	std::vector<std::pair<gs_texrender_t *, gs_stagesurf_t *>> leftovers;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_encodeQueue.clear();
		m_encodesInFlight = 0;
		for (auto &item : m_jobs)
			leftovers.emplace_back(item.second.texrender, item.second.stagesurf);
		m_jobs.clear();
	}

	if (!leftovers.empty()) {
		obs_enter_graphics();
		for (auto &item : leftovers) {
			if (item.second)
				gs_stagesurface_destroy(item.second);
			if (item.first)
				gs_texrender_destroy(item.first);
		}
		obs_leave_graphics();
	}
}
