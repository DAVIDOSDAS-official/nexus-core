#include <gtest/gtest.h>

#include <nexus/system/process.hpp>

using nexus::system::commandExists;
using nexus::system::runCommand;

TEST(ProcessTest, ReadsOutput) {
    const auto result = runCommand("echo hello");

    EXPECT_TRUE(result.ran);
    EXPECT_TRUE(result.ok);
    ASSERT_EQ(result.lines.size(), 1u);
    EXPECT_EQ(result.lines[0], "hello");
}

TEST(ProcessTest, ReportsAFailingExitCode) {
    const auto result = runCommand("exit 3");

    EXPECT_TRUE(result.ran);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.exitCode, 3);
}

// The bug this exists for. fgets returns null at end of input and
// also when interrupted by a signal; treating the second as the first
// stops reading early, on a clean line boundary, so the output looks
// complete. A container listing 39,158 lines was read as 1,457 and
// the missing lines were the ones being looked for.
TEST(ProcessTest, ReadsAllOfALongOutput) {
    const auto result = runCommand("seq 1 200000");

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.lines.size(), 200000u);
    EXPECT_EQ(result.lines.front(), "1");
    EXPECT_EQ(result.lines.back(), "200000");
}

// And all of it when the writer is slow enough to be interrupted.
TEST(ProcessTest, ReadsAllOfASlowOutput) {
    const auto result = runCommand(
        "for i in $(seq 1 200); do echo line-$i; done");

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.lines.size(), 200u);
}

TEST(ProcessTest, LinesAndTextAgree) {
    const auto result = runCommand("printf 'a\\nb\\nc\\n'");

    EXPECT_EQ(result.text, "a\nb\nc\n");
    ASSERT_EQ(result.lines.size(), 3u);
    EXPECT_EQ(result.lines[2], "c");
}

TEST(ProcessTest, ATrailingLineWithoutANewlineIsKept) {
    const auto result = runCommand("printf 'a\\nb'");

    ASSERT_EQ(result.lines.size(), 2u);
    EXPECT_EQ(result.lines[1], "b");
}

TEST(ProcessTest, ErrorsAreCapturedWhenAsked) {
    // Wrapped in sh -c so the appended 2>&1 applies to the whole
    // command. Written inline, the redirections are applied left to
    // right and both descriptors end up at the terminal.
    const std::string noisy = R"(sh -c 'echo oops >&2')";

    const auto withErrors = runCommand(noisy, true);
    const auto without = runCommand(noisy, false);

    ASSERT_EQ(withErrors.lines.size(), 1u);
    EXPECT_EQ(withErrors.lines[0], "oops");
    EXPECT_TRUE(without.lines.empty());
}

TEST(ProcessTest, EmptyOutputIsNotAFailure) {
    const auto result = runCommand("true");

    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.lines.empty());
}

TEST(ProcessTest, KnowsWhetherACommandExists) {
    EXPECT_TRUE(commandExists("sh"));
    EXPECT_FALSE(commandExists("nexus-nothing-by-this-name"));
}
