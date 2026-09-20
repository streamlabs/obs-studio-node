#pragma once

#ifdef _WIN32

#include <optional>

namespace osn::cef {

std::optional<int> DispatchSubprocessIfNeeded(int argc, char *argv[]);

} // namespace osn::cef

#endif
