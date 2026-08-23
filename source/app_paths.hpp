// app_paths.hpp - resolves the directory the .nro was launched from, so we can
// look for side files (like the TE API key) next to the executable.
#pragma once
#include <string>

namespace paths {

// Set once at startup from argv[0]. Safe to call with a null/empty path.
void initFromArgv0(const char* argv0);

// Directory containing the running .nro, e.g. "sdmc:/switch/switch_terminal".
// Empty if it couldn't be determined (e.g. launched without a path).
const std::string& appDir();

} // namespace paths
