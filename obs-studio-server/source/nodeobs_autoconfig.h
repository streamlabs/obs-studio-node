/******************************************************************************
    Copyright (C) 2026 by Streamlabs

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

#include <ipc-server.hpp>

namespace autoConfig {

void Register(ipc::server &srv);
void Shutdown();

void GetCapabilities(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
void CreateSession(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
void StartSession(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
void QuerySession(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
void GetResult(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
void CancelSession(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);
void CloseSession(void *data, const int64_t id, const std::vector<ipc::value> &args, std::vector<ipc::value> &rval);

} // namespace autoConfig
