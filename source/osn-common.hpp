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

#include <cstdint>
#include <limits>

namespace osn {
namespace common {

// Reserved wire value for a missing OSN object reference. Live object IDs
// never use this value.
inline constexpr uint64_t INVALID_ID = std::numeric_limits<uint64_t>::max();

#ifdef _WIN32
// Official CEF builds omit Chromium's test trials. In our symbol-enabled build
// one of those trials blocks shader compilation by the host's WARP driver.
inline constexpr char DISABLE_CEF_TEST_TRIALS[] = "--disable-field-trial-config";
#endif

} // namespace common
} // namespace osn
