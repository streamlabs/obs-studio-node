#include <catch2/catch_test_macros.hpp>

#include <obs.h>

#include "autoconfig-video-mix.hpp"
#include "obs-setup.hpp"

namespace {

TEST_CASE("Auto Optimizer selects the active video rendering mode")
{
	CHECK(autoConfig::videoMix::activeRenderingMode(false) == OBS_MAIN_VIDEO_RENDERING);
	CHECK(autoConfig::videoMix::activeRenderingMode(true) == OBS_STREAMING_VIDEO_RENDERING);
}

constexpr char TEST_ENCODER_ID[] = "osn_test_texture_encoder";
constexpr char TEST_OUTPUT_ID[] = "osn_test_video_output";
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

void testOutputPacket(void *, encoder_packet *) {}

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

} // namespace

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
