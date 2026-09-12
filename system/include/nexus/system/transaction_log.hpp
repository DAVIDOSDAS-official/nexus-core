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

    // Whether this record was written before the work started and
    // never finished.
    //
    // The log used to be written after the package manager returned,
    // so being killed while it ran left the packages installed and
    // nothing recorded -- a log that does not merely lack an entry
    // but implies the change never happened. Now a record goes in
    // first saying what is about to be attempted, and is replaced
    // when it is over. One left saying "started" is a machine that
    // was interrupted.
    bool unfinished = false;
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

// Write a record saying what is about to happen. Returns a marker to
// pass to finishTransaction when it is over.
//
// Deliberately separate calls rather than one that wraps the work: an
// interrupted process does not get to run its cleanup, so the record
// has to be durable on disk before the work begins.
std::string beginTransaction(
    const std::string& path,
    const TransactionRecord& record
);

// Replace a started record with what actually happened. If the marker
// is not found the record is appended anyway: losing the outcome is
// worse than duplicating it.
bool finishTransaction(
    const std::string& path,
    const std::string& marker,
    const TransactionRecord& record
);

// Records that were started and never finished.
std::vector<TransactionRecord> unfinishedTransactions(
    const std::string& path
);

std::vector<TransactionRecord> readTransactions(
    const std::string& path
);

// Where the log lives: a system-wide path when running as root, and a
// per-user one otherwise, so a dry run by an ordinary user still
// leaves a trace they can read.
std::string defaultTransactionLog();

// Every place a record might be, because changes are made under sudo
// and read back without it. Reading only the caller's own file
// reports "no changes recorded" about a machine that was changed.
std::vector<std::string> transactionLogPaths();

std::vector<TransactionRecord> readAllTransactions();

std::string currentTimestamp();

}
