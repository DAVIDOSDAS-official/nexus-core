#pragma once

#include <set>
#include <string>
#include <vector>

namespace nexus::system {

enum class TransactionKind {
    Install,
    Remove
};

// Which log a record was read from (0.2.0, ledger I3).
//
// Only root can write the machine's log. The person's own log is in
// their home folder: they can write anything into it, and so can any
// program they run. A record from it is their own account of what was
// done without sudo; it is never this machine's history, and nothing
// that counts or decides may take it for a change root made.
enum class TransactionSource {
    // Not read from a log (a record about to be written), or read by
    // path without saying whose file it is. Never counted as the
    // machine's: not knowing is not "root's".
    Unknown,
    Machine,   // /var/lib/nexus/transactions.log
    Own        // ~/.local/state/nexus/transactions.log
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

    // Where it was read from; see TransactionSource. Not stored in the
    // log: a file cannot be trusted to say whose it is.
    TransactionSource source = TransactionSource::Unknown;
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

// How long the log's lock is waited for, in milliseconds (5000 unless
// set). For tests: a real lock is held for milliseconds.
void setTransactionLockWait(int milliseconds);

// Replace a started record with what actually happened. If the marker
// is not found the record is appended anyway: losing the outcome is
// worse than duplicating it.
//
// The log is rewritten only while its lock is held (0.1.88). If the
// lock cannot be had in time, the outcome is added at the end with a
// "Finishes:" line naming the started record, and readTransactions
// leaves that started record out: one finished change is shown either
// way, and nothing already in the log is touched.
bool finishTransaction(
    const std::string& path,
    const std::string& marker,
    const TransactionRecord& record
);

// Records that were started and never finished.
std::vector<TransactionRecord> unfinishedTransactions(
    const std::string& path
);

// `source` is put on every record read: the caller knows whose file
// `path` is, the file does not get to say (0.2.0, ledger I3).
std::vector<TransactionRecord> readTransactions(
    const std::string& path,
    TransactionSource source = TransactionSource::Unknown
);

// Where the log lives: a system-wide path when running as root, and a
// per-user one otherwise, so a dry run by an ordinary user still
// leaves a trace they can read.
std::string defaultTransactionLog();

// Every place a record might be, because changes are made under sudo
// and read back without it. Reading only the caller's own file
// reports "no changes recorded" about a machine that was changed.
std::vector<std::string> transactionLogPaths();

// The two logs by name (0.2.0, ledger I3). The machine's is written by
// root only. The person's own is empty when there is no home folder.
std::string machineTransactionLog();
std::string ownTransactionLog();

// Both logs as one list, oldest first, every record saying which log
// it came from. Where two records carry the same time, the machine's
// comes first.
std::vector<TransactionRecord> readAllTransactions();

// The same for two given files (tests; readAllTransactions is this
// with the two paths above). An empty path is not read.
std::vector<TransactionRecord> readLabelledTransactions(
    const std::string& machineLog,
    const std::string& ownLog
);

// Changes that started and never finished, by whose log says so
// (0.2.0, ledger I3). `machine` is what `nexus doctor` warns about: a
// change root began may have left the system part-way. `own` is the
// rest -- the person's own log, or a source nobody named: shown, never
// counted with the machine's.
struct InterruptedChanges {
    std::vector<TransactionRecord> machine;
    std::vector<TransactionRecord> own;
};

InterruptedChanges interruptedChanges(
    const std::vector<TransactionRecord>& records
);

// Whether a record is the machine's history: read from root's log.
// One from the person's own log, or from nobody knows where, is not
// (0.2.0, ledger I3).
bool isMachineRecord(const TransactionRecord& record);

std::string currentTimestamp();

// When this machine last started, in the same form as currentTimestamp
// (from /proc/stat's btime). Empty if unknown.
std::string bootTimestamp();

// Whether a `nexus update` that was "staged" has taken effect: the
// machine has restarted since it was written, and the Nexus running now
// is that version or newer. The record says "Nexus 0.1.46" (or
// "Nexus 0.1.46, rebuilt with..."); an update that named no Nexus
// version counts once the machine has restarted. The Asus showed
// "update -> Nexus 0.1.46 [staged]" while running 0.1.46 (13 October).
bool updateTookEffect(
    const TransactionRecord& record,
    const std::string& runningVersion,   // "0.1.46"
    const std::string& bootTime          // bootTimestamp()
);

}
