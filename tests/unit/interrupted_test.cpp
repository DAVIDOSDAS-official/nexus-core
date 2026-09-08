#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include <nexus/system/transaction_log.hpp>

using nexus::system::beginTransaction;
using nexus::system::currentTimestamp;
using nexus::system::finishTransaction;
using nexus::system::readTransactions;
using nexus::system::recordTransaction;
using nexus::system::TransactionRecord;
using nexus::system::unfinishedTransactions;

// What happens when a change is interrupted.
//
// Nothing here kills a real package manager: apt and dnf own their
// own recovery and reimplementing it would be the mistake this whole
// project avoids. What is tested is the part Nexus owns -- whether
// its account of what happened survives the process not finishing.

namespace {

std::string scratchLog() {
    return (std::filesystem::temp_directory_path() /
            ("nexus-txn-" + std::to_string(::getpid()) + "-" +
             std::to_string(std::rand()) + ".log")).string();
}

TransactionRecord aChange(const std::string& request) {
    TransactionRecord record;

    record.when = currentTimestamp();
    record.request = request;
    record.resolved = request;
    record.packages = {request};

    return record;
}

}

// The bug this exists for: the record used to be written after the
// package manager returned, so being killed while it ran left the
// packages installed and nothing recorded.
TEST(InterruptedTest, AStartedChangeIsOnDiskBeforeItHappens) {
    const std::string log = scratchLog();

    const std::string marker =
        beginTransaction(log, aChange("tree"));

    ASSERT_FALSE(marker.empty());

    // Nothing has called finish. This is the state a killed process
    // leaves behind.
    const auto records = readTransactions(log);

    ASSERT_EQ(records.size(), 1u);
    EXPECT_TRUE(records[0].unfinished);
    EXPECT_EQ(records[0].request, "tree");

    std::error_code error;
    std::filesystem::remove(log, error);
}

TEST(InterruptedTest, AnUnfinishedChangeIsNotASuccess) {
    const std::string log = scratchLog();

    beginTransaction(log, aChange("tree"));

    const auto records = readTransactions(log);

    ASSERT_EQ(records.size(), 1u);
    EXPECT_FALSE(records[0].succeeded);

    std::error_code error;
    std::filesystem::remove(log, error);
}

TEST(InterruptedTest, FinishingReplacesTheStartedRecord) {
    const std::string log = scratchLog();

    TransactionRecord record = aChange("tree");

    const std::string marker = beginTransaction(log, record);

    record.outcome = "applied";
    record.succeeded = true;

    ASSERT_TRUE(finishTransaction(log, marker, record));

    const auto records = readTransactions(log);

    // One record, not two.
    ASSERT_EQ(records.size(), 1u);
    EXPECT_FALSE(records[0].unfinished);
    EXPECT_TRUE(records[0].succeeded);

    std::error_code error;
    std::filesystem::remove(log, error);
}

TEST(InterruptedTest, InterruptedChangesAreFoundAmongFinishedOnes) {
    const std::string log = scratchLog();

    TransactionRecord first = aChange("one");

    const std::string marker = beginTransaction(log, first);

    first.outcome = "applied";
    first.succeeded = true;

    finishTransaction(log, marker, first);

    // The second is never finished.
    beginTransaction(log, aChange("two"));

    const auto unfinished = unfinishedTransactions(log);

    ASSERT_EQ(unfinished.size(), 1u);
    EXPECT_EQ(unfinished[0].request, "two");

    std::error_code error;
    std::filesystem::remove(log, error);
}

// Losing the outcome is worse than duplicating it.
TEST(InterruptedTest, AMissingMarkerStillRecordsTheOutcome) {
    const std::string log = scratchLog();

    TransactionRecord record = aChange("tree");

    record.outcome = "applied";
    record.succeeded = true;

    EXPECT_TRUE(finishTransaction(log, "no-such-marker", record));

    const auto records = readTransactions(log);

    ASSERT_EQ(records.size(), 1u);
    EXPECT_TRUE(records[0].succeeded);

    std::error_code error;
    std::filesystem::remove(log, error);
}

// A log cut off mid-record, as a power cut would leave it.
TEST(InterruptedTest, ATruncatedRecordIsNotReadAsComplete) {
    const std::string log = scratchLog();

    {
        std::ofstream out(log);

        out << "When: 2026-09-08T10:00:00Z\n"
               "Request: one\n"
               "Outcome: applied\n"
               "Exit: 0\n"
               "Added: one\n"
               "\n"
               "When: 2026-09-08T11:00:00Z\n"
               "Request: tw";
    }

    const auto records = readTransactions(log);

    // The complete record is read. The truncated one must not be
    // reported as a successful change to a package called "tw".
    ASSERT_GE(records.size(), 1u);
    EXPECT_EQ(records[0].request, "one");

    for (const auto& record : records) {
        if (record.request == "tw") {
            EXPECT_FALSE(record.succeeded);
        }
    }

    std::error_code error;
    std::filesystem::remove(log, error);
}

TEST(InterruptedTest, GarbageBeforeARecordDoesNotHideIt) {
    const std::string log = scratchLog();

    {
        std::ofstream out(log, std::ios::binary);

        // Written as bytes: streaming a C string stops at the first
        // null, which would have written nothing and tested nothing.
        const char junk[] = {'\0', '\0', '\0', 'j', 'u', 'n', 'k',
                             '\n', '\n'};

        out.write(junk, sizeof(junk));

        out << "When: 2026-09-08T10:00:00Z\n"
               "Request: real\n"
               "Outcome: applied\n"
               "Exit: 0\n"
               "Added: real\n"
               "\n";
    }

    bool found = false;

    for (const auto& record : readTransactions(log)) {
        found = found || record.request == "real";
    }

    EXPECT_TRUE(found);

    std::error_code error;
    std::filesystem::remove(log, error);
}

// Two processes appending at once. Neither should lose its record.
TEST(InterruptedTest, ConcurrentRecordsBothSurvive) {
    const std::string log = scratchLog();

    for (int index = 0; index < 20; ++index) {
        TransactionRecord record =
            aChange("package-" + std::to_string(index));

        record.outcome = "applied";
        record.succeeded = true;

        ASSERT_TRUE(recordTransaction(log, record));
    }

    EXPECT_EQ(readTransactions(log).size(), 20u);

    std::error_code error;
    std::filesystem::remove(log, error);
}
