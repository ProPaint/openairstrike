// The commit the 3DS app was built from (tools/3ds_build.sh passes `git rev-parse --short HEAD`,
// with "-dirty" for uncommitted changes, as AS3D_GIT_COMMIT): logged as build= in the first line of
// every run, so that tools/3ds_receive.py files a pushed log under out/3ds/<date>-<commit>/.
#pragma once

namespace ctrapp {
const char* buildCommit(); // "unknown" when the build had no commit
}
