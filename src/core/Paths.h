#pragma once

#include <filesystem>

namespace bl::paths {

// Where BrokenLiquid keeps its files. Normal installs follow the Windows conventions from the plan:
//   config  -> %APPDATA%\BrokenLiquid        (settings.json, keymap, plugins)
//   local   -> %LOCALAPPDATA%\BrokenLiquid   (logs, autosave, crash dumps, caches)
// Portable mode (a file named "portable.flag" next to the executable) keeps everything in
// <exe dir>\userdata instead.

std::filesystem::path executableDir();
bool isPortable();

std::filesystem::path configDir();
std::filesystem::path localDataDir();
std::filesystem::path logDir();
std::filesystem::path crashDumpDir();
std::filesystem::path autosaveDir();
std::filesystem::path settingsFile();

// Creates the directory (and parents) if needed and returns it.
std::filesystem::path ensure(const std::filesystem::path& dir);

} // namespace bl::paths
