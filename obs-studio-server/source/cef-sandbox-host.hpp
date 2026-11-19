#pragma once

#ifdef _WIN32

#include <optional>

namespace osn::cef {

void SetBootstrapSandboxInfo(void *sandbox_info);
std::optional<int> DispatchSubprocessIfNeeded(int argc, char *argv[]);

} // namespace osn::cef

#endif
