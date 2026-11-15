#pragma once
#include "protocol.hpp"
#include <iosfwd>
#include <random>

namespace lab {
// Simulator owns the entire deterministic experiment.
//
// It is the "whole world" in the lab: all replicas' shared application state,
// concrete Protocol instances, queued messages and timers, network partitions,
// virtual time, client sessions, and observable request history. CLI commands
// such as step(), isolate(), and status() operate on this world.
//
// Simulator is protocol-agnostic. It delivers an opaque message or timer to the
// selected Protocol instance but does not interpret QuePaxa phases, Raft terms,
// or eventual-replication events. A fresh cluster selects one protocol factory
// in the constructor; each replica receives its own private Protocol object.
//
// Protocol code is not given Simulator directly. For each callback Simulator
// constructs a Runtime scoped to the receiving node:
//
//   Simulator owns world -> dispatches Protocol callback -> passes Runtime(n)
//
// This split keeps simulation concerns in Simulator and prevents a protocol
// from consulting peer memory. Runtime exposes only the engine services a local
// replica legitimately needs. It is a facade/capability, not a second engine.
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
    // Runtime forwards its restricted operations to these private engine
    // methods. Friendship avoids making low-level send/learn/schedule methods
    // public to the CLI while keeping Runtime itself small.
    friend class Runtime;

    // One concrete, private protocol state machine per replica. Simulator owns
    // their lifetimes but does not know their implementation-specific state.
    std::vector<std::unique_ptr<Protocol>> protocols;

    // Shared deterministic services and observer state.
    std::mt19937_64 random;
    std::ostream& out;
    int sequence = 0, operation_order = 0, request_sequence = 0;

    // Engine primitives called directly by Simulator or through Runtime.
    void send(Message message);
    void schedule(int node, const std::string& tag, int milliseconds, int generation);
    void learn(int node, int slot, const Command& command);
    void apply_decided_prefix(int node);
    void on_response(const Message& message);
    void record_local_result(int node, const Command& command, const std::string& result);
};
} // namespace lab
