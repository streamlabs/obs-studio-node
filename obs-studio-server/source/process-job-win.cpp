#include "process-job-win.hpp"

#include <windows.h>
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

void DisableKillOnClose()
{
	if (job_handle && !SetLimitFlags(JOB_OBJECT_LIMIT_BREAKAWAY_OK))
		blog(LOG_WARNING, "Child process job: clearing kill-on-close failed (%lu)", GetLastError());
}

} // namespace osn::job
