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

#include "util-crashmanager.h"
#include "util-metricsprovider.h"

#include <algorithm>
#include <chrono>
#include <codecvt>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <deque>
#include <map>
#include <obs.h>
#include <queue>
#include <sstream>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <filesystem>
#include <random>
#include <unordered_set>

#ifdef WIN32
#include "StackWalker.h"
#include <WinBase.h>
#include "DbgHelp.h"
#include "Shlobj.h"
#pragma comment(lib, "Dbghelp.lib")
#pragma comment(lib, "pdh.lib")
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include "TCHAR.h"
#include "pdh.h"
#include "psapi.h"
#elif defined(__APPLE__)
#include <libproc.h>
#include <signal.h>
#include <unistd.h>
#endif

#include "nodeobs_api.h"
#include "nodeobs_service.h"
#include "osn-error.hpp"
#include "shared.hpp"

#ifdef ENABLE_CRASHREPORT
#include "client/crash_report_database.h"
#include "client/crashpad_client.h"
#include "client/settings.h"
#include "nodeobs_api.h"
#endif

#include "nlohmann/json.hpp"

//////////////////////
// STATIC VARIABLES //
//////////////////////
std::queue<std::pair<int, std::string>> lastActions;
std::deque<std::string> serverWarnings;
constexpr size_t MaximumServerWarnings = 50;
std::mutex messageMutex;
#ifdef WIN32
// Global/static variables
std::vector<std::string> handledOBSCrashes;
PDH_HQUERY cpuQuery;
PDH_HCOUNTER cpuTotal;
util::MetricsProvider metricsClient;
LPTOP_LEVEL_EXCEPTION_FILTER crashpadInternalExceptionFilterMethod = nullptr;
HANDLE memoryDumpEvent = INVALID_HANDLE_VALUE;
std::filesystem::path memoryDumpFolder;
#elif defined(__APPLE__)
struct sigaction oldSegv = {};
struct sigaction oldAbrt = {};
struct sigaction oldBus = {};
#endif

std::string appState = "starting"; // "starting","idle","encoding","shutdown"
std::string reportServerUrl = "";
// Crashpad variables
#ifdef ENABLE_CRASHREPORT
std::wstring globalAppData_path;
std::wstring appdata_path;
crashpad::CrashpadClient client;
std::unique_ptr<crashpad::CrashReportDatabase> database;
std::string url;
base::FilePath db;
base::FilePath handler;
std::vector<std::string> arguments;
std::string workingDirectory;
static nlohmann::json briefCrashInfo;
static std::mutex briefCrashInfoMutex;
static std::wstring_view briefCrashInfoBasename(L"brief-crash-info.json");
#ifdef _WIN32
static std::wstring_view pathSeparator(L"\\");
#else
static std::wstring_view pathSeparator(L"/");
#endif
#endif

std::string appStateFile;

std::chrono::steady_clock::time_point initialTime;
bool reportsEnabled = true;
std::map<std::string, std::string> annotations;

/////////////
// FORWARD //
/////////////

std::string FormatVAString(const char *const format, va_list args);
void RewindCallStack();

typedef long long LongLong;

// Transform a byte value into a string + sufix
std::string PrettyBytes(uint64_t bytes)
{
	const char *suffixes[7];
	char temp[100];
	suffixes[0] = "b";
	suffixes[1] = "kb";
	suffixes[2] = "mb";
	suffixes[3] = "gb";
	suffixes[4] = "tb";
	suffixes[5] = "pb";
	suffixes[6] = "eb";
	uint64_t s = 0; // which suffix to use
	double count = double(bytes);
	while (count >= 1024 && s < 7) {
		s++;
		count /= 1024;
	}
	if (count - floor(count) == 0.0) {
		snprintf(temp, sizeof(temp), "%d%s", static_cast<int>(count), suffixes[s]);
	} else {
		snprintf(temp, sizeof(temp), "%.1f%s", count, suffixes[s]);
	}

	return std::string(temp);
}

std::wstring utf8_to_wstring(const std::string &utf8)
{
	return std::wstring(utf8.begin(), utf8.end());
}

std::string wstring_to_utf8(const std::wstring &wide)
{
	return std::string(wide.begin(), wide.end());
}

void RequestComputerUsageParams(long long &totalPhysMem, long long &physMemUsed, size_t &physMemUsedByMe, double &totalCPUUsed, long long &commitMemTotal,
				long long &commitMemLimit)
{
#ifdef WIN32

	MEMORYSTATUSEX memInfo = {0};
	PROCESS_MEMORY_COUNTERS pmc;
	PDH_FMT_COUNTERVALUE counterVal;
	PERFORMANCE_INFORMATION perfInfo = {0};

	memInfo.dwLength = sizeof(MEMORYSTATUSEX);
	GlobalMemoryStatusEx(&memInfo);
	GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
	PdhCollectQueryData(cpuQuery);
	PdhGetFormattedCounterValue(cpuTotal, PDH_FMT_DOUBLE, NULL, &counterVal);

	totalPhysMem = memInfo.ullTotalPhys;
	physMemUsed = (memInfo.ullTotalPhys - memInfo.ullAvailPhys);
	physMemUsedByMe = pmc.WorkingSetSize + pmc.PagefileUsage;
	totalCPUUsed = counterVal.doubleValue;
	perfInfo.cb = sizeof(PERFORMANCE_INFORMATION);
	if (GetPerformanceInfo(&perfInfo, sizeof(PERFORMANCE_INFORMATION))) {
		commitMemTotal = perfInfo.CommitTotal * perfInfo.PageSize;
		commitMemLimit = perfInfo.CommitLimit * perfInfo.PageSize;
	}
#else
	totalPhysMem = g_util_osx->getTotalPhysicalMemory();
	physMemUsed = g_util_osx->getTotalPhysicalMemory() - g_util_osx->getAvailableMemory();
	physMemUsedByMe = g_util_osx->getTotalPhysicalMemory() - g_util_osx->getAvailableMemory();
	totalCPUUsed = g_util_osx->getPhysicalCores();
	commitMemTotal = LongLong(0);
	commitMemLimit = LongLong(0);

#endif
}

