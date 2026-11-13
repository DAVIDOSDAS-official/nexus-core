#include <nexus/system/transaction_log.hpp>

#include <algorithm>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <fcntl.h>
#include <grp.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <cerrno>
#include <vector>
#include <set>
#include <atomic>
#include <iterator>

#include <nexus/system/control_file.hpp>

namespace nexus::system {

namespace {

// One line, whatever the text holds: the log is "Key: value" lines with
// a blank line between records, so a line break inside a value would
// start a new key -- or a new record (0.1.87, audit).
std::string oneLine(std::string text) {
    for (char& c : text) {
        if (c == '\n' || c == '\r' || c == '\0') c = ' ';
    }
    return text;
}

// How long to wait for the lock, in milliseconds. A lock is held for a
// read and a write, so five seconds means its holder is stopped or
// stuck. Changed only by tests (setTransactionLockWait).
std::atomic<int> lockWaitMilliseconds{5000};

// Held while the log is added to or rewritten, so two nexus processes
// (the Shop and a terminal) cannot each write back what they read and
// lose what the other added. The lock is its own file beside the log
// because the log itself is replaced by rename. Owner-only: a lock file
// others can open is one others can hold, and root would wait on it.
//
// held() says whether the lock was actually taken (0.1.88, review of
// patch 113). 0.1.87 waited five seconds and then carried on as if it
// had it, read-modify-replace included: exactly the unlocked rewrite
// the lock exists to prevent. Now the caller asks, and without the
// lock the log is only ever added to, never rewritten.
class LogLock {
public:
    explicit LogLock(const std::string& path) {
        fd_ = ::open((path + ".lock").c_str(),
                     O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd_ < 0) return;
        const int wait = lockWaitMilliseconds.load();
        for (int waited = 0;; waited += 20) {
            if (::flock(fd_, LOCK_EX | LOCK_NB) == 0) {
                held_ = true;
                return;
            }
            // Busy, or interrupted: try again until the time is up.
            // Anything else (a filesystem without locks) will not get
            // better by waiting.
            if (errno != EWOULDBLOCK && errno != EINTR) return;
            if (waited >= wait) return;
            ::usleep(20 * 1000);
        }
    }
    ~LogLock() {
        if (fd_ >= 0) ::close(fd_);   // closing releases it
    }
    LogLock(const LogLock&) = delete;
    LogLock& operator=(const LogLock&) = delete;

