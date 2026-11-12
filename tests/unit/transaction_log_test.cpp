#include <gtest/gtest.h>

#include <vector>

#include <filesystem>

#include <nexus/system/transaction_log.hpp>

using nexus::system::currentTimestamp;
using nexus::system::readTransactions;
using nexus::system::recordTransaction;
using nexus::system::TransactionRecord;

namespace {

// Every log a test makes, with the lock file the log code puts beside
// it, is removed when the test program ends (0.1.88: the lock files
// used to stay in /tmp).
struct TemporaryLogs {
    std::vector<std::string> paths;
    pid_t owner = ::getpid();
    ~TemporaryLogs() {
        if (::getpid() != owner) return;   // a forked child: not its to remove
        for (const std::string& path : paths) {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(path + ".lock", error);
            std::filesystem::remove(path + ".new", error);
        }
    }
};

std::string temporaryLog() {
    static TemporaryLogs made;
    const std::string path =
        (std::filesystem::temp_directory_path() /
         ("nexus-test-log-" + std::to_string(::getpid()) + "-" +
          std::to_string(std::rand()))).string();
    made.paths.push_back(path);
    return path;
}

}

TEST(TransactionLogTest, RecordsAndReadsBack) {
    const std::string path = temporaryLog();

    TransactionRecord record;

    record.when = "2026-09-01T19:18:20Z";
    record.request = "gaming";
    record.resolved = "steam-installer";
    record.packages = {"steam-installer", "steam-libs"};
    record.outcome = "applied";
    record.succeeded = true;

    ASSERT_TRUE(recordTransaction(path, record));

    const auto records = readTransactions(path);

    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].request, "gaming");
    EXPECT_EQ(records[0].resolved, "steam-installer");
    EXPECT_EQ(records[0].packages.size(), 2u);
    EXPECT_TRUE(records[0].succeeded);

    std::error_code error;
    std::filesystem::remove(path, error);
}

TEST(TransactionLogTest, AppendsRatherThanReplaces) {
    const std::string path = temporaryLog();

    TransactionRecord first;
    first.when = "2026-09-01T10:00:00Z";
    first.request = "one";
    first.outcome = "applied";
    first.succeeded = true;

    TransactionRecord second = first;
    second.when = "2026-09-01T11:00:00Z";
    second.request = "two";

    ASSERT_TRUE(recordTransaction(path, first));
    ASSERT_TRUE(recordTransaction(path, second));

    const auto records = readTransactions(path);

    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].request, "one");
    EXPECT_EQ(records[1].request, "two");

    std::error_code error;
    std::filesystem::remove(path, error);
}

// A log that says a failed transaction added fourteen packages is
// worse than no log: somebody debugging the machine would go looking
// for them.
TEST(TransactionLogTest, AFailedChangeSaysAttemptedNotAdded) {
    const std::string path = temporaryLog();

    TransactionRecord record;

    record.when = "2026-09-01T19:18:41Z";
    record.request = "lolcat";
    record.packages = {"lolcat", "ruby"};
    record.outcome = "failed";
    record.exitCode = 100;
    record.succeeded = false;

    ASSERT_TRUE(recordTransaction(path, record));

    const auto records = readTransactions(path);

    ASSERT_EQ(records.size(), 1u);
    EXPECT_FALSE(records[0].succeeded);
    EXPECT_EQ(records[0].packages.size(), 2u);
    EXPECT_EQ(records[0].exitCode, 100);

    std::error_code error;
    std::filesystem::remove(path, error);
}

TEST(TransactionLogTest, ReadingAMissingLogIsEmpty) {
    EXPECT_TRUE(readTransactions("/nonexistent/nexus.log").empty());
}

TEST(TransactionLogTest, WritingToNowhereFails) {
    TransactionRecord record;

    record.when = currentTimestamp();
    record.request = "x";

    EXPECT_FALSE(recordTransaction("", record));
}

TEST(TransactionLogTest, TimestampsAreIsoUtc) {
    const std::string when = currentTimestamp();

    ASSERT_EQ(when.size(), 20u);
    EXPECT_EQ(when[4], '-');
    EXPECT_EQ(when[10], 'T');
    EXPECT_EQ(when.back(), 'Z');
}

