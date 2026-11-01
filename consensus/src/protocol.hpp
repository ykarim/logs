#pragma once
#include "model.hpp"
#include <functional>
#include <memory>
#include <ostream>

namespace lab {
class Runtime;

// Display/read-context snapshot only. The engine never uses this to decide a slot.
struct ProtocolView {
    std::string status;
    std::set<std::string> visible_events;
    std::size_t buffered = 0;
};

// ONE instance per replica. A protocol owns its private state and message types.
// The engine knows only this interface, not terms, ballots, phases or priorities.
class Protocol {
  public:
    virtual ~Protocol() = default;
    virtual bool uses_ordered_log() const = 0;
    virtual void submit(const Command&, const Session&, Runtime&) = 0;
    virtual void receive(const Message&, Runtime&) = 0;
    virtual void on_timer(const Timer&, Runtime&) = 0;
    virtual void sync(Runtime&) = 0;
    virtual ProtocolView view() const = 0;
    virtual void inspect(std::ostream&) const = 0;
};

using ProtocolFactory = std::function<std::unique_ptr<Protocol>(int node, int count)>;
// Registration is the only place shared code names concrete implementations.
ProtocolFactory select_protocol(const std::string& name);
} // namespace lab