    bool held() const { return held_; }

private:
    int fd_ = -1;
    bool held_ = false;
};

// The folder's own record of what it holds, on the disk: a new file or
// a rename is not durable until the folder is synced too (0.1.88).
void syncFolderOf(const std::string& path) {
    const std::filesystem::path file(path);
    const std::string folder =
        file.has_parent_path() ? file.parent_path().string() : std::string(".");
    const int fd = ::open(folder.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return;
    ::fsync(fd);
    ::close(fd);
}

bool writeAll(int fd, const std::string& text) {
    std::size_t done = 0;
    while (done < text.size()) {
        const ssize_t n = ::write(fd, text.data() + done, text.size() - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        done += static_cast<std::size_t>(n);
    }
    return true;
}

// Added at the end, and on the disk before this returns: a record still
// in a buffer when the power goes is a record that was never written.
// Safe with or without the lock (0.1.88):
//   - one write() with O_APPEND, so two appenders never interleave;
//   - if the log was replaced (renamed over) between the open and the
//     write, the write went into the file that is no longer the log;
//     that is noticed -- the path now names another file -- and the
//     record is added to the new one;
//   - a log whose last record was cut short (a full disk, a kill in
//     the middle of a write) is closed off with a blank line first, so
//     the new record does not run into the broken one.
// Who may read the log (0.1.91, round-4 audit L-item). It says what
// was installed and removed, and when; that is nobody else's business
// on a shared machine. Owner root, group the administrators' group
// (wheel on Fedora, sudo on Debian and Ubuntu), mode 0640: the
// people who could run nexus with sudo can still read `nexus history`
// without it, and other accounts cannot. Applied when root writes,
// so a log made 0644 by an older nexus is put right on its next
// write. Permissions are only ever taken away (a 0600 log stays
// 0600); a log written by an ordinary user only loses its "others"
// bits.
void restrictLog(int fd) {
    struct stat info {};
    if (::fstat(fd, &info) != 0) return;
    if (::geteuid() == 0) {
        for (const char* name : {"wheel", "sudo", "admin"}) {
            if (const struct group* found = ::getgrnam(name)) {
                if (info.st_gid != found->gr_gid) {
                    [[maybe_unused]] const int ignored = ::fchown(fd, 0, found->gr_gid);
                }
                break;
            }
        }
    }
    // Only ever taken away: a log someone made 0600 stays 0600.
    const mode_t wanted = (info.st_mode & 0777) & 0640;
    if ((info.st_mode & 0777) != wanted) {
        [[maybe_unused]] const int ignored = ::fchmod(fd, wanted);
    }
}

bool appendToLog(const std::string& path, const std::string& text) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        const bool existed = ::access(path.c_str(), F_OK) == 0;
        const int fd = ::open(path.c_str(),
                              O_RDWR | O_APPEND | O_CREAT | O_CLOEXEC, 0640);
        if (fd < 0) return false;
        restrictLog(fd);

        std::string whole;
        struct stat before {};
        if (::fstat(fd, &before) == 0 && before.st_size > 0) {
            char tail[2] = {'\n', '\n'};
            const off_t from = before.st_size >= 2 ? before.st_size - 2 : 0;
            const ssize_t got = ::pread(fd, tail, 2, from);
            if (got == 2 && !(tail[0] == '\n' && tail[1] == '\n')) {
                whole = tail[1] == '\n' ? "\n" : "\n\n";
            } else if (got == 1 && tail[0] != '\n') {
                whole = "\n\n";
            }
        }
        whole += text;

        bool ok = writeAll(fd, whole);
        ok = ::fsync(fd) == 0 && ok;

        // Still the log? (Replaced by a rewrite while this was writing.)
        struct stat mine {}, named {};
        const bool sameFile = ::fstat(fd, &mine) == 0 &&
                              ::stat(path.c_str(), &named) == 0 &&
                              mine.st_ino == named.st_ino &&
                              mine.st_dev == named.st_dev;
        ok = ::close(fd) == 0 && ok;
        if (!ok) return false;
        if (!existed) syncFolderOf(path);
        if (sameFile) return true;
        // Otherwise: again, into the file that is the log now.
    }
    return false;
}

// What a log looked like when it was read: enough to tell, before it is
// replaced, whether anyone has added to it since.
struct LogSnapshot {
    int fd = -1;            // kept open: the old file, even once renamed over
    off_t size = 0;
    std::string contents;
};

bool readLog(const std::string& path, LogSnapshot& snapshot) {
    snapshot.fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (snapshot.fd < 0) return false;
    snapshot.contents.clear();
    char buffer[65536];
    while (true) {
        const ssize_t n = ::read(snapshot.fd, buffer, sizeof buffer);
        if (n > 0) {
            snapshot.contents.append(buffer, static_cast<std::size_t>(n));
        } else if (n == 0) {
            break;
        } else if (errno != EINTR) {
            ::close(snapshot.fd);
            snapshot.fd = -1;
            return false;
        }
    }
    snapshot.size = static_cast<off_t>(snapshot.contents.size());
    return true;
}

// The whole log replaced in one step: written beside it, synced, then
// renamed over it. A crash, a full disk or a kill at any moment leaves
// either the old log or the new one -- before 0.1.87 the log was
// emptied first and written second, and everything in it could be lost.
//
// Only ever called with the lock held. Even so, a writer without the
// lock can exist (an older nexus, a lock that could not be had), so
// nothing it adds may be lost either (0.1.88):
//   - if the log grew between the read and the rename, the caller is
//     told (Grew) and reads again;
//   - anything added to the old file after that last look -- in the
//     instant before the rename, or by a writer that had it open -- is
//     copied across to the new log afterwards.
enum class Replaced { Yes, Grew, Failed };

Replaced replaceLog(const std::string& path, const LogSnapshot& old,
                    const std::string& contents) {
    const std::string fresh = path + ".new";
    mode_t mode = 0640;   // see restrictLog: kept, never widened
    struct stat info {};
    if (::fstat(old.fd, &info) == 0) mode = (info.st_mode & 0777) & 0640;

    ::unlink(fresh.c_str());   // a leftover from a run that was killed
    const int fd = ::open(fresh.c_str(),
                          O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, mode);
    if (fd < 0) return Replaced::Failed;
    bool ok = writeAll(fd, contents);
    ok = ::fchmod(fd, mode) == 0 && ok;   // open() is cut down by the umask
    restrictLog(fd);
    ok = ::fsync(fd) == 0 && ok;
    ok = ::close(fd) == 0 && ok;
    if (!ok) {
        ::unlink(fresh.c_str());
        return Replaced::Failed;
    }

    // Added to since it was read? Then what was read is out of date.
    struct stat now {};
    if (::fstat(old.fd, &now) != 0 || now.st_size != old.size) {
        ::unlink(fresh.c_str());
        return Replaced::Grew;
    }

    if (::rename(fresh.c_str(), path.c_str()) != 0) {
        ::unlink(fresh.c_str());
        return Replaced::Failed;
    }
    syncFolderOf(path);

    // The old file is still open here. Whatever reached it after the
    // look above belongs in the log.
    if (::fstat(old.fd, &now) == 0 && now.st_size > old.size) {
        std::string late(static_cast<std::size_t>(now.st_size - old.size), '\0');
        const ssize_t got = ::pread(old.fd, late.data(), late.size(), old.size);
        if (got > 0) {
            late.resize(static_cast<std::size_t>(got));
            appendToLog(path, late);
        }
    }
    return Replaced::Yes;
}

// One record as it is stored. marker: only while it is "started".
// finishes: on a finished record that could not replace its "started"
// half and was added after it instead; readers drop the half it names.
std::string formatRecord(const TransactionRecord& record, const std::string& marker,
                         const std::string& finishes = "") {
    std::ostringstream out;
    out << "When: " << oneLine(record.when) << "\n";
    if (!marker.empty()) out << "Marker: " << oneLine(marker) << "\n";
    if (!finishes.empty()) out << "Finishes: " << oneLine(finishes) << "\n";
    out << "Request: " << oneLine(record.request) << "\n"
        << "Resolved: " << oneLine(record.resolved) << "\n"
        << "Outcome: " << oneLine(record.outcome) << "\n"
        << "Exit: " << record.exitCode << "\n";
    if (!record.packages.empty()) {
        if (!marker.empty() || !record.succeeded) {
            out << "Attempted:";
        } else if (record.kind == TransactionKind::Remove) {
            out << "Removed:";
        } else {
            out << "Added:";
        }
        for (const std::string& name : record.packages) {
            out << " " << oneLine(name);
        }
        out << "\n";
    }
    out << "\n";
    return out.str();
}

}  // namespace

void setTransactionLockWait(int milliseconds) {
    lockWaitMilliseconds.store(milliseconds < 0 ? 0 : milliseconds);
}

std::string currentTimestamp() {
    const std::time_t now = std::time(nullptr);

    std::tm parts{};

    if (gmtime_r(&now, &parts) == nullptr) {
        return "";
    }

    char buffer[32]{};

    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &parts);

