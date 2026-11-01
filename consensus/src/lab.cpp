#include "lab.hpp"
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace lab {
namespace {
std::string node_name(int n) { return "n" + std::to_string(n + 1); }
void require(bool condition, const std::string& text) {
    if (!condition) throw std::runtime_error(text);
}
bool newer(const Event& a, const Event& b) {
    return std::tie(a.clock, a.origin, a.id) > std::tie(b.clock, b.origin, b.id);
}
}

std::string execute(std::map<std::string,std::string>& config, const Command& c) {
    if (c.kind == "get") return config.contains(c.key) ? config.at(c.key) : "<missing>";
    if (c.kind == "cas") {
        auto old = config.contains(c.key) ? config.at(c.key) : "<missing>";
        if (old != c.expected) return "not-swapped";
        config[c.key] = c.value;
        return "swapped";
    }
    config[c.key] = c.value;
    return "ok";
}

Simulator::Simulator(int count, std::uint64_t seed, std::string selected, std::ostream& output)
    : mode(std::move(selected)), random(seed), out(output) {
    require(count == 3 || count == 5, "Use 3 or 5 nodes.");
    require(mode == "quepaxa" || mode == "eventual" || mode == "causal", "Unknown mode.");
    nodes.resize(count);
}
void Simulator::say(const std::string& text) { last = text; out << text << '\n'; }
void Simulator::send(Message message) {
    message.id = ++sequence;
    message.at = now + delay;
    ++sent;
    messages.push_back(std::move(message));
}
void Simulator::broadcast(int node, int slot) {
    auto& p = nodes[node].proposers.at(slot);
    for (int to = 0; to < static_cast<int>(nodes.size()); ++to) {
        Message m;
        m.kind = "record"; m.from = node; m.to = to;
        m.slot = slot; m.step = p.step;
        m.proposal = p.outgoing(node, leader, random);
        send(m);
    }
}
void Simulator::submit(int node, Command command) {
    require(nodes.at(node).up, "Node is stopped.");
    if (command.id.empty()) command.id = "r" + std::to_string(++request_sequence);
    for (const auto& r : history) {
        require(r.command.id != command.id || r.command == command,
                "Request ID belongs to a different operation.");
    }
    if (mode != "quepaxa") { write_event(node, command); return; }
    auto& n = nodes[node];
    int slot = n.applied + 1;
    require(!n.proposers.contains(slot),
            "This node already proposes this slot. Use another node, or run first.");
    Request request;
    request.command = command; request.node = node; request.start = now;
    request.invoke_order = ++operation_order;
    history.push_back(request);
    n.proposers.insert_or_assign(slot, Proposer(node, command));
    broadcast(node, slot);
    say(command.id + " pending: " + node_name(node) + " proposes " + command.kind +
        " in slot " + std::to_string(slot) + ". Submission is not success.");
}

bool Simulator::step(int id) {
    if (messages.empty()) { say("No queued messages. Pending requests may need retry/sync."); return false; }
    auto chosen = messages.end();
    if (id) {
        chosen = std::find_if(messages.begin(), messages.end(), [id](const auto& m) { return m.id == id; });
    } else {
        chosen = std::min_element(messages.begin(), messages.end(), [](const auto& a, const auto& b) {
            return std::tie(a.at,a.id) < std::tie(b.at,b.id);
        });
    }
    require(chosen != messages.end(), "No such message ID.");
    Message m = *chosen;
    messages.erase(chosen);
    now = std::max(now, m.at);
    auto route = m.kind + " #" + std::to_string(m.id) + " " + node_name(m.from) + " -> " + node_name(m.to);
    if (!nodes[m.from].up || !nodes[m.to].up || blocked.contains({m.from,m.to})) {
        ++dropped; say("t=" + std::to_string(now) + " DROP " + route); return true;
    }
    ++delivered;
    say("t=" + std::to_string(now) + " deliver " + route + " slot=" +
        std::to_string(m.slot) + " step=" + std::to_string(m.step));
    auto& n = nodes[m.to];
    if (m.kind == "record") {
        Message response;
        response.kind = "reply"; response.from = m.to; response.to = m.from;
        response.slot = m.slot; response.step = m.step;
        response.reply = n.recorders[m.slot].record(m.step, m.proposal);
        send(response);
    } else if (m.kind == "reply") {
        auto found = n.proposers.find(m.slot);
        if (found == n.proposers.end() || found->second.done || found->second.step != m.step) return true;
        auto progress = found->second.receive(m.from, m.reply, static_cast<int>(nodes.size())/2 + 1);
        if (progress.decision) {
            say(node_name(m.to) + " DECIDED slot " + std::to_string(m.slot) + " = " + progress.decision->id +
                ". Quorum evidence establishes the outcome; learners and client still need messages.");
            for (int to = 0; to < static_cast<int>(nodes.size()); ++to) {
                Message learn;
                learn.kind = "learn"; learn.from = m.to; learn.to = to;
                learn.slot = m.slot; learn.command = *progress.decision;
                send(learn);
            }
        } else if (progress.advance) {
            int logical = found->second.step;
            say(node_name(m.to) + " advances to logical step " + std::to_string(logical) +
                " (phase " + std::to_string(logical%4) + "). See Proposer::receive.");
            broadcast(m.to, m.slot);
        }
    } else if (m.kind == "learn") {
        require(!n.decisions.contains(m.slot) || n.decisions.at(m.slot) == m.command,
                "BUG: conflicting decisions!");
        n.decisions[m.slot] = m.command;
        apply(m.to);
    } else if (m.kind == "response") {
        for (auto& r : history) {
            if (r.command.id != m.command.id || r.node != m.to || r.complete) continue;
            r.complete = true; r.result = m.command.value; r.end = now;
            r.return_order = ++operation_order;
            auto& session = sessions[r.command.client];
            session.through = std::max(session.through, m.slot);
            say(r.command.id + " SUCCESS: " + r.result + " (" + std::to_string(r.end-r.start) + "ms)");
        }
    } else if (m.kind == "event") {
        accept_event(m.to, m.event);
    }
    return true;
}
void Simulator::apply(int node) {
    auto& n = nodes[node];
    while (n.decisions.contains(n.applied + 1)) {
        auto command = n.decisions.at(++n.applied);
        if (!n.results.contains(command.id)) n.results[command.id] = execute(n.config, command);
        // The simulator supplies client return addresses. No remote replica's
        // state is consulted here; every result comes from this local log.
        for (const auto& request : history) {
            if (request.complete || request.command.id != command.id) continue;
            Message response;
            response.kind = "response"; response.from = node; response.to = request.node;
            response.slot = n.applied; response.command = command;
            response.command.value = n.results.at(command.id);
            send(response);
        }
    }
}
void Simulator::run(int limit) {
    require(limit > 0 && limit <= 100000, "Run limit must be 1..100000.");
    for (int i = 0; i < limit && !messages.empty(); ++i) step();
    say("t=" + std::to_string(now) + "ms queued=" + std::to_string(messages.size()) +
        " sent=" + std::to_string(sent) + " delivered=" + std::to_string(delivered) + " dropped=" + std::to_string(dropped));
}
void Simulator::sync() {
    for (int from = 0; from < static_cast<int>(nodes.size()); ++from) {
        if (!nodes[from].up) continue;
        for (int to = 0; to < static_cast<int>(nodes.size()); ++to) {
            if (to == from) continue;
            for (const auto& [slot, command] : nodes[from].decisions) {
                Message m; m.kind = "learn"; m.from = from; m.to = to; m.slot = slot; m.command = command; send(m);
            }
            for (const auto& [_, event] : nodes[from].events) {
                Message m; m.kind = "event"; m.from = from; m.to = to; m.event = event; send(m);
            }
        }
    }
    say("Anti-entropy queued. Run to deliver. Healing links alone does not resend dropped traffic.");
}
void Simulator::record_read(int node, const Command& command, const std::string& result) {
    Request r;
    r.command = command; r.node = node; r.start = r.end = now;
    r.invoke_order = r.return_order = ++operation_order;
    r.complete = true; r.result = result;
    history.push_back(r);
}
void Simulator::read(int node, const std::string& key, const std::string& consistency, const std::string& client) {
    auto& n = nodes.at(node);
    require(n.up, "Node is stopped.");
    if (consistency == "linearizable") {
        require(mode == "quepaxa", "Linearizable reads need the consensus log in this lab.");
        submit(node, {"",client,"get",key,"",""}); return;
    }
    require(consistency == "local" || consistency == "session", "Read mode: local, session, linearizable.");
    auto& context = sessions[client];
    if (consistency == "session") {
        require(mode != "quepaxa" || n.applied >= context.through,
                "WAIT: session needs applied >= " + std::to_string(context.through) + "; sync or use a caught-up node.");
        for (const auto& id : context.seen) require(n.events.contains(id), "WAIT: session needs event " + id);
        context.through = std::max(context.through,n.applied);
        for (const auto& [id,_] : n.events) context.seen.insert(id);
    }
    Command command{"r" + std::to_string(++request_sequence),client,"get",key,"",""};
    auto result = execute(n.config,command);
    record_read(node,command,result);
    say(consistency + " read " + node_name(node) + ": " + key + "=" + result + " (0 simulated network ms)");
}
void Simulator::write_event(int node, const Command& command) {
    require(command.kind == "set", "Async modes support set; atomic compare-and-set requires coordination.");
    auto& n = nodes[node];
    auto& context = sessions[command.client];
    if (mode == "causal") {
        for (const auto& id : context.seen) require(n.events.contains(id), "WAIT: missing session dependency " + id);
    }
    Event event;
    event.id = command.id; event.key = command.key; event.value = command.value;
    event.clock = ++n.clock; event.origin = node;
    for (const auto& [id,_] : n.events) event.dependencies.insert(id);
    accept_event(node,event);
    context.seen.insert(event.id);
    record_read(node,command,"ok");
    for (int to = 0; to < static_cast<int>(nodes.size()); ++to) {
        if (to == node) continue;
        Message m; m.kind = "event"; m.from = node; m.to = to; m.event = event; send(m);
    }
    say(command.id + " accepted LOCALLY at " + node_name(node) + ". Remote nodes may disagree.");
}
void Simulator::accept_event(int node, Event event) {
    auto& n = nodes[node];
    if (mode == "causal") {
        for (const auto& id : event.dependencies) {
            if (n.events.contains(id)) continue;
            n.buffered[event.id] = event;
            say(node_name(node) + " buffers " + event.id + ": waiting for cause " + id);
            return;
        }
    }
    n.events[event.id] = event;
    n.buffered.erase(event.id);
    n.clock = std::max(n.clock,event.clock);
    auto winner = event;
    for (const auto& [_,other] : n.events) if (other.key == event.key && newer(other,winner)) winner = other;
    n.config[event.key] = winner.value;
    // Deliver newly unblocked effects in a deterministic order.
    for (;;) {
        auto ready = std::find_if(n.buffered.begin(),n.buffered.end(),[&](const auto& pair) {
            for (const auto& dep : pair.second.dependencies) if (!n.events.contains(dep)) return false;
            return true;
        });
        if (ready == n.buffered.end()) break;
        auto next = ready->second;
        n.buffered.erase(ready);
        accept_event(node,next);
    }
}
void Simulator::isolate(int node) {
    for (int other = 0; other < static_cast<int>(nodes.size()); ++other) {
        if (node == other) continue;
        blocked.insert({node,other}); blocked.insert({other,node});
    }
    say(node_name(node) + " isolated. Self messages still work.");
}
void Simulator::stop(int node) {
    nodes.at(node).up = false;
    say(node_name(node) + " stopped (crash-stop; start a new cluster to reset).");
}
void Simulator::retry(int node, const std::string& id) {
    require(mode == "quepaxa", "Retries with shared IDs are currently supported only in consensus mode.");
    auto it = std::find_if(history.begin(),history.end(),[&](const auto& r) {return r.command.id == id;});
    require(it != history.end(), "Unknown request ID.");
    Command command = it->command;
    // Transport retry is NOT a new logical operation in the client history.
    auto& n = nodes.at(node);
    require(n.up, "Node is stopped.");
    int slot = n.applied + 1;
    require(!n.proposers.contains(slot), "This node already participated in this slot. Retry via a different node, or sync its decided slot first.");
    n.proposers.emplace(slot,Proposer(node,command));
    broadcast(node,slot);
    say("Retrying " + id + " via " + node_name(node) + "; response still goes to original client endpoint.");
}
void Simulator::duplicate(int id) {
    auto it = std::find_if(messages.begin(),messages.end(),[id](const auto& m) {return m.id == id;});
    require(it != messages.end(), "Unknown message ID.");
    send(*it); say("Duplicated envelope with a new transport ID.");
}
void Simulator::drop(int id) {
    auto it = std::find_if(messages.begin(),messages.end(),[id](const auto& m) {return m.id == id;});
    require(it != messages.end(), "Unknown message ID.");
    messages.erase(it); ++dropped; say("Dropped envelope. Dropping a request does not undo any decision.");
}
void Simulator::tick(int milliseconds) {
    require(milliseconds >= 0 && milliseconds <= 1000000, "tick must be 0..1000000 ms.");
    now += milliseconds;
    say("Clock advanced without delivering messages. A client deadline does not cancel its operation.");
}
void Simulator::status() {
    out << "mode=" << mode << " t=" << now << "ms preferred=" << node_name(leader)
        << " quorum=" << nodes.size()/2+1 << '\n';
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
        const auto& n = nodes[i];
        out << node_name(i) << " up=" << n.up << " applied=" << n.applied
            << " events=" << n.events.size() << " buffered=" << n.buffered.size() << " {";
        for (const auto& [key,value] : n.config) out << key << '=' << value << ' ';
        out << "}\n";
    }
}
void Simulator::inspect(int node) {
    const auto& n = nodes.at(node);
    out << node_name(node) << ": applied=" << n.applied << '\n';
    for (const auto& [slot,r] : n.recorders)
        out << "  slot " << slot << " recorder step=" << r.step << " first=" << r.first.command.id
            << " priority=" << r.first.priority << " aggregate=" << r.aggregate.command.id << '\n';
    for (const auto& [slot,p] : n.proposers)
        out << "  slot " << slot << " proposer step=" << p.step << " replies=" << p.replies.size()
            << " candidate=" << p.candidate.command.id << " done=" << p.done << '\n';
    for (const auto& [slot,c] : n.decisions) out << "  learned slot " << slot << " = " << c.id << ' ' << c.kind << ' ' << c.key << '\n';
    for (const auto& [id,e] : n.events) {
        out << "  event " << id << ' ' << e.key << '=' << e.value << " stamp=(" << e.clock << ',' << node_name(e.origin) << ") deps=";
        for (const auto& dep : e.dependencies) out << dep << ' ';
        out << '\n';
    }
    for (const auto& [id,_] : n.buffered) out << "  buffered " << id << '\n';
}
void Simulator::show_messages() {
    for (const auto& m : messages)
        out << '#' << m.id << " at=" << m.at << ' ' << m.kind << ' ' << node_name(m.from) << " -> " << node_name(m.to)
            << " slot=" << m.slot << " step=" << m.step << " request="
            << (m.kind == "event" ? m.event.id : m.kind == "record" ? m.proposal.command.id : m.command.id) << '\n';
}
void Simulator::show_history() {
    for (const auto& r : history) {
        out << r.command.id << " client=" << r.command.client << ' ' << r.command.kind << ' ' << r.command.key
            << " via=" << node_name(r.node) << " invoked=" << r.start << "ms ";
        if (r.complete) out << "returned=" << r.end << "ms result=" << r.result;
        else out << "PENDING/UNKNOWN (not a confirmed failure)";
        out << '\n';
    }
}
void Simulator::check_agreement() const {
    require(mode == "quepaxa", "Log agreement applies to quepaxa mode. Inspect event versions and convergence in async modes.");
    std::map<int,Command> decisions;
    for (const auto& n : nodes) {
        for (const auto& [slot,command] : n.decisions) {
            require(!decisions.contains(slot) || decisions.at(slot) == command,"Agreement violation at slot " + std::to_string(slot));
            decisions[slot] = command;
        }
        if (mode != "quepaxa") continue;
        std::map<std::string,std::string> replay, results;
        for (int slot = 1; slot <= n.applied; ++slot) {
            require(n.decisions.contains(slot),"Applied log contains a hole.");
            const auto& c = n.decisions.at(slot);
            if (!results.contains(c.id)) results[c.id] = execute(replay,c);
        }
        require(replay == n.config && results == n.results,"Application does not match log replay.");
    }
}
bool Simulator::check_linearizable() {
    require(history.size() <= 12,"History checker supports up to 12 operations; start a new cluster.");
    int explored = 0;
    using Map = std::map<std::string,std::string>;
    std::function<bool(Map,Map,unsigned)> search = [&](Map state, Map results, unsigned used) {
        ++explored;
        require(explored <= 200000, "INCONCLUSIVE: history search exceeded 200000 states; use a smaller history.");
        bool finished = true;
        for (unsigned i = 0; i < history.size(); ++i) if (history[i].complete && !(used & (1u<<i))) finished = false;
        if (finished) return true; // Remaining pending operations may be omitted.
        for (unsigned i = 0; i < history.size(); ++i) {
            if (used & (1u<<i)) continue;
            const auto& r = history[i];
            bool allowed = true;
            for (unsigned j = 0; j < history.size(); ++j) {
                const auto& earlier = history[j];
                if (earlier.complete && earlier.return_order < r.invoke_order && !(used & (1u<<j))) allowed = false;
            }
            if (!allowed) continue;
            auto next = state, cache = results;
            if (!cache.contains(r.command.id)) cache[r.command.id] = execute(next,r.command);
            if (r.complete && cache.at(r.command.id) != r.result) continue;
            if (search(next,cache,used | (1u<<i))) return true;
        }
        return false;
    };
    bool valid = search({}, {}, 0);
    say(std::string(valid ? "PASS" : "VIOLATION") + ": linearizability of this observed history (" + std::to_string(explored) +
        " states searched). " + (valid ? "A legal ordering exists; this is not a proof for other runs." : "No legal ordering explains these results."));
    return valid;
}
} // namespace lab