// A log that says a removal added a package is worse than no log.
TEST(TransactionLogTest, ARemovalSaysRemoved) {
    const std::string path = temporaryLog();

    TransactionRecord record;

    record.kind = nexus::system::TransactionKind::Remove;
    record.when = "2026-09-01T19:57:05Z";
    record.request = "remove tree";
    record.resolved = "tree";
    record.packages = {"tree"};
    record.outcome = "applied";
    record.succeeded = true;

    ASSERT_TRUE(recordTransaction(path, record));

    const auto records = readTransactions(path);

    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].kind, nexus::system::TransactionKind::Remove);
    EXPECT_TRUE(records[0].succeeded);
    EXPECT_EQ(records[0].packages.size(), 1u);

    std::error_code error;
    std::filesystem::remove(path, error);
}

TEST(TransactionLogTest, AnInstallStillSaysAdded) {
    const std::string path = temporaryLog();

    TransactionRecord record;

    record.kind = nexus::system::TransactionKind::Install;
    record.when = "2026-09-01T19:18:20Z";
    record.request = "tree";
    record.packages = {"tree"};
    record.outcome = "applied";
    record.succeeded = true;

    ASSERT_TRUE(recordTransaction(path, record));

    const auto records = readTransactions(path);

    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].kind, nexus::system::TransactionKind::Install);

    std::error_code error;
    std::filesystem::remove(path, error);
}

// "update -> Nexus 0.1.46 [staged]" while 0.1.46 ran (Asus, 13 October).
TEST(TransactionLogTest, AStagedUpdateTakesEffectAfterTheRestart) {
    nexus::system::TransactionRecord update;
    update.request = "update";
    update.resolved = "Nexus 0.1.46";
    update.outcome = "staged";
    update.succeeded = true;
    update.when = "2026-10-11T15:55:53Z";

    using nexus::system::updateTookEffect;
    // Restarted since, running that version or a newer one.
    EXPECT_TRUE(updateTookEffect(update, "0.1.46", "2026-10-13T20:40:00Z"));
    EXPECT_TRUE(updateTookEffect(update, "0.1.50", "2026-10-13T20:40:00Z"));
    EXPECT_TRUE(updateTookEffect(update, "0.2.0", "2026-10-13T20:40:00Z"));
    // Not restarted yet.
    EXPECT_FALSE(updateTookEffect(update, "0.1.46", "2026-10-10T08:00:00Z"));
    // Restarted, but into the old version (the boot menu's previous one).
    EXPECT_FALSE(updateTookEffect(update, "0.1.45", "2026-10-13T20:40:00Z"));
    // A weekly rebuild names its version too.
    update.resolved = "Nexus 0.1.46, rebuilt with Fedora's latest updates";
    EXPECT_TRUE(updateTookEffect(update, "0.1.46", "2026-10-13T20:40:00Z"));
    // Apps only: no version to compare, restarting is enough.
    update.resolved = "3 apps";
    EXPECT_TRUE(updateTookEffect(update, "0.1.46", "2026-10-13T20:40:00Z"));
    // Not an update, or not staged: untouched.
    update.request = "gcc-c++";
    EXPECT_FALSE(updateTookEffect(update, "0.1.46", "2026-10-13T20:40:00Z"));
    update.request = "update";
    update.outcome = "failed";
    EXPECT_FALSE(updateTookEffect(update, "0.1.46", "2026-10-13T20:40:00Z"));
    // Unknown boot time: say nothing new.
    update.outcome = "staged";
    EXPECT_FALSE(updateTookEffect(update, "0.1.46", ""));
}

// ---- 0.1.87 (audit) -------------------------------------------------

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fstream>
#include <vector>

using nexus::system::beginTransaction;
using nexus::system::finishTransaction;

