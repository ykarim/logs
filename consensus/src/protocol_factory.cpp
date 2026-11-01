#include "protocol.hpp"
#include "protocols/quepaxa_driver.hpp"
#include "protocols/async_replication.hpp"
#include "support.hpp"

namespace lab {
// Add a protocol here after implementing its node-local Protocol class.
// No network, application, read, or checker changes are required to register it.
ProtocolFactory select_protocol(const std::string& name) {
    if (name == "quepaxa") {
        return [](int node, int count) { return std::make_unique<QuePaxa>(node, count); };
    }
    if (name == "eventual" || name == "causal") {
        return [causal = name == "causal"](int node, int count) {
            return std::make_unique<AsyncReplication>(node, count, causal);
        };
    }
    throw std::runtime_error("Unknown mode.");
}
} // namespace lab
