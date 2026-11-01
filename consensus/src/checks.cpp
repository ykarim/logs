#include "simulator.hpp"
#include "support.hpp"
#include <functional>

namespace lab {
// Observer-only correctness checks, separate from protocol execution.
void Simulator::check_agreement() const {
    require(protocols.front()->uses_ordered_log(),
            "Log agreement applies to ordered-log protocols. Inspect event versions and "
            "convergence in async modes.");
    std::map<int, Command> decisions;
    for (const auto& n : nodes) {
        for (const auto& [slot, command] : n.decisions) {
            require(!decisions.contains(slot) || decisions.at(slot) == command,
                    "Agreement violation at slot " + std::to_string(slot));
            decisions[slot] = command;
        }
        std::map<std::string, std::string> replay, results;
        for (int slot = 1; slot <= n.applied; ++slot) {
            require(n.decisions.contains(slot), "Applied log contains a hole.");
            const auto& c = n.decisions.at(slot);
            if (!results.contains(c.id))
                results[c.id] = execute(replay, c);
        }
        require(replay == n.config && results == n.results,
                "Application does not match log replay.");
    }
}

bool Simulator::check_linearizable() {
    require(history.size() <= 12,
            "History checker supports up to 12 operations; start a new cluster.");
    int explored = 0;
    using Map = std::map<std::string, std::string>;
    // Try operations one at a time against an ordinary map. Reject an ordering
    // if it contradicts a recorded response or real-time precedence. Pending
    // operations may have executed, so we may include them without a response.
    // `used` is a bit set: bit i means history[i] is already in this trial order.
    std::function<bool(Map, Map, unsigned)> search = [&](Map state, Map results, unsigned used) {
        ++explored;
        require(explored <= 200000,
                "INCONCLUSIVE: history search exceeded 200000 states; use a smaller history.");
        bool finished = true;
        for (unsigned i = 0; i < history.size(); ++i)
            if (history[i].complete && !(used & (1u << i)))
                finished = false;
        if (finished)
            return true; // Remaining pending operations may be omitted.
        for (unsigned i = 0; i < history.size(); ++i) {
            if (used & (1u << i))
                continue;
            const auto& r = history[i];
            bool allowed = true;
            for (unsigned j = 0; j < history.size(); ++j) {
                const auto& earlier = history[j];
                if (earlier.complete && earlier.return_order < r.invoke_order &&
                    !(used & (1u << j)))
                    allowed = false;
            }
            if (!allowed)
                continue;
            auto next = state, cache = results;
            if (!cache.contains(r.command.id))
                cache[r.command.id] = execute(next, r.command);
            if (r.complete && cache.at(r.command.id) != r.result)
                continue;
            if (search(next, cache, used | (1u << i)))
                return true;
        }
        return false;
    };
    bool valid = search({}, {}, 0);
    say(std::string(valid ? "PASS" : "VIOLATION") + ": linearizability of this observed history (" +
        std::to_string(explored) + " states searched). " +
        (valid ? "A legal ordering exists; this is not a proof for other runs."
               : "No legal ordering explains these results."));
    return valid;
}
} // namespace lab
