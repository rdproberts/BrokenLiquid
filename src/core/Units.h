#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace bl {

// Document unit systems, named like the familiar CAD set (PLAN.md 7.7).
// Geometry is always stored in millimetres; units only affect display and input.
enum class UnitSystem { MMGS, CGS, MKS, IPS };

struct Units {
    UnitSystem system = UnitSystem::MMGS;
    int decimals = 2; // length decimals shown in the UI
    int angleDecimals = 2;

    bool operator==(const Units&) const = default;
};

namespace units {

// Millimetres per display length unit (mm: 1, cm: 10, m: 1000, in: 25.4).
double lengthFactor(UnitSystem system);
// "mm", "cm", "m", "in"
const char* lengthSymbol(UnitSystem system);
// "MMGS", "CGS", "MKS", "IPS"
const char* systemName(UnitSystem system);
// "millimetre, gram, second" etc.
const char* systemDescription(UnitSystem system);
std::optional<UnitSystem> systemFromName(std::string_view name);

// Formatting of values stored in mm / mm^2 / mm^3 / radians, with the unit symbol appended.
std::string formatLength(double mm, const Units& u, bool withSymbol = true);
std::string formatArea(double mm2, const Units& u);
std::string formatVolume(double mm3, const Units& u);
std::string formatAngle(double radians, const Units& u);

// Parses user input for a length into millimetres. Accepts plain numbers (document units),
// numbers with a unit ("20mm", "2 in", "1.5cm", "0.1m", "3\""), and simple arithmetic with
// + - * / and parentheses ("10+5", "=(40-2*3)/2", "1in + 5mm"). A leading '=' is allowed.
// Returns nullopt for anything else.
std::optional<double> parseLength(std::string_view text, const Units& u);

// Parses an angle in degrees (optionally "deg" / "°" / "rad" suffix) into radians.
std::optional<double> parseAngle(std::string_view text);

} // namespace units
} // namespace bl
