# Reading the code

The project uses C++20 and standard containers. There are no background threads,
external consensus libraries, or generated protocol bindings.

## A useful reading order

1. `Command`, `Proposal`, `Reply` in `src/quepaxa.hpp`: what is on the wire?
2. `Recorder::record`: what can one node remember and report?
3. `Proposer::outgoing` and `receive`: what evidence changes its next action?
4. `Message`, `Node`, `Request` in `src/lab.hpp`: separate local and observer state.
5. `Simulator::step` in `src/lab.cpp`: deliver exactly one envelope.
6. `Simulator::apply`: apply a contiguous decided prefix and deduplicate requests.
7. `Simulator::read`: compare local, session, and logged reads.
8. `write_event` and `accept_event`: the separate asynchronous architecture.
9. `check_linearizable`: try legal sequential explanations of client histories.
10. `src/main.cpp`: parse commands into these operations.

`execute` is the map's sequential specification. The running application and
history checker both use it. That keeps the rules for CAS and absent values
explicit, but means independent outcome tests remain important too.

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
- `run` has a message bound. An empty queue does not prove client completion.
- `retry` is one original client operation, not a second invocation in history.
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

To debug a transition, use `-O0 -g` (the Make default) and a debugger breakpoint
in `Simulator::step` or `Proposer::receive`. Tests use ordinary exceptions and
assertions implemented in a tiny helper, so no test framework hides the behavior.
