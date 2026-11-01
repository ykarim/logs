#pragma once
#include <stdexcept>
#include <string>

namespace lab {
// Small display/precondition helpers; no protocol decisions live here.
inline std::string node_name(int index) {
    return "n" + std::to_string(index + 1);
}
inline void require(bool condition, const std::string& explanation) {
    if (!condition)
        throw std::runtime_error(explanation);
}
} // namespace lab
