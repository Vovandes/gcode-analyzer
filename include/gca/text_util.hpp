// Small locale-independent text helpers shared by analyzers and writers.
#pragma once

#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

namespace gca {

/// Fixed-point number with `prec` decimals; never "-0".
inline std::string fixedNumber(double v, int prec) {
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << std::fixed << std::setprecision(prec) << v;
    std::string s = os.str();
    if (!s.empty() && s[0] == '-' && s.find_first_not_of("-0.") == std::string::npos) s.erase(0, 1);
    return s;
}

/// Up to 3 decimals, trailing zeros trimmed ("12.5", "3").
inline std::string trimmedNumber(double v) {
    std::string s = fixedNumber(v, 3);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

} // namespace gca
