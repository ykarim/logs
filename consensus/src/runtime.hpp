#pragma once
#include "model.hpp"
#include <random>

namespace lab {
class Simulator;

// Runtime is the limited view of the simulated world given to ONE protocol
// replica while that replica handles a request, message, or timer.
//
// A protocol needs a few services that it cannot provide for itself: sending a
// network message, scheduling a timer, using deterministic randomness, and
// reporting an operation that it has decided. Giving the protocol a Simulator&
// would also expose every replica, every queued message, and the simulated
// clock. That would make it easy to accidentally "cheat" by reading another
// node's memory instead of learning through messages.
//
// Runtime is therefore a small capability object between a Protocol and the
// Simulator:
//
//   Protocol for n2 -> Runtime scoped to n2 -> shared Simulator services
//
// It remembers which node is currently running and automatically stamps that
// node as the sender. application() returns only that node's application state.
// It deliberately has no method for reading another node or the message queue.
//
// Runtime does not own a node, thread, network, or clock. Simulator creates a
// short-lived Runtime on the stack when dispatching work and keeps ownership of
// all underlying state. Calls such as send() and schedule() enqueue future
// events; they do not synchronously run another replica.
class Runtime {
  public:
    // Identity and cluster size visible to this protocol replica.
    int node_id() const;
    int node_count() const;

    // Mutable application state for this node only. Protocol-private state
    // remains in the concrete Protocol object itself.
    Node& application();

    // The engine-owned generator makes seeded experiments reproducible.
    std::mt19937_64& random();

    // Queue work in the shared deterministic engine. send() overwrites the
    // sender with node_id(), so a protocol cannot impersonate another replica.
    void send(Message message);
    void schedule(const std::string& tag, int milliseconds, int generation = 0);

    // Two ways a protocol may report useful work to the shared application:
    // ordered-log protocols learn a slot; async replication accepts locally.
    void learn(int slot, const Command& command); // Ordered-log application path.
    void complete_locally(const Command& command, const std::string& result);

    // Add a human-readable explanation to the experiment trace.
    void say(const std::string& explanation);

  private:
    // Only Simulator may construct a Runtime, ensuring the node identity and
    // backing engine always match the event being dispatched.
    friend class Simulator;
    Runtime(Simulator& engine, int node) : engine(engine), node(node) {}

    // Non-owning references valid only while the Simulator that created this
    // Runtime is alive. Protocols should not store Runtime beyond a callback.
    Simulator& engine;
    int node;
};
} // namespace lab
