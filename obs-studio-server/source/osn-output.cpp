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

#include "osn-Output.hpp"

#include "nodeobs_api.h"

namespace {

struct CallbackData {
	std::string signal;
	osn::Output *outputClass;
};

} // namespace

osn::Output::Output(const std::vector<std::string> &signals) :
    m_signals(signals)
{
    output = nullptr;
    canvas = nullptr;
}

osn::Output::~Output()
{
}

void osn::Output::createOutput(const std::string &type, const std::string &name)
{
	deleteOutput();
	output = obs_output_create(type.c_str(), name.c_str(), nullptr, nullptr);

	auto onStopped = [](void *data, calldata_t *) {
		osn::Output *context = reinterpret_cast<osn::Output *>(data);
		std::unique_lock<std::mutex> lock(context->m_mtxOutputStop);
		context->m_cvStop.notify_one();
	};

	signal_handler *sh = obs_output_get_signal_handler(output);
	signal_handler_connect(sh, "stop", onStopped, this);

	ConnectSignals();
}

void osn::Output::deleteOutput()
{
	if (!output)
		return;

	if (obs_output_active(output)) {
		obs_output_stop(output);
		std::unique_lock<std::mutex> lock(m_mtxOutputStop);
		m_cvStop.wait_for(lock, std::chrono::seconds(20));
	}
	obs_output_release(output);
	output = nullptr;
}

static void callback(void *data, calldata_t *params)
{
	auto info = reinterpret_cast<CallbackData *>(data);

	if (!info)
		return;

	std::string signal = info->signal;
	auto outputClass = info->outputClass;

	if (!outputClass->output)
		return;

	const char *error = obs_output_get_last_error(outputClass->output);

	std::unique_lock<std::mutex> ulock(outputClass->signalsMtx);
	outputClass->signalsReceived.push({signal, (int)calldata_int(params, "code"), error ? std::string(error) : ""});
}

void osn::Output::ConnectSignals()
{
	if (!output)
		return;

	signal_handler *handler = obs_output_get_signal_handler(output);
	for (const auto &signal : m_signals) {
		auto *cd = new CallbackData();
		cd->signal = signal;
		cd->outputClass = this;
		signal_handler_connect(handler, signal.c_str(), callback, cd);
	}
}

void osn::Output::startOutput()
{
	if (!output)
		return;

	outdated_driver_error::instance()->set_active(true);
	bool result = obs_output_start(output);
	outdated_driver_error::instance()->set_active(false);

	if (result)
		return;

	int code = 0;
	std::string errorMessage = "";
	std::string outdated_driver_error = outdated_driver_error::instance()->get_error();

	if (outdated_driver_error.size() != 0) {
		errorMessage = outdated_driver_error;
		code = OBS_OUTPUT_OUTDATED_DRIVER;
	} else {
		const char *error = obs_output_get_last_error(output);
		if (error) {
			errorMessage = error;
			blog(LOG_INFO, "Last streaming error: %s", error);
		}
		code = OBS_OUTPUT_ERROR;
	}

	std::unique_lock<std::mutex> ulock(signalsMtx);
	signalsReceived.push({"stop", code, errorMessage});
}
