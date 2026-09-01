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
