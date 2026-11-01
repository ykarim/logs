#include "simulator.hpp"
#include "protocols/quepaxa.hpp"
#include "runtime.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>

void test_protocol_boundary();

using namespace lab;
namespace {
void expect(bool ok, const std::string& reason) {
    if (!ok)
        throw std::runtime_error(reason);
}
Command set(std::string value, std::string client = "alice") {
    return {"", client, "set", "x", value, ""};
}
void drain(Simulator& s) {
    s.run();
    expect(s.messages.empty(), "run exhausted its message bound");
}
void fast_path() {
    std::ostringstream out;
    Simulator s(3, 42, "quepaxa", out);
    s.submit(0, set("true"));
    drain(s);
    expect(s.history[0].complete, "write didn't complete");
    expect(s.now == 40, "fast path should use record/reply/learn/response, four 10ms hops");
    for (const auto& n : s.nodes)
        expect(n.config.at("x") == "true", "replica missed application");
    s.read(2, "x", "linearizable", "bob");
    drain(s);
    expect(s.check_linearizable(), "logged read should be linearizable");
    s.check_agreement();
}
void quorum_and_stale_reads() {
    std::ostringstream out;
    Simulator s(3, 42, "quepaxa", out);
    s.isolate(2);
    s.submit(0, set("true"));
    drain(s);
    expect(s.history[0].complete, "majority should complete");
    s.read(2, "x", "local", "bob");
    expect(!s.check_linearizable(), "stale read was not detected");
    s.check_agreement();
    bool waited = false;
    try {
        s.read(2, "x", "session", "alice");
    } catch (const std::runtime_error&) {
        waited = true;
    }
    expect(waited, "writer session should wait on stale replica");
    s.read(2, "x", "linearizable", "bob");
    drain(s);
    expect(!s.history.back().complete, "minority must not complete logged read");
    s.blocked.clear();
    s.sync();
    drain(s);
    expect(s.nodes[2].config.at("x") == "true", "catchup failed");
}
void stopped_leader() {
    std::ostringstream out;
    Simulator s(3, 7, "quepaxa", out);
    s.stop(0);
    s.submit(1, set("yes"));
    drain(s);
    expect(s.history[0].complete, "non-leader should decide without leader");
    expect(s.now >= 80, "fallback needs more communication than fast path");
    s.check_agreement();
}
void deadlines_and_deduplication() {
    std::ostringstream out;
    Simulator s(3, 42, "quepaxa", out);
    s.submit(0, set("old"));
    drain(s);
    Command c{"", "alice", "cas", "x", "new", "old"};
    s.submit(0, c);
    // Deliver through application, deliberately keep all client responses queued.
    while (true) {
        auto it = std::find_if(s.messages.begin(), s.messages.end(),
                               [](const auto& m) { return m.kind != "response"; });
        if (it == s.messages.end())
            break;
        int id = it->id;
        s.step(id);
    }
    expect(!s.history[1].complete, "response should still be pending");
    expect(s.nodes[1].config.at("x") == "new", "pending request should already have executed");
    s.tick(1000);
    s.retry(1, "r2");
    drain(s);
    expect(s.history[1].result == "swapped", "retry must return original CAS result");
    expect(s.nodes[1].config.at("x") == "new", "retry changed state");
    expect(s.check_linearizable(), "retry broke logical history");
    s.check_agreement();
}
void duplicates_are_not_quorums() {
    Proposer p(0, {"r1", "alice", "set", "x", "a", ""});
    Reply reply{4, p.candidate, {}};
    expect(!p.receive(0, reply, 2).decision, "one vote isn't a quorum");
    expect(!p.receive(0, reply, 2).decision, "duplicate vote counted twice");
    expect(p.receive(1, reply, 2).decision.has_value(), "distinct votes should decide");
    Recorder r;
    auto a = p.candidate;
    r.record(4, a);
    r.record(5, a);
    Proposal b = a;
    b.priority = 0;
    r.record(3, b);
    expect(r.step == 5 && r.first == a, "stale request changed recorder");
}
void asynchronous_modes() {
    std::ostringstream out;
    for (auto mode : {"eventual", "causal"}) {
        Simulator s(3, 42, mode, out);
        s.isolate(0);
        s.submit(0, set("west", "alice"));
        s.submit(1, set("east", "bob"));
        drain(s);
        expect(s.nodes[0].config.at("x") != s.nodes[1].config.at("x"),
               "partition should expose conflict");
        s.blocked.clear();
        s.sync();
        drain(s);
        for (const auto& n : s.nodes)
            expect(n.config == s.nodes[0].config, "event replicas failed to converge");
    }
    for (auto mode : {"eventual", "causal"}) {
        Simulator s(3, 42, mode, out);
        s.submit(0, {"", "alice", "set", "destination", "east", ""}); // messages 1,2
        s.submit(0, {"", "alice", "set", "enabled", "true", ""});     // messages 3,4
        s.step(3); // Deliver effect to n2 before its cause.
        expect(s.nodes[1].config.contains("enabled") == (std::string(mode) == "eventual"),
               "causal dependency buffering incorrect");
        s.step(1);
        expect(s.nodes[1].config.at("destination") == "east" &&
                   s.nodes[1].config.at("enabled") == "true",
               "cause did not release effect");
    }
}
void randomized_agreement() {
    std::ostringstream out;
    // Concurrent contenders; randomized transport order, duplicate RPCs and a
    // temporarily isolated recorder. Check every learned decision, every run.
    for (int seed = 1; seed <= 150; ++seed) {
        out.str("");
        out.clear();
        Simulator s(5, seed, "quepaxa", out);
        std::mt19937 schedule(seed);
        s.isolate(4);
        for (int n = 0; n < 4; ++n)
            s.submit(n, set(std::to_string(n), "c" + std::to_string(n)));
        int count = 0;
        while (!s.messages.empty() && count++ < 5000) {
            int id = s.messages[schedule() % s.messages.size()].id;
            if (count % 13 == 0)
                s.duplicate(id);
            s.step(id);
            s.check_agreement();
        }
        expect(count < 5000, "random execution did not drain");
        expect(!s.nodes[0].decisions.empty(), "majority made no decision");
        s.blocked.clear();
        s.sync();
        drain(s);
        s.check_agreement();
        expect(s.check_linearizable(), "concurrent completed history not linearizable");
        for (const auto& n : s.nodes)
            expect(n.config == s.nodes[0].config, "healed replicas differ");
    }
}
} // namespace
int main() {
    try {
        test_protocol_boundary();
        fast_path();
        quorum_and_stale_reads();
        stopped_leader();
        deadlines_and_deduplication();
        duplicates_are_not_quorums();
        asynchronous_modes();
        randomized_agreement();
        std::cout << "PASS: protocol boundary/timers, consistency, retry, dependency and 150 "
                     "seeded schedule tests\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