    return buffer;
}

std::string defaultTransactionLog() {
    if (::geteuid() == 0) {
        return "/var/lib/nexus/transactions.log";
    }

    if (const char* home = std::getenv("HOME")) {
        return std::string(home) +
               "/.local/state/nexus/transactions.log";
    }

    return "";
}

std::string bootTimestamp() {
    std::ifstream in("/proc/stat");
    std::string word;
    while (in >> word) {
        if (word == "btime") {
            long long seconds = 0;
            if (!(in >> seconds)) {
                return "";
            }
            const std::time_t when = static_cast<std::time_t>(seconds);
            std::tm parts{};
            if (gmtime_r(&when, &parts) == nullptr) {
                return "";
            }
            char buffer[32]{};
            std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &parts);
            return buffer;
        }
    }
    return "";
}

namespace {

// "0.1.46" -> {0, 1, 46}; anything else stops at the first non-digit.
std::vector<long> versionParts(const std::string& text) {
    std::vector<long> parts;
    long current = -1;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            current = (current < 0 ? 0 : current * 10) + (c - '0');
        } else if (c == '.' && current >= 0) {
            parts.push_back(current);
            current = -1;
        } else {
            break;
        }
    }
    if (current >= 0) {
        parts.push_back(current);
    }
    return parts;
}

}  // namespace

