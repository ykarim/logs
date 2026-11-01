# Consensus and consistency playground — growing plan

Update this document as experiments expose new questions. Keep implemented
behavior separate from intentions. Language decision: **C++20**, standard
library first. Prefer explicit state transitions over framework abstractions.

## Purpose

Learn how distributed nodes agree, what clients are promised, and what changes
when messages become slow, lost, duplicated, or reordered. Make the protocol
readable enough to follow one operation from submission to response.

The motivating example is a tiny configuration service. A config map is useful
because it is simple, but operations such as routing changes and compare-and-set
make ordering and stale reads consequential. Keep configuration distribution
separate from agreement: a decided update can still be absent at a lagging node.

The original inspiration is [Cloudflare's Meerkat introduction](https://blog.cloudflare.com/meerkat-introduction/).
That article describes an experimental control-plane consensus service, not a
finished production configuration-distribution system to reproduce.

## Learning approach

1. Predict one outcome before running an experiment.
2. Submit one operation and inspect its messages.
3. Deliver one message; compare the node's state before and after.
4. Introduce one fault and repeat.
5. Explain both the returned values and the operations that could not finish.

Use short commands, concrete values, and small diagrams. Define new terms when
introduced. Guides should include expected outcomes and common mistaken
conclusions. Readers should be able to stop after any lesson and resume later.

## Code readability

The current code groups behavior by lesson: client submission lives in
`src/client.cpp`, QuePaxa traffic/phase rules under `src/protocols/`, and shared
log application in `src/application.cpp`. Reads, transport, inspection and
checking have their own short files.
Start with `docs/WALKTHROUGH.md`; keep numbered protocol handlers in execution
order. Explanations should identify exactly where decisions, application and
client responses differ. Use descriptive phase names and ordinary control flow.

Preserve existing experiment behavior during structural cleanup. Avoid adding
abstraction layers that obscure protocol transitions. The user must vet each
new third-party library before adoption; no new library is needed for this
refactor. Formatting configuration is optional developer tooling, not a build
or runtime dependency.

## Architecture

Independent node state machines run in a deterministic event simulator. A node
receives a message and changes only its local state. The simulator controls the
network and virtual clock. It can inspect all nodes for teaching and checking;
protocol decisions cannot use that global view.

The first version uses no threads, sockets, or real sleeps. This makes message
order and failures reproducible. A seed reproduces randomness for this toolchain;
standard-library random distribution details may differ across platforms.
Later, put the same core behind a real transport and separate processes.

The shared engine now depends on a per-replica `Protocol` interface. Each
implementation owns its private state and payload types. A node-local `Runtime`
provides sending, deterministic timers and application/completion services.
The engine routes payloads without understanding terms, ballots or phases.
An injectable test protocol verifies the boundary with custom messages/timers.
Selection happens when a fresh cluster is created; live protocol migration is
outside current scope. The interface and timer plumbing do not implement Raft,
hedging, durability or automatic repair by themselves.

Layers:

- Protocol: implementation-owned state, message handlers and timer rules.
- Application: ordered config operations, deterministic application, deduplication.
- Simulation: queued envelopes, delivery order, virtual time, link faults.
- CLI: commands, inspection, histories, guided transcripts.

## Consensus track

Start with [QuePaxa's paper](https://bford.info/pub/os/quepaxa/quepaxa.pdf).
Map implementation rules to the paper. Do not substitute majority voting and
call it QuePaxa. A majority response is meaningful only in the protocol state
that gives it meaning.

Current implementation:

- [x] Shared engine with a per-node protocol interface, opaque payload routing,
  deterministic timer delivery and separate ordered-log/async application paths.
- [x] One-slot recorder and proposer core, including phase progression/catch-up.
- [x] Fixed preferred proposer and first-round fast path; randomized fallback.
- [x] Independent protocol instances for ordered log slots.
- [x] Concurrent proposers, log-prefix application, local deduplication by request ID.
- [x] Compare-and-set and reads ordered through the log.
- [x] Manual retry and exchange of learned decisions after a partition.
- [ ] Automatic payload dissemination, retries, catch-up and losing-command resubmission.
- [ ] Automatic hedging schedule; measure extra work versus responsiveness.
- [ ] Adaptive preferred-proposer selection and hedging delays.
- [ ] Durable storage, safe recovery, snapshots and membership changes.
- [ ] Multi-Paxos and Raft on the same simulator and application workload.
- [ ] Compare additional experimental protocols after the fundamentals are clear.

Nodes currently fail by crash-stop. No Byzantine behavior, disk corruption,
restart safety, or production readiness is claimed. Liveness assumes a live
majority and appropriate message delivery; randomization is not permission for
an isolated minority to decide. The simulator allows adversarial inspection of
priorities for teaching, so arbitrary hand-picked schedules do not inherit the
paper's probabilistic progress guarantees.

## Leadership and progress — planned lessons

The current lab fixes n1 as the preferred proposer. It does not yet choose or
replace that preference automatically. Teach these additions in order:

1. **Selection and scope.** Make the initial preferred proposer configurable
   when creating a cluster. Show that the preference is shared for a slot's
   first round; changing it independently at one node is unsafe. Demonstrate
   the special fast-path priority and distinguish it from Lamport counters.
   Keep that priority as part of QuePaxa: only the agreed preferred proposer
   receives it in the first round of a slot. Other proposers use random
   priorities; subsequent rounds are leaderless with random priorities for all.
   Remove the hardcoded identity, not the fast-path rule. An optional leaderless
   comparison may disable this optimization, clearly labeled as an experiment.
2. **Request distribution before hedging.** Introduce an explicit submitter
   (client library or front-end service) and queued submission messages. One
   logical request ID must survive distribution to multiple replicas and retries.
   Start with distribution to all replicas as described in QuePaxa section 3.1;
   later compare preferred-first submission with delayed fan-out. A backup
   cannot propose a request it has not received. Show request receipt separately
   from recorder participation and proposer activation. Model submitter-to-node
   delays/failures separately from replica-to-replica traffic, including failure
   of the initially contacted node before it forwards anything.
3. **Automatic hedging.** Add visible virtual activation timers for additional
   proposers. Submit once, slow or stop the preferred node, and watch another
   proposer continue without an election. Compare zero, short and long hedging
   delays using completion latency, activation count and message count. Keep
   each proposer's knowledge local; suppress redundant work using received
   progress evidence rather than a simulator-wide decision flag.
4. **Automatic completion and repair.** Retry lost traffic, resubmit commands
   that lose a slot, and catch up lagging replicas. Preserve request identity
   and original results. Healing should eventually restore progress without
   requiring manual `sync` or resubmission in these new lessons.
5. **Adaptive preference.** Introduce epochs spanning multiple log slots. Start
   with a documented rotation, measure observed performance, then agree on a
   preferred proposer and hedging schedule for a future epoch. Map this to
   QuePaxa section 5.3. Explain how the shared schedule is established, including
   bootstrapping and when changes take effect. Compare fixed and adaptive
   selection when one node is slow but alive, and when performance changes.
   Separate measuring a candidate's performance from agreeing on the choice.
   Document a bootstrap preference and an exact future slot/epoch boundary for
   changes; a lagging node must establish the applicable schedule before using
   a preferred fast path. Other proposers may drive agreement on the schedule
   under the existing rules even when the current preferred proposer is down.
   Hedge activation does not itself replace the preferred proposer.
6. **Raft elections.** Implement terms, follower/candidate/leader roles,
   heartbeats, randomized election timeouts, majority votes and log-freshness
   voting rules alongside log replication. Explain that elections happen on
   suspected failure, rather than at fixed periodic voting cycles. Include
   split votes, false suspicions from delayed heartbeats, and an old leader
   returning after a newer term has formed.
7. **Side-by-side experiments.** Compare QuePaxa and Raft with the same config
   operations, client guarantee, cluster layout and fault schedule. Distinguish
   a preferred proposer providing an efficiency advantage from Raft leadership
   driving replication within a term. Measure outage duration, client latency
   and communication under a crashed leader, slow leader, changing latency and
   partition. Both must preserve safety and require adequate quorum connectivity.

### Define progress before implementing activation

Replace vague descriptions such as "sufficient progress" with a local predicate
for a particular slot and logical step. QuePaxa section 5.2 schedules redundant
effort for the relevant step; a hedge is not merely a single whole-request timer.
Protocol step completion and the final slot decision are different events.

Planned observable evidence:

- A proposer gathers distinct-recorder quorum replies and completes a logical
  step under the core's transition rules.
- Another node receives a valid later-step protocol message, learning that an
  earlier step has been completed. Follow the protocol's catch-up rules and
  carry the candidate forward; do not invent a replacement preferred proposer.
- A node learns the slot's decision and can stop proposing for that slot, while
  still finishing ordered application and client response delivery.

Receiving the request, seeing a heartbeat, or recording one proposal at the same
step is not evidence that the step completed. A backup may be unaware of real
remote progress; if its activation delay expires, redundant work is acceptable.
The simulator must never suppress a hedge using another node's private state.

Before coding, document exactly which received messages establish each predicate
and map them to the paper/reference implementation. If explicit progress notices
are added for clarity, label them as a lab design choice and define their trust
and delivery assumptions. A crash-only node can report its own completed work;
this does not constitute a Byzantine-verifiable quorum certificate.

Give timers a slot, step and generation so old timer events cannot activate
obsolete work. Re-arm according to the documented per-step schedule; unrelated
traffic or incomplete work must not postpone backup activation indefinitely.
Expiration means "start helping," not "declare failure," "elect a leader," or
"declare the write successful."

Add per-node or per-link latency controls so one proposer can be slow while
others communicate quickly. Trace receipt, timer scheduling, local progress
evidence, activation/suppression, decision, application and response separately.
Include a plain-language reason and the relevant slot/step in each event.

Acceptance experiments: healthy preferred proposer suppresses unnecessary work
when evidence arrives in time; a crash before request forwarding cannot strand
a request with submitter fan-out; a proposer stalled after partial progress is
helped on the stalled step; delayed progress evidence permits safe redundant
activation; stale timers cannot revive decided slots; a live majority can finish
while a minority cannot. Check that one logical request executes once and
compare decision latency separately from client-observed completion latency.

Each lesson needs an executable transcript, expected observations, a short code
reading path and targeted safety/progress checks. Timers must advance through
the simulator's event queue so results remain reproducible. Mark these features
as planned until both implementation and experiments are present.

References: [QuePaxa sections 4.2.5 and 5](https://bford.info/pub/os/quepaxa/quepaxa.pdf),
[Raft section 5](https://raft.github.io/raft.pdf).

## Consistency track

Keep the client guarantee fixed when comparing consensus algorithms. Changing
QuePaxa to Raft is different from changing linearizable reads to local reads.

- [x] Local reads: immediate local state, possibly stale.
- [x] Session reads: carry observed progress across replicas; wait if behind.
- [x] Linearizable reads: log the read and apply every preceding slot first.
- [ ] End-to-end lagging-reader catch-up and automatic read resubmission into
  a later slot, with no manual repair commands (see experiment below).
- [x] Separate eventual replication: accept local writes, later exchange events.
- [x] Causal dependency buffering: hide effects until their causes arrive.
- [x] Deterministic conflict winner, while retaining all event versions for inspection.
- [ ] Dedicated sequential-consistency implementation and checker.
- [ ] Bounded-staleness guarantees with an explicit bound and refusal when unprovable.
- [ ] Vector/version clocks, explicit concurrent-version display, alternative conflict policies.
- [ ] CRDT counters and sets to show when merging is naturally meaningful.
- [ ] More extensive causal/session history checking.

The async conflict rule uses Lamport counter, origin node, then event ID. It is
a deterministic teaching rule, not physical “last write wins.” Eventual
convergence alone promises no particular latest value. Causal mode buffers
explicit dependencies; use session reads to carry observations between nodes.
Local reads intentionally opt out of client-context tracking.

### Reads through the protocol — planned complete scenario

Reading and writing clients submit operations; replicas perform consensus on
their behalf. Readers do not become voting members. Preserve both logged reads
for linearizability and local reads for deliberately stale observations.

Add an experiment matching the lagging-reader example in the Meerkat article:

1. Start with X=0. Let n1/n2 decide and apply `set X=1` in slot 1 while n3
   misses the learning message. Keep or restore quorum connectivity for n3.
2. Submit a linearizable read to n3. It initially proposes the read in slot 1,
   which looks empty locally. Existing protocol evidence must preserve the
   earlier write rather than allow the read to replace it.
3. n3 learns the earlier decision, applies the missing prefix, and automatically
   resubmits the same logical read into an available later slot.
4. Apply through the read's decided slot and return 1. The read itself does not
   mutate X. Trace request ID, slot, step, catch-up, resubmission and result.
5. Repeat while n3 cannot reach a majority: its local read can return 0, but its
   linearizable read must remain pending. Heal communication and verify eventual
   completion without manual `sync` or retry under the new automatic behavior.

Distinguish a lagging but connected node from an isolated one. Also distinguish
receiving a decision from applying all preceding slots, and application from
client receipt of the response. Lost learning/response messages do not undo a
decision; another replica's response may complete the client operation, while
absence of every response leaves its outcome unknown. Test response loss and
same-ID retries along with the lagging read.

Current boundary: the lab already logs linearizable reads and demonstrates
stale local reads and minority blocking. It does not automatically complete the
read after it loses an occupied slot. Keep that gap visible until implemented.

Source: [Meerkat's read ordering example](https://blog.cloudflare.com/meerkat-introduction/).

Consistency models are not a single speed slider. Removing real-time order
still leaves sequential consistency requiring a shared order. Causal consistency
permits different orders for independent updates. Session guarantees constrain
one client's experience. Read availability can require waiting when that client
moves to a replica missing its context.

## Transaction track — later

Explore read committed, snapshot isolation, serializability and strict
serializability with multi-key workloads. Keep transaction isolation distinct
from replica freshness. Introduce write skew and lost updates through concrete
examples. State whether each guarantee covers a key, an operation over the map,
or a transaction across multiple keys.

## CLI and experiments

Implemented commands cover creating 3/5-node clusters, writes, compare-and-set,
three read modes, per-message stepping/reordering, duplication/drop, directed
link cuts, isolation/healing, crash-stop, virtual delay, clock advancement,
catch-up, retry, histories and inspection.

Existing scripted lessons:

1. First decision and logged read.
2. Stale reads versus agreement and session guarantees.
3. Failed preferred proposer.
4. Concurrent compare-and-set and lost proposals.
5. A timed-out client with an already-applied write; retries retain identity.
6. Partitioned local writes and conflict reconciliation.
7. Causes arriving after effects.
8. Majority versus minority in a five-node cluster.

Future lessons: slow-but-alive proposer, automatic hedging, false election
suspicions in Raft, asymmetric links, stale messages after healing, durable
restarts, read optimizations and regional latency layouts.

For each new consensus lesson, include an end-to-end text sequence showing:
client submission -> request distribution -> proposer activation -> recorder
exchanges -> decision -> learning -> ordered application -> client response.
Show both the one-round-trip fast path and the multi-exchange fallback, with
quorum step evidence explained alongside the messages. Use a single mutation
before introducing concurrent mutations. Keep proposed trace text and commands
clearly separate from the currently implemented CLI.

## Verification

Check agreement per slot, prefix application, deterministic replay and duplicate
execution behavior. Search small client histories for linearizability, including
pending operations that might have taken effect. A timeout is an unknown outcome,
not a confirmed failure. A passing history check is evidence for that history,
not a proof of the implementation.

Current tests cover application/read semantics, minority blocking, preferred
node failure, retries, duplicate replies, old recorder requests, async convergence,
causal buffering and 150 seeded concurrent delivery schedules. The eight guide
transcripts also have expected-outcome checks. Add targeted tests when new
behavior introduces a new correctness obligation.

## Next increments

1. Work through the existing lessons and improve explanations where confusing.
2. Make the shared initial preference configurable; add explicit submitter
   distribution, then per-step hedging with local evidence, timers and metrics.
3. Add automatic retries, losing-command resubmission and catch-up without
   hiding protocol transitions.
4. Add epoch-based adaptive preference and the leadership experiments above.
5. Add Raft elections and replication, then compare matching fault scenarios.
6. Add Multi-Paxos and expand consistency and transaction experiments.

Useful references: [consistency models](https://jepsen.io/consistency),
[causal consistency](https://jepsen.io/consistency/models/causal),
[sequential consistency](https://jepsen.io/consistency/models/sequential),
[Dynamo's design](https://www.allthingsdistributed.com/files/amazon-dynamo-sosp2007.pdf).
