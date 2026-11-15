#pragma once

#include "config.hpp"
#include <compare>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <algorithm>

namespace lab {

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

// One QuePaxa round consists of four record/reply exchanges. The proposer sends
// its current candidate to all recorders and waits for a quorum in every phase;
// the phase determines what the returned evidence lets the proposer conclude:
//
//   0 propose:
//     Introduce prioritized proposals and select the best proposal observed by
//     a quorum. The preferred proposer may decide immediately on the fast path.
//
//   1 spread_candidate:
//     Write that selected candidate to a quorum. This spreads knowledge of the
//     candidate; the proposer does not make another selection in this phase.
//
//   2 check_decision:
//     Gather evidence produced by the spread. If the candidate agrees with the
//     best relevant recorder summary, enough replicas overlap to decide it.
//
//   3 choose_next_round:
//     If phase 2 could not decide, choose the best safe candidate found in the
//     evidence and carry it into the next round instead of starting over.
//
// step = 4 * round + phase, so steps 4, 5, 6, and 7 are the four phases of
// round 1. These are logical progress steps, not wall-clock time.
enum class Phase { propose = 0, spread_candidate = 1, check_decision = 2, choose_next_round = 3 };

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
    Proposer(int owner, Command command) : candidate{highest_priority, owner, std::move(command)} {}

    Phase phase() const {
        return static_cast<Phase>(step % 4);
    }

    // Decide which priority THIS outgoing proposal gets. A backup does not
    // become preferred just because it starts working.
    Proposal outgoing(int owner, int leader, std::mt19937_64& random) const {
        auto proposal = candidate;
        if (phase() == Phase::propose && (step > 4 || owner != leader)) {
            // Independent priority for each recorder. Zero is reserved for nil.
            std::uniform_int_distribution<std::uint64_t> draw(1, highest_priority - 1);
            proposal.priority = draw(random);
        }
        return proposal;
    }

    Progress receive(int from, const Reply& reply, int quorum) {
        if (done)
            return {};
        // Another proposer already advanced this recorder. Catch up using
        // its first proposal at that later step; do not restart with our value.
        if (reply.step > step) {
            step = reply.step;
            candidate = reply.first;
            replies.clear();
            return {true, {}};
        }
        if (reply.step != step)
            return {};
        replies[from] = reply; // Two replies from one recorder count only once.
        if (static_cast<int>(replies.size()) < quorum)
            return {};

        Proposal best_first, best_previous;
        bool all_firsts_have_special_priority = true;
        for (const auto& [_, response] : replies) {
            best_first = std::max(best_first, response.first);
            best_previous = std::max(best_previous, response.previous);
            all_firsts_have_special_priority =
                all_firsts_have_special_priority && response.first.priority == highest_priority;
        }
        // The protocol's four phases. Every phase already gathered a quorum
        // above. This switch determines what that particular evidence means.
        switch (phase()) {
        case Phase::propose:
            // FAST PATH: a quorum first recorded the unique preferred proposal.
            if (all_firsts_have_special_priority)
                return decide(best_first.command);
            // FALLBACK: adopt the best first proposal, even if it isn't ours.
            candidate = best_first;
            break;
        case Phase::spread_candidate:
            // We have spread our candidate to a quorum. No new selection here.
            break;
        case Phase::check_decision:
            // Compare our spread candidate with the best aggregate from the
            // preceding step. A match is QuePaxa's fallback decision condition.
            if (candidate == best_previous)
                return decide(candidate.command);
            break;
        case Phase::choose_next_round:
            // No decision this round: carry the gathered candidate forward.
            // outgoing() will randomize priorities for the next round.
            candidate = best_previous;
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
