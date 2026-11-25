#include "core/Log.h"

#include "bl/Version.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#ifdef _WIN32
#include <spdlog/sinks/msvc_sink.h>
#endif

#include <memory>
#include <mutex>
#include <vector>

namespace bl::log {
namespace {
std::once_flag g_once;
std::filesystem::path g_file;
} // namespace

void init(const std::filesystem::path& dir) {
    std::call_once(g_once, [&] {
        std::vector<spdlog::sink_ptr> sinks;
        try {
            std::filesystem::create_directories(dir);
            g_file = dir / "bl.log";
            sinks.push_back(
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(g_file.string(), 2 * 1024 * 1024, 5));
        } catch (const std::exception&) {
            g_file.clear(); // fall back to console/debugger only
        }
#ifdef _WIN32
        sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
#endif
        sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());

        auto logger = std::make_shared<spdlog::logger>("bl", sinks.begin(), sinks.end());
        logger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%^%l%$] [t%t] %v");
        logger->set_level(spdlog::level::debug);
        logger->flush_on(spdlog::level::warn);
        spdlog::set_default_logger(logger);
        spdlog::flush_every(std::chrono::seconds(3));

        spdlog::info("{} {} (build {}) starting", kAppName, kVersion, kBuildId);
    });
}

std::filesystem::path currentFile() { return g_file; }

void shutdown() {
    spdlog::info("shutting down");
    spdlog::shutdown();
}

} // namespace bl::log