void GetUserInfo(std::string &computerName)
{
#ifdef WIN32
	TCHAR infoBuf[MAX_COMPUTERNAME_LENGTH + 1];
	DWORD bufCharCount = MAX_COMPUTERNAME_LENGTH + 1;

	if (!GetComputerName(infoBuf, &bufCharCount))
		return;

	using convert_typeX = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_typeX, wchar_t> converterX;

	computerName = converterX.to_bytes(std::wstring(infoBuf));
#else
	computerName = g_util_osx->getComputerName();
#endif
}

nlohmann::json RequestProcessList()
{
#ifdef WIN32
	DWORD aProcesses[1024], cbNeeded, cProcesses;
	nlohmann::json result = nlohmann::json::object();

	if (!EnumProcesses(aProcesses, sizeof(aProcesses), &cbNeeded)) {
		return 1;
	}

	// Calculate how many process identifiers were returned
	cProcesses = cbNeeded / sizeof(DWORD);
	unsigned reported_processes_count = 0;
	unsigned skipped_processes_count = 0;
	unsigned unprocessed_processes_count = 0;
	// Get the name and process identifier for each process
	for (unsigned i = 0; i < cProcesses; i++) {
		if (aProcesses[i] != 0) {
			TCHAR szProcessName[MAX_PATH] = TEXT("<unknown>");

			// Get a handle to the process
			DWORD processID = aProcesses[i];
			HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processID);

			// Get the process name.
			if (NULL != hProcess) {
				HMODULE hMod;
				DWORD cbNeeded;

				if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded)) {
					GetModuleBaseName(hProcess, hMod, szProcessName, sizeof(szProcessName) / sizeof(TCHAR));

					PROCESS_MEMORY_COUNTERS pmc = {};
					if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
						SIZE_T totalProcessMemory = pmc.PagefileUsage + pmc.WorkingSetSize;
						if (totalProcessMemory > 1024 * 1024 * 32) {
							result.push_back({std::to_string(processID),
									  std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t>().to_bytes(szProcessName) +
										  std::string(", ") + std::to_string(totalProcessMemory / 1024 / 1024) +
										  std::string("Mb")});

							reported_processes_count++;
						} else {
							skipped_processes_count++;
						}
					}

					CloseHandle(hProcess);
				}
			} else {
				skipped_processes_count++;
			}
		} else {
			skipped_processes_count++;
		}
		if (reported_processes_count >= 149) {
			unprocessed_processes_count = cProcesses - i;
			break;
		}
	}
	result.push_back({std::string("0"), std::string("Total:") + std::to_string(cProcesses) + std::string(", Skipped:") +
						    std::to_string(skipped_processes_count) + std::string(", Unprocessd:") +
						    std::to_string(unprocessed_processes_count)});

	return result;
#else
	nlohmann::json result = nlohmann::json::object();

	int max_pids = 1024;
	std::vector<pid_t> pids(max_pids);

	int num_pids = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), max_pids * sizeof(pid_t));
	if (num_pids < 0) {
		blog(LOG_WARNING, "Failed to get process list.");
		return result;
	}

	int skipped_processes_count = 0;
	int unprocessed_processes_count = 0;
	int total_processes_count = 0;
	int count = num_pids / sizeof(pid_t);
	for (int i = 0; i < count; ++i) {
		if (pids[i] == 0) {
			skipped_processes_count++;
			continue;
		}

		struct proc_bsdinfo procInfo;
		int ret = proc_pidinfo(pids[i], PROC_PIDTBSDINFO, 0, &procInfo, sizeof(proc_bsdinfo));
		if (ret <= 0) {
			unprocessed_processes_count++;
			continue; // Skip if information can't be retrieved
		}

		std::ostringstream oss;
		oss << procInfo.pbi_name << " UID " << procInfo.pbi_uid << " GID " << procInfo.pbi_gid;
		result.push_back({std::to_string(pids[i]), oss.str()});
		total_processes_count++;
	}

	std::ostringstream oss;
	oss << "Total " << total_processes_count << " proc_pidinfo failed " << unprocessed_processes_count << " skipped " << skipped_processes_count;
	result.push_back({std::to_string(0), oss.str()});
	return result;

#endif
}

//////////////////
// CrashManager //
//////////////////
std::wstring util::CrashManager::GetMemoryDumpEventName_Start()
{
#ifdef WIN32
	return L"Global\\SLOBSMEMORYDUMPEVENT" + std::to_wstring(GetCurrentProcessId());
#else
	return L"Global\\SLOBSMEMORYDUMPEVENT";
#endif
}

std::wstring util::CrashManager::GetMemoryDumpEventName_Fail()
{
#ifdef WIN32
	return L"Global\\SLOBSMEMORYDUMPEVENTFAIL" + std::to_wstring(GetCurrentProcessId());
#else
	return L"Global\\SLOBSMEMORYDUMPEVENTFAIL";
#endif
}

std::wstring util::CrashManager::GetMemoryDumpEventName_Success()
{
#ifdef WIN32
	return L"Global\\SLOBSMEMORYDUMPEVENTSUCCESS" + std::to_wstring(GetCurrentProcessId());
#else
	return L"Global\\SLOBSMEMORYDUMPEVENTSUCCESS";
#endif
}

#ifdef WIN32
bool util::CrashManager::IsMemoryDumpEnabled()
{
	if (std::filesystem::exists(memoryDumpFolder) && std::filesystem::is_directory(memoryDumpFolder)) {
		return true;
	} else {
		return false;
	}
}