bool updateTookEffect(
    const TransactionRecord& record,
    const std::string& runningVersion,
    const std::string& bootTime
) {
    if (record.request != "update" || record.outcome != "staged" ||
        record.unfinished || bootTime.empty() || record.when.empty() ||
        !(record.when < bootTime)) {
        return false;
    }
    const auto at = record.resolved.find("Nexus ");
    if (at == std::string::npos) {
        return true;  // restarted since, and nothing more to compare
    }
    const auto wanted = versionParts(record.resolved.substr(at + 6));
    const auto running = versionParts(runningVersion);
    if (wanted.empty() || running.empty()) {
        return false;
    }
    return !(running < wanted);
}

std::vector<std::string> transactionLogPaths() {
    // Changes are made under sudo and read back without it, so the
    // account of what happened was written to one file and looked
    // for in another. "No changes recorded" was true of the file it
    // read and false of the machine.
    //
    // A tool that changes a system owes an account of what it did,
    // and an account only its author can see is not one.
    std::vector<std::string> paths{"/var/lib/nexus/transactions.log"};

    if (const char* home = std::getenv("HOME")) {
        paths.push_back(std::string(home) +
                        "/.local/state/nexus/transactions.log");
    }

    return paths;
}

std::vector<TransactionRecord> readAllTransactions() {
    std::vector<TransactionRecord> all;

    for (const std::string& path : transactionLogPaths()) {
        for (const TransactionRecord& record :
             readTransactions(path)) {
            all.push_back(record);
        }
    }

    // One machine's history rather than two files', oldest first.
    std::sort(all.begin(), all.end(),
              [](const TransactionRecord& left,
                 const TransactionRecord& right) {
                  return left.when < right.when;
              });

    return all;
}

bool recordTransaction(
    const std::string& path,
    const TransactionRecord& record
) {
    if (path.empty()) {
        return false;
    }

    std::error_code error;

    const std::filesystem::path file(path);

    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), error);
    }

    // With the lock if it can be had; adding is safe without it too
    // (appendToLog says why), so a busy lock never costs a record.
    const LogLock lock(path);
    return appendToLog(path, formatRecord(record, ""));
}

std::string beginTransaction(
    const std::string& path,
    const TransactionRecord& record
) {
    TransactionRecord started = record;

    // The marker has to be unique enough to find again, and this
    // process plus this instant is enough: one process does one
    // transaction at a time.
    const std::string marker =
        started.when + "/" + std::to_string(::getpid());

    started.outcome = "started";
    started.succeeded = false;

    if (path.empty()) {
        return "";
    }

    // The folder may not exist yet (a person's first change): made
    // here, so the "started" half is on disk before the work begins.
    std::error_code error;
    const std::filesystem::path file(path);
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), error);
    }

    // Failing to record is not a reason to refuse the work, but the
    // caller is told so it can say so.
    const LogLock lock(path);
    return appendToLog(path, formatRecord(started, marker)) ? marker : "";
}

