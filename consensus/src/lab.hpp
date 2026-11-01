#pragma once
#include "quepaxa.hpp"
#include <iosfwd>
#include <set>
#include <vector>

namespace lab {

// Async replication uses explicit dependencies instead of compact version
// vectors. The Lamport clock orders conflicts; it does not measure freshness.
struct Event {
    std::string id, key, value;
    int clock = 0, origin = 0;
    std::set<std::string> dependencies;
    auto operator<=>(const Event&) const = default;
};
struct Node {
    bool up = true;
    int applied = 0, clock = 0;
    std::map<int, Recorder> recorders;
    std::map<int, Proposer> proposers;
    std::map<int, Command> decisions;
    std::map<std::string, std::string> config, results;
    std::map<std::string, Event> events, buffered;
};
struct Message {
    int id = 0, at = 0, from = 0, to = 0, slot = 0, step = 0;
    std::string kind;
    Proposal proposal;
    Reply reply;
    Command command;
    Event event;
};
struct Request {
    Command command;
    int node = 0, start = 0, end = 0, invoke_order = 0, return_order = 0;
    bool complete = false;
    std::string result;
};
struct Session {
    int through = 0;
    std::set<std::string> seen;
};

class Simulator {
public:
    Simulator(int count, std::uint64_t seed, std::string mode, std::ostream& out);
    std::vector<Node> nodes;
    std::vector<Message> messages;
    std::vector<Request> history;
    std::map<std::string, Session> sessions;
    std::set<std::pair<int,int>> blocked;
    std::string mode, last;
    int now = 0, delay = 10, leader = 0;
    int sent = 0, delivered = 0, dropped = 0;

    void submit(int node, Command command);
    void read(int node, const std::string& key, const std::string& consistency,
              const std::string& client);
    bool step(int message_id = 0);
    void run(int limit = 10000);
    void sync();
    void status();
    void inspect(int node);
    void show_messages();
    void show_history();
    void check_agreement() const;
    bool check_linearizable();
    void isolate(int node);
    void stop(int node);
    void retry(int node, const std::string& id);
    void duplicate(int id);
    void drop(int id);
    void tick(int milliseconds);
    void say(const std::string& text);

private:
    std::mt19937_64 random;
    std::ostream& out;
    int sequence = 0, operation_order = 0, request_sequence = 0;
    void send(Message message);
    void broadcast(int node, int slot);
    void apply(int node);
    void accept_event(int node, Event event);
    void write_event(int node, const Command& command);
    void record_read(int node, const Command& command, const std::string& result);
};

// Shared by state-machine application and the history checker.
std::string execute(std::map<std::string,std::string>& config, const Command& command);
} // namespace lab
