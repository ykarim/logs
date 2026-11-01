# Experiments

Build once with `make`, then open `./build/consensus-lab`.

Do one lesson at a time. Before `run`, guess the answer to the question. After
`run`, compare `status` and `history`. There is no need to understand every trace
line on the first pass.

Each lesson has a complete executable transcript in `experiments/`. To replay:

```sh
./build/consensus-lab --script experiments/01-first-decision.lab
```

All times below are virtual milliseconds. Nothing sleeps. A local operation has
zero simulated network latency; it still does real computation on your machine.

## 1. One write, four separate moments

**Question:** Does submitting an update mean every node already has it?

```text
new quepaxa 3 42
set n1 checkout true
status
messages
step
inspect n1
run
status
history
```

**Expect:** Immediately after submission, the config maps are empty and the
request is pending. After one `step`, n1's recorder has stored a proposal, but
nothing has been applied. After `run`, all three maps contain `checkout=true`,
and the client has a success response.

**Why:** Recording a proposal, deciding a slot, learning it, applying it, and
responding are different transitions.

Now run:

```text
get n3 checkout linearizable bob
run
check
check linearizability
```

**Expect:** The read gets its own log slot and returns true. Both checks pass.

**Code:** `Recorder::record`, `Proposer::receive`, then `Simulator::apply`.

**Gotcha:** The preferred n1 can decide after one quorum round trip. The client
response takes more time because learning and response delivery follow it.
Self messages use the same delay as other messages in this teaching model.

## 2. Agreement can pass while linearizability fails

**Question:** Can a node safely return an old value after another node reports a
successful write?

```text
new quepaxa 3 42
isolate n3
set n1 checkout true alice
run
get n3 checkout local bob
check
check linearizability
get n3 checkout session alice
```

**Expect:** n1 and n2 have true. n3 returns `<missing>`. Agreement passes;
linearizability reports a violation. Alice's session read reports WAIT.

**Why:** No conflicting log decisions occurred, but Bob's read started after
the successful write and returned the old state. Alice carries a watermark for
the write; n3 cannot satisfy it.

```text
heal
sync
run
get n3 checkout session alice
```

**Expect:** Alice now reads true. The earlier bad read stays in history; healing
does not erase a linearizability violation that already happened.

**Gotcha:** Bob has a different session. A new session has no automatic knowledge
of Alice's writes. Session consistency and linearizability are different promises.

## 3. Lose the preferred proposer

**Question:** Must the remaining nodes elect a new leader before writing?

```text
new quepaxa 3 42
stop n1
set n2 checkout true
run
inspect n2
history
check
```

**Expect:** The write completes through n2 and n3. You see logical phases 1 and 2,
with more communication than lesson 1. No election timeout is advanced.

**Why:** n2 can drive the asynchronous fallback itself. n1 was an efficiency
preference, not the only node allowed to make progress.

**Gotcha:** This does not mean the protocol can write without a majority. It also
does not demonstrate automatic hedging: you explicitly activated n2 by submitting
there. Automatic delayed activation is a future increment.

## 4. Two clients try to replace the same value

**Question:** Can both clients replace the original `east` value successfully?

```text
new quepaxa 3 42
set n1 route east
run
cas n2 route east west alice
cas n3 route east central bob
run
history
```

**Expect:** One command wins the contested slot. A losing command may remain
pending because this version does not automatically put it in a later slot.

Finish both logical requests:

```text
retry n1 r2
run
retry n2 r3
run
history
check linearizability
```

**Expect:** Exactly one CAS reports `swapped`; the other reports `not-swapped`.
The particular winner is less important than this rule. The check passes.

**Why:** CAS checks and updates are one application operation. Once one changes
`east`, the other cannot successfully compare against `east`.

**Gotcha:** Consensus decides one value for a slot, not that every proposal will
be accepted. Application retry and protocol agreement are separate responsibilities.

## 5. A deadline does not mean failure

