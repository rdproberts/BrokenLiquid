#pragma once

#include <filesystem>

namespace bl::crash {

// Installs an unhandled-exception filter that writes a minidump (BrokenLiquid-<timestamp>.dmp)
// into `dumpDir` and flushes the log. Dumps stay on this machine; nothing is uploaded.
void install(const std::filesystem::path& dumpDir);

} // namespace bl::crash
