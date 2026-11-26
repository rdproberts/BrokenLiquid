#include "core/Units.h"

#include <gtest/gtest.h>

#include <numbers>

using namespace bl;

namespace {
Units mm() { return Units{}; }
Units inch() {
    Units u;
    u.system = UnitSystem::Inch;
    u.decimals = 3;
    return u;
}
Units cgs() {
    Units u;
    u.system = UnitSystem::Centimetre;
    return u;
}
} // namespace

TEST(Units, FormatsInDocumentUnits) {
    EXPECT_EQ(units::formatLength(25.4, mm()), "25.40 mm");
    EXPECT_EQ(units::formatLength(25.4, inch()), "1.000 in");
    EXPECT_EQ(units::formatLength(25.4, inch(), false), "1.000");
    EXPECT_EQ(units::formatLength(-0.001, mm()), "0.00 mm"); // no "-0.00"
    EXPECT_EQ(units::formatArea(100.0, mm()), "100.00 mm\xC2\xB2");
    EXPECT_EQ(units::formatVolume(1000.0, cgs()), "1.00 cm\xC2\xB3");
    EXPECT_EQ(units::formatAngle(std::numbers::pi / 2, mm()), "90.00\xC2\xB0");
}

TEST(Units, ParsesPlainNumbersInDocumentUnits) {
    EXPECT_DOUBLE_EQ(*units::parseLength("20", mm()), 20.0);
    EXPECT_DOUBLE_EQ(*units::parseLength("2", inch()), 50.8);
    EXPECT_DOUBLE_EQ(*units::parseLength("  -3.5 ", mm()), -3.5);
    EXPECT_DOUBLE_EQ(*units::parseLength("1e2", mm()), 100.0);
}

TEST(Units, ParsesUnitSuffixes) {
    EXPECT_DOUBLE_EQ(*units::parseLength("1in", mm()), 25.4);
    EXPECT_DOUBLE_EQ(*units::parseLength("1\"", mm()), 25.4);
    EXPECT_DOUBLE_EQ(*units::parseLength("2 cm", mm()), 20.0);
    EXPECT_DOUBLE_EQ(*units::parseLength("1m", mm()), 1000.0);
    EXPECT_DOUBLE_EQ(*units::parseLength("5mm", inch()), 5.0); // "m" does not swallow "mm"
    EXPECT_DOUBLE_EQ(*units::parseLength("1ft", mm()), 304.8);
}

TEST(Units, ParsesArithmetic) {
    EXPECT_DOUBLE_EQ(*units::parseLength("=40/2", mm()), 20.0);
    EXPECT_DOUBLE_EQ(*units::parseLength("1in + 5", mm()), 30.4);
    EXPECT_DOUBLE_EQ(*units::parseLength("(10 + 5) * 2", mm()), 30.0);
    EXPECT_DOUBLE_EQ(*units::parseLength("10 - 2 - 3", mm()), 5.0);
    EXPECT_DOUBLE_EQ(*units::parseLength("2 * 3 + 1", mm()), 7.0);
}

TEST(Units, RejectsGarbage) {
    EXPECT_FALSE(units::parseLength("", mm()));
    EXPECT_FALSE(units::parseLength("abc", mm()));
    EXPECT_FALSE(units::parseLength("1 +", mm()));
    EXPECT_FALSE(units::parseLength("(1", mm()));
    EXPECT_FALSE(units::parseLength("1 / 0", mm()));
    EXPECT_FALSE(units::parseLength("3 apples", mm()));
}

TEST(Units, ParsesAngles) {
    EXPECT_NEAR(*units::parseAngle("90"), std::numbers::pi / 2, 1e-12);
    EXPECT_NEAR(*units::parseAngle("=180/4"), std::numbers::pi / 4, 1e-12);
    EXPECT_FALSE(units::parseAngle("x"));
}

TEST(Units, SystemNamesRoundTrip) {
    for (UnitSystem s : {UnitSystem::Millimetre, UnitSystem::Centimetre, UnitSystem::Metre, UnitSystem::Inch}) {
        EXPECT_EQ(units::systemFromName(units::systemName(s)), s);
    }
    EXPECT_FALSE(units::systemFromName("furlongs"));
}
