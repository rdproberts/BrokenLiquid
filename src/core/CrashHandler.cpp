#include "core/CrashHandler.h"

#include <spdlog/spdlog.h>

#include <system_error>

#ifdef _WIN32
#include <windows.h>
// dbghelp.h must follow windows.h
#include <dbghelp.h>
#endif

namespace bl::crash {

#ifdef _WIN32
namespace {

wchar_t g_dumpDir[MAX_PATH] = {};

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* info) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t file[MAX_PATH];
    swprintf_s(file, L"%s\\BrokenLiquid-%04u%02u%02u-%02u%02u%02u.dmp", g_dumpDir, t.wYear, t.wMonth, t.wDay,
               t.wHour, t.wMinute, t.wSecond);

    HANDLE h = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = info;
        mei.ClientPointers = FALSE;
        const auto type =
            static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory);
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h, type, &mei, nullptr, nullptr);
        CloseHandle(h);
    }
    try {
        spdlog::critical("unhandled exception 0x{:08X}; minidump written to crash folder",
                         static_cast<unsigned>(info->ExceptionRecord->ExceptionCode));
        spdlog::default_logger()->flush();
    } catch (...) {
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace
#endif

void install(const std::filesystem::path& dumpDir) {
#ifdef _WIN32
    std::error_code ec;
    std::filesystem::create_directories(dumpDir, ec);
    wcsncpy_s(g_dumpDir, dumpDir.wstring().c_str(), _TRUNCATE);
    SetUnhandledExceptionFilter(onUnhandledException);
#else
    (void)dumpDir;
#endif
}

} // namespace bl::crash
