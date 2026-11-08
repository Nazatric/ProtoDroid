// Diagnostic probes: detours that only observe (log arguments and caller) and then continue into the
// original function. Selected with PROTODROID_PROBE=<name>; used to locate code, never shipped as behaviour.
#pragma once

namespace protodroid {

void InstallProbes();

}  // namespace protodroid
