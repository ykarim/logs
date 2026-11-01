#pragma once

#include <compare>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <algorithm>

namespace lab {

struct Command {
    std::string id, client, kind, key, value, expected;
    auto operator<=>(const Command&) const = default;
};

// A proposal is ordered by priority, then proposer, then value. Full values are
// carried in messages to keep availability separate from payload retrieval.
struct Proposal {
    std::uint64_t priority = 0;
    int owner = 0;
    Command command;
    auto operator<=>(const Proposal&) const = default;
};
constexpr auto highest_priority = std::numeric_limits<std::uint64_t>::max();

struct Reply {
    int step = 0;
    Proposal first, previous;
};

// QuePaxa's interval summary register (paper Algorithm 3).
// A logical step measures protocol progress, NOT elapsed time.
struct Recorder {
    int step = 0;
    Proposal first, aggregate, previous;

    Reply record(int incoming_step, const Proposal& proposal) {
        if (incoming_step > step) {
            previous = incoming_step == step + 1 ? aggregate : Proposal{};
            step = incoming_step;
            first = aggregate = proposal;
        } else if (incoming_step == step) {
            aggregate = std::max(aggregate, proposal);
        }
        // Old writes cannot change current state, but still get a fresh reply.
        return {step, first, previous};
    }
};

struct Progress {
    bool advance = false;
    std::optional<Command> decision;
};

// One proposer for ONE log slot. Follows paper Algorithm 4.
// See docs/CODE.md for the source and the deliberately omitted optimizations.
struct Proposer {
    int step = 4; // round 1, phase 0
    Proposal candidate;
    std::map<int, Reply> replies;
    bool done = false;

    Proposer() = default;
    Proposer(int owner, Command command)
        : candidate{highest_priority, owner, std::move(command)} {}

    Proposal outgoing(int owner, int leader, std::mt19937_64& random) const {
        auto proposal = candidate;
        if (step % 4 == 0 && (step > 4 || owner != leader)) {
            // Independent priority for each recorder. Zero is reserved for nil.
            std::uniform_int_distribution<std::uint64_t> draw(1, highest_priority - 1);
            proposal.priority = draw(random);
        }
        return proposal;
    }

    Progress receive(int from, const Reply& reply, int quorum) {
        if (done) return {};
        if (reply.step > step) {
            step = reply.step;
            candidate = reply.first;
            replies.clear();
            return {true, {}};
        }
        if (reply.step != step) return {};
        replies[from] = reply; // Two replies from one recorder count only once.
        if (static_cast<int>(replies.size()) < quorum) return {};

        Proposal first, previous;
        bool fast = true;
        for (const auto& [_, response] : replies) {
            first = std::max(first, response.first);
            previous = std::max(previous, response.previous);
            fast = fast && response.first.priority == highest_priority;
        }
        switch (step % 4) {
        case 0:
            if (fast) return decide(first.command);
            candidate = first;
            break;
        case 1: // Spread the candidate. No additional local computation.
            break;
        case 2:
            if (candidate == previous) return decide(candidate.command);
            break;
        case 3:
            candidate = previous;
            break;
        }
        ++step;
        replies.clear();
        return {true, {}};
    }

private:
    Progress decide(const Command& command) {
        done = true;
        return {false, command};
    }
};

} // namespace lab