bool util::CrashManager::InitializeMemoryDump()
{
	bool ret = false;
	if (!IsMemoryDumpEnabled())
		return ret;

	PSECURITY_DESCRIPTOR securityDescriptor = (PSECURITY_DESCRIPTOR)LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
	if (InitializeSecurityDescriptor(securityDescriptor, SECURITY_DESCRIPTOR_REVISION)) {
		if (SetSecurityDescriptorDacl(securityDescriptor, TRUE, NULL, FALSE)) {
			SECURITY_ATTRIBUTES eventSecurityAttr = {0};
			eventSecurityAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
			eventSecurityAttr.lpSecurityDescriptor = securityDescriptor;
			eventSecurityAttr.bInheritHandle = FALSE;

			memoryDumpEvent = CreateEvent(&eventSecurityAttr, TRUE, FALSE, GetMemoryDumpEventName_Start().c_str());
			if (memoryDumpEvent != NULL && memoryDumpEvent != INVALID_HANDLE_VALUE) {
				ret = true;
			}
		}
	}
	LocalFree(securityDescriptor);

	return ret;
}

bool util::CrashManager::SignalMemoryDump()
{
	bool result = false;

	if (memoryDumpEvent != NULL && memoryDumpEvent != INVALID_HANDLE_VALUE) {
		if (SetEvent(memoryDumpEvent)) {

			constexpr int failEvent = 0;
			constexpr int successEvent = 1;

			HANDLE handles[2] = {OpenEvent(EVENT_ALL_ACCESS, FALSE, GetMemoryDumpEventName_Fail().c_str()),
					     OpenEvent(EVENT_ALL_ACCESS, FALSE, GetMemoryDumpEventName_Success().c_str())};

			if (handles[0] != NULL && handles[0] != INVALID_HANDLE_VALUE && handles[1] != NULL && handles[1] != INVALID_HANDLE_VALUE) {
				DWORD ret = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

				if (ret - WAIT_OBJECT_0 == successEvent) {
					result = true;
				}
			}

			if (handles[0] != NULL && handles[0] != INVALID_HANDLE_VALUE)
				CloseHandle(handles[0]);

			if (handles[1] != NULL && handles[1] != INVALID_HANDLE_VALUE)
				CloseHandle(handles[1]);
		}

		CloseHandle(memoryDumpEvent);
		memoryDumpEvent = INVALID_HANDLE_VALUE;
	}

	return result;
}

std::wstring util::CrashManager::GetMemoryDumpPath()
{
	return memoryDumpFolder.generic_wstring();
}

std::wstring util::CrashManager::GetMemoryDumpName()
{
	static std::wstring dmpName;

	if (!dmpName.empty())
		return dmpName;

	std::mt19937 rangen(std::random_device{}());

	auto randomCharacter = [&]() {
		auto crand = [&](char min, char max) {
			std::uniform_int_distribution<char> distribution(min, max);
			return distribution(rangen);
		};

		// ascinum for simplicity
		return crand(0, 1) == 0 ? crand('A', 'Z') : crand('0', '9');
	};

	std::wstring randomCode;

	for (int i = 0; i < 15; ++i)
		randomCode.push_back(randomCharacter());

	using namespace std::chrono;
	seconds ms = duration_cast<seconds>(system_clock::now().time_since_epoch());
	return dmpName = L"obs." + std::to_wstring(ms.count()) + L"." + randomCode;
}

#else
bool util::CrashManager::IsMemoryDumpEnabled()
{
	return false;
}
bool util::CrashManager::InitializeMemoryDump()
{
	return IsMemoryDumpEnabled();
}
bool util::CrashManager::SignalMemoryDump()
{
	return false;
}
std::wstring util::CrashManager::GetMemoryDumpPath()
{
	return L"";
}
std::wstring util::CrashManager::GetMemoryDumpName()
{
	return L"";
}
#endif

bool util::CrashManager::Initialize(char *path, const std::string &appdata)
{
#ifdef ENABLE_CRASHREPORT
	globalAppData_path = utf8_to_wstring(appdata);
	std::ostringstream oss;
	oss << appdata << std::filesystem::path::preferred_separator << "appState";
	appStateFile = oss.str();

	annotations.insert({{"crashpad_status", "internal crash handler missed"}});
	annotations.insert({{"sentry[user][ip_address]", "{{auto}}"}});

	workingDirectory = path;
	if (!SetupCrashpad()) {
		return false;
	}
#if defined(__APPLE__)
	InstallSigActionHandler();
#endif

	// Handler for obs errors (mainly for bcrash() calls)
	base_set_crash_handler(
		[](const char *format, va_list args, void *param) {
			std::string strFormat, errorMessage;
			if (format == nullptr) {
				errorMessage = "unknown error";
			} else {
				errorMessage = FormatVAString(format, args);
				strFormat = format;
			}

			// Check if this crash error is handled internally (if this is a known
			// error that we can't do anything about it, just let the application
			// crash normally
			if (!TryHandleCrash(strFormat, errorMessage))
				HandleCrash(errorMessage);
		},
		nullptr);

	// Redirect all the calls from std::terminate
	std::set_terminate([]() { HandleCrash("Direct call to std::terminate"); });

#ifdef WIN32
	memoryDumpFolder = std::filesystem::u8path(appdata + "\\CrashMemoryDump");

	// There's a static local wstring inside this function, now it's cached for thread safe read access
	util::CrashManager::GetMemoryDumpName();

	// Setup the windows exeption filter
	auto ExceptionHandlerMethod = [](struct _EXCEPTION_POINTERS *ExceptionInfo) {
		HandleCrash("UnhandledExceptionFilter", false);

		// Call the crashpad internal exception filter method since we overrided it here and
		// it must be called to proper generate a report
		return crashpadInternalExceptionFilterMethod(ExceptionInfo);
	};

	// This method will substitute the crashpad unhandled exception filter method by our one, returning
	// the old method used by it, we will store this method pointer to be able to call it directly
	crashpadInternalExceptionFilterMethod = SetUnhandledExceptionFilter(ExceptionHandlerMethod);

	// Setup the metrics query for the CPU usage
	// Ref: https://stackoverflow.com/questions/63166/how-to-determine-cpu-and-memory-consumption-from-inside-a-process
	PdhOpenQuery(NULL, NULL, &cpuQuery);
	PdhAddEnglishCounter(cpuQuery, L"\\Processor(_Total)\\% Processor Time", NULL, &cpuTotal);
	PdhCollectQueryData(cpuQuery);

	// The atexit will check if obs was safelly closed
	std::atexit(HandleExit);
	std::at_quick_exit(HandleExit);
#endif

#endif

	initialTime = std::chrono::steady_clock::now();
	return true;
}

