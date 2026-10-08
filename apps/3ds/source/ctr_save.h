// Save policy of the 3DS app (M2.5, docs/3ds-port.md): the sleep guard around SD writes and the
// `mode=savetest` check of the save path.
#pragma once

#include <string>

namespace ctrsave {

// Registers the as3d::FileWriteScope hook: aptSetSleepAllowed(false) while a file is being
// written (the lid closing waits for the write), true otherwise. HOME stays allowed always.
void installSleepGuard();

// `mode=savetest` in run.ini: writes the profile 50 times with modified options and verifies each,
// lists the directory for *.tmp, forces a failed rename, a missing directory and a stale temporary.
// Output: <dir>/savetest_log.txt (and the files under <dir>/savetest/).
void runTest(const std::string& dir);

} // namespace ctrsave
