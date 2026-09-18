#include <catch2/catch_test_macros.hpp>

#include <obs.h>
#include <media-io/video-frame.h>
#include <util/platform.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include "auto-optimizer-video-mix.hpp"
#include "obs-setup.hpp"

namespace {

TEST_CASE("Auto Optimizer selects the active video rendering mode")
{
	CHECK(autoOptimizer::videoMix::activeRenderingMode(false) == OBS_MAIN_VIDEO_RENDERING);
	CHECK(autoOptimizer::videoMix::activeRenderingMode(true) == OBS_STREAMING_VIDEO_RENDERING);
}

constexpr char TEST_ENCODER_ID[] = "osn_test_texture_encoder";
constexpr char TEST_OUTPUT_ID[] = "osn_test_video_output";
constexpr char TEST_RAW_VIDEO_ENCODER_ID[] = "osn_test_raw_video_encoder";
constexpr char TEST_AUDIO_ENCODER_ID[] = "osn_test_audio_encoder";
constexpr char TEST_AV_OUTPUT_ID[] = "osn_test_av_output";
constexpr char TEST_MULTITRACK_AV_OUTPUT_ID[] = "osn_test_multitrack_av_output";
constexpr uint32_t SOURCE_WIDTH = 1280;
constexpr uint32_t SOURCE_HEIGHT = 720;
constexpr uint32_t SCALED_WIDTH = 960;
constexpr uint32_t SCALED_HEIGHT = 540;

const char *testEncoderName(void *)
{
	return "OSN Test Texture Encoder";
}

void *testEncoderCreate(obs_data_t *, obs_encoder_t *encoder)
{
	return encoder;
}

void testEncoderDestroy(void *) {}

bool testEncoderEncodeTexture(void *, encoder_texture *, int64_t, uint64_t, uint64_t *, encoder_packet *, bool *receivedPacket)
{
	*receivedPacket = false;
	return true;
}

struct TestOutputContext {
	obs_output_t *output = nullptr;
	std::array<std::atomic<uint32_t>, MAX_OUTPUT_VIDEO_ENCODERS> videoPackets{};
	std::atomic<uint32_t> audioPackets{0};
};

const char *testOutputName(void *)
{
	return "OSN Test Video Output";
}

void *testOutputCreate(obs_data_t *, obs_output_t *output)
{
	return new TestOutputContext{output};
}

void testOutputDestroy(void *data)
{
	delete static_cast<TestOutputContext *>(data);
}

bool testOutputStart(void *data)
{
	auto *context = static_cast<TestOutputContext *>(data);
	if (!context || !obs_output_can_begin_data_capture(context->output, 0) || !obs_output_initialize_encoders(context->output, 0))
		return false;
	return obs_output_begin_data_capture(context->output, 0);
}

void testOutputStop(void *data, uint64_t)
{
	auto *context = static_cast<TestOutputContext *>(data);
	if (context)
		obs_output_end_data_capture(context->output);
}

void testOutputPacket(void *data, encoder_packet *packet)
{
	auto *context = static_cast<TestOutputContext *>(data);
	if (!packet)
		return;
	if (packet->type == OBS_ENCODER_VIDEO && packet->track_idx < context->videoPackets.size())
		context->videoPackets[packet->track_idx].fetch_add(1);
	else if (packet->type == OBS_ENCODER_AUDIO)
		context->audioPackets.fetch_add(1);
}

bool testEncoderEncode(void *data, encoder_frame *frame, encoder_packet *packet, bool *receivedPacket)
{
	// Only packet delivery and A/V synchronization matter here; no codec or
	// network dependency is needed to exercise libobs's encoded output path.
	static uint8_t payload[] = {0, 0, 0, 1};
	packet->data = payload;
	packet->size = sizeof(payload);
	packet->pts = frame->pts;
	packet->dts = frame->pts;
	packet->type = obs_encoder_get_type(static_cast<obs_encoder_t *>(data));
	packet->keyframe = true;
	*receivedPacket = true;
	return true;
}

size_t testAudioFrameSize(void *)
{
	return AUDIO_OUTPUT_FRAMES;
}

bool testAudioInput(void *, uint64_t startTimestamp, uint64_t, uint64_t *outputTimestamp, uint32_t, audio_data_mixes_outputs *)
{
	*outputTimestamp = startTimestamp;
	return true;
}

void registerTestTypes()
{
	obs_encoder_info encoderInfo{};
	encoderInfo.id = TEST_ENCODER_ID;
	encoderInfo.type = OBS_ENCODER_VIDEO;
	encoderInfo.codec = "h264";
	encoderInfo.get_name = testEncoderName;
	encoderInfo.create = testEncoderCreate;
	encoderInfo.destroy = testEncoderDestroy;
	encoderInfo.caps = OBS_ENCODER_CAP_PASS_TEXTURE;
	encoderInfo.encode_texture2 = testEncoderEncodeTexture;
	obs_register_encoder(&encoderInfo);

	obs_output_info outputInfo{};
	outputInfo.id = TEST_OUTPUT_ID;
	outputInfo.flags = OBS_OUTPUT_VIDEO | OBS_OUTPUT_ENCODED;
	outputInfo.get_name = testOutputName;
	outputInfo.create = testOutputCreate;
	outputInfo.destroy = testOutputDestroy;
	outputInfo.start = testOutputStart;
	outputInfo.stop = testOutputStop;
	outputInfo.encoded_packet = testOutputPacket;
	obs_register_output(&outputInfo);

	encoderInfo.id = TEST_RAW_VIDEO_ENCODER_ID;
	encoderInfo.caps = 0;
	encoderInfo.encode_texture2 = nullptr;
	encoderInfo.encode = testEncoderEncode;
	obs_register_encoder(&encoderInfo);

	encoderInfo.id = TEST_AUDIO_ENCODER_ID;
	encoderInfo.type = OBS_ENCODER_AUDIO;
	encoderInfo.codec = "aac";
	encoderInfo.get_frame_size = testAudioFrameSize;
	obs_register_encoder(&encoderInfo);

	outputInfo.id = TEST_AV_OUTPUT_ID;
	outputInfo.flags |= OBS_OUTPUT_AUDIO;
	obs_register_output(&outputInfo);

	outputInfo.id = TEST_MULTITRACK_AV_OUTPUT_ID;
	outputInfo.flags |= OBS_OUTPUT_MULTI_TRACK_VIDEO;
	obs_register_output(&outputInfo);
}

obs_video_info makeVideoInfo()
{
	obs_video_info info{};
#ifdef _WIN32
	info.graphics_module = "libobs-d3d11.dll";
#elif defined(__APPLE__)
	info.graphics_module = "libobs-opengl.dylib";
#else
	info.graphics_module = "libobs-opengl.so";
#endif
	info.fps_num = 60;
	info.fps_den = 1;
	info.fps_type = 1;
	info.base_width = SOURCE_WIDTH;
	info.base_height = SOURCE_HEIGHT;
	info.output_width = SOURCE_WIDTH;
	info.output_height = SOURCE_HEIGHT;
	info.output_format = VIDEO_FORMAT_NV12;
	info.adapter = 0;
	info.gpu_conversion = true;
	info.colorspace = VIDEO_CS_709;
	info.range = VIDEO_RANGE_PARTIAL;
	info.scale_type = OBS_SCALE_BILINEAR;
	return info;
}

enum class IdentitySource {
	CanvasOwned,
	Registered,
};

class VideoMixResources {
public:
	~VideoMixResources() { cleanup(); }