void util::CrashManager::Configure()
{
#ifdef WIN32
	// Add all obs crash messages that are supposed to be handled by our application and
	// shouldn't cause a crash report (because there is no point on reporting since we
	// cannot control them)
	// You don't need to set the entire message, we will just check for a substring match
	// in the main error message
	{
		// commenting to let d3d11 crashes be reported to sentry
		// handledOBSCrashes.push_back("Failed to recreate D3D11");
		// ...
	}
#endif
}

bool util::CrashManager::SetupCrashpad()
{
	if (!reportsEnabled || reportServerUrl.size() == 0) {
		return false;
	}

#ifdef ENABLE_CRASHREPORT

#if defined(_WIN32)
	HRESULT hResult;
	PWSTR ppszPath;

	hResult = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &ppszPath);

	appdata_path.assign(ppszPath);
	appdata_path.append(L"\\obs-studio-node-server");

	CoTaskMemFree(ppszPath);
#endif

	arguments.push_back("--no-rate-limit");

#ifdef WIN32
	std::wstring handler_path(L"crashpad_handler.exe");
#else
	std::string handler_path = workingDirectory + '/';
	handler_path.append("crashpad_handler");
#endif

#ifdef __APPLE__
	std::string appdata_path = wstring_to_utf8(globalAppData_path) + "/Crashpad";
#endif
	db = base::FilePath(appdata_path);
	handler = base::FilePath(handler_path);

	database = crashpad::CrashReportDatabase::Initialize(db);
	if (database == nullptr || database->GetSettings() == NULL)
		return false;

	database->GetSettings()->SetUploadsEnabled(true);
	bool asynchronous_start = true;
#if defined(__APPLE__)
	asynchronous_start = false;
#endif

	bool rc = client.StartHandler(handler, db, db, reportServerUrl, annotations, arguments, /* restartable */ true, asynchronous_start);
	if (!rc) {
		blog(LOG_WARNING, "Unable to start crash handler");
		return false;
	}

#ifdef WIN32
	// Windows will wait since asynchronous_start is set to true.
	rc = client.WaitForHandlerStart(INFINITE);
	if (!rc)
		return false;
#endif

#endif

	return true;
}

void util::CrashManager::HandleExit() noexcept
{
	// If we are exiting normally and obs is active, we have a problem because some
	// modules and threads could potentially be running and this will result in a
	// crash that is masked. The real issue here is why we are exiting without
	// finishing the obs first.
	if (obs_initialized()) {
		// Proceed to add more info to our crash reporter but don't call abort, we
		// cannot ensure that when at exit a call to obs_initialized will be safe, it
		// could be in an invalid state, we will let the application continue and if
		// this results in a crash at least we will know what caused it
		HandleCrash("AtExit", false);
	}
}

