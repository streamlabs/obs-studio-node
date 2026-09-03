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

#include <napi.h>

namespace autoOptimizer {
void Init(Napi::Env env, Napi::Object exports);

// Stops client-side polling and releases the callback. If a session is active,
// its server-side cancellation/close is attempted while IPC is still usable.
// This is idempotent and safe to call from disconnect and environment cleanup.
void Shutdown();
}
