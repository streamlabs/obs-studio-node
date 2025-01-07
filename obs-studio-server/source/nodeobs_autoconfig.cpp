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
#include <array>
#include <future>
#include "osn-error.hpp"
#include "shared.hpp"

enum class Type { Invalid, Streaming, Recording };

enum class Service { Twitch, Hitbox, Beam, YouTube, Other };

enum class Encoder { x264, NVENC, QSV, AMD, Stream, appleHW, appleHWM1 };

enum class Quality { Stream, High };

enum class FPSType : int { PreferHighFPS, PreferHighRes, UseCurrent, fps30, fps60 };

enum ThreadedTests : int { BandwidthTest, StreamEncoderTest, RecordingEncoderTest, SaveStreamSettings, SaveSettings, SetDefaultSettings, Count };

class AutoConfigInfo {
public:
	AutoConfigInfo(const std::string &a_event, const std::string &a_description, double a_percentage)
	{
		event = a_event;
		description = a_description;
		percentage = a_percentage;
	};
	~AutoConfigInfo(){};

	std::string event;
	std::string description;
	double percentage;
};

std::array<std::future<void>, ThreadedTests::Count> asyncTests;
std::mutex eventsMutex;
std::queue<AutoConfigInfo> events;

Service serviceSelected = Service::Other;
Quality recordingQuality = Quality::Stream;
Encoder recordingEncoder = Encoder::Stream;
Encoder streamingEncoder = Encoder::x264;
Type type = Type::Streaming;
FPSType fpsType = FPSType::PreferHighFPS;
uint64_t idealBitrate = 2500;
uint64_t baseResolutionCX = 1920;
uint64_t baseResolutionCY = 1080;
uint64_t idealResolutionCX = 1280;
uint64_t idealResolutionCY = 720;
int idealFPSNum = 60;
int idealFPSDen = 1;
std::string serviceName;
std::string serverName;
std::string server;
std::string key;

bool hardwareEncodingAvailable = false;
bool nvencAvailable = false;
bool jimnvencAvailable = false;
bool qsvAvailable = false;
bool vceAvailable = false;
bool appleHWAvailable = false;

int startingBitrate = 2500;
bool customServer = false;
bool bandwidthTest = true;
bool testRegions = true;

bool regionNA = false;
bool regionSA = false;
bool regionEU = false;
bool regionAS = false;
bool regionOC = false;

bool preferHighFPS = true;
bool preferHardware = true;
int specificFPSNum = 0;
int specificFPSDen = 0;

std::condition_variable cv;
std::mutex m;
bool cancel = false;
bool started = false;

bool softwareTested = false;

struct ServerInfo {
	std::string name;
	std::string address;
	int bitrate = 0;
	int ms = -1;

	inline ServerInfo() {}

