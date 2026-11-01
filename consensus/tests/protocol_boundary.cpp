#include "simulator.hpp"
#include "runtime.hpp"
#include <sstream>
#include <stdexcept>

namespace {
void expect(bool condition, const char* reason) {
    if (!condition)
        throw std::runtime_error(reason);
}
struct ProbePayload {
    lab::Command command;
};

// TEST ONLY: not a consensus algorithm. Its custom message and timer prove the
// engine can run an implementation without knowing ANY of its protocol types.
class TimerProbe final : public lab::Protocol {
  public:
    bool uses_ordered_log() const override {
        return true;
    }
    void submit(const lab::Command& command, const lab::Session&, lab::Runtime& runtime) override {
        pending = command;
        runtime.schedule("start", 3, 0); // Obsolete generation.
        runtime.schedule("start", 7, 1);
    }
    void on_timer(const lab::Timer& timer, lab::Runtime& runtime) override {
        if (timer.generation != 1)
            return;
        lab::Message message;
        message.kind = "probe-message";
        message.from = 999; // Runtime must stamp our real node ID.
        message.to = (runtime.node_id() + 1) % runtime.node_count();
        message.payload = ProbePayload{pending};
        runtime.send(message);
    }
    void receive(const lab::Message& message, lab::Runtime& runtime) override {
        expect(message.kind == "probe-message", "engine changed custom message kind");
        auto command = std::any_cast<const ProbePayload&>(message.payload).command;
        runtime.learn(1, command);
    }
    void sync(lab::Runtime&) override {}
    lab::ProtocolView view() const override {
        return {};
    }
    void inspect(std::ostream& out) const override {
        out << "  test probe\n";
    }

  private:
    lab::Command pending;
};
lab::ProtocolFactory probe() {
    return [](int, int) { return std::make_unique<TimerProbe>(); };
}
} // namespace

void test_protocol_boundary() {
    std::ostringstream output;
    lab::Simulator simulator(3, 42, "test-only-probe", output, probe());
    simulator.submit(0, {"", "alice", "set", "x", "1", ""});
    expect(simulator.messages.empty() && simulator.timers.size() == 2, "submit bypassed timers");
    simulator.step();
    expect(simulator.now == 3 && simulator.messages.empty(), "obsolete timer started work");
    simulator.step();
    expect(simulator.now == 7 && simulator.messages.size() == 1, "active timer didn't send");
    expect(simulator.messages.front().from == 0, "protocol impersonated another sender");
    simulator.run();
    expect(simulator.nodes[1].config.at("x") == "1",
           "generic application did not execute decision");
    expect(simulator.nodes[0].config.empty(), "protocol accessed remote replica state");
    expect(simulator.history.front().complete, "generic client response didn't complete");
    expect(simulator.history.front().result == "ok", "generic response has wrong result");
    expect(simulator.now == 27, "timer/message order changed");
    expect(simulator.check_linearizable(), "probe history should be linearizable");
    simulator.check_agreement();
    simulator.inspect(1);
    expect(output.str().find("test probe") != std::string::npos, "inspection didn't delegate");

    lab::Simulator partitioned(3, 42, "probe", output, probe());
    partitioned.blocked.insert({0, 1});
    partitioned.submit(0, {"", "alice", "set", "x", "1", ""});
    partitioned.run();
    expect(partitioned.now == 17 && partitioned.sent == 1, "link fault blocked a local timer");
    expect(partitioned.dropped == 1 && !partitioned.history.front().complete,
           "custom protocol bypassed network fault controls");

    lab::Simulator crashed(3, 42, "probe", output, probe());
    crashed.submit(0, {"", "alice", "set", "x", "1", ""});
    crashed.stop(0);
    crashed.run();
    expect(crashed.sent == 0 && crashed.messages.empty(), "stopped node fired protocol work");
}
