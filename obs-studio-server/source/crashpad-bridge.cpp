#define OSN_CRASHPAD_BRIDGE_IMPLEMENTATION
#include "crashpad-bridge.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

#include "client/crash_report_database.h"
#include "client/crashpad_client.h"
#include "client/settings.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

std::mutex crashpad_mutex;
std::unique_ptr<crashpad::CrashpadClient> crashpad_client;
std::unique_ptr<crashpad::CrashReportDatabase> crashpad_database;
std::atomic<osn_crashpad_exception_callback> host_exception_callback{nullptr};
std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> internal_exception_filter{nullptr};
std::atomic_bool bridge_active{false};

LONG WINAPI BridgeExceptionFilter(EXCEPTION_POINTERS *exception_pointers) noexcept;

long CallInternalExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter, void *exception_pointers) noexcept
{
	try {
		if (filter)
			return filter(static_cast<EXCEPTION_POINTERS *>(exception_pointers));
	} catch (...) {
	}

	return EXCEPTION_CONTINUE_SEARCH;
}

bool BuildCrashpadArguments(const char *const *annotation_keys, const char *const *annotation_values, uint32_t annotation_count, const char *const *arguments,
			    uint32_t argument_count, std::map<std::string, std::string> &annotations, std::vector<std::string> &handler_arguments)
{
	if ((annotation_count && (!annotation_keys || !annotation_values)) || (argument_count && !arguments)) {
		return false;
	}

	for (uint32_t index = 0; index < annotation_count; ++index) {
		if (!annotation_keys[index] || !annotation_values[index])
			return false;
		annotations.emplace(annotation_keys[index], annotation_values[index]);
	}

	for (uint32_t index = 0; index < argument_count; ++index) {
		if (!arguments[index])
			return false;
		handler_arguments.emplace_back(arguments[index]);
	}

	return true;
}

LONG WINAPI BridgeExceptionFilter(EXCEPTION_POINTERS *exception_pointers) noexcept
{
	if (!bridge_active.load(std::memory_order_acquire))
		return EXCEPTION_CONTINUE_SEARCH;

	const auto callback = host_exception_callback.load(std::memory_order_acquire);
	if (callback) {
		try {
			callback(exception_pointers);
		} catch (...) {
			// Never allow a server exception to escape a Windows exception filter.
		}
	}

	// HandleCrash refreshes Crashpad so its annotations describe this failure.
	// Use the filter installed by that refresh, unless reporting was disabled
	// while the host callback was running.
	if (!bridge_active.load(std::memory_order_acquire))
		return EXCEPTION_CONTINUE_SEARCH;

	return CallInternalExceptionFilter(internal_exception_filter.load(std::memory_order_acquire), exception_pointers);
}

} // namespace

extern "C" bool __cdecl osn_crashpad_bridge_start(const char *report_server_url, const char *const *annotation_keys, const char *const *annotation_values,
						  uint32_t annotation_count, const char *const *arguments, uint32_t argument_count)
{
	try {
		if (!report_server_url || !*report_server_url)
			return false;

		std::map<std::string, std::string> annotations;
		std::vector<std::string> handler_arguments;
		if (!BuildCrashpadArguments(annotation_keys, annotation_values, annotation_count, arguments, argument_count, annotations, handler_arguments))
			return false;

		PWSTR roaming_app_data = nullptr;
		const HRESULT result = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming_app_data);
		if (FAILED(result) || !roaming_app_data)
			return false;

		std::wstring database_path(roaming_app_data);
		CoTaskMemFree(roaming_app_data);
		database_path.append(L"\\obs-studio-node-server");

		const base::FilePath database_file_path(database_path);
		const base::FilePath handler_path(L"crashpad_handler.exe");

		// An exception may arrive while Crashpad is starting. Its filter must
		// never wait on a mutex owned by the faulting thread; the already-installed
		// Crashpad filter can still handle that exception without refreshed annotations.
		std::unique_lock<std::mutex> lock(crashpad_mutex, std::try_to_lock);
		if (!lock.owns_lock())
			return false;

		crashpad_database.reset();

		auto database = crashpad::CrashReportDatabase::Initialize(database_file_path);
		if (!database || !database->GetSettings())
			return false;
		if (!database->GetSettings()->SetUploadsEnabled(true))
			return false;

		if (!crashpad_client)
			crashpad_client = std::make_unique<crashpad::CrashpadClient>();
		if (!crashpad_client->StartHandler(handler_path, database_file_path, database_file_path, report_server_url, annotations, handler_arguments,
						   /* restartable */ true,
						   /* asynchronous_start */ true)) {
			return false;
		}
		if (!crashpad_client->WaitForHandlerStart(INFINITE))
			return false;

		crashpad_database = std::move(database);
		return true;
	} catch (...) {
		return false;
	}
}

extern "C" bool __cdecl osn_crashpad_bridge_set_exception_callback(osn_crashpad_exception_callback callback)
{
	try {
		if (!callback)
			return false;

		host_exception_callback.store(callback, std::memory_order_release);
		const LPTOP_LEVEL_EXCEPTION_FILTER previous_filter = SetUnhandledExceptionFilter(BridgeExceptionFilter);
		if (previous_filter != BridgeExceptionFilter)
			internal_exception_filter.store(previous_filter, std::memory_order_release);
		bridge_active.store(true, std::memory_order_release);
		return true;
	} catch (...) {
		return false;
	}
}

extern "C" void __cdecl osn_crashpad_bridge_shutdown(void)
{
	try {
		// Leave the bridge filter installed but inert. Windows has no atomic
		// compare-and-restore operation for the process exception filter, so
		// attempting to restore it here could overwrite a newer owner's filter.
		bridge_active.store(false, std::memory_order_release);
		host_exception_callback.store(nullptr, std::memory_order_release);
		internal_exception_filter.store(nullptr, std::memory_order_release);

		std::lock_guard<std::mutex> lock(crashpad_mutex);
		crashpad_client.reset();
		crashpad_database.reset();
	} catch (...) {
	}
}

#endif