	inline ServerInfo(const char *name_, const char *address_) : name(name_), address(address_) {}
};
void autoConfig::Register(ipc::server &srv)
{
	std::shared_ptr<ipc::collection> cls = std::make_shared<ipc::collection>("AutoConfig");

	cls->register_function(std::make_shared<ipc::function>("InitializeAutoConfig", std::vector<ipc::type>{ipc::type::String, ipc::type::String},
							       autoConfig::InitializeAutoConfig));
	cls->register_function(std::make_shared<ipc::function>("StartBandwidthTest", std::vector<ipc::type>{}, autoConfig::StartBandwidthTest));
	cls->register_function(std::make_shared<ipc::function>("StartStreamEncoderTest", std::vector<ipc::type>{}, autoConfig::StartStreamEncoderTest));
	cls->register_function(std::make_shared<ipc::function>("StartRecordingEncoderTest", std::vector<ipc::type>{}, autoConfig::StartRecordingEncoderTest));
	cls->register_function(std::make_shared<ipc::function>("StartCheckSettings", std::vector<ipc::type>{}, autoConfig::StartCheckSettings));
	cls->register_function(std::make_shared<ipc::function>("StartSetDefaultSettings", std::vector<ipc::type>{}, autoConfig::StartSetDefaultSettings));
	cls->register_function(std::make_shared<ipc::function>("StartSaveStreamSettings", std::vector<ipc::type>{}, autoConfig::StartSaveStreamSettings));
	cls->register_function(std::make_shared<ipc::function>("StartSaveSettings", std::vector<ipc::type>{}, autoConfig::StartSaveSettings));
	cls->register_function(std::make_shared<ipc::function>("TerminateAutoConfig", std::vector<ipc::type>{}, autoConfig::TerminateAutoConfig));
	cls->register_function(std::make_shared<ipc::function>("Query", std::vector<ipc::type>{}, autoConfig::Query));

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

void autoConfig::TestHardwareEncoding(void)
{
	size_t idx = 0;
	const char *id;
	while (obs_enum_encoder_types(idx++, &id)) {
		if (id == nullptr)
			continue;
		if (strcmp(id, "ffmpeg_nvenc") == 0)
			hardwareEncodingAvailable = nvencAvailable = true;
		if (strcmp(id, "jim_nvenc") == 0)
			hardwareEncodingAvailable = jimnvencAvailable = true;
		else if (strcmp(id, "obs_qsv11") == 0)
			hardwareEncodingAvailable = qsvAvailable = true;
		else if (strcmp(id, "h264_texture_amf") == 0)
			hardwareEncodingAvailable = vceAvailable = true;
		else if (strcmp(id, APPLE_HARDWARE_VIDEO_ENCODER) == 0 || strcmp(id, APPLE_HARDWARE_VIDEO_ENCODER_M1) == 0)
			hardwareEncodingAvailable = appleHWAvailable = true;
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

bool autoConfig::CanTestServer(const char *server)
{
	if (!testRegions || (regionNA && regionSA && regionEU && regionAS && regionOC))
		return true;

	if (serviceSelected == Service::Twitch) {
		if (astrcmp_n(server, "NA:", 3) == 0 || astrcmp_n(server, "US West:", 8) == 0 || astrcmp_n(server, "US East:", 8) == 0 ||
		    astrcmp_n(server, "US Central:", 11) == 0) {
			return regionNA;
		} else if (astrcmp_n(server, "South America:", 14) == 0) {
			return regionSA;
		} else if (astrcmp_n(server, "EU:", 3) == 0) {
			return regionEU;
		} else if (astrcmp_n(server, "Asia:", 5) == 0) {
			return regionAS;
		} else if (astrcmp_n(server, "Australia:", 10) == 0) {
			return regionOC;
		} else {
			return true;
		}
	} else if (serviceSelected == Service::Hitbox) {
		if (strcmp(server, "Default") == 0) {
			return true;
		} else if (astrcmp_n(server, "US-West:", 8) == 0 || astrcmp_n(server, "US-East:", 8) == 0) {
			return regionNA;
		} else if (astrcmp_n(server, "South America:", 14) == 0) {
			return regionSA;
		} else if (astrcmp_n(server, "EU-", 3) == 0) {
			return regionEU;
		} else if (astrcmp_n(server, "South Korea:", 12) == 0 || astrcmp_n(server, "Asia:", 5) == 0 || astrcmp_n(server, "China:", 6) == 0) {
			return regionAS;
		} else if (astrcmp_n(server, "Oceania:", 8) == 0) {
			return regionOC;
		} else {
			return true;
		}
	} else if (serviceSelected == Service::Beam) {
		if (astrcmp_n(server, "US:", 3) == 0 || astrcmp_n(server, "Canada:", 7) || astrcmp_n(server, "Mexico:", 7)) {
			return regionNA;
		} else if (astrcmp_n(server, "Brazil:", 7) == 0) {
			return regionSA;
		} else if (astrcmp_n(server, "EU:", 3) == 0) {
			return regionEU;
		} else if (astrcmp_n(server, "South Korea:", 12) == 0 || astrcmp_n(server, "Asia:", 5) == 0 || astrcmp_n(server, "India:", 6) == 0) {
			return regionAS;
		} else if (astrcmp_n(server, "Australia:", 10) == 0) {
			return regionOC;
		} else {
			return true;
		}
	} else {
		return true;
	}

	return false;
}

void GetServers(std::vector<ServerInfo> &servers)
{
	OBSData settings = obs_data_create();
	obs_data_release(settings);
	// obs_data_set_string(settings, "service", wiz->serviceName.c_str());
	//FIX ME
	obs_data_set_string(settings, "service", serviceName.c_str());

	obs_properties_t *ppts = obs_get_service_properties("rtmp_common");
	obs_property_t *p = obs_properties_get(ppts, "service");
	obs_property_modified(p, settings);

	p = obs_properties_get(ppts, "server");
	size_t count = obs_property_list_item_count(p);
	servers.reserve(count);

	for (size_t i = 0; i < count; i++) {
		const char *name = obs_property_list_item_name(p, i);
		const char *server = obs_property_list_item_string(p, i);

		if (autoConfig::CanTestServer(name)) {
			ServerInfo info(name, server);
			servers.push_back(info);
		}
	}

	obs_properties_destroy(ppts);
}

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

	events.pop();

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
	serverName = "Auto (Recommended)";
	server = "auto";

	obs_output_t *streamOutput = OBS_service::getStreamingOutput(StreamServiceId::Main);
	if (streamOutput)
		OBS_service::setStreamingOutput(nullptr, StreamServiceId::Main);

	cancel = false;

	rval.push_back(ipc::value((uint64_t)ErrorCode::Ok));
	AUTO_DEBUG;
}

void autoConfig::StartBandwidthTest(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval)
{
	asyncTests[ThreadedTests::BandwidthTest] = std::async(std::launch::async, TestBandwidthThread);

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

	startingBitrate = (int)obs_data_get_int(vencoder_settings, "bitrate");
	if (obs_output_get_frames_dropped(output) || (int)bitrate < (startingBitrate * 75 / 100)) {
		server.bitrate = (int)bitrate * 70 / 100;
	} else {
		server.bitrate = startingBitrate;
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

void autoConfig::TestBandwidthThread(void)
{
	eventsMutex.lock();
	events.push(AutoConfigInfo("starting_step", "bandwidth_test", 0));
	eventsMutex.unlock();

	bool connected = false;
	bool stopped = false;
	bool errorOnStop = false;
	bool gotError = false;

	obs_video_info video = {0};
	bool have_users_info = obs_get_video_info(&video);

	obs_video_info *ovi = obs_create_video_info();

	if (!have_users_info) {
		video = *ovi;
		video.fps_num = 60;
		video.fps_den = 1;
	} else {
		video.fps_num = ovi->fps_num;
		video.fps_den = ovi->fps_den;
	}

	video.base_width = 1280;
	video.base_height = 720;
	video.output_width = 128;
	video.output_height = 128;

	int ret = obs_set_video_info(ovi, &video);
	if (ret != OBS_VIDEO_SUCCESS) {
		eventsMutex.lock();
		events.push(AutoConfigInfo("error", "invalid_video_settings", 0));
		eventsMutex.unlock();
		obs_remove_video_info(ovi);
		return;
	}

	const char *serverType = "rtmp_common";

	OBSEncoder vencoder = obs_video_encoder_create("obs_x264", "test_x264", nullptr, nullptr);
	OBSEncoder aencoder = obs_audio_encoder_create("ffmpeg_aac", "test_aac", nullptr, 0, nullptr);
	OBSService service = obs_service_create(serverType, "test_service", nullptr, nullptr);
	OBSOutput output = obs_output_create("rtmp_output", "test_stream", nullptr, nullptr);

	/* -----------------------------------*/
	/* configure settings                 */

	// service: "service", "server", "key"
	// vencoder: "bitrate", "rate_control",
	//           obs_service_apply_encoder_settings
	// aencoder: "bitrate"
	// output: "bind_ip" via main config -> "Output", "BindIP"
	//         obs_output_set_service

	OBSData service_settings = obs_data_create();
	OBSData vencoder_settings = obs_data_create();
	OBSData aencoder_settings = obs_data_create();
	OBSData output_settings = obs_data_create();
	obs_data_release(service_settings);
	obs_data_release(vencoder_settings);
	obs_data_release(aencoder_settings);
	obs_data_release(output_settings);

	obs_service_t *currentService = OBS_service::getService(StreamServiceId::Main);
	if (currentService) {
		obs_data_t *currentServiceSettings = obs_service_get_settings(currentService);
		if (currentServiceSettings) {
			if (serviceName.compare("") == 0)
				serviceName = obs_data_get_string(currentServiceSettings, "service");

			key = obs_service_get_key(currentService);
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
		obs_output_release(output);
		obs_encoder_release(vencoder);
		obs_encoder_release(aencoder);
		obs_service_release(service);
		obs_remove_video_info(ovi);
		return;
	}

	if (!customServer) {
		if (serviceName == "Twitch")
			serviceSelected = Service::Twitch;
		else if (serviceName == "hitbox.tv")
			serviceSelected = Service::Hitbox;
		else if (serviceName == "beam.pro")
			serviceSelected = Service::Beam;
		else if (serviceName.find("YouTube") != std::string::npos)
			serviceSelected = Service::YouTube;
		else
			serviceSelected = Service::Other;
	} else {
		serviceSelected = Service::Other;
	}
	std::string keyToEvaluate = key;

	if (serviceSelected == Service::Twitch) {
		string_depad_key(key);
		keyToEvaluate += "?bandwidthtest";
	}

	if (serviceSelected == Service::YouTube) {
		serverName = "Stream URL";
		server = obs_service_get_url(currentService);
	}

	obs_data_set_string(service_settings, "service", serviceName.c_str());
	obs_data_set_string(service_settings, "key", keyToEvaluate.c_str());

	//Setting starting bitrate
	OBSData service_settingsawd = obs_data_create();
	obs_data_release(service_settingsawd);

	obs_data_set_string(service_settingsawd, "service", serviceName.c_str());

	OBSService servicewad = obs_service_create(serverType, "temp_service", service_settingsawd, nullptr);
	obs_service_release(servicewad);

	int bitrate = 10000;

	OBSData settings = obs_data_create();
	obs_data_release(settings);
	obs_data_set_int(settings, "bitrate", bitrate);
	obs_service_apply_encoder_settings(servicewad, settings, nullptr);

	int awstartingBitrate = (int)obs_data_get_int(settings, "bitrate");
	obs_data_set_int(vencoder_settings, "bitrate", awstartingBitrate);
	obs_data_set_string(vencoder_settings, "rate_control", "CBR");
	obs_data_set_string(vencoder_settings, "preset", "veryfast");
	obs_data_set_int(vencoder_settings, "keyint_sec", 2);

	obs_data_set_int(aencoder_settings, "bitrate", 32);

	const char *bind_ip = config_get_string(ConfigManager::getInstance().getBasic(), "Output", "BindIP");
	obs_data_set_string(output_settings, "bind_ip", bind_ip);

	/* -----------------------------------*/
	/* determine which servers to test    */

	std::vector<ServerInfo> servers;
	if (customServer)
		servers.emplace_back(server.c_str(), server.c_str());
	else
		GetServers(servers);

	/* just use the first server if it only has one alternate server */
	if (servers.size() < 3)
		servers.resize(1);

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
	} else {
		for (size_t i = 0; i < servers.size(); i++) {
			EvaluateBandwidth(servers[i], connected, stopped, success, errorOnStop, service_settings, service, output, vencoder_settings);
			eventsMutex.lock();
			events.push(AutoConfigInfo("progress", "bandwidth_test", (double)(i + 1) * 100 / servers.size()));
			eventsMutex.unlock();
		}
	}

	if (!success && !gotError) {
		eventsMutex.lock();
		events.push(AutoConfigInfo("error", "invalid_stream_settings", 0));
		eventsMutex.unlock();
		gotError = true;
	}

	if (!gotError) {
		for (auto &server : servers) {
			bool close = abs(server.bitrate - bestBitrate) < 400;

			if ((!close && server.bitrate > bestBitrate) || (close && server.ms < bestMS)) {
				bestServer = server.address;
				bestServerName = server.name;
				bestBitrate = server.bitrate;
				bestMS = server.ms;
			}
		}
		server = bestServer;
		serverName = bestServerName;
		idealBitrate = bestBitrate;
	}

	obs_output_release(output);
	obs_encoder_release(vencoder);
	obs_encoder_release(aencoder);
	obs_service_release(service);
	ret = obs_remove_video_info(ovi);
	if (ret != OBS_VIDEO_SUCCESS) {
		blog(LOG_ERROR, "[VIDEO_CANVAS] failed to remove video canvas %08X", ovi);
	}

	if (!gotError) {
		eventsMutex.lock();
		events.push(AutoConfigInfo("stopping_step", "bandwidth_test", 100));
		eventsMutex.unlock();
	}
}

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
	long double val = EstimateBitrateVal((int)baseResolutionCX, (int)baseResolutionCY, 60, 1) / 5800.0l;
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
	int baseCX = (int)baseResolutionCX;
	int baseCY = (int)baseResolutionCY;

	std::vector<Result> results;

	int pcores = os_get_physical_cores();
	int maxDataRate;
	if (pcores >= 4) {
		maxDataRate = int(baseResolutionCX * baseResolutionCY * 60 + 1000);
	} else {
		maxDataRate = 1280 * 720 * 30 + 1000;
	}

	auto testRes = [&](long double div, int fps_num, int fps_den, bool force) {
		if (results.size() >= 3)
			return;

		if (!fps_num || !fps_den) {
			fps_num = specificFPSNum;
			fps_den = specificFPSDen;
		}

		long double fps = ((long double)fps_num / (long double)fps_den);

		int cx = int((long double)baseCX / div);
		int cy = int((long double)baseCY / div);

		long double rate = (long double)cx * (long double)cy * fps;
		if (!force && rate > maxDataRate)
			return;

		int minBitrate = int(EstimateMinBitrate(cx, cy, fps_num, fps_den) * 114 / 100);
		if (type == Type::Recording)
			force = true;
		if (force || idealBitrate >= minBitrate)
			results.emplace_back(cx, cy, fps_num, fps_den);
	};

	if (specificFPSNum && specificFPSDen) {
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

	if (!specificFPSNum && preferHighFPS && results.size() > 1) {
		Result &result1 = results[0];
		Result &result2 = results[1];

		if (result1.fps_num == 30 && result2.fps_num == 60) {
			int nextArea = result2.cx * result2.cy;
			if (nextArea >= minArea)
				results.erase(results.begin());
		}
	}

	Result result = results.front();
	idealResolutionCX = result.cx;
	idealResolutionCY = result.cy;

	if (idealResolutionCX * idealResolutionCY > 1280 * 720) {
		idealResolutionCX = 1280;
		idealResolutionCY = 720;
	}

	idealFPSNum = result.fps_num;
	idealFPSDen = result.fps_den;
}

bool autoConfig::TestSoftwareEncoding()
{
	OBSEncoder vencoder = obs_video_encoder_create("obs_x264", "test_x264", nullptr, nullptr);
	OBSEncoder aencoder = obs_audio_encoder_create("ffmpeg_aac", "test_aac", nullptr, 0, nullptr);
	OBSOutput output = obs_output_create("null_output", "null", nullptr, nullptr);

	/* -----------------------------------*/
	/* configure settings                 */

	OBSData aencoder_settings = obs_data_create();
	OBSData vencoder_settings = obs_data_create();
	obs_data_release(aencoder_settings);
	obs_data_release(vencoder_settings);
	obs_data_set_int(aencoder_settings, "bitrate", 32);

	if (type != Type::Recording) {
		obs_data_set_int(vencoder_settings, "keyint_sec", 2);
		obs_data_set_int(vencoder_settings, "bitrate", idealBitrate);
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

	int baseCX = int(baseResolutionCX);
	int baseCY = int(baseResolutionCY);

	/* -----------------------------------*/
	/* calculate starting test rates      */

	int pcores = os_get_physical_cores();
	int lcores = os_get_logical_cores();
	int maxDataRate;
	if (lcores > 8 || pcores > 4) {
		/* superb */
		maxDataRate = int(baseResolutionCX * baseResolutionCY * 60 + 1000);

	} else if (lcores > 4 && pcores == 4) {
		/* great */
		maxDataRate = int(baseResolutionCX * baseResolutionCY * 60 + 1000);

	} else if (pcores == 4) {
		/* okay */
		maxDataRate = int(baseResolutionCX * baseResolutionCY * 30 + 1000);

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
			fps_num = specificFPSNum;
			fps_den = specificFPSDen;
		}

		long double fps = ((long double)fps_num / (long double)fps_den);

		int cx = int((long double)baseCX / div);
		int cy = int((long double)baseCY / div);

		if (!force && type != Type::Recording) {
			int est = int(EstimateMinBitrate(cx, cy, fps_num, fps_den));
			if (est > idealBitrate)
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

	if (specificFPSNum && specificFPSDen) {
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

	if (!specificFPSNum && preferHighFPS && results.size() > 1) {
		Result &result1 = results[0];
		Result &result2 = results[1];

		if (result1.fps_num == 30 && result2.fps_num == 60) {
			int nextArea = result2.cx * result2.cy;
			if (nextArea >= minArea)
				results.erase(results.begin());
		}
	}

	Result result = results.front();
	idealResolutionCX = result.cx;
	idealResolutionCY = result.cy;

	if (idealResolutionCX * idealResolutionCY > 1280 * 720) {
		idealResolutionCX = 1280;
		idealResolutionCY = 720;
	}

	idealFPSNum = result.fps_num;
	idealFPSDen = result.fps_den;

	long double fUpperBitrate = EstimateUpperBitrate(result.cx, result.cy, result.fps_num, result.fps_den);

	int upperBitrate = int(floor(fUpperBitrate / 50.0l) * 50.0l);

	if (streamingEncoder != Encoder::x264) {
		upperBitrate *= 114;
		upperBitrate /= 100;
	}

	if (idealBitrate > upperBitrate)
		idealBitrate = upperBitrate;

	obs_output_release(output);
	obs_encoder_release(vencoder);
	obs_encoder_release(aencoder);

	int ret = obs_remove_video_info(ovi);
	if (ret != OBS_VIDEO_SUCCESS) {
		blog(LOG_ERROR, "[VIDEO_CANVAS] Failed to remove video info after TestSoftwareEncoding, %08X", ovi);
	}

	softwareTested = true;
	return true;
}

void autoConfig::TestStreamEncoderThread()
{
	eventsMutex.lock();
	events.push(AutoConfigInfo("starting_step", "streamingEncoder_test", 0));
	eventsMutex.unlock();

	TestHardwareEncoding();

	if (!softwareTested) {
		if (!preferHardware || !hardwareEncodingAvailable) {
			if (!TestSoftwareEncoding()) {
				return;
			}
		}
	}

	if (preferHardware && !softwareTested && hardwareEncodingAvailable)
		FindIdealHardwareResolution();

	if (!softwareTested) {
		if (nvencAvailable || jimnvencAvailable)
			streamingEncoder = Encoder::NVENC;
		else if (qsvAvailable)
			streamingEncoder = Encoder::QSV;
		else if (vceAvailable)
			streamingEncoder = Encoder::AMD;
		// HW encoding seems to not be stable on Mac
		// else if (appleHWAvailable)
		// 	streamingEncoder = Encoder::appleHW;
	} else {
		streamingEncoder = Encoder::x264;
	}

	eventsMutex.lock();
	events.push(AutoConfigInfo("stopping_step", "streamingEncoder_test", 100));
	eventsMutex.unlock();
}

void autoConfig::TestRecordingEncoderThread()
{
	eventsMutex.lock();
	events.push(AutoConfigInfo("starting_step", "recordingEncoder_test", 0));
	eventsMutex.unlock();

	TestHardwareEncoding();

	if (!hardwareEncodingAvailable && !softwareTested) {
		if (!TestSoftwareEncoding()) {
			return;
		}
	}

	if (type == Type::Recording && hardwareEncodingAvailable)
		FindIdealHardwareResolution();

	recordingQuality = Quality::High;

	bool recordingOnly = type == Type::Recording;

	if (hardwareEncodingAvailable) {
		if (nvencAvailable || jimnvencAvailable)
			recordingEncoder = Encoder::NVENC;
		else if (qsvAvailable)
			recordingEncoder = Encoder::QSV;
		else if (vceAvailable)
			recordingEncoder = Encoder::AMD;
		// HW encoding seems to not be stable on Mac
		// else if (appleHWAvailable)
		// 	recordingEncoder = Encoder::appleHW;
	} else {
		recordingEncoder = Encoder::x264;
	}

	if (recordingEncoder != Encoder::NVENC) {
		if (!recordingOnly) {
			recordingEncoder = Encoder::Stream;
			recordingQuality = Quality::Stream;
		}
	}

	eventsMutex.lock();
	events.push(AutoConfigInfo("stopping_step", "recordingEncoder_test", 100));
	eventsMutex.unlock();
}

inline const char *GetEncoderId(Encoder enc)
{
	switch (enc) {
	case Encoder::NVENC:
		return jimnvencAvailable ? "jim_nvenc" : "ffmpeg_nvenc";
	case Encoder::QSV:
		return "obs_qsv11";
	case Encoder::AMD:
		return "h264_texture_amf";
	case Encoder::appleHW:
		return APPLE_HARDWARE_VIDEO_ENCODER;
	case Encoder::appleHWM1:
		return APPLE_HARDWARE_VIDEO_ENCODER_M1;
	case Encoder::x264:
		return "obs_x264";
	default:
		return jimnvencAvailable ? "jim_nvenc" : "ffmpeg_nvenc";
	}
};

inline const char *GetEncoderDisplayName(Encoder enc)
{
	switch (enc) {
	case Encoder::NVENC:
		return SIMPLE_ENCODER_NVENC;
	case Encoder::QSV:
		return SIMPLE_ENCODER_QSV;
	case Encoder::AMD:
		return SIMPLE_ENCODER_AMD;
	case Encoder::appleHW:
		return APPLE_HARDWARE_VIDEO_ENCODER;
	case Encoder::appleHWM1:
		return APPLE_HARDWARE_VIDEO_ENCODER_M1;
	default:
		return SIMPLE_ENCODER_X264;
	}
};

bool autoConfig::CheckSettings(void)
{
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
	video.output_width = (uint32_t)idealResolutionCX;
	video.output_height = (uint32_t)idealResolutionCY;
	video.fps_num = idealFPSNum;
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

	OBSEncoder vencoder = obs_video_encoder_create(GetEncoderId(streamingEncoder), "test_encoder", nullptr, nullptr);
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

	obs_data_set_int(vencoder_settings, "bitrate", idealBitrate);
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
}

void autoConfig::SetDefaultSettings(void)
{
	eventsMutex.lock();
	events.push(AutoConfigInfo("starting_step", "setting_default_settings", 0));
	eventsMutex.unlock();

	idealResolutionCX = 1280;
	idealResolutionCY = 720;
	idealFPSNum = 30;
	recordingQuality = Quality::High;
	idealBitrate = 2500;
	streamingEncoder = Encoder::x264;
	recordingEncoder = Encoder::Stream;

	eventsMutex.lock();
	events.push(AutoConfigInfo("stopping_step", "setting_default_settings", 100));
	eventsMutex.unlock();
}

void autoConfig::SaveStreamSettings()
{
	/* ---------------------------------- */
	/* save service                       */

	eventsMutex.lock();
	events.push(AutoConfigInfo("starting_step", "saving_service", 0));
	eventsMutex.unlock();

	const char *service_id = "rtmp_common";

	obs_service_t *oldService = OBS_service::getService(StreamServiceId::Main);
	OBSData hotkeyData = obs_hotkeys_save_service(oldService);
	obs_data_release(hotkeyData);

	OBSData settings = obs_data_create();

	if (!customServer)
		obs_data_set_string(settings, "service", serviceName.c_str());
	obs_data_set_string(settings, "server", server.c_str());
	obs_data_set_string(settings, "key", key.c_str());

	OBSService newService = obs_service_create(service_id, "default_service", settings, hotkeyData);

	if (!newService)
		return;

	OBS_service::setService(newService, StreamServiceId::Main);
	OBS_service::saveService();

	/* ---------------------------------- */
	/* save stream settings               */
	config_set_int(ConfigManager::getInstance().getBasic(), "SimpleOutput", "VBitrate", idealBitrate);
	config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "StreamEncoder", GetEncoderDisplayName(streamingEncoder));
	config_remove_value(ConfigManager::getInstance().getBasic(), "SimpleOutput", "UseAdvanced");

	config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);

	eventsMutex.lock();
	events.push(AutoConfigInfo("stopping_step", "saving_service", 100));
	eventsMutex.unlock();
}

void autoConfig::SaveSettings()
{
	eventsMutex.lock();
	events.push(AutoConfigInfo("starting_step", "saving_settings", 0));
	eventsMutex.unlock();

	if (recordingEncoder != Encoder::Stream)
		config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecEncoder", GetEncoderDisplayName(recordingEncoder));

	const char *quality = recordingQuality == Quality::High ? "Small" : "Stream";

	config_set_string(ConfigManager::getInstance().getBasic(), "Output", "Mode", "Simple");
	config_set_string(ConfigManager::getInstance().getBasic(), "SimpleOutput", "RecQuality", quality);
	config_set_int(ConfigManager::getInstance().getBasic(), "Video", "OutputCX", idealResolutionCX);
	config_set_int(ConfigManager::getInstance().getBasic(), "Video", "OutputCY", idealResolutionCY);
	config_set_int(ConfigManager::getInstance().getBasic(), "Video", "Canvases", 1);

	config_set_bool(ConfigManager::getInstance().getBasic(), "Output", "DynamicBitrate", false);

	if (fpsType != FPSType::UseCurrent) {
		config_set_uint(ConfigManager::getInstance().getBasic(), "Video", "FPSType", 0);
		config_set_string(ConfigManager::getInstance().getBasic(), "Video", "FPSCommon", std::to_string(idealFPSNum).c_str());
	}

	config_save_safe(ConfigManager::getInstance().getBasic(), "tmp", nullptr);

	eventsMutex.lock();
	events.push(AutoConfigInfo("stopping_step", "saving_settings", 100));
	events.push(AutoConfigInfo("done", "", 0));
	eventsMutex.unlock();
}