void util::CrashManager::HandleCrash(const std::string &_crashInfo, bool callAbort) noexcept
{
#ifdef ENABLE_CRASHREPORT

	// If for any reason this is true, it means that we are crashing inside this same
	// method, if that happens just call abort and ignore any remaining processing since
	// we cannot continue.
	static bool insideCrashMethod = false;
	static bool insideRewindCallstack = false; //if this is true then we already crashed inside StackWalker and try to skip it this time.
	if (insideCrashMethod && !insideRewindCallstack)
		abort();

	SaveToAppStateFile();

	annotations.clear();

	int known_crash_id = 0;

	if (is_allocator_failed()) {
		known_crash_id = 0x1;
	}

	// Get the information about the total of CPU and RAM used by this user
	long long totalPhysMem = 1;
	long long physMemUsed = 0;
	double totalCPUUsed = 0.0;
	size_t physMemUsedByMe = 0;
	long long commitMemTotal = 0ll;
	long long commitMemLimit = 1ll;
	std::string computerName;

	try {
		RequestComputerUsageParams(totalPhysMem, physMemUsed, physMemUsedByMe, totalCPUUsed, commitMemTotal, commitMemLimit);

		GetUserInfo(computerName);
	} catch (...) {
	}

	auto timeElapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - initialTime);

	// Setup all the custom annotations that are important too our crash report
	nlohmann::json systemResources = nlohmann::json::object();
	systemResources.push_back({"Leaks", std::to_string(bnum_allocs())});
	systemResources.push_back({"Total RAM", PrettyBytes(totalPhysMem)});

	systemResources.push_back(
		{"Total used RAM", PrettyBytes(physMemUsed) + " - percentage: " + std::to_string(double(physMemUsed * 100) / double(totalPhysMem)) + "%"});
	systemResources.push_back(
		{"OBS64 RAM", PrettyBytes(physMemUsedByMe) + " - percentage: " + std::to_string(double(physMemUsedByMe * 100) / double(totalPhysMem)) + "%"});
	systemResources.push_back({"Commit charge", PrettyBytes(commitMemTotal) + " of " + PrettyBytes(commitMemLimit)});
	systemResources.push_back({"CPU usage", std::to_string(int(totalCPUUsed)) + "%"});
	annotations.insert({{"System Resources", systemResources.dump(4)}});

	annotations.insert({{"Time elapsed: ", std::to_string(timeElapsed.count()) + "s"}});
	annotations.insert({{"Status", obs_initialized() ? "initialized" : "shutdown"}});
	try {
		annotations.insert({{"Process List", RequestProcessList().dump(4)}});
	} catch (...) {
	}

	// Annotations attached to the Sentry minidump:
	//   "OBS log general" — rolling tail of the libOBS log (capped at LogReport::MaximumMessages lines)
	//   "Last actions"    — recent IPC calls received by the server (capped at MaximumActionsRegistered)
	//   "Server warnings" — server-detected anomalies recorded via AddServerWarning (capped at MaximumServerWarnings)
	try {
		annotations.insert({{"OBS log general", RequestOBSLog().dump(4)}});
		annotations.insert({{"Crash reason", _crashInfo}});
		annotations.insert({{"Computer name", computerName}});
		annotations.insert({{"Last actions", ComputeActions().dump(4)}});
		annotations.insert({{"Server warnings", ComputeServerWarnings().dump(4)}});
	} catch (...) {
	}

	annotations.insert({{"sentry[release]", OBS_API::getCurrentVersion()}});
	annotations.insert({{"sentry[user][username]", OBS_API::getUsername()}});
	annotations.insert({{"sentry[user][ip_address]", "{{auto}}"}});
	annotations.insert({{"sentry[environment]", getAppState()}});

	SaveBriefCrashInfoToFile();

	// It is crucial to rewind call stack before calling 'SignalMemoryDump' because we gather info about crashed module
	insideCrashMethod = true;
	try {
		if (!insideRewindCallstack) {
			insideRewindCallstack = true;
			RewindCallStack();
			insideRewindCallstack = false;
		} else {
			blog(LOG_INFO, "Recrashed in RewindCallStack");
		}
	} catch (...) {
		//ignore exceptions to not loose current crash info
	}
	insideCrashMethod = false;

	// if saved memory dump
	const bool uploadedFullDump = SignalMemoryDump();
	if (uploadedFullDump) {
		std::string dmpNameA;
		std::wstring dmpNameW = util::CrashManager::GetMemoryDumpName();
		std::transform(dmpNameW.begin(), dmpNameW.end(), std::back_inserter(dmpNameA), [](wchar_t c) { return (char)c; });
		annotations.insert({{"sentry[tags][memorydump]", "true"}});
		annotations.insert({{"sentry[tags][s3dmp]", dmpNameA.c_str()}});
	}

	insideCrashMethod = true;
	// Recreate crashpad instance, this is a well defined/supported operation
	SetupCrashpad();

	// This value is true by default and only false if we're planning to let crashpad handle cleanup
	if (callAbort)
		abort();
	else
		blog(LOG_INFO, "Server finished 'HandleCrash', crashpad will now make a sentry report");

	insideCrashMethod = false;
#else
	SignalMemoryDump();
#endif
}

void util::CrashManager::SaveBriefCrashInfoToFile()
{
	std::ofstream briefInfoFile;

#ifdef ENABLE_CRASHREPORT
	std::string serialized = briefCrashInfo.dump(4);

	std::wstring briefCrashInfoFilename(globalAppData_path);
	if (*briefCrashInfoFilename.rbegin() != L'/' && *briefCrashInfoFilename.rbegin() != L'\\') {
		briefCrashInfoFilename += pathSeparator;
	}
	briefCrashInfoFilename += briefCrashInfoBasename;

	std::ofstream briefCrashInfoFile;
	briefCrashInfoFile.open(briefCrashInfoFilename);
	briefCrashInfoFile << serialized;
	briefCrashInfoFile.flush();
	briefCrashInfoFile.close();
#endif
}

void util::CrashManager::UpdateBriefCrashInfo()
{
#ifdef ENABLE_CRASHREPORT
	std::scoped_lock lock(briefCrashInfoMutex);
	briefCrashInfo["app_state"] = getAppState();
	briefCrashInfo["username"] = OBS_API::getUsername();
	if (auto it = annotations.find("sentry[tags][s3dmp]"); it != annotations.end()) {
		briefCrashInfo["s3dmp"] = (*it).second;
	}
	SaveBriefCrashInfoToFile();
#endif
}

#if !defined(_WIN32)
void util::CrashManager::UpdateBriefCrashInfoAppState()
{
#ifdef ENABLE_CRASHREPORT
	std::scoped_lock lock(briefCrashInfoMutex);
	briefCrashInfo["app_state"] = getAppState();
	SaveBriefCrashInfoToFile();
#endif
}
#endif

#if !defined(_WIN32)
void util::CrashManager::UpdateBriefCrashInfoUsername()
{
#ifdef ENABLE_CRASHREPORT
	std::scoped_lock lock(briefCrashInfoMutex);
	briefCrashInfo["username"] = OBS_API::getUsername();
	SaveBriefCrashInfoToFile();
#endif
}
#endif

#if !defined(_WIN32)
void util::CrashManager::DeleteBriefCrashInfoFile()
{
#ifdef ENABLE_CRASHREPORT
	std::scoped_lock lock(briefCrashInfoMutex);
	briefCrashInfo.clear();

	std::wstring briefCrashInfoFilename(globalAppData_path);
	if (*briefCrashInfoFilename.rbegin() != L'/' && *briefCrashInfoFilename.rbegin() != L'\\') {
		briefCrashInfoFilename += pathSeparator;
	}
	briefCrashInfoFilename += briefCrashInfoBasename;

	std::filesystem::path briefCrashInfoPath = std::filesystem::u8path(wstring_to_utf8(briefCrashInfoFilename));

	if (std::filesystem::exists(briefCrashInfoPath) && std::filesystem::is_regular_file(briefCrashInfoPath)) {
		std::filesystem::remove(briefCrashInfoPath);
	}
#endif
}
#endif

