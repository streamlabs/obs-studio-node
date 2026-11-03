#pragma once

namespace osn::job {

void AssignCurrentProcess();
void TerminateOtherProcesses();
void DisableKillOnClose();

} // namespace osn::job
