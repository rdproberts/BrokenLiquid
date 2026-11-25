#pragma once

#include <spdlog/spdlog.h>

#include <filesystem>

namespace bl::log {

// Sets up the default logger: rotating file in `dir` (bl.log, 5 x 2 MB) plus the debugger output
// on Windows. Safe to call more than once; later calls are ignored.
void init(const std::filesystem::path& dir);

std::filesystem::path currentFile();

// Flushes and drops all loggers (call before exit).
void shutdown();

} // namespace bl::log