	bool initialize(IdentitySource identitySource = IdentitySource::CanvasOwned)
	{
		obs_video_info info = makeVideoInfo();
		if (obs_reset_video(&info) != OBS_VIDEO_SUCCESS)
			return false;

		canvas = obs_get_video_info_by_index2(0);
		if (!canvas)
			return false;

		video_t *sourceVideo = obs_get_video();
		if (identitySource == IdentitySource::Registered) {
			view = obs_view_create();
			sourceVideo = view ? obs_view_add2(view, canvas) : nullptr;
		}
		if (!sourceVideo)
			return false;

		encoder = obs_video_encoder_create(TEST_ENCODER_ID, "osn video mix lifecycle encoder", nullptr, nullptr);
		if (!encoder)
			return false;

		obs_encoder_set_video(encoder, sourceVideo);
		obs_encoder_set_scaled_size(encoder, SCALED_WIDTH, SCALED_HEIGHT);
		obs_encoder_set_gpu_scale_type(encoder, OBS_SCALE_BILINEAR);

		output = obs_output_create(TEST_OUTPUT_ID, "osn video mix lifecycle output", nullptr, nullptr);
		if (!output)
			return false;
		obs_output_set_video_encoder(output, encoder);
		return true;
	}

	bool initializeEncoder() { return obs_output_initialize_encoders(output, 0); }