// A request is whatever was typed. A line break in it used to start a
// new line in the log, where "Outcome: applied" could be made up, or a
// blank line could cut the record in two.
TEST(TransactionLogTest, ALineBreakInAFieldDoesNotMakeASecondRecord) {
    const std::string path = temporaryLog();

    TransactionRecord record;
    record.when = "2026-11-12T10:00:00Z";
    record.request = "install x\n\nWhen: 2020-01-01T00:00:00Z\nRequest: forged\nOutcome: applied";
    record.resolved = "a\rb";
    record.outcome = "failed";
    record.succeeded = false;
    record.exitCode = 1;

    ASSERT_TRUE(recordTransaction(path, record));
    const std::string marker = beginTransaction(path, record);
    ASSERT_FALSE(marker.empty());
    ASSERT_TRUE(finishTransaction(path, marker, record));

    const auto records = readTransactions(path);
    ASSERT_EQ(records.size(), 2u);
    for (const auto& read : records) {
        EXPECT_EQ(read.when, "2026-11-12T10:00:00Z");
        EXPECT_EQ(read.outcome, "failed");
        EXPECT_EQ(read.request.find('\n'), std::string::npos);
        EXPECT_NE(read.request.find("forged"), std::string::npos);   // kept, on one line
        EXPECT_EQ(read.resolved.find('\r'), std::string::npos);
    }

    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(path + ".lock", error);
}

// Finishing a record rewrites the log. It used to do that in place
// (truncate, then write): two nexus processes at once -- the Shop and a
// terminal -- could each write back what they had read, and whatever
// the other had added in between was gone (25 of 150 records survived
// this test before the fix).
TEST(TransactionLogTest, FinishingAtTheSameTimeLosesNothing) {
    const std::string path = temporaryLog();
    const int workers = 6;
    const int each = 25;

    std::vector<pid_t> children;
    for (int w = 0; w < workers; ++w) {
        const pid_t child = ::fork();
        ASSERT_GE(child, 0);
        if (child == 0) {
            for (int i = 0; i < each; ++i) {
                TransactionRecord record;
                // One process, one transaction at a time, is what the
                // marker (time + pid) is built for; the time differs
                // per record here because these run within one second.
                record.when = "2026-11-12T10:" + std::to_string(10 + w) + ":" + std::to_string(10 + i) + "Z";
                record.request = "job " + std::to_string(w) + "-" + std::to_string(i);
                record.resolved = "x";
                const std::string marker = beginTransaction(path, record);
                record.outcome = "applied";
                record.succeeded = true;
                if (marker.empty() || !finishTransaction(path, marker, record)) ::_exit(1);
            }
            ::_exit(0);
        }
        children.push_back(child);
    }
    for (pid_t child : children) {
        int status = 0;
        ASSERT_EQ(::waitpid(child, &status, 0), child);
        EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    const auto records = readTransactions(path);
    EXPECT_EQ(records.size(), static_cast<std::size_t>(workers * each));
    for (const auto& record : records) {
        EXPECT_EQ(record.outcome, "applied") << record.request;
    }
    EXPECT_TRUE(nexus::system::unfinishedTransactions(path).empty());

    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(path + ".lock", error);
}

// Rewriting keeps the file's permissions, leaves no half-written copy
// behind, and the lock file is the owner's alone: a lock others could
// open is a lock others could hold, and root would wait on it.
TEST(TransactionLogTest, FinishingKeepsPermissionsAndLeavesNoScraps) {
    const std::string path = temporaryLog();

    TransactionRecord record;
    record.when = "2026-11-12T11:00:00Z";
    record.request = "x";
    record.resolved = "y";
    const std::string marker = beginTransaction(path, record);
    ASSERT_FALSE(marker.empty());
    ASSERT_EQ(::chmod(path.c_str(), 0600), 0);

    record.outcome = "applied";
    record.succeeded = true;
    ASSERT_TRUE(finishTransaction(path, marker, record));

    struct stat info {};
    ASSERT_EQ(::stat(path.c_str(), &info), 0);
    EXPECT_EQ(info.st_mode & 0777, 0600u);
    EXPECT_FALSE(std::filesystem::exists(path + ".new"));
    ASSERT_EQ(::stat((path + ".lock").c_str(), &info), 0);
    EXPECT_EQ(info.st_mode & 0077, 0u);

    const auto records = readTransactions(path);
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].outcome, "applied");

    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(path + ".lock", error);
}

