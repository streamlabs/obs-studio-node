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
#include <queue>
#include <vector>

namespace osn {

struct OutputSignalInfo {
	std::string signal;
	int code = 0;
	std::string errorMessage;
};

class Output {
public:
	Output(const std::vector<std::string> &signals);
	virtual ~Output();

    void ConnectSignals();
    void createOutput(const std::string &type, const std::string &name);
	void deleteOutput();
	void startOutput();


    obs_output_t *output;
    std::mutex signalsMtx;
    std::queue<OutputSignalInfo> signalsReceived;
    obs_video_info *canvas;

private:
	std::condition_variable m_cvStop;
	std::mutex m_mtxOutputStop;

    const std::vector<std::string> m_signals;
};

}