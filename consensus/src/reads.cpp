#include "simulator.hpp"
#include "support.hpp"
#include <algorithm>

namespace lab {
// Local reads bypass the log. Session reads check a watermark.
// Linearizable reads become commands in the same log as writes.
void Simulator::record_local_result(int node, const Command& command, const std::string& result) {
    Request r;
    r.command = command;
    r.node = node;
    r.start = r.end = now;
    r.invoke_order = r.return_order = ++operation_order;
    r.complete = true;
    r.result = result;
    history.push_back(r);
    // Local writes and session reads carry their own observed context.
    if (command.kind != "get") {
        sessions[command.client].seen.insert(command.id);
    }
}

void Simulator::read(int node, const std::string& key, const std::string& consistency,
                     const std::string& client) {
    auto& local = nodes.at(node);
    require(local.up, "Node is stopped.");
    if (consistency == "linearizable") {
        require(protocols[node]->uses_ordered_log(),
                "Linearizable reads need the consensus log in this lab.");
        submit(node, {"", client, "get", key, "", ""});
        return;
    }
    require(consistency == "local" || consistency == "session",
            "Read mode: local, session, linearizable.");
    auto& context = sessions[client];
    auto visible = protocols[node]->view().visible_events;
    if (consistency == "session") {
        require(!protocols[node]->uses_ordered_log() || local.applied >= context.through,
                "WAIT: session needs applied >= " + std::to_string(context.through) +
                    "; sync or use a caught-up node.");
        for (const auto& id : context.seen)
            require(visible.contains(id), "WAIT: session needs event " + id);
        context.through = std::max(context.through, local.applied);
        for (const auto& id : visible)
            context.seen.insert(id);
    }
    Command command{"r" + std::to_string(++request_sequence), client, "get", key, "", ""};
    auto result = execute(local.config, command);
    record_local_result(node, command, result);
    say(consistency + " read " + node_name(node) + ": " + key + "=" + result +
        " (0 simulated network ms)");
}
} // namespace lab
