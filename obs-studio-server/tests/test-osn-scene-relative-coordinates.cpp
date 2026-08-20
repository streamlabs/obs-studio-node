#include <catch2/catch_test_macros.hpp>

#include <obs.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>

namespace {

constexpr char TEST_SOURCE_ID[] = "relative_coordinate_test_source";
constexpr float TEST_EPSILON = 0.001f;

const char *testSourceGetName(void *)
{
	return "Relative Coordinate Test Source";
}

void *testSourceCreate(obs_data_t *, obs_source_t *source)
{
	return source;
}

void testSourceDestroy(void *) {}

uint32_t testSourceGetWidth(void *)
{
	return 320;
}

uint32_t testSourceGetHeight(void *)
{
	return 180;
}

obs_source_info makeTestSourceInfo()
{
	obs_source_info info{};
	info.id = TEST_SOURCE_ID;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = testSourceGetName;
	info.create = testSourceCreate;
	info.destroy = testSourceDestroy;
	info.get_width = testSourceGetWidth;
	info.get_height = testSourceGetHeight;
	return info;
}

obs_transform_info makeAuthoredTransform()
{
	obs_transform_info transform{};
	transform.pos = {123.5f, 234.5f};
	transform.rot = 17.0f;
	transform.scale = {1.25f, 0.75f};
	transform.alignment = OBS_ALIGN_CENTER;
	transform.bounds_type = OBS_BOUNDS_STRETCH;
	transform.bounds_alignment = OBS_ALIGN_RIGHT | OBS_ALIGN_BOTTOM;
	transform.bounds = {640.0f, 360.0f};
	transform.crop_to_bounds = true;
	return transform;
}

struct ObsDataDeleter {
	void operator()(obs_data_t *data) const
	{
		if (data)
			obs_data_release(data);
	}
};

struct ObsDataArrayDeleter {
	void operator()(obs_data_array_t *array) const
	{
		if (array)
			obs_data_array_release(array);
	}
};

using ObsDataPtr = std::unique_ptr<obs_data_t, ObsDataDeleter>;
using ObsDataArrayPtr = std::unique_ptr<obs_data_array_t, ObsDataArrayDeleter>;

class SavedSceneItem {
public:
	explicit SavedSceneItem(obs_sceneitem_t *item) : array(obs_data_array_create())
	{
		if (!array)
			return;

		obs_sceneitem_save(item, array.get());
		if (obs_data_array_count(array.get()) == 1)
			data.reset(obs_data_array_item(array.get(), 0));
	}

	explicit operator bool() const { return data != nullptr; }
	obs_data_t *get() const { return data.get(); }

private:
	ObsDataArrayPtr array;
	ObsDataPtr data;
};

class SceneFixture {
public:
	~SceneFixture() { cleanup(); }

	bool initialize()
	{
		started = obs_startup("en-US", nullptr, nullptr);
		if (!started)
			return false;

		ObsDataPtr privateData(obs_data_create());
		if (!privateData)
			return false;
		obs_data_set_bool(privateData.get(), "AbsoluteCoordinates", false);
		obs_apply_private_data(privateData.get());

		static const obs_source_info testSourceInfo = makeTestSourceInfo();
		obs_register_source(&testSourceInfo);

		scene = obs_scene_create_private("relative coordinate test scene");
		source = obs_source_create_private(TEST_SOURCE_ID, "relative coordinate test source", nullptr);
		if (!scene || !source)
			return false;

		item = obs_scene_add(scene, source);
		return item != nullptr;
	}

	obs_video_info *createCanvas(uint32_t width, uint32_t height)
	{
		if (canvas)
			return nullptr;

		canvas = obs_create_video_info();
		if (!canvas)
			return nullptr;

		canvas->base_width = width;
		canvas->base_height = height;
		canvas->output_width = width;
		canvas->output_height = height;
		return canvas;
	}

	int cleanup()
	{
		if (cleaned)
			return canvasRemovalResult;
		cleaned = true;

		if (item) {
			obs_sceneitem_set_canvas(item, nullptr);
			obs_sceneitem_remove(item);
			item = nullptr;
		}
		if (source) {
			obs_source_release(source);
			source = nullptr;
		}
		if (scene) {
			obs_scene_release(scene);
			scene = nullptr;
		}
		if (canvas) {
			canvasRemovalResult = obs_remove_video_info(canvas);
			canvas = nullptr;
		}
		if (started) {
			obs_shutdown();
			started = false;
		}

		return canvasRemovalResult;
	}

