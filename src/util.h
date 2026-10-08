#pragma once
#include <sstream>
#include <string>
#include <cmath>
#include <limits>

// Единый формат чисел при выводе: целые значения без ".0"
// (print(1.5 + 1.5) печатает "3", а не "3.000000").
inline std::string formatNumber(double value) {
    std::ostringstream out;
    if (std::isfinite(value) && value >= static_cast<double>(std::numeric_limits<long long>::min()) &&
        value < static_cast<double>(std::numeric_limits<long long>::max()) &&
        std::trunc(value) == value)
        out << static_cast<long long>(value);
    else
        out << value;
    return out.str();
}
