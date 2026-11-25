#include "core/Units.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace bl::units {

double lengthFactor(UnitSystem system) {
    switch (system) {
    case UnitSystem::MMGS: return 1.0;
    case UnitSystem::CGS: return 10.0;
    case UnitSystem::MKS: return 1000.0;
    case UnitSystem::IPS: return 25.4;
    }
    return 1.0;
}

const char* lengthSymbol(UnitSystem system) {
    switch (system) {
    case UnitSystem::MMGS: return "mm";
    case UnitSystem::CGS: return "cm";
    case UnitSystem::MKS: return "m";
    case UnitSystem::IPS: return "in";
    }
    return "mm";
}

const char* systemName(UnitSystem system) {
    switch (system) {
    case UnitSystem::MMGS: return "MMGS";
    case UnitSystem::CGS: return "CGS";
    case UnitSystem::MKS: return "MKS";
    case UnitSystem::IPS: return "IPS";
    }
    return "MMGS";
}

const char* systemDescription(UnitSystem system) {
    switch (system) {
    case UnitSystem::MMGS: return "millimetre, gram, second";
    case UnitSystem::CGS: return "centimetre, gram, second";
    case UnitSystem::MKS: return "metre, kilogram, second";
    case UnitSystem::IPS: return "inch, pound, second";
    }
    return "";
}

std::optional<UnitSystem> systemFromName(std::string_view name) {
    for (UnitSystem s : {UnitSystem::MMGS, UnitSystem::CGS, UnitSystem::MKS, UnitSystem::IPS}) {
        if (name == systemName(s)) {
            return s;
        }
    }
    return std::nullopt;
}

