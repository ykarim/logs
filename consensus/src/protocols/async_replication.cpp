#include "async_replication.hpp"
#include "runtime.hpp"
#include "support.hpp"
#include <algorithm>
#include <tuple>

namespace lab {
// This is the separate eventual/causal mode; QuePaxa never calls these rules.
namespace {
// Higher Lamport counter wins, then origin, then ID. This is a deterministic
// merge policy, not a claim about which write happened last in wall-clock time.
bool wins_conflict(const Event& a, const Event& b) {
    return std::tie(a.clock, a.origin, a.id) > std::tie(b.clock, b.origin, b.id);
}
} // namespace
void AsyncReplication::submit(const Command& command, const Session& context, Runtime& runtime) {
    require(command.kind == "set",
            "Async modes support set; atomic compare-and-set requires coordination.");

    if (causal) {
        for (const auto& id : context.seen)
            require(events.contains(id), "WAIT: missing session dependency " + id);
    }
    Event event;
    event.id = command.id;
    event.key = command.key;
    event.value = command.value;
    event.clock = ++clock;
    event.origin = node;
    for (const auto& [id, _] : events)
        event.dependencies.insert(id);
    accept_event(event, runtime);
    runtime.complete_locally(command, "ok");
    for (int to = 0; to < count; ++to) {
        if (to == node)
            continue;
        Message m;
        m.kind = "event";
        m.to = to;
        m.request_id = event.id;
        m.payload = event;
        runtime.send(m);
    }
    runtime.say(command.id + " accepted LOCALLY at " + node_name(node) +
                ". Remote nodes may disagree.");
}

void AsyncReplication::accept_event(Event event, Runtime& runtime) {
    if (causal) {
        for (const auto& id : event.dependencies) {
            if (events.contains(id))
                continue;
            buffered[event.id] = event;
            runtime.say(node_name(node) + " buffers " + event.id + ": waiting for cause " + id);
            return;
        }
    }
    events[event.id] = event;
    buffered.erase(event.id);
    clock = std::max(clock, event.clock);
    auto winner = event;
    for (const auto& [_, other] : events)
        if (other.key == event.key && wins_conflict(other, winner))
            winner = other;
    runtime.application().config[event.key] = winner.value;
    // Deliver newly unblocked effects in a deterministic order.
    for (;;) {
        auto ready = std::find_if(buffered.begin(), buffered.end(), [&](const auto& pair) {
            for (const auto& dep : pair.second.dependencies)
                if (!events.contains(dep))
                    return false;
            return true;
        });
        if (ready == buffered.end())
            break;
        auto next = ready->second;
        buffered.erase(ready);
        accept_event(next, runtime);
    }
}
void AsyncReplication::receive(const Message& message, Runtime& runtime) {
    require(message.kind == "event", "Unexpected async message: " + message.kind);
    accept_event(std::any_cast<const Event&>(message.payload), runtime);
}
void AsyncReplication::on_timer(const Timer&, Runtime&) {
    // Periodic anti-entropy is a future feature. Manual sync is implemented below.
}
void AsyncReplication::sync(Runtime& runtime) {
    for (int to = 0; to < count; ++to) {
        if (to == node)
            continue;
        for (const auto& [_, event] : events) {
            Message message;
            message.kind = "event";
            message.to = to;
            message.request_id = event.id;
            message.payload = event;
            runtime.send(message);
        }
    }
}
ProtocolView AsyncReplication::view() const {
    ProtocolView snapshot;
    for (const auto& [id, _] : events)
        snapshot.visible_events.insert(id);
    snapshot.buffered = buffered.size();
    return snapshot;
}
void AsyncReplication::inspect(std::ostream& out) const {
    for (const auto& [id, event] : events) {
        out << "  event " << id << ' ' << event.key << '=' << event.value << " stamp=("
            << event.clock << ',' << node_name(event.origin) << ") deps=";
        for (const auto& dep : event.dependencies)
            out << dep << ' ';
        out << '\n';
    }
    for (const auto& [id, _] : buffered)
        out << "  buffered " << id << '\n';
}
} // namespace lab