	bool getEncoderVideoInfo(obs_video_info &info) const { return obs_get_video_info_for_encoder(encoder, &info); }

	int removeCanvas()
	{
		if (!canvas)
			return canvasRemovalResult;

		canvasRemovalResult = obs_remove_video_info(canvas);
		if (canvasRemovalResult == OBS_VIDEO_SUCCESS)
			canvas = nullptr;
		return canvasRemovalResult;
	}

	int cleanup()
	{
		if (cleaned)
			return canvasRemovalResult;
		cleaned = true;

		if (output) {
			obs_output_set_video_encoder(output, nullptr);
			obs_output_release(output);
			output = nullptr;
		}
		if (encoder) {
			obs_encoder_release(encoder);
			encoder = nullptr;
		}
		if (view) {
			obs_view_remove(view);
			obs_view_destroy(view);
			view = nullptr;
		}
		obs_wait_for_destroy_queue();
		removeCanvas();
		return canvasRemovalResult;
	}

private:
	obs_video_info *canvas = nullptr;
	obs_view_t *view = nullptr;
	obs_encoder_t *encoder = nullptr;
	obs_output_t *output = nullptr;
	int canvasRemovalResult = OBS_VIDEO_SUCCESS;
	bool cleaned = false;
};

enum class AudioVideoWorkload {
	SingleTrack,
	Multitrack60Fps,
};

class AudioVideoResources {
public:
	~AudioVideoResources() { cleanup(); }

	bool initialize(bool standaloneVideo = true, AudioVideoWorkload workload = AudioVideoWorkload::SingleTrack)
	{
		const bool multitrack = workload == AudioVideoWorkload::Multitrack60Fps;
		trackCount = multitrack ? videoEncoders.size() : 1;
		inputFps = multitrack ? 60 : 30;
		obs_video_info info = makeVideoInfo();
		if (multitrack)
			info.fps_num = 30;
		if (obs_reset_video(&info) != OBS_VIDEO_SUCCESS)
			return false;
		canvas = obs_get_video_info_by_index2(0);
		if (!canvas)
			return false;

		if (standaloneVideo) {
			video_output_info videoInfo{};
			videoInfo.name = "osn standalone A/V input";
			videoInfo.format = VIDEO_FORMAT_NV12;
			videoInfo.fps_num = inputFps;
			videoInfo.fps_den = 1;
			videoInfo.width = 64;
			videoInfo.height = 64;
			videoInfo.cache_size = 3;
			videoInfo.colorspace = VIDEO_CS_709;
			videoInfo.range = VIDEO_RANGE_PARTIAL;
			if (video_output_open(&ownedVideo, &videoInfo) != VIDEO_OUTPUT_SUCCESS)
				return false;
		}

		audio_output_info audioInfo{};
		audioInfo.name = "osn standalone A/V audio";
		audioInfo.samples_per_sec = 48000;
		audioInfo.format = AUDIO_FORMAT_FLOAT_PLANAR;
		audioInfo.speakers = SPEAKERS_STEREO;
		audioInfo.input_callback = testAudioInput;
		if (audio_output_open(&audio, &audioInfo) != AUDIO_OUTPUT_SUCCESS)
			return false;

		audioEncoder = obs_audio_encoder_create(TEST_AUDIO_ENCODER_ID, "osn A/V audio encoder", nullptr, 0, nullptr);
		if (!audioEncoder)
			return false;
		for (size_t track = 0; track < trackCount; track++) {
			const auto name = "osn A/V video encoder " + std::to_string(track);
			auto *&encoder = videoEncoders[track];
			encoder = obs_video_encoder_create(TEST_RAW_VIDEO_ENCODER_ID, name.c_str(), nullptr, nullptr);
			if (!encoder)
				return false;
			if (ownedVideo) {
				obs_encoder_set_video(encoder, ownedVideo);
			} else {
				// obs_get_video() belongs to the core main canvas, which survives a
				// partial video reset. Bind the registered canvas removed by this test.
				obs_core_video_mix_t *mix = obs_video_mix_get(canvas, OBS_MAIN_VIDEO_RENDERING);
				if (!mix)
					return false;
				obs_encoder_set_video_mix(encoder, mix);
			}
			// Match the 60/60/30/30 FPS ladder returned by Twitch for the failed
			// promotion, without requiring NVENC, Twitch credentials, or a network.
			obs_encoder_set_frame_rate_divisor(encoder, track < 2 ? 1 : 2);
		}
		obs_encoder_set_audio(audioEncoder, audio);

		output = obs_output_create(multitrack ? TEST_MULTITRACK_AV_OUTPUT_ID : TEST_AV_OUTPUT_ID, "osn A/V output", nullptr, nullptr);
		if (!output)
			return false;
		for (size_t track = 0; track < trackCount; track++)
			obs_output_set_video_encoder2(output, videoEncoders[track], track);
		obs_output_set_audio_encoder(output, audioEncoder, 0);
		return true;
	}

