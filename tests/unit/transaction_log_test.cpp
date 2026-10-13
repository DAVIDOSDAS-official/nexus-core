#include <gtest/gtest.h>

#include <filesystem>

#include <nexus/system/transaction_log.hpp>

using nexus::system::currentTimestamp;
using nexus::system::readTransactions;
using nexus::system::recordTransaction;
using nexus::system::TransactionRecord;

namespace {

std::string temporaryLog() {
    return (std::filesystem::temp_directory_path() /
            ("nexus-log-" + std::to_string(::getpid()) + "-" +
             std::to_string(std::rand()))).string();
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