// ---- 0.1.88 (review of patch 113) -----------------------------------

#include <sys/file.h>

#include <chrono>
#include <csignal>
#include <random>
#include <set>

namespace {

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

ino_t inodeOf(const std::string& path) {
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0 ? info.st_ino : 0;
}

void removeLog(const std::string& path) {
    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(path + ".lock", error);
    std::filesystem::remove(path + ".new", error);
}

TransactionRecord finished(const std::string& request) {
    TransactionRecord record;
    record.when = "2026-11-12T12:00:00Z";
    record.request = request;
    record.resolved = "x";
    record.outcome = "applied";
    record.succeeded = true;
    record.packages = {"p"};
    return record;
}

// A process that takes the log's lock and keeps it until killed.
pid_t holdTheLock(const std::string& path) {
    int ready[2];
    if (::pipe(ready) != 0) return -1;
    const pid_t child = ::fork();
    if (child == 0) {
        ::close(ready[0]);
        const int fd = ::open((path + ".lock").c_str(), O_RDWR | O_CREAT, 0600);
        if (fd < 0 || ::flock(fd, LOCK_EX) != 0) ::_exit(1);
        (void)!::write(ready[1], "x", 1);
        for (;;) ::pause();
    }
    ::close(ready[1]);
    char byte = 0;
    (void)!::read(ready[0], &byte, 1);
    ::close(ready[0]);
    return child;
}

}  // namespace

// Someone else holds the lock and does not let go. 0.1.87 waited five
// seconds and then rewrote the log anyway. Now: no rewrite (the file is
// the same file afterwards, with everything that was in it), the
// outcome is added at the end, and it reads as one finished change.
TEST(TransactionLogTest, WithoutTheLockTheLogIsAddedToNeverRewritten) {
    const std::string path = temporaryLog();
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(recordTransaction(path, finished("earlier " + std::to_string(i))));
    }

    nexus::system::setTransactionLockWait(200);
    const pid_t holder = holdTheLock(path);
    ASSERT_GT(holder, 0);

    TransactionRecord record = finished("while locked");
    const auto start = std::chrono::steady_clock::now();
    const std::string marker = beginTransaction(path, record);
    ASSERT_FALSE(marker.empty());
    const std::string before = slurp(path);
    const ino_t inode = inodeOf(path);

    ASSERT_TRUE(finishTransaction(path, marker, record));
    const auto took = std::chrono::steady_clock::now() - start;
    EXPECT_LT(took, std::chrono::seconds(3));          // bounded, twice

    const std::string after = slurp(path);
    EXPECT_EQ(inodeOf(path), inode);                    // not replaced
    EXPECT_EQ(after.compare(0, before.size(), before), 0);   // only added to
    EXPECT_NE(after.find("Finishes: " + marker), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(path + ".new"));

    auto records = readTransactions(path);
    ASSERT_EQ(records.size(), 4u);
    EXPECT_EQ(records[3].request, "while locked");
    EXPECT_EQ(records[3].outcome, "applied");
    EXPECT_TRUE(records[3].succeeded);
    EXPECT_TRUE(nexus::system::unfinishedTransactions(path).empty());

    // The holder goes away: the next change is rewritten in place as
    // usual, and what was added without the lock is still there.
    ::kill(holder, SIGKILL);
    ::waitpid(holder, nullptr, 0);
    nexus::system::setTransactionLockWait(5000);

    TransactionRecord next = finished("after");
    next.when = "2026-11-12T12:00:01Z";
    const std::string nextMarker = beginTransaction(path, next);
    ASSERT_TRUE(finishTransaction(path, nextMarker, next));
    EXPECT_NE(inodeOf(path), inode);
    EXPECT_EQ(slurp(path).find("Marker: " + nextMarker), std::string::npos);
    // (The "started" half from the locked-out change stays in the file,
    // retired by its Finishes line; nothing is rewritten just to tidy.)

    records = readTransactions(path);
    ASSERT_EQ(records.size(), 5u);
    EXPECT_EQ(records[4].request, "after");
    EXPECT_TRUE(nexus::system::unfinishedTransactions(path).empty());

    removeLog(path);
}

