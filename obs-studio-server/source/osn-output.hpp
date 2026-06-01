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

#include <mutex>
#include <optional>
#include <queue>
#include <vector>

namespace osn {

class Output {
public:
	struct SignalInfo {
		std::string signal;
		int code = 0;
		std::string errorMessage;
	};

public:
	Output(const std::vector<std::string> &signals);
	virtual ~Output();

	void CreateOutput(const std::string &type, const std::string &name);
	void SetOutput(obs_output_t *output);
	virtual void DeleteOutput();
	void StartOutput();

	// If no signal, will return an empty optional. Thread safe.
	std::optional<SignalInfo> PopReceivedSignal();

	void SetCanvas(obs_video_info *canvas);
	obs_video_info *GetCanvas();
	const obs_video_info *GetCanvas() const;

	// Returns the libobs video output bound to this output's canvas for the
	// given rendering mode, or NULL if the canvas video mix is not ready
	// (e.g. obs_reset_video failed at startup due to a lost GPU device).
	video_t *GetCanvasVideo(obs_video_rendering_mode mode);

	obs_output_t *GetOutput();
	const obs_output_t *GetOutput() const;

private:
	friend void OutputSignalCallback(void *data, calldata_t *params);

	// One per connected signal; carries the signal name and owner so the
	// forwarding callback can identify them. Heap-allocated in ConnectSignals
	// and freed in DisconnectSignals.
	struct CallbackData {
		std::string signal;
		osn::Output *outputClass = nullptr;
	};

	void InitOutput(obs_output_t *output);

	void ConnectSignals();

	// Disconnects every handler ConnectSignals/InitOutput wired up and frees
	// the CallbackData. Must run while m_output is still valid.
	void DisconnectSignals();

	static void OnStopped(void *data, calldata_t *params);

	obs_video_info *m_canvas = nullptr;
	obs_output_t *m_output = nullptr;

	std::mutex m_signalsMtx;
	std::queue<SignalInfo> m_signalsReceived;

	std::condition_variable m_cvStop;
	std::mutex m_mtxOutputStop;
	bool m_outputStopped = false;

	const std::vector<std::string> m_signals;
	std::vector<CallbackData *> m_signalCallbackData;
};

}
