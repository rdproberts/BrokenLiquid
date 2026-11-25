#include "core/Settings.h"

#include <spdlog/spdlog.h>

#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace bl {

nlohmann::json::json_pointer Settings::toPointer(std::string_view dottedKey) {
    std::string ptr;
    ptr.reserve(dottedKey.size() + 1);
    ptr.push_back('/');
    for (char c : dottedKey) {
        switch (c) {
        case '.': ptr.push_back('/'); break;
        case '~': ptr += "~0"; break; // JSON-pointer escaping
        case '/': ptr += "~1"; break;
        default: ptr.push_back(c); break;
        }
    }
    return nlohmann::json::json_pointer(ptr);
}

void Settings::load(const fs::path& file) {
    std::scoped_lock lock(m_mutex);
    m_file = file;
    m_data = nlohmann::json::object();

    std::error_code ec;
    if (!fs::exists(file, ec)) {
        return;
    }
    std::ifstream in(file, std::ios::binary);
    try {
        nlohmann::json parsed = nlohmann::json::parse(in);
        if (parsed.is_object()) {
            m_data = std::move(parsed);
            return;
        }
        throw std::runtime_error("settings root is not an object");
    } catch (const std::exception& e) {
        in.close();
        spdlog::warn("settings file {} is unreadable ({}); using defaults", file.string(), e.what());
        fs::path broken = file;
        broken += ".corrupt";
        fs::rename(file, broken, ec);
    }
}

bool Settings::save() const {
    std::scoped_lock lock(m_mutex);
    if (m_file.empty()) {
        return false;
    }
    std::error_code ec;
    fs::create_directories(m_file.parent_path(), ec);

    fs::path tmp = m_file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            spdlog::error("cannot write {}", tmp.string());
            return false;
        }
        out << m_data.dump(2) << '\n';
        out.flush();
        if (!out) {
            return false;
        }
    }
    fs::rename(tmp, m_file, ec); // replaces the existing file
    if (ec) {
        spdlog::error("cannot replace {}: {}", m_file.string(), ec.message());
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

bool Settings::contains(std::string_view key) const {
    std::scoped_lock lock(m_mutex);
    return m_data.contains(toPointer(key));
}

void Settings::remove(std::string_view key) {
    std::scoped_lock lock(m_mutex);
    const auto ptr = toPointer(key);
    if (!m_data.contains(ptr)) {
        return;
    }
    auto parent = ptr.parent_pointer();
    m_data.at(parent).erase(ptr.back());
}

void Settings::reset() {
    std::scoped_lock lock(m_mutex);
    m_data = nlohmann::json::object();
}

nlohmann::json Settings::snapshot() const {
    std::scoped_lock lock(m_mutex);
    return m_data;
}

Settings& Settings::global() {
    static Settings instance;
    return instance;
}

} // namespace bl
