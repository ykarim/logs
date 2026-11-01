#pragma once
#include <compare>
#include <map>
#include <string>

namespace lab {
// One client operation. Its ID stays the same across transport retries.
struct Command {
    std::string id, client, kind, key, value, expected;
    auto operator<=>(const Command&) const = default;
};

// The ordinary, single-machine meaning of get/set/compare-and-set.
// Consensus chooses WHEN this runs; this function defines WHAT it does.
std::string execute(std::map<std::string, std::string>& config, const Command& command);
} // namespace lab
