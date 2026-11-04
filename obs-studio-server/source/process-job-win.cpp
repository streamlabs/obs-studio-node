#include "process-job-win.hpp"

#include <windows.h>
#include <vector>
#include <obs.h>

namespace osn::job {

namespace {

// Never closed: its lifetime is what bounds the children.
HANDLE job_handle = nullptr;

bool SetLimitFlags(DWORD flags)
{
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
	info.BasicLimitInformation.LimitFlags = flags;
	return SetInformationJobObject(job_handle, JobObjectExtendedLimitInformation, &info, sizeof(info)) != FALSE;
}

} // namespace

void AssignCurrentProcess()
{
	if (job_handle)
		return;

	// Null attributes keep the handle non-inheritable, so the children never hold the job open.
	HANDLE job = CreateJobObjectW(nullptr, nullptr);
	if (!job) {
		blog(LOG_WARNING, "Child process job: CreateJobObject failed (%lu)", GetLastError());
		return;
	}

	job_handle = job;
	if (!SetLimitFlags(JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_BREAKAWAY_OK) || !AssignProcessToJobObject(job_handle, GetCurrentProcess())) {
		blog(LOG_WARNING, "Child process job: setup failed (%lu), continuing without it", GetLastError());
		job_handle = nullptr;
		CloseHandle(job);
	}
}

void TerminateOtherProcesses()
{
	if (!job_handle)
		return;

	// The list is truncated to the buffer on ERROR_MORE_DATA, so retry with the reported count.
	std::vector<char> buffer(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + 64 * sizeof(ULONG_PTR));
	for (int attempt = 0; attempt < 4; ++attempt) {
		auto *list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST *>(buffer.data());
		if (QueryInformationJobObject(job_handle, JobObjectBasicProcessIdList, list, (DWORD)buffer.size(), nullptr)) {
			const DWORD self = GetCurrentProcessId();
			for (DWORD i = 0; i < list->NumberOfProcessIdsInList; ++i) {
				if (list->ProcessIdList[i] == self)
					continue;
				HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)list->ProcessIdList[i]);
				if (!process)
					continue;
				TerminateProcess(process, 1);
				CloseHandle(process);
			}
			return;
		}
		if (GetLastError() != ERROR_MORE_DATA)
			break;
		auto *needed = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST *>(buffer.data());
		buffer.resize(sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + (needed->NumberOfAssignedProcesses + 16) * sizeof(ULONG_PTR));
	}
	blog(LOG_WARNING, "Child process job: listing processes failed (%lu)", GetLastError());
}

void DisableKillOnClose()
{
	if (job_handle && !SetLimitFlags(JOB_OBJECT_LIMIT_BREAKAWAY_OK))
		blog(LOG_WARNING, "Child process job: clearing kill-on-close failed (%lu)", GetLastError());
}

} // namespace osn::job
