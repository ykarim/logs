# A few words to know

Read this alongside the experiments; memorizing it first is unnecessary.

## Node and replica

A node is one simulated machine. A replica stores its own copy of the state.
Different copies can lag. No node reads another node's memory.

## Message

A message carries information from one node to another. Sending is different
from receiving. `messages` shows what has been sent but not delivered yet.
`step` delivers one envelope. Nodes communicate with themselves through the
queue too, so the timing model is simple and uniform.

## Quorum

A majority: 2 of 3, or 3 of 5. Any two majorities intersect. That overlap helps a
protocol preserve an earlier choice, but a majority count alone is not a safe
consensus algorithm. The state and rules attached to those replies matter.

## Proposal, decision, application, response

These are four different events:

```text
client submits -> proposer gathers evidence -> decision
                  -> replica learns -> applies in order -> client gets response
```

A proposal can lose to another proposal. A node can miss a learning message.
A response can be lost even after an update has been applied.

## Safety and progress

Safety means something forbidden never happens, such as two different decisions
for the same log slot. Progress means useful work eventually finishes under the
protocol's assumptions. Waiting can preserve safety. A safe system can still be
unavailable to a particular client.

## Linearizability

Each completed operation must fit a legal sequential story. Its apparent instant
falls between invocation and response. If A finishes before B starts, A must
appear first. Overlapping operations can take either order.

A successful `set x true`, followed by `get x`, must return true if nothing
changes x in between. The read can contact a different node. The node must
coordinate, catch up, or decline to answer if it cannot guarantee this.

The lab's logged read takes a place in the log and evaluates the map after all
preceding slots. A local read skips this ordering step and may be stale.

## Session guarantees

A session is a named client, such as `alice`. Its token says what progress it has
already observed. In consensus mode the token is an applied-log watermark. In
async modes it is a set of visible event IDs.

A session read cannot proceed on a replica missing that context. This supports
read-your-writes and monotonic observations in this lab. It does not promise the
latest writes made by other clients. The CLI prints WAIT; retry after catch-up.
A local read does not update the session token.

## Sequential consistency

There must be one legal order preserving each client's own operation order.
Unlike linearizability, it need not preserve real-time order between clients.
It is on our roadmap, not a CLI mode yet. Arbitrary stale reads do not implement it.

## Eventual and causal replication

Async nodes accept writes locally without deciding one global write order.
Exchange all updates and they converge using a deterministic conflict rule.
During a partition, replicas may disagree.

Causal buffering adds: make a cause visible before its effect. In our example,
`enabled=true` depends on the earlier `destination=east`. It waits if its
prerequisite is missing. Independent writes still need conflict handling.

When moving clients in causal mode, use session reads and the same client name.
Changing the client name starts a different session, with no transferred context.

## Timeouts and retries

A client deadline says how long the client waited. It does not undo a write.
Pending operations may already have happened. The checker allows that possibility.

`retry n2 r1` reuses request r1. Application nodes remember its original result.
For a compare-and-set, a retry returns the original outcome instead of executing
it again. Repeating the `cas` command creates a new request and can return a
different result. Deduplication state is currently in memory only.

## Consistency and isolation

Replica consistency asks what observations a distributed object permits.
Transaction isolation asks how multi-operation transactions interact. They are
related but distinct. Our application currently exposes individual atomic
operations over a map; it does not implement multi-key transactions.