namespace {

std::string fixed(double v, int decimals) {
    if (std::abs(v) < 0.5 * std::pow(10.0, -decimals)) {
        v = 0.0; // avoid "-0.00"
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

} // namespace

std::string formatLength(double mm, const Units& u, bool withSymbol) {
    std::string s = fixed(mm / lengthFactor(u.system), u.decimals);
    if (withSymbol) {
        s += ' ';
        s += lengthSymbol(u.system);
    }
    return s;
}

std::string formatArea(double mm2, const Units& u) {
    const double f = lengthFactor(u.system);
    return fixed(mm2 / (f * f), u.decimals) + ' ' + lengthSymbol(u.system) + "\xC2\xB2"; // ²
}

std::string formatVolume(double mm3, const Units& u) {
    const double f = lengthFactor(u.system);
    return fixed(mm3 / (f * f * f), u.decimals) + ' ' + lengthSymbol(u.system) + "\xC2\xB3"; // ³
}

std::string formatAngle(double radians, const Units& u) {
    return fixed(radians * 180.0 / std::numbers::pi, u.angleDecimals) + "\xC2\xB0"; // °
}

namespace {

// Recursive-descent parser for "expr := term (('+'|'-') term)*", "term := factor (('*'|'/') factor)*",
// "factor := ('+'|'-') factor | number [unit] | '(' expr ')'". Numbers without a unit use the
// document's unit. Multiplication/division by a unit-less number scales; we keep it simple and
// treat every number as a length in mm after unit conversion except the right operand of * and /,
// which is a plain factor.
class Parser {
public:
    Parser(std::string_view text, double defaultFactor) : m_s(text), m_defaultFactor(defaultFactor) {}

    std::optional<double> parse() {
        skip();
        if (peek() == '=') {
            ++m_i;
        }
        auto v = expr(true);
        skip();
        if (!v || m_i != m_s.size()) {
            return std::nullopt;
        }
        return v;
    }

private:
    char peek() const { return m_i < m_s.size() ? m_s[m_i] : '\0'; }
    void skip() {
        while (m_i < m_s.size() && std::isspace(static_cast<unsigned char>(m_s[m_i]))) {
            ++m_i;
        }
    }

    // `length` = true: numbers are lengths (apply units); false: plain scale factors.
    std::optional<double> expr(bool length) {
        auto left = term(length);
        while (left) {
            skip();
            const char op = peek();
            if (op != '+' && op != '-') {
                break;
            }
            ++m_i;
            auto right = term(length);
            if (!right) {
                return std::nullopt;
            }
            *left = op == '+' ? *left + *right : *left - *right;
        }
        return left;
    }

    std::optional<double> term(bool length) {
        auto left = factor(length);
        while (left) {
            skip();
            const char op = peek();
            if (op != '*' && op != '/') {
                break;
            }
            ++m_i;
            auto right = factor(false);
            if (!right || (op == '/' && *right == 0.0)) {
                return std::nullopt;
            }
            *left = op == '*' ? *left * *right : *left / *right;
        }
        return left;
    }

    std::optional<double> factor(bool length) {
        skip();
        const char c = peek();
        if (c == '+' || c == '-') {
            ++m_i;
            auto v = factor(length);
            return v ? std::optional<double>(c == '-' ? -*v : *v) : std::nullopt;
        }
        if (c == '(') {
            ++m_i;
            auto v = expr(length);
            skip();
            if (!v || peek() != ')') {
                return std::nullopt;
            }
            ++m_i;
            return v;
        }
        return number(length);
    }

    std::optional<double> number(bool length) {
        const size_t start = m_i;
        while (m_i < m_s.size() && (std::isdigit(static_cast<unsigned char>(m_s[m_i])) || m_s[m_i] == '.')) {
            ++m_i;
        }
        // Optional exponent: 1e3
        if (m_i > start && m_i < m_s.size() && (m_s[m_i] == 'e' || m_s[m_i] == 'E') && m_i + 1 < m_s.size() &&
            (std::isdigit(static_cast<unsigned char>(m_s[m_i + 1])) || m_s[m_i + 1] == '-' || m_s[m_i + 1] == '+')) {
            ++m_i;
            if (m_s[m_i] == '-' || m_s[m_i] == '+') {
                ++m_i;
            }
            while (m_i < m_s.size() && std::isdigit(static_cast<unsigned char>(m_s[m_i]))) {
                ++m_i;
            }
        }
        if (m_i == start) {
            return std::nullopt;
        }
        double value = 0.0;
        try {
            value = std::stod(std::string(m_s.substr(start, m_i - start)));
        } catch (...) {
            return std::nullopt;
        }
        if (!length) {
            return value;
        }
        skip();
        return value * unitFactor();
    }

    double unitFactor() {
        struct U {
            const char* name;
            double mm;
        };
        static const U table[] = {{"mm", 1.0}, {"cm", 10.0}, {"m", 1000.0}, {"in", 25.4}, {"\"", 25.4}, {"ft", 304.8}};
        for (const U& u : table) {
            const std::string_view n(u.name);
            if (m_s.substr(m_i, n.size()) == n) {
                // "m" must not swallow the start of "mm".
                const size_t end = m_i + n.size();
                if (end < m_s.size() && std::isalpha(static_cast<unsigned char>(m_s[end]))) {
                    continue;
                }
                m_i = end;
                return u.mm;
            }
        }
        return m_defaultFactor;
    }

    std::string_view m_s;
    size_t m_i = 0;
    double m_defaultFactor;
};

} // namespace

std::optional<double> parseLength(std::string_view text, const Units& u) {
    return Parser(text, lengthFactor(u.system)).parse();
}

std::optional<double> parseAngle(std::string_view text) {
    std::string s(text);
    bool radians = false;
    auto strip = [&s](std::string_view suffix) {
        if (s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0) {
            s.resize(s.size() - suffix.size());
            return true;
        }
        return false;
    };
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    if (strip("rad")) {
        radians = true;
    } else if (!strip("deg")) {
        strip("\xC2\xB0");
    }
    const auto v = Parser(s, 1.0).parse(); // plain numbers: default factor 1
    if (!v) {
        return std::nullopt;
    }
    return radians ? *v : *v * std::numbers::pi / 180.0;
}

} // namespace bl::units
