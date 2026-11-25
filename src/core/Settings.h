#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

namespace bl {

// User-level ("System Options") settings stored as JSON.
// Keys are dotted paths, e.g. "ui.theme" or "view.mouse.reverseZoom".
// Thread-safe. Values that fail to convert fall back to the supplied default.
class Settings {
public:
    Settings() = default;

    // Loads the file if it exists (a corrupt file is renamed to *.corrupt and defaults are used).
    void load(const std::filesystem::path& file);
    // Writes atomically (temp file + replace). Returns false on failure.
    bool save() const;

    template <class T>
    T get(std::string_view key, const T& fallback) const {
        std::scoped_lock lock(m_mutex);
        const auto ptr = toPointer(key);
        if (!m_data.contains(ptr)) {
            return fallback;
        }
        try {
            return m_data.at(ptr).get<T>();
        } catch (const nlohmann::json::exception&) {
            return fallback;
        }
    }

    template <class T>
    void set(std::string_view key, const T& value) {
        std::scoped_lock lock(m_mutex);
        m_data[toPointer(key)] = value;
    }

    bool contains(std::string_view key) const;
    void remove(std::string_view key);
    void reset(); // "Reset to defaults": clears everything

    const std::filesystem::path& file() const { return m_file; }
    nlohmann::json snapshot() const;

    // Process-wide instance used by the application.
    static Settings& global();

private:
    static nlohmann::json::json_pointer toPointer(std::string_view dottedKey);

    nlohmann::json m_data = nlohmann::json::object();
    std::filesystem::path m_file;
    mutable std::mutex m_mutex;
};

} // namespace bl
