#include "simulator.hpp"
#include "support.hpp"
#include <iostream>

namespace lab {
// Observer-only output. These functions cannot advance a protocol.
void Simulator::status() {
    out << "mode=" << mode << " t=" << now << "ms";
    auto summary = protocols.front()->view().status;
    if (!summary.empty())
        out << ' ' << summary;
    out << '\n';
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
        const auto& n = nodes[i];
        auto view = protocols[i]->view();
        out << node_name(i) << " up=" << n.up << " applied=" << n.applied
            << " events=" << view.visible_events.size() << " buffered=" << view.buffered << " {";
        for (const auto& [key, value] : n.config)
            out << key << '=' << value << ' ';
        out << "}\n";
    }
}

void Simulator::inspect(int node) {
    const auto& n = nodes.at(node);
    out << node_name(node) << ": applied=" << n.applied << '\n';
    protocols.at(node)->inspect(out);
    for (const auto& [slot, c] : n.decisions)
        out << "  learned slot " << slot << " = " << c.id << ' ' << c.kind << ' ' << c.key << '\n';
}

void Simulator::show_messages() {
    for (const auto& m : messages)
        out << '#' << m.id << " at=" << m.at << ' ' << m.kind << ' ' << node_name(m.from) << " -> "
            << node_name(m.to) << " slot=" << m.slot << " step=" << m.step
            << " request=" << m.request_id << '\n';
    for (const auto& timer : timers)
        out << '#' << timer.id << " at=" << timer.at << " timer " << node_name(timer.node)
            << " tag=" << timer.tag << " generation=" << timer.generation << '\n';
}

void Simulator::show_history() {
    for (const auto& r : history) {
        out << r.command.id << " client=" << r.command.client << ' ' << r.command.kind << ' '
            << r.command.key << " via=" << node_name(r.node) << " invoked=" << r.start << "ms ";
        if (r.complete)
            out << "returned=" << r.end << "ms result=" << r.result;
        else
            out << "PENDING/UNKNOWN (not a confirmed failure)";
        out << '\n';
    }
}
} // namespace lab
