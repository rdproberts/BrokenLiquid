#include "core/Paths.h"

#include <gtest/gtest.h>

namespace fs = std::filesystem;

TEST(Paths, ExecutableDirExists) {
    const fs::path dir = bl::paths::executableDir();
    EXPECT_FALSE(dir.empty());
    EXPECT_TRUE(fs::is_directory(dir));
}

TEST(Paths, LayoutIsConsistent) {
    EXPECT_EQ(bl::paths::settingsFile().parent_path(), bl::paths::configDir());
    EXPECT_EQ(bl::paths::logDir().parent_path(), bl::paths::localDataDir());
    EXPECT_EQ(bl::paths::crashDumpDir().parent_path(), bl::paths::localDataDir());
    EXPECT_EQ(bl::paths::settingsFile().filename(), "settings.json");
}

TEST(Paths, EnsureCreatesDirectories) {
    const fs::path dir = fs::temp_directory_path() / "bl_tests" / "ensure" / "nested";
    fs::remove_all(dir.parent_path());
    EXPECT_EQ(bl::paths::ensure(dir), dir);
    EXPECT_TRUE(fs::is_directory(dir));
}
