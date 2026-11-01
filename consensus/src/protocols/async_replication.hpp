#pragma once
#include "protocol.hpp"

namespace lab {
struct Event {
    std::string id, key, value;
    int clock = 0, origin = 0;
    std::set<std::string> dependencies;
};
// Separate merge/application path: this never emits an agreed log entry.
class AsyncReplication final : public Protocol {
  public:
    AsyncReplication(int node, int count, bool causal) : node(node), count(count), causal(causal) {}
    bool uses_ordered_log() const override {
        return false;
    }
    void submit(const Command&, const Session&, Runtime&) override;
    void receive(const Message&, Runtime&) override;
    void on_timer(const Timer&, Runtime&) override;
    void sync(Runtime&) override;
    ProtocolView view() const override;
    void inspect(std::ostream&) const override;

  private:
    int node, count;
    bool causal;
    int clock = 0;
    std::map<std::string, Event> events, buffered;
    void accept_event(Event, Runtime&);
};
} // namespace lab