	bool groupVideoEncoders()
	{
		encoderGroup = obs_encoder_group_create();
		if (!encoderGroup)
			return false;
		for (size_t track = 0; track < trackCount; track++) {
			if (!obs_encoder_set_group(videoEncoders[track], encoderGroup))
				return false;
		}
		return true;
	}

	bool initializeEncoders() { return obs_output_initialize_encoders(output, 0); }
	bool start() { return obs_output_start(output); }
	bool hasVideoInput() const { return obs_encoder_video(videoEncoders[0]) != nullptr; }
	uint32_t inputFrames() const { return video_output_get_total_frames(ownedVideo); }
	uint32_t videoPackets(size_t track) const { return static_cast<TestOutputContext *>(obs_obj_get_data(output))->videoPackets[track].load(); }
	uint32_t audioPackets() const { return static_cast<TestOutputContext *>(obs_obj_get_data(output))->audioPackets.load(); }

	bool waitForAudioVideoPackets()
	{
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
		const auto frameDuration = std::chrono::nanoseconds(1000000000ULL / inputFps);
		auto nextFrame = std::chrono::steady_clock::now();
		// This input deliberately has its own timestamp origin, just like the
		// optimizer's synthetic feeder. Do not align it to the graphics thread:
		// doing so would hide the grouped-encoder startup regression.
		uint64_t timestamp = os_gettime_ns();
		while (std::chrono::steady_clock::now() < deadline) {
			bool allTracksDelivered = audioPackets() >= 3;
			for (size_t track = 0; track < trackCount; track++)
				allTracksDelivered = allTracksDelivered && videoPackets(track) >= 3;
			if (allTracksDelivered)
				return true;

			video_frame frame{};
			if (video_output_lock_frame(ownedVideo, &frame, 1, timestamp)) {
				for (size_t row = 0; row < 64; row++)
					std::memset(frame.data[0] + row * frame.linesize[0], 16, 64);
				for (size_t row = 0; row < 32; row++)
					std::memset(frame.data[1] + row * frame.linesize[1], 128, 64);
				video_output_unlock_frame(ownedVideo);
			}
			// Feed on this bounded test loop, avoiding a worker that could outlive
			// an assertion failure and retain the standalone video input.
			timestamp += frameDuration.count();
			nextFrame += frameDuration;
			std::this_thread::sleep_until(nextFrame);
		}
		return false;
	}

	int removeCanvas()
	{
		if (!canvas)
			return OBS_VIDEO_SUCCESS;
		const int result = obs_remove_video_info(canvas);
		if (result == OBS_VIDEO_SUCCESS)
			canvas = nullptr;
		return result;
	}

