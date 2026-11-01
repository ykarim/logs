#include "simulator.hpp"
#include "runtime.hpp"
#include "support.hpp"
#include <algorithm>
#include <iostream>
#include <tuple>

namespace lab {
Simulator::Simulator(int count, std::uint64_t seed, std::string selected, std::ostream& output,
                     ProtocolFactory factory)
    : mode(std::move(selected)), random(seed), out(output) {
    require(count == 3 || count == 5, "Use 3 or 5 nodes.");
    if (!factory)
        factory = select_protocol(mode);
    nodes.resize(count);
    for (int node = 0; node < count; ++node) {
        auto protocol = factory(node, count);
        require(protocol != nullptr, "Protocol factory returned no implementation.");
        if (!protocols.empty()) {
            require(protocol->uses_ordered_log() == protocols.front()->uses_ordered_log(),
                    "A cluster must use one application ordering model.");
        }
        protocols.push_back(std::move(protocol));
    }
}
void Simulator::say(const std::string& text) {
    last = text;
    out << text << '\n';
}
void Simulator::send(Message message) {
    message.id = ++sequence;
    message.at = now + delay;
    ++sent;
    messages.push_back(std::move(message));
}
void Simulator::schedule(int node, const std::string& tag, int milliseconds, int generation) {
    require(milliseconds >= 0 && milliseconds <= 1000000, "Timer delay must be 0..1000000 ms.");
    timers.push_back({++sequence, now + milliseconds, node, tag, generation});
}

// Messages and local timers share one deterministic order: (scheduled time, ID).
// Protocol state, payloads, and timer meaning are invisible to this function.
bool Simulator::step(int id) {
    if (messages.empty() && timers.empty()) {
        say("No queued messages. Pending requests may need retry/sync.");
        return false;
    }
    int selected = 0, at = 0;
    bool is_timer = false;
    auto consider = [&](int candidate_id, int candidate_time, bool timer) {
        if (id && candidate_id != id)
            return;
        if (!selected || std::tie(candidate_time, candidate_id) < std::tie(at, selected)) {
            selected = candidate_id;
            at = candidate_time;
            is_timer = timer;
        }
    };
    for (const auto& message : messages)
        consider(message.id, message.at, false);
    for (const auto& timer : timers)
        consider(timer.id, timer.at, true);
    require(selected != 0, "No such message/timer ID.");
    now = std::max(now, at);

    if (is_timer) {
        auto chosen = std::find_if(timers.begin(), timers.end(),
                                   [&](const auto& timer) { return timer.id == selected; });
        Timer timer = *chosen;
        timers.erase(chosen);
        if (!nodes[timer.node].up) {
            say("Ignore timer on stopped " + node_name(timer.node));
            return true;
        }
        say("t=" + std::to_string(now) + " timer " + node_name(timer.node) + " tag=" + timer.tag);
        Runtime runtime(*this, timer.node);
        protocols[timer.node]->on_timer(timer, runtime);
        return true;
    }

    auto chosen = std::find_if(messages.begin(), messages.end(),
                               [&](const auto& message) { return message.id == selected; });
    Message message = *chosen;
    messages.erase(chosen);
    auto route = message.kind + " #" + std::to_string(message.id) + " " + node_name(message.from) +
                 " -> " + node_name(message.to);
    if (!nodes[message.from].up || !nodes[message.to].up ||
        blocked.contains({message.from, message.to})) {
        ++dropped;
        say("t=" + std::to_string(now) + " DROP " + route);
        return true;
    }
    ++delivered;
    say("t=" + std::to_string(now) + " deliver " + route + " slot=" + std::to_string(message.slot) +
        " step=" + std::to_string(message.step));
    if (message.kind == "response") {
        on_response(message); // Shared client observation, not a protocol message.
    } else {
        Runtime runtime(*this, message.to);
        protocols[message.to]->receive(message, runtime);
    }
    return true;
}
void Simulator::run(int limit) {
    require(limit > 0 && limit <= 100000, "Run limit must be 1..100000.");
    for (int i = 0; i < limit && (!messages.empty() || !timers.empty()); ++i)
        step();
    say("t=" + std::to_string(now) + "ms queued=" +
        std::to_string(messages.size() + timers.size()) + " sent=" + std::to_string(sent) +
        " delivered=" + std::to_string(delivered) + " dropped=" + std::to_string(dropped));
}
void Simulator::sync() {
    for (int node = 0; node < static_cast<int>(nodes.size()); ++node) {
        if (!nodes[node].up)
            continue;
        Runtime runtime(*this, node);
        protocols[node]->sync(runtime);
    }
    say("Anti-entropy queued. Run to deliver. Healing links alone does not resend dropped "
        "traffic.");
}

void Simulator::isolate(int node) {
    for (int other = 0; other < static_cast<int>(nodes.size()); ++other) {
        if (node == other)
            continue;
        blocked.insert({node, other});
        blocked.insert({other, node});
    }
    say(node_name(node) + " isolated. Self messages still work.");
}

void Simulator::stop(int node) {
    nodes.at(node).up = false;
    say(node_name(node) + " stopped (crash-stop; start a new cluster to reset).");
}

void Simulator::duplicate(int id) {
    auto it = std::find_if(messages.begin(), messages.end(),
                           [id](const auto& message) { return message.id == id; });
    require(it != messages.end(), "Unknown message ID.");
    send(*it);
    say("Duplicated envelope with a new transport ID.");
}

void Simulator::drop(int id) {
    auto it = std::find_if(messages.begin(), messages.end(),
                           [id](const auto& message) { return message.id == id; });
    require(it != messages.end(), "Unknown message ID.");
    messages.erase(it);
    ++dropped;
    say("Dropped envelope. Dropping a request does not undo any decision.");
}

void Simulator::tick(int milliseconds) {
    require(milliseconds >= 0 && milliseconds <= 1000000, "tick must be 0..1000000 ms.");
    now += milliseconds;
    say("Clock advanced without delivering messages. A client deadline does not cancel its "
        "operation.");
}
} // namespace lab
