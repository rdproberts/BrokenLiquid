#include "core/Paths.h"

#include <cstdlib>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace bl::paths {
namespace {

constexpr const wchar_t* kFolderName = L"BrokenLiquid";

fs::path envPath(const wchar_t* name) {
#ifdef _WIN32
    wchar_t* value = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&value, &len, name) == 0 && value) {
        fs::path result(value);
        free(value);
        return result;
    }
    return {};
#else
    (void)name;
    return {};
#endif
}

fs::path portableRoot() { return executableDir() / "userdata"; }

} // namespace

fs::path executableDir() {
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (n == 0) {
            return fs::current_path();
        }
        if (n < buffer.size()) {
            buffer.resize(n);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return fs::path(buffer).parent_path();
#else
    return fs::current_path();
#endif
}

bool isPortable() {
    std::error_code ec;
    return fs::exists(executableDir() / "portable.flag", ec);
}

fs::path configDir() {
    if (isPortable()) {
        return portableRoot() / "config";
    }
    fs::path base = envPath(L"APPDATA");
    if (base.empty()) {
        base = fs::temp_directory_path();
    }
    return base / kFolderName;
}

fs::path localDataDir() {
    if (isPortable()) {
        return portableRoot() / "local";
    }
    fs::path base = envPath(L"LOCALAPPDATA");
    if (base.empty()) {
        base = fs::temp_directory_path();
    }
    return base / kFolderName;
}

fs::path logDir() { return localDataDir() / "logs"; }
fs::path crashDumpDir() { return localDataDir() / "crashdumps"; }
fs::path autosaveDir() { return localDataDir() / "autosave"; }
fs::path settingsFile() { return configDir() / "settings.json"; }

fs::path ensure(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

} // namespace bl::paths