	int cleanup()
	{
		if (output) {
			if (obs_output_active(output))
				obs_output_force_stop(output);
			// Releasing the output joins its data-capture shutdown before the
			// encoders and their privately owned inputs are destroyed.
			obs_output_release(output);
			output = nullptr;
		}
		if (encoderGroup) {
			obs_encoder_group_destroy(encoderGroup);
			encoderGroup = nullptr;
		}
		for (auto *&encoder : videoEncoders) {
			if (encoder)
				obs_encoder_release(encoder);
			encoder = nullptr;
		}
		if (audioEncoder) {
			obs_encoder_release(audioEncoder);
			audioEncoder = nullptr;
		}
		obs_wait_for_destroy_queue();
		if (ownedVideo) {
			video_output_close(ownedVideo);
			ownedVideo = nullptr;
		}
		if (audio) {
			audio_output_close(audio);
			audio = nullptr;
		}
		return removeCanvas();
	}

private:
	obs_video_info *canvas = nullptr;
	video_t *ownedVideo = nullptr;
	audio_t *audio = nullptr;
	std::array<obs_encoder_t *, 4> videoEncoders{};
	size_t trackCount = 1;
	uint32_t inputFps = 30;
	obs_encoder_group_t *encoderGroup = nullptr;
	obs_encoder_t *audioEncoder = nullptr;
	obs_output_t *output = nullptr;
};

void checkStandaloneMultitrackPackets(bool grouped)
{
	osn::tests::ObsSetup setup;
	registerTestTypes();

	// Exercise a fresh output twice so a failed startup must also release the
	// encoder group, its audio pairing, and the independent input safely.
	for (int iteration = 0; iteration < 2; iteration++) {
		INFO("lifecycle iteration " << iteration << ", grouped=" << grouped);
		AudioVideoResources resources;
		REQUIRE(resources.initialize(true, AudioVideoWorkload::Multitrack60Fps));
		if (grouped)
			REQUIRE(resources.groupVideoEncoders());
		REQUIRE(resources.start());
		resources.waitForAudioVideoPackets();
		CHECK(resources.inputFrames() >= 3);
		for (size_t track = 0; track < 4; track++) {
			INFO("video track " << track);
			CHECK(resources.videoPackets(track) >= 3);
		}
		CHECK(resources.audioPackets() >= 3);
		const auto cleanupStart = std::chrono::steady_clock::now();
		CHECK(resources.cleanup() == OBS_VIDEO_SUCCESS);
		CHECK(std::chrono::steady_clock::now() - cleanupStart < std::chrono::seconds(3));
		CHECK(obs_get_video_info_by_index2(0) == nullptr);
	}
}

} // namespace

TEST_CASE("Standalone video and audio inputs deliver encoded A/V packets", "[video-mix][standalone-av]")
{
	osn::tests::ObsSetup setup;
	registerTestTypes();

	// Recreate the inputs and output to catch pairing state retained by cleanup.
	for (int iteration = 0; iteration < 2; iteration++) {
		INFO("lifecycle iteration " << iteration);
		AudioVideoResources resources;
		REQUIRE(resources.initialize());
		REQUIRE(resources.start());
		CHECK(resources.waitForAudioVideoPackets());
		const auto cleanupStart = std::chrono::steady_clock::now();
		CHECK(resources.cleanup() == OBS_VIDEO_SUCCESS);
		CHECK(std::chrono::steady_clock::now() - cleanupStart < std::chrono::seconds(3));
		CHECK(obs_get_video_info_by_index2(0) == nullptr);
	}
}

TEST_CASE("Encoded A/V output cannot start after its canvas input is removed", "[video-mix][canvas-identity][standalone-av]")
{
	osn::tests::ObsSetup setup;
	registerTestTypes();

	AudioVideoResources resources;
	REQUIRE(resources.initialize(false));
	REQUIRE(resources.initializeEncoders());
	REQUIRE(resources.hasVideoInput());
	REQUIRE(resources.removeCanvas() == OBS_VIDEO_SUCCESS);
	REQUIRE_FALSE(resources.hasVideoInput());
	CHECK_FALSE(resources.start());
	CHECK(resources.cleanup() == OBS_VIDEO_SUCCESS);
}

TEST_CASE("Ungrouped standalone 60 FPS ladder delivers A/V with a 30 FPS renderer", "[video-mix][standalone-av][multitrack-cadence]")
{
	checkStandaloneMultitrackPackets(false);
}

