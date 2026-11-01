# Consensus lab

A small C++20 playground for understanding distributed systems by controlling
messages and watching what each node knows. No third-party libraries.

Start with one write and one message. Add faults after that makes sense.

```sh
make
./build/consensus-lab
```

Inside the CLI:

```text
set n1 checkout true
messages
step
inspect n1
run
status
```

`set` submits an operation. It has not succeeded until the trace says `SUCCESS`.
`step` delivers one message. `run` delivers the queued messages. `status` compares
the nodes. `help` lists every command.

Read [the experiment guide](docs/EXPERIMENTS.md) next. Each experiment has a
question, commands, an expected outcome, and the reason behind it.

Run a complete guided transcript:

```sh
./build/consensus-lab --script experiments/01-first-decision.lab
make test
```

- [Growing plan and scope](PLAN.md)
- [Concepts in plain language](docs/CONCEPTS.md)
- [Experiments](docs/EXPERIMENTS.md)
- [Code reading path and protocol boundaries](docs/CODE.md)

Implemented: QuePaxa's per-slot proposer/recorder core, an ordered config log,
local/session/logged reads, compare-and-set, request deduplication, a controlled
network, small-history linearizability checking, and separate eventual/causal
replication experiments. These are educational implementations, with crash-stop
nodes and manually initiated retry/catch-up. Automatic hedging, adaptive leader
selection, durable restart, real processes, other consensus protocols, and
transaction isolation remain on the plan.

A C++20 compiler and `make` are sufficient. macOS Command Line Tools provide
these here. The optional CMake build is:

```sh
cmake -S . -B build/cmake
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

CMake is not installed in the current environment; the Make build is tested.