Use the full transcript so you can select the exact envelopes:

```sh
./build/consensus-lab --script experiments/05-unknown-outcome.lab
```

**Question:** If a client has not received success, could another client already
read its update?

**Expect:** The transcript delivers recording replies and learning messages, but
withholds response envelopes. After `tick 1000`, the original request is still
`PENDING/UNKNOWN`. Another node's local read sees true. The history is still
linearizable: the pending write is allowed to have taken effect.

**Why:** `tick` advances only the clock. It does not deliver, cancel or roll back
anything. A response can be delayed after the actual operation.

**Gotcha:** `retry n2 r1` preserves identity. A new `set` or `cas` command creates
a new logical request. The test suite also checks that a CAS retry returns its
original result rather than rerunning the condition.

## 6. Accept writes on both sides of a partition

**Question:** What do we gain and what do we now have to resolve?

```text
new eventual 3 42
isolate n1
set n1 route west alice
set n2 route east bob
run
status
get n1 route local charlie
check linearizability
heal
sync
run
status
inspect n3
```

**Expect:** Both writes succeed locally during the partition. n1 initially says
west; n2 says east. The stale read violates linearizability. After exchange, all
nodes choose east, but `inspect` retains both versions.

**Why east?** These concurrent writes both have Lamport counter 1. The origin
node breaks the tie, so n2's event wins. This is a deliberately simple deterministic
rule, not an assertion that east was the best or physically latest update.

**Gotcha:** Convergence does not mean every acknowledged value survives as the
visible value. Application meaning determines whether this conflict policy is useful.
A Lamport counter cannot by itself tell you that two writes were concurrent.

## 7. See an effect before its cause

```text
new eventual 3 42
set n1 destination east
set n1 enabled true
messages
step 3
status
step 1
status
```

**Question:** What does n2 show after `step 3`?

**Expect:** n2 sees `enabled=true` without a destination. Message 3 carries the
effect; message 1 carries the cause.

Now repeat with causal buffering:

```text
new causal 3 42
set n1 destination east
set n1 enabled true
step 3
status
inspect n2
step 1
status
```

**Expect:** n2 buffers the enable event. After message 1 arrives, both values
become visible. `buffered` returns to zero.

**Why:** The second event names the first as a dependency. Missing causes delay
visibility even though the effect's message has arrived.

**Gotcha:** This is not a transaction combining the two writes. Seeing only the
destination is allowed. Seeing the effect without its dependency is what is
prevented. Use session reads when moving a client between replicas so its
observations travel with it; a local read opts out of that context tracking.

## 8. Majority and minority

```sh
./build/consensus-lab --script experiments/08-majority-minority.lab
```

**Question:** Which group can finish writes in a five-node cluster?

**Expect:** n1/n2/n3 can decide. Isolated n4 cannot decide its write. It stays
pending even when its own recorder answers. After healing and catch-up, retrying
that request can complete it in a later slot.

**Why:** Three distinct recorder replies are needed. A single recorder cannot
manufacture a quorum by replying many times.

**Gotcha:** `run` means “process the queue,” not “every request must have finished.”
Inspect `history`. Dropped messages need explicit `sync` or a retry. A pending
operation may still happen after the network recovers.

## Small variations to try yourself

- In lesson 1, change `delay 10` to `delay 100` before submission. Compare client
  latency. Existing queued envelopes keep their scheduled times.
- Use `duplicate ID` on a reply. Confirm the duplicate does not count as a second
  recorder. Each envelope's transport ID differs, but its sender remains the same.
- Replace isolation with `block n1 n2`. Only that direction is blocked. Predict
  whether enough request/reply routes still exist.
- Deliver an old `record` message after newer logical steps. Inspect the recorder;
  the old request cannot roll its state backward.
- Drop a `learn` message, run the queue, then `sync` and `run`. Compare learned
  decisions and applied slots.

Each `new` clears the old experiment, including clients, faults, and history.
