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

// The complete per-replica QuePaxa integration. Pure decision rules remain in
// quepaxa.hpp; record/reply/learn traffic and private state live here.
class QuePaxa final : public Protocol {
  public:
    QuePaxa(int node, int count) : node(node), count(count) {}
    bool uses_ordered_log() const override {
        return true;
    }
    void submit(const Command&, const Session&, Runtime&) override;
    void receive(const Message&, Runtime&) override;
    void on_timer(const Timer&, Runtime&) override;
    void sync(Runtime&) override;
    ProtocolView view() const override;
    void inspect(std::ostream&) const override;

  private:
    int node, count;
    int preferred = 0;
    std::map<int, Recorder> recorders;
    std::map<int, Proposer> proposers;
    void send_record_requests(int slot, Runtime&);
    void on_record(const Message&, Runtime&);
    void on_reply(const Message&, Runtime&);
    void on_learn(const Message&, Runtime&);
};
} // namespace lab