bool finishTransaction(
    const std::string& path,
    const std::string& marker,
    const TransactionRecord& record
) {
    if (marker.empty()) {
        return recordTransaction(path, record);
    }

    {
        // Read, change and put back under one lock (released before the
        // fallback below, which takes it itself).
        const LogLock lock(path);

        // Only with the lock in hand (0.1.88, review of patch 113):
        // rewriting without it is how records were lost before 0.1.87.
        // Three rounds: each time the log turns out to have grown
        // since it was read, it is read again.
        for (int round = 0; lock.held() && round < 3; ++round) {
            LogSnapshot old;
            if (!readLog(path, old)) {
                break;
            }

            std::string contents = old.contents;
            const std::string needle = "Marker: " + oneLine(marker) + "\n";
            const std::size_t at = contents.find(needle);
            Replaced outcome = Replaced::Failed;

            if (at != std::string::npos) {
                // Find the stanza around the marker and replace the
                // whole thing.
                std::size_t begin = contents.rfind("\nWhen: ", at);

                begin = begin == std::string::npos ? 0 : begin + 1;

                std::size_t end = contents.find("\n\n", at);

                end = end == std::string::npos ? contents.size() : end + 2;

                const std::string finishedRecord = formatRecord(record, "");

                contents.replace(begin, end - begin, finishedRecord);

                // A "started" half that reached the log twice (added
                // without the lock while the log was being replaced:
                // appendToLog) is finished once and its copies removed.
                std::size_t again = begin + finishedRecord.size();

                while ((again = contents.find(needle, again)) != std::string::npos) {
                    std::size_t from = contents.rfind("\nWhen: ", again);

                    from = from == std::string::npos ? 0 : from + 1;

                    std::size_t to = contents.find("\n\n", again);

                    to = to == std::string::npos ? contents.size() : to + 2;

                    contents.erase(from, to - from);
                    again = from;
                }

                outcome = replaceLog(path, old, contents);
            }

            ::close(old.fd);

            if (outcome == Replaced::Yes) {
                return true;
            }
            if (outcome != Replaced::Grew) {
                break;
            }
        }
    }

    // No lock, no log, no such marker, or the rewrite failed: the
    // outcome is added at the end instead, naming the "started" half it
    // finishes so that readers show one finished change and not an
    // interrupted one beside it. Losing the outcome is worse than
    // duplicating it; rewriting without the lock is worse than both.
    if (path.empty()) {
        return false;
    }
    std::error_code error;
    const std::filesystem::path file(path);
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), error);
    }
    const LogLock lock(path);
    return appendToLog(path, formatRecord(record, "", marker));
}

std::vector<TransactionRecord> unfinishedTransactions(
    const std::string& path
) {
    std::vector<TransactionRecord> unfinished;

    for (const TransactionRecord& record : readTransactions(path)) {
        if (record.unfinished) {
            unfinished.push_back(record);
        }
    }

    return unfinished;
}

std::vector<TransactionRecord> readTransactions(
    const std::string& path
) {
    std::vector<TransactionRecord> records;

    std::ifstream input(path);

    if (!input) {
        return records;
    }

    // A finished record added after its "started" half (it could not
    // replace it: see finishTransaction) names that half's marker. The
    // half is then not an interrupted change, and is left out; a
    // finished record that arrived twice is kept once.
    const std::vector<ControlStanza> stanzas = parseControlStream(input);
    std::set<std::string> finished;

    for (const ControlStanza& stanza : stanzas) {
        if (stanza.has("when") && stanza.has("finishes")) {
            finished.insert(stanza.value("finishes"));
        }
    }

    std::set<std::string> seen;

    for (const ControlStanza& stanza : stanzas) {
        if (!stanza.has("when")) {
            continue;
        }

        if (stanza.value("outcome") == "started" && stanza.has("marker") &&
            finished.count(stanza.value("marker")) != 0) {
            continue;
        }

        if (stanza.has("finishes") &&
            !seen.insert(stanza.value("finishes")).second) {
            continue;
        }

        // The same "started" half twice is one interrupted change.
        if (stanza.value("outcome") == "started" && stanza.has("marker") &&
            !seen.insert("started " + stanza.value("marker")).second) {
            continue;
        }

        TransactionRecord record;

        record.when = stanza.value("when");
        record.request = stanza.value("request");
        record.resolved = stanza.value("resolved");
        record.outcome = stanza.value("outcome");
        record.exitCode = std::atoi(stanza.value("exit").c_str());
        record.unfinished = record.outcome == "started";
        record.succeeded =
            !record.unfinished &&
            (stanza.has("added") || stanza.has("removed"));

        record.kind = stanza.has("removed")
            ? TransactionKind::Remove
            : TransactionKind::Install;

        std::string body = stanza.value("attempted");

        if (stanza.has("added")) {
            body = stanza.value("added");
        } else if (stanza.has("removed")) {
            body = stanza.value("removed");
        }

        std::istringstream names(body);

        std::string name;

        while (names >> name) {
            record.packages.insert(name);
        }

        records.push_back(std::move(record));
    }

    return records;
}

}