TEST_CASE("Grouped standalone 60 FPS ladder delivers A/V with a 30 FPS renderer", "[video-mix][standalone-av][multitrack-cadence]")
{
	checkStandaloneMultitrackPackets(true);
}

TEST_CASE("Encoder GPU rescale supports a canvas-owned identity across reinitialization", "[video-mix][canvas-identity]")
{
	osn::tests::ObsSetup setup;
	registerTestTypes();

	// Repeat encoder cleanup followed by canvas removal to verify that the
	// encoder-only mix leaves no stale identity state before video is initialized again.
	for (int iteration = 0; iteration < 2; iteration++) {
		INFO("lifecycle iteration " << iteration);
		VideoMixResources resources;
		REQUIRE(resources.initialize());

		obs_video_info sourceInfo{};
		REQUIRE(resources.getEncoderVideoInfo(sourceInfo));
		CHECK(sourceInfo.output_width == SOURCE_WIDTH);
		CHECK(sourceInfo.output_height == SOURCE_HEIGHT);

		REQUIRE(resources.initializeEncoder());
		obs_video_info encoderInfo{};
		REQUIRE(resources.getEncoderVideoInfo(encoderInfo));
		CHECK(encoderInfo.output_width == SCALED_WIDTH);
		CHECK(encoderInfo.output_height == SCALED_HEIGHT);

		CHECK(resources.cleanup() == OBS_VIDEO_SUCCESS);
		CHECK(obs_get_video_info_by_index2(0) == nullptr);
	}
}

TEST_CASE("Encoder GPU rescale retains a registered identity until teardown", "[video-mix][canvas-identity]")
{
	osn::tests::ObsSetup setup;
	registerTestTypes();

	VideoMixResources resources;
	REQUIRE(resources.initialize(IdentitySource::Registered));
	REQUIRE(resources.initializeEncoder());

	obs_video_info encoderInfo{};
	REQUIRE(resources.getEncoderVideoInfo(encoderInfo));
	CHECK(encoderInfo.output_width == SCALED_WIDTH);
	CHECK(encoderInfo.output_height == SCALED_HEIGHT);

	CHECK(resources.removeCanvas() == OBS_VIDEO_INFO_IN_USE);
	CHECK(resources.cleanup() == OBS_VIDEO_SUCCESS);
	CHECK(obs_get_video_info_by_index2(0) == nullptr);
}

TEST_CASE("Auxiliary video mixes use the shared render cadence", "[video-mix][frame-rate]")
{
	osn::tests::ObsSetup setup;

	obs_video_info info = makeVideoInfo();
	REQUIRE(obs_reset_video(&info) == OBS_VIDEO_SUCCESS);
	obs_video_info *canvas = obs_get_video_info_by_index2(0);
	REQUIRE(canvas);
	obs_core_video_mix_t *identityMix = obs_video_mix_get(canvas, OBS_MAIN_VIDEO_RENDERING);
	REQUIRE(identityMix);

	obs_view_t *view = obs_view_create();
	REQUIRE(view);

	obs_video_info auxiliaryInfo = makeVideoInfo();
	auxiliaryInfo.fps_num = 30;
	CHECK(obs_view_add_auxiliary_mix(view, &auxiliaryInfo, identityMix) == nullptr);

	// Equivalent rational rates are accepted even when represented differently.
	auxiliaryInfo.fps_num = 60000;
	auxiliaryInfo.fps_den = 1000;
	obs_core_video_mix_t *auxiliaryMix = obs_view_add_auxiliary_mix(view, &auxiliaryInfo, identityMix);
	REQUIRE(auxiliaryMix);

	video_t *auxiliaryVideo = obs_video_mix_get_video(auxiliaryMix);
	REQUIRE(auxiliaryVideo);
	const video_output_info *outputInfo = video_output_get_info(auxiliaryVideo);
	REQUIRE(outputInfo);
	CHECK(outputInfo->fps_num == 60);
	CHECK(outputInfo->fps_den == 1);

	obs_view_remove(view);
	obs_view_destroy(view);
	obs_wait_for_destroy_queue();
	CHECK(obs_remove_video_info(canvas) == OBS_VIDEO_SUCCESS);
}
