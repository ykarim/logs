#include "config.hpp"

namespace lab {
std::string execute(std::map<std::string, std::string>& config, const Command& c) {
    if (c.kind == "get")
        return config.contains(c.key) ? config.at(c.key) : "<missing>";
    if (c.kind == "cas") {
        auto old = config.contains(c.key) ? config.at(c.key) : "<missing>";
        if (old != c.expected)
            return "not-swapped";
        config[c.key] = c.value;
        return "swapped";
    }
    config[c.key] = c.value;
    return "ok";
}
} // namespace lab
