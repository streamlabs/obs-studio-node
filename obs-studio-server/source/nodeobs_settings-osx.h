#pragma once

#ifdef __APPLE__

#include <string>
#include <utility>
#include <vector>

// Returns (localizedName, uniqueID) pairs for all connected video capture devices.
// Requires macOS 12 or later; returns an empty vector on older systems.
std::vector<std::pair<std::string, std::string>> getVideoDevicesMacOS();

#endif
