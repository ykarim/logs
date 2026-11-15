#pragma once
#include "protocol.hpp"
#include "quepaxa.hpp"

namespace lab {
// Algorithm-specific wire payloads; the network never interprets these.
struct RecordRequest {
    Proposal proposal;
};
struct RecordReply {
    Reply evidence;
};
struct LearnDecision {
    Command command;
};

// One QuePaxa object represents the protocol state inside ONE replica.
// Simulator creates one of these for n1, one for n2, and so on. The objects do
// not share memory; they can learn about one another only through messages sent
// with Runtime.
//
// Every replica can play both QuePaxa roles:
//
//   * recorder: remembers proposals sent by any proposer and returns evidence;
//   * proposer: drives a client command through the protocol toward a decision.
//
// The common healthy path looks like this:
//
//   client submits X to n1
//       -> n1 creates Proposer(slot, X)
//       -> n1 sends a "record" to every replica
//       -> each replica updates Recorder(slot) and sends a "reply"
//       -> n1 interprets a quorum of replies
//       -> after the fast path, or more fallback phases, n1 decides X
//       -> n1 broadcasts "learn(slot, X)"
//       -> each replica passes X to Runtime for in-order application
//
// A decision and application are deliberately separate. QuePaxa chooses the
// command for a slot; Runtime/the shared application layer applies learned slots
// in order and decides when a waiting client may receive success.
//
// This class is the message-handling adapter around the small decision rules in
// quepaxa.hpp. In particular, Recorder::record summarizes local evidence and
// Proposer::receive explains what a quorum means in each of the four phases.
class QuePaxa final : public Protocol {
  public:
    QuePaxa(int node, int count) : node(node), count(count) {}

    // QuePaxa chooses one command for each numbered log slot. This differs from
    // the async/eventual protocol, which can accept independent local events.
    bool uses_ordered_log() const override {
        return true;
    }

    // Start proposing a newly submitted client command in this node's next
    // unapplied slot. Submission begins work; it does not mean the command won.
    void submit(const Command&, const Session&, Runtime&) override;

    // Dispatch one delivered QuePaxa record, reply, or learn message. Network
    // delay, loss, duplication, and partitions are handled by Simulator.
    void receive(const Message&, Runtime&) override;

    // Reserved for planned automatic proposer hedging. There are no QuePaxa
    // protocol timers in the current teaching implementation.
    void on_timer(const Timer&, Runtime&) override;

    // Re-send this replica's known decisions to peers. This is simple manual
    // catch-up for the lab, not QuePaxa's full production recovery machinery.
    void sync(Runtime&) override;

    // Small read-only summaries used by status and inspect CLI commands. These
    // never influence protocol decisions.
    ProtocolView view() const override;
    void inspect(std::ostream&) const override;

  private:
    // Immutable identity of this replica and size of its configured cluster.
    int node, count;

    // The preferred proposer gets QuePaxa's unique maximum priority on the
    // first fast-path attempt. It is an optimization, not an exclusive leader:
    // another node is still allowed to propose and use the fallback path. The
    // demo currently fixes this to n1 (index 0); shared preference changes and
    // automatic replacement are later milestones in PLAN.md.
    int preferred = 0;

    // Independent state for each log slot. A replica may record proposals from
    // other nodes even when it is not itself proposing that slot.
    std::map<int, Recorder> recorders;

    // Proposer state exists only for slots this node has tried to drive. Each
    // Proposer holds its candidate, current logical step, and deduplicated
    // replies. It is separate from recorder state because the roles are
    // logically independent even when they live in the same process.
    std::map<int, Proposer> proposers;

    // Outgoing half of a proposer step: send the step's proposal to every
    // recorder. Replies return asynchronously through on_reply().
    void send_record_requests(int slot, Runtime&);

    // The three incoming halves of the record -> reply -> learn journey.
    void on_record(const Message&, Runtime&);
    void on_reply(const Message&, Runtime&);
    void on_learn(const Message&, Runtime&);
};
} // namespace lab
