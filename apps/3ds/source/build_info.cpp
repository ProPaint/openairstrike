#include "build_info.h"

#ifndef AS3D_GIT_COMMIT
#define AS3D_GIT_COMMIT "unknown"
#endif

namespace ctrapp {
const char* buildCommit() { return AS3D_GIT_COMMIT; }
} // namespace ctrapp