// Writers that have the lock and writers that do not, at the same time:
// the worst case for a log that is sometimes replaced. Nothing that was
// there is lost, and every change ends up finished.
TEST(TransactionLogTest, LockedAndUnlockedWritersTogetherLoseNothing) {
    const std::string path = temporaryLog();
    const int earlier = 40;
    for (int i = 0; i < earlier; ++i) {
        ASSERT_TRUE(recordTransaction(path, finished("earlier " + std::to_string(i))));
    }

    const int workers = 8;
    const int each = 30;
    std::vector<pid_t> children;
    for (int w = 0; w < workers; ++w) {
        const pid_t child = ::fork();
        ASSERT_GE(child, 0);
        if (child == 0) {
            // Odd workers never wait for the lock: when it is busy they
            // go without, as after a timeout.
            nexus::system::setTransactionLockWait(w % 2 == 1 ? 0 : 5000);
            for (int i = 0; i < each; ++i) {
                TransactionRecord record = finished("job " + std::to_string(w) + "-" + std::to_string(i));
                record.when = "2026-11-12T13:" + std::to_string(10 + w) + ":" + std::to_string(10 + i) + "Z";
                const std::string marker = beginTransaction(path, record);
                if (marker.empty() || !finishTransaction(path, marker, record)) ::_exit(1);
            }
            ::_exit(0);
        }
        children.push_back(child);
    }
    for (pid_t child : children) {
        int status = 0;
        ASSERT_EQ(::waitpid(child, &status, 0), child);
        EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    std::set<std::string> applied;
    int earlierSeen = 0;
    for (const auto& record : readTransactions(path)) {
        EXPECT_EQ(record.outcome, "applied") << record.request;
        if (record.request.rfind("earlier ", 0) == 0) ++earlierSeen;
        if (record.request.rfind("job ", 0) == 0) applied.insert(record.request);
    }
    EXPECT_EQ(earlierSeen, earlier);
    EXPECT_EQ(applied.size(), static_cast<std::size_t>(workers * each));
    EXPECT_TRUE(nexus::system::unfinishedTransactions(path).empty());

    removeLog(path);
}

// A process killed at any moment while it is changing the log leaves a
// log that reads, with everything that was in it and everything that
// was reported finished. (A kill, not a power cut: that needs a real
// disk and cannot be tested here.)
TEST(TransactionLogTest, KilledWhileWritingLeavesAWholeLog) {
    const std::string path = temporaryLog();
    const int earlier = 300;   // enough that a rewrite takes a moment
    for (int i = 0; i < earlier; ++i) {
        ASSERT_TRUE(recordTransaction(path, finished("earlier " + std::to_string(i))));
    }

    std::mt19937 random(20261112);
    std::set<std::string> reported;

    // At least 25 kills; more on a slow or busy machine, until the
    // children have between them finished a fair number of changes.
    for (int round = 0; round < 80 && (round < 25 || reported.size() < 25); ++round) {
        int done[2];
        ASSERT_EQ(::pipe(done), 0);
        const pid_t child = ::fork();
        ASSERT_GE(child, 0);
        if (child == 0) {
            ::close(done[0]);
            for (int i = 0;; ++i) {
                const std::string name = "round " + std::to_string(round) + "-" + std::to_string(i);
                TransactionRecord record = finished(name);
                record.when = "2026-11-12T14:" + std::to_string(10 + round) + ":" + std::to_string(i);
                const std::string marker = beginTransaction(path, record);
                if (marker.empty() || !finishTransaction(path, marker, record)) ::_exit(1);
                const std::string line = name + "\n";
                if (::write(done[1], line.data(), line.size()) < 0) ::_exit(1);
            }
        }
        ::close(done[1]);
        ::usleep(500 + random() % 12000);
        ::kill(child, SIGKILL);
        ::waitpid(child, nullptr, 0);

        std::string said;
        char buffer[4096];
        ssize_t n = 0;
        while ((n = ::read(done[0], buffer, sizeof buffer)) > 0) said.append(buffer, static_cast<std::size_t>(n));
        ::close(done[0]);
        std::istringstream lines(said);
        std::string line;
        while (std::getline(lines, line)) reported.insert(line);

        // After every kill: all of the old history, every change that
        // was reported finished, and nothing that is not a record.
        int earlierSeen = 0;
        std::set<std::string> appliedNow;
        for (const auto& record : readTransactions(path)) {
            ASSERT_FALSE(record.when.empty());
            ASSERT_FALSE(record.request.empty());
            if (record.request.rfind("earlier ", 0) == 0) ++earlierSeen;
            if (record.outcome == "applied") appliedNow.insert(record.request);
        }
        ASSERT_EQ(earlierSeen, earlier) << "after kill " << round;
        for (const std::string& name : reported) {
            ASSERT_EQ(appliedNow.count(name), 1u) << name << " after kill " << round;
        }
        // At most the one change that was in flight is unfinished.
        ASSERT_LE(nexus::system::unfinishedTransactions(path).size(), static_cast<std::size_t>(round + 1));
    }
    EXPECT_GT(reported.size(), 0u);    // the children did get work done

    removeLog(path);
}

// A record cut short at the end of the log (a full disk, a kill in the
// middle of a write) does not swallow the next one.
TEST(TransactionLogTest, ARecordAddedAfterATornOneStandsOnItsOwn) {
    for (const std::string& tail : {std::string("Request: cut sho"), std::string("Request: cut short\n")}) {
        const std::string path = temporaryLog();
        {
            std::ofstream out(path);
            out << "When: 2026-11-12T10:00:00Z\nRequest: whole\nResolved: x\nOutcome: applied\nExit: 0\nAdded: p\n\n"
                << "When: 2026-11-12T10:00:01Z\n" << tail;
        }
        ASSERT_TRUE(recordTransaction(path, finished("after the torn one")));

        const auto records = readTransactions(path);
        ASSERT_EQ(records.size(), 3u);
        EXPECT_EQ(records[0].request, "whole");
        EXPECT_EQ(records[1].when, "2026-11-12T10:00:01Z");
        EXPECT_EQ(records[2].request, "after the torn one");
        EXPECT_EQ(records[2].when, "2026-11-12T12:00:00Z");
        EXPECT_EQ(records[2].outcome, "applied");
        removeLog(path);
    }
}

// How a finished record that was added after its "started" half reads.
TEST(TransactionLogTest, AFinishesLineRetiresTheStartedHalf) {
    const std::string path = temporaryLog();
    {
        std::ofstream out(path);
        out << "When: 2026-11-12T10:00:00Z\nMarker: m1\nRequest: one\nResolved: x\nOutcome: started\nExit: 0\n\n"
            << "When: 2026-11-12T10:00:01Z\nMarker: m2\nRequest: two\nResolved: x\nOutcome: started\nExit: 0\n\n"
            << "When: 2026-11-12T10:00:00Z\nFinishes: m1\nRequest: one\nResolved: x\nOutcome: applied\nExit: 0\nAdded: p\n\n"
            // The same finished record a second time, and the same
            // started half a second time: each counts once.
            << "When: 2026-11-12T10:00:00Z\nFinishes: m1\nRequest: one\nResolved: x\nOutcome: applied\nExit: 0\nAdded: p\n\n"
            << "When: 2026-11-12T10:00:01Z\nMarker: m2\nRequest: two\nResolved: x\nOutcome: started\nExit: 0\n\n";
    }
    const auto records = readTransactions(path);
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].request, "two");
    EXPECT_TRUE(records[0].unfinished);
    EXPECT_EQ(records[1].request, "one");
    EXPECT_EQ(records[1].outcome, "applied");
    EXPECT_TRUE(records[1].succeeded);

    const auto unfinished = nexus::system::unfinishedTransactions(path);
    ASSERT_EQ(unfinished.size(), 1u);
    EXPECT_EQ(unfinished[0].request, "two");

    // Finishing "two" in the ordinary way removes both of its halves.
    ASSERT_TRUE(finishTransaction(path, "m2", finished("two")));
    EXPECT_TRUE(nexus::system::unfinishedTransactions(path).empty());
    EXPECT_EQ(readTransactions(path).size(), 2u);

    removeLog(path);
}