void util::CrashManager::SetReportServerUrl(const std::string &url)
{
	// dev environment
	//url = "https://o114354.ingest.sentry.io/api/252950/minidump/?sentry_key=8f444a81edd446b69ce75421d5e91d4d";

	if (url.length()) {
		reportServerUrl = url;
	} else {
		bool isPreview = OBS_API::getCurrentVersion().find("preview") != std::string::npos;
		reportServerUrl = isPreview ? std::string("https://sentry.io/api/1406061/minidump/?sentry_key=7376a60665cd40bebbd59d6bf8363172")
					    : std::string("https://sentry.io/api/1283431/minidump/?sentry_key=ec98eac4e3ce49c7be1d83c8fb2005ef");
	}
}

void util::CrashManager::SetVersionName(const std::string &name)
{
	std::cout << "version name " << name.c_str() << std::endl;
	annotations.insert({{"sentry[release]", name}});
}

void util::CrashManager::SetUsername(const std::string &name)
{
	annotations.insert({{"sentry[user][username]", name}});
#if !defined(_WIN32)
	UpdateBriefCrashInfoUsername();
#endif
}

bool util::CrashManager::TryHandleCrash(const std::string &_format, const std::string &_crashMessage)
{
#ifdef WIN32
	// This method can only be called by the obs-studio crash handler method, this means that
	// an internal error occurred.
	// handledOBSCrashes will contain all error messages that we should ignore from obs-studio,
	// like Dx11 errors for example, here we will check if this error is known, if true we will
	// try to finalize SLOBS in an attempt to NOT generate a crash report for it

	bool crashIsHandled = false;
	for (auto &handledCrashes : handledOBSCrashes) {
		if (_format.find(handledCrashes) != std::string::npos) {
			crashIsHandled = true;
			break;
		}
	}
	if (!crashIsHandled)
		return false;

	// If we are here this is a known crash that we don't wanna propagate to the crashpad
	// (because we don't need to handle it or we cannot control it), the idea would be just
	// a call to stop the crashpad handler but since it doesn't have a method like this we
	// can try to finish the application normally to avoid any crash report

	// Optionally send a message to the user here informing that SLOBS found an error and will
	// now close, potentially we could retrieve CPU and RAM usage and show a rich message
	// telling the user that he is using too much cpu/ram or process any Dx11 message and output
	// that to the user

	// Disable reports and kill the obs process
	DWORD pid = GetCurrentProcessId();
	HANDLE hnd = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, TRUE, pid);
	if (hnd != nullptr) {

		DisableReports();

		// Directly blame the user for this error since it was caused by the user side
		util::CrashManager::GetMetricsProvider()->SendStatus("Handled Crash");

		TerminateProcess(hnd, 0);
	}

	// Something really bad went wrong when killing this process, generate a crash report!
	util::CrashManager::HandleCrash(_crashMessage);
#endif

	return false;
}

// Format a var arg string into a c++ std::string type
std::string FormatVAString(const char *const format, va_list args)
{
	static const int MaximumVAStringSize = 63;
	auto temp = std::vector<char>{};
	auto length = std::size_t{MaximumVAStringSize};
	while (temp.size() <= length) {
		temp.resize(length + 1);
		const auto status = std::vsnprintf(temp.data(), temp.size(), format, args);
		if (status < 0)
			throw std::runtime_error{"string formatting error"};
		length = static_cast<std::size_t>(status);
	}

	if (length > MaximumVAStringSize) {
		return "unknown error string";
	}

	return std::string{temp.data(), length};
}

void RewindCallStack()
{
#ifdef WIN32
	class MyStackWalker : public StackWalker {
	public:
		MyStackWalker() : StackWalker() {}

	protected:
		virtual void OnCallstackEntry(CallstackEntryType eType, CallstackEntry &entry)
		{
			// If this entry is valid
			if (entry.offset == 0)
				return;

			// If the entry is inside this file
			std::string fileName = std::string(entry.lineFileName);
			if (fileName.find("util-crashmanager.cpp") != std::string::npos || fileName.find("stackwalker.cpp") != std::string::npos ||
			    fileName.find("StackWalker.cpp") != std::string::npos)
				return;

			if (strlen(entry.name) > 0) {
				std::string function = std::string(entry.name);
				entry.name[0] = 0x00;

				if (strlen(entry.lineFileName) > 0)
					function += std::string(" ") + std::string(entry.lineFileName);
				entry.lineFileName[0] = 0x00;

				if (entry.lineNumber > 0)
					function += std::string(":") + std::to_string(entry.lineNumber);

				if (strlen(entry.moduleName) > 0) {
					std::string moduleName = entry.moduleName;
					function += std::string(" ") + moduleName;

					const auto binaryPath = std::string(entry.loadedImageName);
					std::transform(moduleName.begin(), moduleName.end(), moduleName.begin(),
						       [](unsigned char c) { return std::tolower(c); });
					if (m_excludeModules.find(moduleName) == m_excludeModules.end() && !m_moduleInfoSent) {
						OBS_API::CrashModuleInfo(moduleName, binaryPath);
						m_moduleInfoSent = true;
					}
				}
				entry.moduleName[0] = 0x00;
				entry.loadedImageName[0] = 0x00;

				blog(LOG_INFO, "ST: %s", function.c_str());
			} else {
				std::string function = std::string("unknown function");

				if (strlen(entry.moduleName) > 0)
					function += std::string(" ") + std::string(entry.moduleName);
				entry.moduleName[0] = 0x00;

				blog(LOG_INFO, "ST: %s", function.c_str());
			}
		}

	private:
		bool m_moduleInfoSent = false;

		std::unordered_set<std::string> m_excludeModules = {"kernelbase", "ntdll", "kernel32.dll"};
	};

	MyStackWalker sw;
	sw.ShowCallstack();
#endif
	return;
}

nlohmann::json util::CrashManager::RequestOBSLog()
{
	nlohmann::json result;

	// snapshotOBSLogGeneral returns oldest-first under logMutex; emit newest-first.
	std::deque<std::string> general = OBS_API::snapshotOBSLogGeneral();
	for (auto it = general.rbegin(); it != general.rend(); ++it)
		result.push_back(*it);

	return result;
}

