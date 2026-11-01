#include "quepaxa_driver.hpp"
#include "runtime.hpp"
#include "support.hpp"

namespace lab {
// Read in journey order: submit -> send records -> on_record -> on_reply -> on_learn.
// After on_learn, shared application.cpp handles application and client responses.
void QuePaxa::submit(const Command& command, const Session&, Runtime& runtime) {
    int slot = runtime.application().applied + 1;
    require(!proposers.contains(slot),
            "This node already proposes this slot. Use another node, or run first.");
    proposers.emplace(slot, Proposer(node, command));
    send_record_requests(slot, runtime);
    runtime.say(command.id + " pending: " + node_name(node) + " proposes " + command.kind +
                " in slot " + std::to_string(slot) + ". Submission is not success.");
}

void QuePaxa::send_record_requests(int slot, Runtime& runtime) {
    auto& proposer = proposers.at(slot);
    for (int recorder = 0; recorder < count; ++recorder) {
        Message message;
        message.kind = "record";
        message.to = recorder;
        message.slot = slot;
        message.step = proposer.step;
        auto proposal = proposer.outgoing(node, preferred, runtime.random());
        message.request_id = proposal.command.id;
        message.payload = RecordRequest{proposal};
        runtime.send(message);
    }
}

void QuePaxa::receive(const Message& message, Runtime& runtime) {
    if (message.kind == "record")
        on_record(message, runtime);
    else if (message.kind == "reply")
        on_reply(message, runtime);
    else if (message.kind == "learn")
        on_learn(message, runtime);
    else
        require(false, "Unexpected QuePaxa message: " + message.kind);
}

// Store evidence, not application data. Then send a reply to the proposer.
void QuePaxa::on_record(const Message& message, Runtime& runtime) {
    const auto& request = std::any_cast<const RecordRequest&>(message.payload);
    Reply evidence = recorders[message.slot].record(message.step, request.proposal);
    Message response;
    response.kind = "reply";
    response.to = message.from;
    response.slot = message.slot;
    response.step = message.step;
    response.payload = RecordReply{evidence};
    runtime.send(response);
}

// The phase switch in Proposer::receive determines what quorum evidence means.
void QuePaxa::on_reply(const Message& message, Runtime& runtime) {
    auto found = proposers.find(message.slot);
    if (found == proposers.end())
        return;
    auto& proposer = found->second;
    if (proposer.done || proposer.step != message.step)
        return;
    const auto& reply = std::any_cast<const RecordReply&>(message.payload);
    Progress progress = proposer.receive(message.from, reply.evidence, count / 2 + 1);
    if (progress.decision) {
        runtime.say(
            node_name(node) + " DECIDED slot " + std::to_string(message.slot) + " = " +
            progress.decision->id +
            ". Quorum evidence establishes the outcome; learners and client still need messages.");
        for (int replica = 0; replica < count; ++replica) {
            Message learn;
            learn.kind = "learn";
            learn.to = replica;
            learn.slot = message.slot;
            learn.request_id = progress.decision->id;
            learn.payload = LearnDecision{*progress.decision};
            runtime.send(learn);
        }
    } else if (progress.advance) {
        int logical = proposer.step;
        runtime.say(node_name(node) + " advances to logical step " + std::to_string(logical) +
                    " (phase " + std::to_string(logical % 4) + "). See Proposer::receive.");
        send_record_requests(message.slot, runtime);
    }
}

void QuePaxa::on_learn(const Message& message, Runtime& runtime) {
    const auto& decision = std::any_cast<const LearnDecision&>(message.payload);
    runtime.learn(message.slot, decision.command);
}
void QuePaxa::on_timer(const Timer&, Runtime&) {
    // Automatic hedging is planned. This implementation currently schedules no timers.
}
void QuePaxa::sync(Runtime& runtime) {
    for (int to = 0; to < count; ++to) {
        if (to == node)
            continue;
        for (const auto& [slot, command] : runtime.application().decisions) {
            Message message;
            message.kind = "learn";
            message.to = to;
            message.slot = slot;
            message.request_id = command.id;
            message.payload = LearnDecision{command};
            runtime.send(message);
        }
    }
}
ProtocolView QuePaxa::view() const {
    return {
        "preferred=" + node_name(preferred) + " quorum=" + std::to_string(count / 2 + 1), {}, 0};
}
void QuePaxa::inspect(std::ostream& out) const {
    for (const auto& [slot, r] : recorders)
        out << "  slot " << slot << " recorder step=" << r.step << " first=" << r.first.command.id
            << " priority=" << r.first.priority << " aggregate=" << r.aggregate.command.id << '\n';
    for (const auto& [slot, p] : proposers)
        out << "  slot " << slot << " proposer step=" << p.step << " replies=" << p.replies.size()
            << " candidate=" << p.candidate.command.id << " done=" << p.done << '\n';
}
} // namespace lab
