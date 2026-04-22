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
#include <iostream>
#include <string>
#pragma once
#include <graphics/math-extra.h>
#include <mutex>
#include <obs.hpp>
#include <queue>
#include <thread>
#include "nodeobs_api.h"


namespace autoConfig {
void Register(ipc::server &srv);
void InitializeAutoConfig(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
void StartBandwidthTest(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
void StartStreamEncoderTest(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
void StartRecordingEncoderTest(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
void StartCheckSettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
void StartSetDefaultSettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
void StartSaveStreamSettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
void StartSaveSettings(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
void TerminateAutoConfig(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
void Query(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);

void StopThread();
void FindIdealHardwareResolution();
bool TestSoftwareEncoding();
void TestBandwidthThread();
void TestStreamEncoderThread();
void TestRecordingEncoderThread();
void SaveStreamSettings();
void SaveSettings();
bool CheckSettings();
void SetDefaultSettings();
void TestHardwareEncoding();
bool CanTestServer(const char *server);
void WaitPendingTests(double timeout = 10);
int GetStartingBitrate(const std::string& serviceName);
void TestBandwidthThreadV2(void);
} // namespace autoConfig