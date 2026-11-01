# Reading the code

The project uses C++20 and standard containers. There are no background threads,
external consensus libraries, or generated protocol bindings.

## Start with one write

Read [the one-write walkthrough](WALKTHROUGH.md). Keep these three files open:

1. [`client.cpp`](../src/client.cpp): a client submits a request.
2. [`protocols/quepaxa_driver.cpp`](../src/protocols/quepaxa_driver.cpp):
   QuePaxa sends records, processes replies and announces decisions.
3. [`application.cpp`](../src/application.cpp): applies learned entries and answers clients.

```text
Shared client       QuePaxa implementation             Shared application
submit -----------> submit -> send_record_requests
                                  |
                              on_record
                                  |
                              on_reply --> Proposer::receive
                                  |             decision or another phase
                              on_learn -------------> learn
                                                        |
                                                apply_decided_prefix
                                                        |
                                                   on_response
```

The four QuePaxa phases are visible in
[`protocols/quepaxa.hpp`](../src/protocols/quepaxa.hpp). No callbacks hide the
phase switch. `Runtime::send` queues an envelope; `network.cpp` delivers it later.
`Runtime::learn` is a local call into application code, not another network hop.

## The protocol boundary

[`protocol.hpp`](../src/protocol.hpp) defines one small interface implemented
once per replica. It accepts client submissions, messages, timers and explicit
repair requests. [`runtime.hpp`](../src/runtime.hpp) gives that implementation
access to local application state, sending, timers, reporting a learned decision,
local async completion and trace output. It exposes no other replica's state.
The engine routes opaque message payloads. See [adding a protocol](ADDING_PROTOCOL.md).

## Where each idea lives

| File | Question it answers |
|---|---|
| [`config.cpp`](../src/config.cpp) | What does get/set/CAS mean on an ordinary map? |
| [`client.cpp`](../src/client.cpp) | How are client identity, pending history and retry preserved? |
| [`protocols/quepaxa_driver.cpp`](../src/protocols/quepaxa_driver.cpp) | What does QuePaxa do when a request or record/reply/learn arrives? |
| [`protocols/quepaxa.hpp`](../src/protocols/quepaxa.hpp) | Which reply evidence permits phase advancement or decision? |
| [`application.cpp`](../src/application.cpp) | Where do learned decisions become config updates and client responses? |
| [`protocols/async_replication.cpp`](../src/protocols/async_replication.cpp) | How are eventual/causal writes merged without an ordered log? |
| [`model.hpp`](../src/model.hpp) | What is an envelope, local application state, timer, history or session? |
| [`reads.cpp`](../src/reads.cpp) | How do local, session and logged reads differ? |
| [`network.cpp`](../src/network.cpp) | How are time, delivery, timers and faults simulated? |
| [`inspection.cpp`](../src/inspection.cpp), [`checks.cpp`](../src/checks.cpp) | How do we observe behavior and check correctness? |
| [`protocol_factory.cpp`](../src/protocol_factory.cpp) | Which implementation does a selected name construct? |
| [`main.cpp`](../src/main.cpp) | How are CLI commands parsed? |

`simulator.hpp` declares the shared coordinator. Concrete protocol state and wire
payloads live under `protocols/`; shared headers do not include those types.
There is one ordinary virtual interface, with no dynamic loading or external
framework. `support.hpp` only formats node names and checks preconditions.

`execute` is the map's sequential specification. The application and history
checker both use it; independent outcome tests are therefore important too.

## QuePaxa source mapping

Source: [QuePaxa, SOSP 2023](https://bford.info/pub/os/quepaxa/quepaxa.pdf),
Algorithms 3 and 4, sections 4.2.3–4.2.5.

`Recorder` is the interval summary register. `Proposer` implements the concrete
per-slot algorithm with fixed first-round preferred proposer n1 and randomized
fallback. Full commands are carried in messages rather than hashes. The
comparison uses proposal priority, proposer ID and command as a total order.

These are implemented independently for each log slot. A node proposes into
its own next unapplied slot. A command can lose that contest; this version leaves
it pending until you retry. A node cannot submit a second preferred value for the
same proposer/slot, including after deciding but before learning that decision.

Once a decision is learned, the application waits for all preceding slots before
applying it. Learners send responses to the client endpoint associated with the
original submission. That endpoint is represented by a node for convenience;
the client history and session token belong to the simulator, not the consensus
algorithm. No quorum transition consults another node's memory.

## Things intentionally made explicit

- Transport envelopes have their own IDs, separate from request IDs.
- Distinct recorder senders determine a quorum, not envelope count.
- Logical step numbers and elapsed time are different fields.
- All messages, including self messages, use the configured hop delay.
- `heal` restores routes; `sync` sends known decisions/events again.
- Crash-stop discards delivery to/from the stopped node; restart is not modeled.
- `run` has a delivered-event bound (messages plus timers). An empty queue does not prove client completion.
- `retry` is one original client operation, not a second invocation in history.
- Message/timer IDs share an event sequence; `step` selects the earliest by
  scheduled time then ID. A protocol's timer handler checks its own generation.
- The configured preferred proposer stays fixed. Automatic election/adaptation,
  automatic hedging, periodic repair and durable restart are not implemented.

## Checker boundaries

`check` checks observed per-slot agreement and deterministic replay of applied
prefixes. It deliberately does not certify read freshness.

`check linearizability` searches histories with at most 12 operations, starting
from the empty map. A separate monotonically increasing event order preserves
invocation/response precedence even when virtual timestamps tie. Pending
operations may be omitted or included. Completed operations must match the
sequential specification. The search stops with INCONCLUSIVE above 200,000
states. A completed search can pass or exhibit a violation; it does not prove
all possible executions correct. Local reads are included, so mixing them with
logged writes can intentionally fail this check.

Async modes implement no total-order log. All event versions are retained. Causal
buffering uses dependency sets for transparency, not space efficiency. The
Lamport-based winner provides deterministic convergence and does not make
arbitrary noncommutative operations safe. CAS is therefore rejected in async
modes. Dependencies and sessions have focused behavioral tests; there is no
complete causal-history checker yet.

## Tests

`make test` runs targeted application and protocol tests, 150 seeded concurrent
schedules, and every documented script with expected-outcome assertions.
`tests/protocol_boundary.cpp` injects a deliberately non-consensus test probe
with its own payload and timer. It checks dispatch, stale timer handling,
network faults, crash-stop, shared application and client completion without
registering any probe-specific engine branches.

To debug a transition, use `-O0 -g` (the Make default) and a debugger breakpoint
in `Simulator::step` or `Proposer::receive`. Tests use ordinary exceptions and
assertions implemented in a tiny helper, so no test framework hides the behavior.
