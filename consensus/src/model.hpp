#pragma once
#include "config.hpp"
#include <any>
#include <map>
#include <set>
#include <vector>

namespace lab {
// Shared application state only. No proposer, recorder, term or ballot types.
struct Node {
    bool up = true;
    int applied = 0;
    std::map<int, Command> decisions;
    std::map<std::string, std::string> config;
    std::map<std::string, std::string> results;
};

// The engine routes this envelope without interpreting its protocol payload.
// std::any holds a COPY of a protocol-owned struct. Only that implementation
// casts it back to its own type. Adding Raft messages won't change this file.
struct Message {
    int id = 0, at = 0;
    int from = 0, to = 0;
    std::string kind;
    std::any payload;
    int slot = 0, step = 0; // Optional trace labels, not engine decision inputs.
    std::string request_id; // Optional trace label.
    Command command;        // Used only by engine-generated client responses.
    std::string result;
};
struct Timer {
    int id = 0, at = 0, node = 0;
    std::string tag;
    int generation = 0; // Protocol checks this to ignore obsolete timers.
};

// Observer-side history and context, not protocol decision evidence.
struct Request {
    Command command;
    int node = 0;
    int start = 0, end = 0;
    int invoke_order = 0, return_order = 0;
    bool complete = false;
    std::string result;
};
struct Session {
    int through = 0;
    std::set<std::string> seen;
};
} // namespace lab
