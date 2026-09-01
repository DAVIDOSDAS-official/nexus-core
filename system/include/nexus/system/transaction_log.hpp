#pragma once

#include <set>
#include <string>
#include <vector>

namespace nexus::system {

enum class TransactionKind {
    Install,
    Remove
};

// One thing Nexus did, written down.
struct TransactionRecord {
    TransactionKind kind = TransactionKind::Install;

    std::string when;          // ISO 8601, UTC
    std::string request;       // what was asked for
    std::string resolved;      // what that turned out to mean

    // What the plan involved. The field name states what actually
    // happened -- Added, Removed, or Attempted -- because a log that
    // says a removal added a package, or that a failed transaction
    // added fourteen, is worse than no log: somebody debugging the
    // machine would go looking for things that were never there.
    std::set<std::string> packages;

    std::string outcome;
    bool succeeded = false;
    int exitCode = 0;
};

// Append a record of a change to the log.
//
// A tool that modifies a system owes an account of what it did. apt
// keeps its own history, but apt's history says "installed steam";
// this one says what was asked for, what that resolved to, and why
// those packages and not others -- which is the part nobody else
// records and the part somebody debugging a machine actually needs.
//
// Control-file format, like everything else here, so it can be read
// without a tool.
bool recordTransaction(
    const std::string& path,
    const TransactionRecord& record
);

std::vector<TransactionRecord> readTransactions(
    const std::string& path
);

// Where the log lives: a system-wide path when running as root, and a
// per-user one otherwise, so a dry run by an ordinary user still
// leaves a trace they can read.
std::string defaultTransactionLog();

std::string currentTimestamp();

}
