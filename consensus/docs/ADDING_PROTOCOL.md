# The shared engine and protocol boundary

Choose an implementation when creating a fresh cluster:

```text
new quepaxa 3 42
new eventual 3 42
new causal 3 42
```

Each starts a new experiment. Switching algorithms while retaining a running
cluster's state is not supported. Raft and Multi-Paxos are planned, not yet modes.

## The separation

```text
CLI / client request
       |
Shared engine: client.cpp, network.cpp
       |
       | submit / receive / on_timer / sync
       v
Protocol instance belonging to ONE replica
  QuePaxa: private proposers, recorders, phases
  Async:   private events, dependencies, merge rules
       |
       | Runtime services
       +---- send ------------> queued envelope; normal delay/fault controls
       +---- schedule --------> queued local timer
       +---- learn -----------> shared ordered-log application and response
       +---- complete_locally > record async local acceptance
       +---- application -----> this replica's config/applied prefix only
```

Protocol choices and local merges happen in the implementation. Network faults,
client history and sequential map operations remain shared. Inspection delegates
private-state output to the selected protocol. `view()` provides visible event
IDs for session tokens and summary data, not evidence for quorum decisions.

## Implementing a new algorithm

1. Add a class under `src/protocols/` implementing `Protocol` from
   [`protocol.hpp`](../src/protocol.hpp). There is one instance per replica.
2. Put all algorithm state and message structs there: Raft terms/votes/entries,
   for example, should not appear in the engine's `Node` or `Message` definition.
3. Implement these entry points:

   | Entry point | Responsibility |
   |---|---|
   | `submit` | Accept a client command and start/queue the algorithm's work |
   | `receive` | Decode your own message payload and run the appropriate handler |
   | `on_timer` | Apply your timeout/activation rule; reject obsolete generations |
   | `sync` | Send whatever repair traffic your algorithm requires |
   | `uses_ordered_log` | Declare ordered-log versus local merge behavior |
   | `view`, `inspect` | Explain local progress and private state |

4. Use [`Runtime`](../src/runtime.hpp) services to send messages, schedule timers
   and report local results. It exposes no peer's memory. Sender identity is
   stamped by the engine. The message kind `response` is reserved for shared
   client completion and cannot be sent through the protocol runtime.
5. For an ordered-log protocol, call `runtime.learn(slot, command)` only when
   your algorithm has established a decision or validly learned one. The engine
   checks local conflicts and applies contiguous slots, but does not establish
   consensus for you. Uncommitted entries belong in your own state.
6. For async replication, merge values into `runtime.application().config` and
   call `complete_locally` for the accepted request. Do not emit fake log decisions.
7. Register the name in [`protocol_factory.cpp`](../src/protocol_factory.cpp),
   add source files to Make/CMake, and update CLI help. No algorithm-specific
   dispatch branches belong in `network.cpp`, `reads.cpp`, or `checks.cpp`.
8. Add protocol-specific safety/progress tests and comparable experiment scripts.
   Generic history/log checks remain useful but cannot replace algorithm tests.

## How opaque messages work

`Message::payload` is a standard-library `std::any`, which stores a copy of an
implementation-defined struct. At the receiving handler, that implementation
retrieves the exact type:

```cpp
message.payload = RecordRequest{proposal};
// Later, inside QuePaxa::on_record:
const auto& request = std::any_cast<const RecordRequest&>(message.payload);
```

The engine copies/routes the envelope and never performs that cast. Incorrect
payload types throw rather than silently reinterpret bytes. These are in-memory
messages; a real-process transport would additionally need serialization.
`slot`, `step` and `request_id` on the envelope are optional trace labels; the
network does not interpret them as consensus evidence.

## Timer behavior

Messages and timers share an increasing event ID. Automatic stepping orders both
by scheduled time then ID. You can select a specific event ID manually. Local
timers are unaffected by link cuts; crash-stop prevents them from running.
`tick` advances time without dispatching events; `step`/`run` dispatch them later.

A timer carries a tag and generation. The engine delivers it; the algorithm owns
its meaning and must ignore stale generations. There is no automatic election,
hedging, retransmission or cancellation rule built into the engine.

[`tests/protocol_boundary.cpp`](../tests/protocol_boundary.cpp) injects a tiny
custom implementation through the constructor's factory parameter. It exercises
custom payloads, timers, application, responses and faults without any special
engine branches. It is a test probe, not a consensus algorithm.
