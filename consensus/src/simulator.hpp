#pragma once
#include "protocol.hpp"
#include <iosfwd>
#include <random>

namespace lab {
// Shared engine: network, clock, application, history and observation.
// Concrete protocols are selected once, at construction, through a factory.
class Simulator {
  public:
    Simulator(int count, std::uint64_t seed, std::string mode, std::ostream& out,
              ProtocolFactory factory = {});
    std::vector<Node> nodes;
    std::vector<Message> messages;
    std::vector<Timer> timers;
    std::vector<Request> history;
    std::map<std::string, Session> sessions;
    std::set<std::pair<int, int>> blocked;
    std::string mode, last;
    int now = 0, delay = 10;
    int sent = 0, delivered = 0, dropped = 0;

    void submit(int node, Command command);
    void read(int node, const std::string& key, const std::string& consistency,
              const std::string& client);
    bool step(int event_id = 0);
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
    friend class Runtime;
    std::vector<std::unique_ptr<Protocol>> protocols;
    std::mt19937_64 random;
    std::ostream& out;
    int sequence = 0, operation_order = 0, request_sequence = 0;
    void send(Message message);
    void schedule(int node, const std::string& tag, int milliseconds, int generation);
    void learn(int node, int slot, const Command& command);
    void apply_decided_prefix(int node);
    void on_response(const Message& message);
    void record_local_result(int node, const Command& command, const std::string& result);
};
} // namespace lab
