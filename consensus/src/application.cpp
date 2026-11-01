#include "simulator.hpp"
#include "support.hpp"
#include <algorithm>

namespace lab {
// A protocol reports a locally learned decision through Runtime::learn.
// This application layer is shared by QuePaxa and future Raft/Multi-Paxos.
void Simulator::learn(int node, int slot, const Command& command) {
    require(slot > 0, "Log slots start at 1.");
    auto& local = nodes.at(node);
    require(!local.decisions.contains(slot) || local.decisions.at(slot) == command,
            "BUG: conflicting decisions!");
    local.decisions[slot] = command;
    apply_decided_prefix(node);
}

// 6. APPLY only a contiguous prefix: slot 2 must wait for a missing slot 1.
void Simulator::apply_decided_prefix(int node) {
    auto& local = nodes[node];
    while (local.decisions.contains(local.applied + 1)) {
        const auto& command = local.decisions.at(++local.applied);
        if (!local.results.contains(command.id)) {
            local.results[command.id] = execute(local.config, command);
        }
        // The ID cache preserves the first result, including for a retried CAS.
        // Client return addresses come from the simulator's request table.
        // No remote replica state is consulted to compute this local result.
        for (const auto& request : history) {
            if (request.complete || request.command.id != command.id)
                continue;
            Message response;
            response.kind = "response";
            response.from = node;
            response.to = request.node;
            response.slot = local.applied;
            response.command = command;
            response.request_id = command.id;
            response.result = local.results.at(command.id);
            send(response);
        }
    }
}

// 7. The client RECEIVES the result. A lost response leaves an unknown outcome.
void Simulator::on_response(const Message& message) {
    for (auto& request : history) {
        if (request.complete || request.command.id != message.command.id ||
            request.node != message.to)
            continue;

        request.complete = true;
        request.result = message.result;
        request.end = now;
        request.return_order = ++operation_order;
        auto& session = sessions[request.command.client];
        session.through = std::max(session.through, message.slot);
        say(request.command.id + " SUCCESS: " + request.result + " (" +
            std::to_string(request.end - request.start) + "ms)");
    }
}

} // namespace lab
