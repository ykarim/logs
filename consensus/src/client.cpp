#include "simulator.hpp"
#include "runtime.hpp"
#include "support.hpp"
#include <algorithm>

namespace lab {
// Client identity/history is shared. Choosing how to propose belongs to Protocol.
void Simulator::submit(int node, Command command) {
    require(nodes.at(node).up, "Node is stopped.");
    if (command.id.empty())
        command.id = "r" + std::to_string(++request_sequence);
    for (const auto& request : history) {
        require(request.command.id != command.id || request.command == command,
                "Request ID belongs to a different operation.");
    }
    Runtime runtime(*this, node);
    if (!protocols[node]->uses_ordered_log()) {
        protocols[node]->submit(command, sessions[command.client], runtime);
        return; // Async protocol completes locally, without an ordered log.
    }
    Request request;
    request.command = command;
    request.node = node;
    request.start = now;
    request.invoke_order = ++operation_order;
    history.push_back(request);
    try {
        protocols[node]->submit(command, sessions[command.client], runtime);
    } catch (...) {
        history.pop_back(); // Rejected submission is not a pending operation.
        throw;
    }
}

void Simulator::retry(int node, const std::string& id) {
    require(nodes.at(node).up, "Node is stopped.");
    require(protocols[node]->uses_ordered_log(),
            "Retries with shared IDs are currently supported only in consensus mode.");
    auto request = std::find_if(history.begin(), history.end(),
                                [&](const auto& entry) { return entry.command.id == id; });
    require(request != history.end(), "Unknown request ID.");
    Command command = request->command;
    Runtime runtime(*this, node);
    protocols[node]->submit(command, sessions[command.client], runtime);
    say("Retrying " + id + " via " + node_name(node) +
        "; response still goes to original client endpoint.");
}
} // namespace lab
