#pragma once

// This interface deliberately exposes only C-compatible values. The server is
// built with /MT, while the Crashpad bridge uses /MD, so C++ objects and heap
// ownership must not cross this boundary. Inputs are caller-owned and copied
// synchronously; the API returns no allocated memory, FILE*, fd/HANDLE, or
// errno state. The exception callback receives a non-owning OS payload only.
#if defined(_WIN32)
#include <stdint.h>

#if defined(OSN_CRASHPAD_BRIDGE_IMPLEMENTATION)
#define OSN_CRASHPAD_BRIDGE_API __declspec(dllexport)
#else
#define OSN_CRASHPAD_BRIDGE_API __declspec(dllimport)
#endif

typedef void(__cdecl *osn_crashpad_exception_callback)(void *exception_pointers);

extern "C" {

OSN_CRASHPAD_BRIDGE_API bool __cdecl osn_crashpad_bridge_start(const char *report_server_url, const char *const *annotation_keys,
							       const char *const *annotation_values, uint32_t annotation_count, const char *const *arguments,
							       uint32_t argument_count);

// Registers the server's crash bookkeeping callback. The bridge installs the
// Windows exception filter and forwards to Crashpad's previous filter itself.
OSN_CRASHPAD_BRIDGE_API bool __cdecl osn_crashpad_bridge_set_exception_callback(osn_crashpad_exception_callback callback);

OSN_CRASHPAD_BRIDGE_API void __cdecl osn_crashpad_bridge_shutdown(void);
}

#endif