	obs_sceneitem_t *item = nullptr;

private:
	obs_scene_t *scene = nullptr;
	obs_source_t *source = nullptr;
	obs_video_info *canvas = nullptr;
	int canvasRemovalResult = OBS_VIDEO_SUCCESS;
	bool started = false;
	bool cleaned = false;
};

void checkVec2Finite(const vec2 &value)
{
	CHECK(std::isfinite(value.x));
	CHECK(std::isfinite(value.y));
}

void checkVec2Equal(const vec2 &actual, const vec2 &expected)
{
	CHECK(std::fabs(actual.x - expected.x) <= TEST_EPSILON);
	CHECK(std::fabs(actual.y - expected.y) <= TEST_EPSILON);
}

void checkTransformFinite(const obs_transform_info &transform)
{
	checkVec2Finite(transform.pos);
	CHECK(std::isfinite(transform.rot));
	checkVec2Finite(transform.scale);
	checkVec2Finite(transform.bounds);
}

void checkTransformEqual(const obs_transform_info &actual, const obs_transform_info &expected)
{
	checkVec2Equal(actual.pos, expected.pos);
	CHECK(std::fabs(actual.rot - expected.rot) <= TEST_EPSILON);
	checkVec2Equal(actual.scale, expected.scale);
	CHECK(actual.alignment == expected.alignment);
	CHECK(actual.bounds_type == expected.bounds_type);
	CHECK(actual.bounds_alignment == expected.bounds_alignment);
	checkVec2Equal(actual.bounds, expected.bounds);
	CHECK(actual.crop_to_bounds == expected.crop_to_bounds);
}

void checkSavedVectorsFinite(obs_data_t *data)
{
	constexpr std::array<const char *, 7> vectorNames = {
		"pos", "pos_rel", "scale", "scale_rel", "scale_ref", "bounds", "bounds_rel",
	};

	for (const char *name : vectorNames) {
		REQUIRE(obs_data_has_user_value(data, name));
		vec2 value{};
		obs_data_get_vec2(data, name, &value);
		checkVec2Finite(value);
	}
}

vec2 savedScaleReference(obs_data_t *data)
{
	vec2 value{};
	obs_data_get_vec2(data, "scale_ref", &value);
	return value;
}

} // namespace

TEST_CASE("Relative scene items created before video use a finite fallback", "[scene][relative-coordinates]")
{
	SceneFixture fixture;
	REQUIRE(fixture.initialize());

	obs_video_info currentVideo{};
	CHECK_FALSE(obs_get_video_info_current(&currentVideo));

	obs_transform_info initial{};
	obs_sceneitem_get_info2(fixture.item, &initial);
	checkTransformFinite(initial);

	const obs_transform_info authored = makeAuthoredTransform();
	obs_sceneitem_set_info2(fixture.item, &authored);

	obs_transform_info actual{};
	obs_sceneitem_get_info2(fixture.item, &actual);
	checkTransformFinite(actual);
	checkTransformEqual(actual, authored);

	SavedSceneItem saved(fixture.item);
	REQUIRE(saved);
	checkSavedVectorsFinite(saved.get());
	checkVec2Equal(savedScaleReference(saved.get()), {1.0f, 1.0f});

	CHECK(fixture.cleanup() == OBS_VIDEO_SUCCESS);
}

TEST_CASE("Assigning a canvas rebases the temporary scene item reference", "[scene][relative-coordinates]")
{
	SceneFixture fixture;
	REQUIRE(fixture.initialize());

	const obs_transform_info authored = makeAuthoredTransform();
	obs_sceneitem_set_info2(fixture.item, &authored);

	SavedSceneItem beforeAssignment(fixture.item);
	REQUIRE(beforeAssignment);
	checkVec2Equal(savedScaleReference(beforeAssignment.get()), {1.0f, 1.0f});

	obs_video_info *canvas = fixture.createCanvas(1920, 1080);
	REQUIRE(canvas != nullptr);
	CHECK_FALSE(canvas->initialized);

	obs_sceneitem_set_canvas(fixture.item, canvas);
	REQUIRE(obs_sceneitem_get_canvas(fixture.item) == canvas);

	obs_transform_info actual{};
	obs_sceneitem_get_info2(fixture.item, &actual);
	checkTransformFinite(actual);
	checkTransformEqual(actual, authored);

	SavedSceneItem afterAssignment(fixture.item);
	REQUIRE(afterAssignment);
	checkSavedVectorsFinite(afterAssignment.get());
	checkVec2Equal(savedScaleReference(afterAssignment.get()), {1920.0f, 1080.0f});

	CHECK(fixture.cleanup() == OBS_VIDEO_SUCCESS);
}