nlohmann::json util::CrashManager::ComputeActions()
{
	nlohmann::json result = nlohmann::json::array();

	while (!lastActions.empty()) {
		auto counter = lastActions.front().first;
		auto message = lastActions.front().second;

		// Update the message to reflect the count amount, if applicable
		if (counter > 0) {
			message = message + std::string("|") + std::to_string(counter);
		}

		result.push_back(message);
		lastActions.pop();
	}

	return result;
}

nlohmann::json util::CrashManager::ComputeServerWarnings()
{
	nlohmann::json result;

	// try_lock — the crashing thread may already hold messageMutex (e.g. it crashed inside
	// AddServerWarning/RegisterAction); a blocking lock would hang crash reporting.
	std::unique_lock<std::mutex> lock(messageMutex, std::try_to_lock);
	if (!lock.owns_lock())
		return result;

	for (auto &msg : serverWarnings)
		result.push_back(msg);

	return result;
}

void BindCrtHandlesToStdHandles(bool bindStdIn, bool bindStdOut, bool bindStdErr)
{
#ifdef WIN32
	// Re-initialize the C runtime "FILE" handles with clean handles bound to "nul". We do this because it has been
	// observed that the file number of our standard handle file objects can be assigned internally to a value of -2
	// when not bound to a valid target, which represents some kind of unknown internal invalid state. In this state our
	// call to "_dup2" fails, as it specifically tests to ensure that the target file number isn't equal to this value
	// before allowing the operation to continue. We can resolve this issue by first "re-opening" the target files to
	// use the "nul" device, which will place them into a valid state, after which we can redirect them to our target
	// using the "_dup2" function.
	if (bindStdIn) {
		FILE *dummyFile;
		freopen_s(&dummyFile, "nul", "r", stdin);
	}
	if (bindStdOut) {
		FILE *dummyFile;
		freopen_s(&dummyFile, "nul", "w", stdout);
	}
	if (bindStdErr) {
		FILE *dummyFile;
		freopen_s(&dummyFile, "nul", "w", stderr);
	}

	// Redirect unbuffered stdin from the current standard input handle
	if (bindStdIn) {
		HANDLE stdHandle = GetStdHandle(STD_INPUT_HANDLE);
		if (stdHandle != INVALID_HANDLE_VALUE) {
			int fileDescriptor = _open_osfhandle((intptr_t)stdHandle, _O_TEXT);
			if (fileDescriptor != -1) {
				FILE *file = _fdopen(fileDescriptor, "r");
				if (file != NULL) {
					int dup2Result = _dup2(_fileno(file), _fileno(stdin));
					if (dup2Result == 0) {
						setvbuf(stdin, NULL, _IONBF, 0);
					}
				}
			}
		}
	}

	// Redirect unbuffered stdout to the current standard output handle
	if (bindStdOut) {
		HANDLE stdHandle = GetStdHandle(STD_OUTPUT_HANDLE);
		if (stdHandle != INVALID_HANDLE_VALUE) {
			int fileDescriptor = _open_osfhandle((intptr_t)stdHandle, _O_TEXT);
			if (fileDescriptor != -1) {
				FILE *file = _fdopen(fileDescriptor, "w");
				if (file != NULL) {
					int dup2Result = _dup2(_fileno(file), _fileno(stdout));
					if (dup2Result == 0) {
						setvbuf(stdout, NULL, _IONBF, 0);
					}
				}
			}
		}
	}

	// Redirect unbuffered stderr to the current standard error handle
	if (bindStdErr) {
		HANDLE stdHandle = GetStdHandle(STD_ERROR_HANDLE);
		if (stdHandle != INVALID_HANDLE_VALUE) {
			int fileDescriptor = _open_osfhandle((intptr_t)stdHandle, _O_TEXT);
			if (fileDescriptor != -1) {
				FILE *file = _fdopen(fileDescriptor, "w");
				if (file != NULL) {
					int dup2Result = _dup2(_fileno(file), _fileno(stderr));
					if (dup2Result == 0) {
						setvbuf(stderr, NULL, _IONBF, 0);
					}
				}
			}
		}
	}

	// Clear the error state for each of the C++ standard stream objects. We need to do this, as attempts to access the
	// standard streams before they refer to a valid target will cause the iostream objects to enter an error state. In
	// versions of Visual Studio after 2005, this seems to always occur during startup regardless of whether anything
	// has been read from or written to the targets or not.
	if (bindStdIn) {
		std::wcin.clear();
		std::cin.clear();
	}
	if (bindStdOut) {
		std::wcout.clear();
		std::cout.clear();
	}
	if (bindStdErr) {
		std::wcerr.clear();
		std::cerr.clear();
	}
#endif
}

void util::CrashManager::OpenConsole()
{
#if defined(_WIN32)

	// Allocate a console window for this process
	AllocConsole();

	// Update the C/C++ runtime standard input, output, and error targets to use the console window
	BindCrtHandlesToStdHandles(true, true, true);

#endif
}

void util::CrashManager::IPCValuesToData(const std::vector<ipc::value> &values, nlohmann::json &data)
{
	int paramCounter = 0;
	for (auto &value : values) {
		switch (value.type) {
		case ipc::type::Null: {
			data.push_back({{"arg" + std::to_string(paramCounter), "null"}});
			break;
		}
		case ipc::type::Float: {
			data.push_back({{"arg" + std::to_string(paramCounter), std::to_string(value.value_union.fp32)}});
			break;
		}
		case ipc::type::Double: {
			data.push_back({{"arg" + std::to_string(paramCounter), std::to_string(value.value_union.fp64)}});
			break;
		}
		case ipc::type::Int32: {
			data.push_back({{"arg" + std::to_string(paramCounter), std::to_string(value.value_union.i32)}});
			break;
		}
		case ipc::type::Int64: {
			data.push_back({{"arg" + std::to_string(paramCounter), std::to_string(value.value_union.i64)}});
			break;
		}
		case ipc::type::UInt32: {
			data.push_back({{"arg" + std::to_string(paramCounter), std::to_string(value.value_union.ui32)}});
			break;
		}
		case ipc::type::UInt64: {
			data.push_back({{"arg" + std::to_string(paramCounter), std::to_string(value.value_union.ui64)}});
			break;
		}
		case ipc::type::String: {
			data.push_back({{"arg" + std::to_string(paramCounter), value.value_str}});
			break;
		}
		case ipc::type::Binary: {
			data.push_back({{"arg" + std::to_string(paramCounter), ""}});
			break;
		}
		}

		paramCounter++;
	}
}

