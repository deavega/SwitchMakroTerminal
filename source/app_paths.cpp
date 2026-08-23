#include "app_paths.hpp"

namespace paths {

static std::string g_appDir;

void initFromArgv0(const char* argv0) {
    g_appDir.clear();
    if (!argv0 || !*argv0) return;
    std::string p(argv0);
    // strip the filename after the last path separator
    size_t slash = p.find_last_of("/\\");
    if (slash == std::string::npos) return;   // no directory component
    g_appDir = p.substr(0, slash);            // e.g. "sdmc:/switch/switch_terminal"
}

const std::string& appDir() { return g_appDir; }

} // namespace paths
