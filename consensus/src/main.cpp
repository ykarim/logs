#include "lab.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <unistd.h>

namespace {
const char* help = R"(Start here: docs/EXPERIMENTS.md. Values are single words; <missing> means absent.

new [quepaxa|eventual|causal] [3|5] [seed]   Reset the entire experiment
set NODE KEY VALUE [CLIENT]                Submit a write (default client alice)
cas NODE KEY EXPECTED VALUE [CLIENT]       Atomic compare-and-set via consensus
get NODE KEY [local|session|linearizable] [CLIENT]
retry NODE REQUEST_ID                     Retry same logical request via another proposer
step [MESSAGE_ID]                         Deliver one envelope; choose ID to reorder
run [MAX_MESSAGES]                        Deliver until queue empty or limit (default 10000)
messages                                  Inspect envelopes and IDs
inspect NODE                              Inspect local protocol state and versions
status                                    Compare nodes
history                                   Client invocations and responses
check                                     Check agreement and application replay
check linearizability                     Search this history (max 12 operations)
isolate NODE                              Cut both directions to other nodes
block NODE NODE                           Cut one directed link
heal                                      Restore links (does not resend dropped messages)
sync                                      Queue catch-up; then run
stop NODE                                 Crash-stop a node; no restart support yet
delay MS                                  Delay for newly sent messages (default 10ms)
tick MS                                   Advance clock without delivering messages
duplicate MESSAGE_ID | drop MESSAGE_ID    Inject transport faults
explain                                   Repeat explanation of last transition
help | quit

Example: set n1 checkout true
         run
         get n3 checkout linearizable
         run
)";
int integer(const std::string& text) {
    std::size_t used = 0;
    long value = std::stol(text,&used);
    if (used != text.size() || value < 0 || value > 1000000) throw std::runtime_error("Expected integer 0..1000000.");
    return static_cast<int>(value);
}
int node(const std::string& text, const lab::Simulator& s) {
    if (text.size() < 2 || text[0] != 'n') throw std::runtime_error("Node names are n1, n2, n3 (or n4, n5).");
    int n = integer(text.substr(1));
    if (n < 1 || n > static_cast<int>(s.nodes.size())) throw std::runtime_error("Node is outside this cluster.");
    return n-1;
}
}
int main(int argc, char** argv) {
    std::ifstream script;
    std::istream* input = &std::cin;
    bool interactive = isatty(STDIN_FILENO);
    if (argc > 1) {
        if (argc != 3 || std::string(argv[1]) != "--script") {
            std::cerr << "Usage: consensus-lab [--script FILE]\n"; return 1;
        }
        script.open(argv[2]);
        if (!script) {std::cerr << "Cannot open script.\n"; return 1;}
        input = &script; interactive = false;
    }
    auto sim = std::make_unique<lab::Simulator>(3,42,"quepaxa",std::cout);
    std::cout << "Consensus lab | C++20 | QuePaxa | 3 nodes | seed 42\nType help. Start with docs/EXPERIMENTS.md.\n";
    std::string line;
    int errors = 0;
    while (true) {
        if (interactive) std::cout << "lab> " << std::flush;
        if (!std::getline(*input,line)) break;
        std::istringstream stream(line);
        std::vector<std::string> a;
        for (std::string word; stream >> word;) {
            if (word.starts_with('#')) break;
            a.push_back(word);
        }
        if (a.empty()) continue;
        if (!interactive) std::cout << "lab> " << line << '\n';
        try {
            auto arity = [&](std::size_t low, std::size_t high) {
                if (a.size() < low || a.size() > high) throw std::runtime_error("Wrong argument count; type help.");
            };
            const auto& op = a[0];
            if (op == "quit" || op == "exit") break;
            if (op == "help") std::cout << help;
            else if (op == "new") {
                arity(1,4);
                sim = std::make_unique<lab::Simulator>(a.size()>2 ? integer(a[2]) : 3,
                    a.size()>3 ? integer(a[3]) : 42,a.size()>1 ? a[1] : "quepaxa",std::cout);
                sim->say("Fresh cluster. Old requests, sessions and network faults cleared."); sim->status();
            } else if (op == "set" || op == "cas") {
                bool cas = op == "cas";
                arity(cas ? 5 : 4, cas ? 6 : 5);
                lab::Command c;
                c.kind = op; c.key = a[2]; c.value = a[cas ? 4 : 3];
                c.expected = cas ? a[3] : "";
                c.client = a.size() == (cas ? 6u : 5u) ? a.back() : "alice";
                if (c.value == "<missing>") throw std::runtime_error("<missing> is reserved for absent keys.");
                sim->submit(node(a[1],*sim),c);
            } else if (op == "get") {
                arity(3,5); sim->read(node(a[1],*sim),a[2],a.size()>3 ? a[3] : "local",a.size()>4 ? a[4] : "alice");
            } else if (op == "step") {arity(1,2);sim->step(a.size()>1 ? integer(a[1]) : 0);}
            else if (op == "run") {arity(1,2);sim->run(a.size()>1 ? integer(a[1]) : 10000);}
            else if (op == "status") {arity(1,1);sim->status();}
            else if (op == "messages") {arity(1,1);sim->show_messages();}
            else if (op == "inspect") {arity(2,2);sim->inspect(node(a[1],*sim));}
            else if (op == "history") {arity(1,1);sim->show_history();}
            else if (op == "explain") {arity(1,1);std::cout << sim->last << '\n';}
            else if (op == "check") {
                arity(1,2);
                if (a.size() == 2) {
                    if (a[1] != "linearizability") throw std::runtime_error("Use check linearizability.");
                    sim->check_linearizable();
                } else { sim->check_agreement();sim->say("PASS: observed log agreement and application replay. This does not check read freshness."); }
            } else if (op == "isolate") {arity(2,2);sim->isolate(node(a[1],*sim));}
            else if (op == "block") {arity(3,3);sim->blocked.insert({node(a[1],*sim),node(a[2],*sim)});}
            else if (op == "heal") {arity(1,1);sim->blocked.clear();sim->say("Links healed. Use sync and run to replace dropped traffic.");}
            else if (op == "sync") {arity(1,1);sim->sync();}
            else if (op == "stop") {arity(2,2);sim->stop(node(a[1],*sim));}
            else if (op == "delay") {arity(2,2);sim->delay=integer(a[1]);}
            else if (op == "tick") {arity(2,2);sim->tick(integer(a[1]));}
            else if (op == "retry") {arity(3,3);sim->retry(node(a[1],*sim),a[2]);}
            else if (op == "duplicate") {arity(2,2);sim->duplicate(integer(a[1]));}
            else if (op == "drop") {arity(2,2);sim->drop(integer(a[1]));}
            else throw std::runtime_error("Unknown command; type help.");
        } catch (const std::exception& e) {
            ++errors;
            std::cout << "! " << e.what() << '\n';
        }
    }
    // WAIT is an expected result in some educational scripts, so continue the
    // script and report the count without treating those scripts as test runners.
    if (errors) std::cout << "Commands reporting an error or WAIT: " << errors << '\n';
    return 0;
}
