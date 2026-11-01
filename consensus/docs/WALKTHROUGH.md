# Follow one write through the code

Goal: find the exact places where a request is proposed, decided, applied, and
answered. You can follow this without reading the CLI parser or history checker.

Open [`client.cpp`](../src/client.cpp),
[`protocols/quepaxa_driver.cpp`](../src/protocols/quepaxa_driver.cpp), and
[`application.cpp`](../src/application.cpp) in that order. Keep
[`protocols/quepaxa.hpp`](../src/protocols/quepaxa.hpp) nearby for decision rules. Everything is plain C++20; no networking library
is involved.

## Set up the simplest case

```text
new quepaxa 3 42
set n1 X 1
```

We have three replicas. n1 is preferred. There is one client operation, `r1`.
No failures and no competing writes. n1 will normally use the fast path.

The same node can act as proposer and recorder. Those are roles, not separate
kinds of machine. In code, node indexes start at 0; CLI names start at n1.
`nodes[0]` is therefore n1.

## 1. Submit: create an attempt

Find `Simulator::submit`.

`Simulator::submit` assigns request ID r1 and records the client's invocation.
It calls the selected implementation through `Protocol::submit`.

`QuePaxa::submit` picks the local next unapplied slot: slot 1 on an empty
cluster. It creates a `Proposer` for that slot and calls `send_record_requests`.
The engine does not construct a proposer itself.

**Nothing in the config map has changed.** The request is only pending.

This lab currently sends directly to the named proposer. Automatic submitter
fan-out and backup activation are planned, not hidden inside this function.

## 2. Send record requests: put envelopes in the network

Find `QuePaxa::send_record_requests` in `protocols/quepaxa_driver.cpp`.

The proposer constructs one `record` envelope per recorder, including itself.
Each contains the slot, logical step, and a proposal. `Proposer::outgoing`
chooses its priority. n1 receives the special highest priority for this slot's
first round. QuePaxa's first step is 4: round 1, phase 0.

`runtime.send(message)` stamps the local sender and appends an envelope to a
queue. It does not call the remote node immediately. Type `messages` to see those envelopes.

## 3. Record: store protocol evidence and reply

Type `step`. The network delivers one envelope to `QuePaxa::receive`, which
dispatches it to `on_record`.

`QuePaxa::on_record` calls `Recorder::record`, which remembers the first proposal and
highest-ranked proposal at that step. It queues a `reply` containing its logical
step, its first proposal, and its prior-step aggregate.

**Recording a proposal still does not change X.** Inspect n1:

```text
inspect n1
status
```

You should see recorder state but no applied config mutation.

## 4. Interpret replies: the decision happens here

Keep typing `step`. Each delivered reply enters `on_reply`.

`QuePaxa::on_reply` calls `Proposer::receive`. Open it in `protocols/quepaxa.hpp`:

- Replies are keyed by recorder ID. Duplicating one reply cannot create a majority.
- Until enough distinct recorders reply, it waits.
- In `Phase::propose`, a quorum whose first proposals all carry the special
  priority permits a fast-path decision.
- Without that condition, the candidate moves through the named fallback phases.

`Progress::decision` means a choice has been established for this slot.
`Progress::advance` means to run another logical step, not that the write succeeded.

Back in `on_reply`, a decision causes `learn` messages to be queued. Watch for
`DECIDED` in the trace. The local config may still be unchanged at this moment.

## 5. Learn: record the chosen operation

A delivered `learn` envelope enters `QuePaxa::on_learn`. It decodes its own
`LearnDecision` payload and calls `runtime.learn`. The shared application code
stores the local decision and calls `apply_decided_prefix`. The protocol, not
the engine, was responsible for establishing that decision's validity.

A learned entry can wait: if slot 2 arrives before slot 1, the application cannot
skip the missing operation. Learning and application are different facts.

## 6. Apply: change the application state

`apply_decided_prefix` executes consecutive learned slots. It calls
[`execute`](../src/config.cpp), which gives ordinary map operations their meaning.

**This is where X becomes 1.** The recorder did not do that earlier.

The `results` map caches the first outcome for each request ID. A repeated ID
returns that outcome without executing the operation again. This matters for
CAS, whose result could otherwise change on a retry.

The replica queues a `response` envelope. Its `result` is separate from the
original command. The client's receipt is another event, not a direct function call.

## 7. Response: the client finally knows

The delivered response enters `Simulator::on_response` in `application.cpp`.
It marks the history entry complete, records the result and latency, and advances
the client's session watermark.

**This is where the trace prints SUCCESS.** If this message is lost, the earlier
decision/application is not undone; the client simply lacks the result.

## Read the journey again

```text
Client operation r1: set X=1
    |
    submit -> send_record_requests
    |
    RECORD envelope delivered -> on_record -> Recorder::record
    |
    REPLY envelope delivered  -> on_reply  -> Proposer::receive
    |                                         |
    |                                         DECISION
    |
    LEARN envelope delivered  -> on_learn -> runtime.learn -> apply -> execute
    |                                                               |
    |                                                               X=1
    |
    RESPONSE delivered        -> on_response
                                                |
                                                client sees SUCCESS
```

There are multiple envelopes and replicas at each stage. A majority can establish
the decision without waiting for every replica. All replicas need not apply
before the client receives success.

Next: run `experiments/03-preferred-node-failure.lab` and inspect the phase switch
again. Then read [`reads.cpp`](../src/reads.cpp): a linearizable read enters this
same journey as a `get` command; a local read accesses the map immediately.

## What changed to make protocols swappable?

`network.cpp` no longer recognizes QuePaxa record/reply/learn messages. It calls
`protocol.receive(message, runtime)`. Only QuePaxa decodes its payload and chooses
which handler runs. Its maps of proposers and recorders are private to its own
replica instance. Shared `Node` stores application data, not QuePaxa state.

The application still applies an agreed log for any ordered-log implementation.
The eventual/causal implementation instead merges events into its local map and
reports local acceptance. It does not pretend those writes are log decisions.

Timer delivery is now supported by the same engine. QuePaxa currently schedules
none: this boundary enables later hedging, but does not implement hedging itself.
