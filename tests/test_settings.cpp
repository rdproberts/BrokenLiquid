#include "core/Settings.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

fs::path tempFile(const char* name) {
    const fs::path dir = fs::temp_directory_path() / "bl_tests";
    fs::create_directories(dir);
    const fs::path p = dir / name;
    fs::remove(p);
    return p;
}

} // namespace

TEST(Settings, DefaultsWhenMissing) {
    bl::Settings s;
    s.load(tempFile("missing.json"));
    EXPECT_EQ(s.get<std::string>("ui.theme", "light"), "light");
    EXPECT_EQ(s.get<int>("view.fps", 60), 60);
    EXPECT_FALSE(s.contains("ui.theme"));
}

TEST(Settings, DottedKeysNest) {
    bl::Settings s;
    s.set("view.mouse.reverseZoom", true);
    s.set("view.mouse.speed", 1.5);
    const auto j = s.snapshot();
    ASSERT_TRUE(j.contains("view"));
    EXPECT_TRUE(j["view"]["mouse"]["reverseZoom"].get<bool>());
    EXPECT_DOUBLE_EQ(j["view"]["mouse"]["speed"].get<double>(), 1.5);
}

TEST(Settings, RoundTrip) {
    const fs::path file = tempFile("roundtrip.json");
    {
        bl::Settings s;
        s.load(file);
        s.set("ui.theme", std::string("dark"));
        s.set("files.recent", std::vector<std::string>{"a.step", "b.step"});
        s.set("units.decimals", 3);
        ASSERT_TRUE(s.save());
    }
    bl::Settings s;
    s.load(file);
    EXPECT_EQ(s.get<std::string>("ui.theme", ""), "dark");
    EXPECT_EQ(s.get<int>("units.decimals", 0), 3);
    const auto recent = s.get<std::vector<std::string>>("files.recent", {});
    ASSERT_EQ(recent.size(), 2u);
    EXPECT_EQ(recent[1], "b.step");
}

TEST(Settings, WrongTypeFallsBack) {
    bl::Settings s;
    s.set("ui.theme", std::string("dark"));
    EXPECT_EQ(s.get<int>("ui.theme", 7), 7);
}

TEST(Settings, CorruptFileIsSetAside) {
    const fs::path file = tempFile("corrupt.json");
    fs::path corrupt = file;
    corrupt += ".corrupt";
    fs::remove(corrupt);
    {
        std::ofstream out(file);
        out << "{ this is not json";
    }
    bl::Settings s;
    s.load(file);
    EXPECT_FALSE(s.contains("anything"));
    EXPECT_TRUE(fs::exists(corrupt));
}

TEST(Settings, RemoveAndReset) {
    bl::Settings s;
    s.set("a.b", 1);
    s.set("a.c", 2);
    s.remove("a.b");
    EXPECT_FALSE(s.contains("a.b"));
    EXPECT_TRUE(s.contains("a.c"));
    s.reset();
    EXPECT_FALSE(s.contains("a.c"));
}
