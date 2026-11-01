#pragma once
#include "model.hpp"
#include <random>

namespace lab {
class Simulator;

// A node-local doorway into engine services. No access to other replicas' state.
// These are ordinary method calls; send/schedule enqueue work for later delivery.
class Runtime {
  public:
    int node_id() const;
    int node_count() const;
    Node& application();
    std::mt19937_64& random();
    void send(Message message);
    void schedule(const std::string& tag, int milliseconds, int generation = 0);
    void learn(int slot, const Command& command); // Ordered-log application path.
    void complete_locally(const Command& command, const std::string& result);
    void say(const std::string& explanation);

  private:
    friend class Simulator;
    Runtime(Simulator& engine, int node) : engine(engine), node(node) {}
    Simulator& engine;
    int node;
};
} // namespace lab