void util::CrashManager::AddServerWarning(const std::string &warning)
{
	std::lock_guard<std::mutex> lock(messageMutex);
	serverWarnings.push_back(warning);
	if (serverWarnings.size() > MaximumServerWarnings) {
		serverWarnings.pop_front();
	}
}

void RegisterAction(const std::string &message)
{
	static const int MaximumActionsRegistered = 50;
	std::lock_guard<std::mutex> lock(messageMutex);

	// Check if this and the last message are the same, if true just add a counter
	if (lastActions.size() > 0 && message.compare(lastActions.back().second) == 0) {
		lastActions.back().first++;
	} else {
		lastActions.push({0, message});
		if (lastActions.size() > MaximumActionsRegistered) {
			lastActions.pop();
		}
	}
}

void util::CrashManager::setAppState(const std::string &newState)
{
	appState = newState;
#if !defined(_WIN32)
	UpdateBriefCrashInfoAppState();
#endif
}

std::string util::CrashManager::getAppState()
{
	if (appState.compare("idle") == 0) {
		std::string encoding_state = "";
		std::string need_space = "";
		if (OBS_service::getStreamingOutput(StreamServiceId::Main)) {
			if (OBS_service::isStreamingOutputActive(StreamServiceId::Main) || OBS_service::isStreamingOutputActive(StreamServiceId::Second)) {
				encoding_state += "activestreaming";
			} else {
				encoding_state += "inactivestreaming";
			}
			need_space = " ";
		}
		if (OBS_service::getRecordingOutput()) {
			if (OBS_service::isRecordingOutputActive()) {
				encoding_state += need_space;
				encoding_state += " activerecording";
				need_space = " ";
			}
		}
		if (OBS_service::getReplayBufferOutput()) {
			if (OBS_service::isReplayBufferOutputActive()) {
				encoding_state += need_space;
				encoding_state += " activereply";
			}
		}
		if (encoding_state.size() > 0)
			return encoding_state;
	}
	return appState;
}

void util::CrashManager::ProcessPreServerCall(const std::string &cname, const std::string &fname, const std::vector<ipc::value> &args)
{
	std::string jsonEntry = cname + std::string("::") + fname;

	// Perform this only if this user have a high crash rate (TODO: this check must be implemented)
	/*
	nlohmann::json ipcValues = nlohmann::json::array();
	IPCValuesToData(args, ipcValues);
	jsonEntry["ipc values"] = ipcValues;
	*/

	RegisterAction(jsonEntry);
}

void util::CrashManager::ProcessPostServerCall(const std::string &cname, const std::string &fname, const std::vector<ipc::value> &args)
{
	if (args.size() == 0) {
		AddServerWarning(std::string("No return params on method ") + fname + std::string(" for class ") + cname);
	} else if ((ErrorCode)args[0].value_union.ui64 != ErrorCode::Ok) {
		AddServerWarning(std::string("Server call returned error number ") + std::to_string(args[0].value_union.ui64) + " on method " + fname +
				 std::string(" for class ") + cname);
	}
}

void util::CrashManager::DisableReports()
{
	reportsEnabled = false;

#ifdef ENABLE_CRASHREPORT

	client.~CrashpadClient();
	database->~CrashReportDatabase();
	database = nullptr;

#endif
}

util::MetricsProvider *const util::CrashManager::GetMetricsProvider()
{
#ifdef WIN32
	return &metricsClient;
#endif
}

void util::CrashManager::SaveToAppStateFile()
{
	std::ifstream state_file(appStateFile, std::ios::in);
	if (!state_file.is_open())
		return;

	std::ostringstream buffer;
	buffer << state_file.rdbuf();
	state_file.close();

	std::string current_status = buffer.str();
	if (current_status.size() == 0)
		return;

	const std::string flag_value = "obs_crash";
	const std::string flag_name = "detected";

	try {
		nlohmann::json jsonEntry = nlohmann::json::parse(current_status);
		jsonEntry[flag_name] = flag_value;
		std::string updated_status = jsonEntry.dump(-1);

		std::ofstream out_state_file;
		out_state_file.open(appStateFile, std::ios::trunc | std::ios::out);
		if (!out_state_file.is_open())
			return;

		out_state_file << updated_status << "\n";
		out_state_file.flush();
		out_state_file.close();
	} catch (...) {
	}
}

#if defined(__APPLE__)
void util::CrashManager::SignalActionHandler(int sig, siginfo_t *signalInfo, void *)
{
	// Restore previous signal handler so it can handle our upcoming abort request
	sigaction(SIGSEGV, &oldSegv, NULL);
	sigaction(SIGABRT, &oldAbrt, NULL);
	sigaction(SIGBUS, &oldBus, NULL);

	std::ostringstream oss;
	oss << "Fatal error sig: " << sig;
	if (signalInfo) {
		oss << " code: " << signalInfo->si_code;
		oss << " addr: " << signalInfo->si_addr;
	}

	util::CrashManager::HandleCrash(oss.str());
}

void util::CrashManager::InstallSigActionHandler()
{
	struct sigaction sa = {};
	sa.sa_sigaction = SignalActionHandler;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);

	sigaction(SIGSEGV, &sa, &oldSegv);
	sigaction(SIGABRT, &sa, &oldAbrt);
	sigaction(SIGBUS, &sa, &oldBus);
}
#endif
