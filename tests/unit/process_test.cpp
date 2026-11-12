#include <gtest/gtest.h>

#include <csignal>

#include <sys/stat.h>
#include <sys/wait.h>
#include <cerrno>

#include <atomic>
#include <cstdlib>
#include <unistd.h>
#include <thread>
#include <filesystem>
#include <fstream>

#include <cstdlib>

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

// Commands are read in the plain C locale, whatever the machine's
// language. A machine installed in Serbian had dnf print its
// transaction table in Cyrillic, and nothing that read it could.
TEST(ProcessTest, CommandsRunInThePlainLocale) {
    const char* oldAll = std::getenv("LC_ALL");
    const char* oldLanguage = std::getenv("LANGUAGE");
    const std::string savedAll = oldAll ? oldAll : "";
    const std::string savedLanguage = oldLanguage ? oldLanguage : "";

    ::setenv("LC_ALL", "sr_RS.UTF-8", 1);
    ::setenv("LANGUAGE", "sr", 1);

    // A sequence, so the setting has to reach past the first command.
    const auto result = runCommand("true; echo \"$LC_ALL/$LANGUAGE\"");

    if (oldAll) { ::setenv("LC_ALL", savedAll.c_str(), 1); }
    else { ::unsetenv("LC_ALL"); }
    if (oldLanguage) { ::setenv("LANGUAGE", savedLanguage.c_str(), 1); }
    else { ::unsetenv("LANGUAGE"); }

    ASSERT_EQ(result.lines.size(), 1u);
    EXPECT_EQ(result.lines[0], "C.UTF-8/");
}

// An outside review (November 2026): names were hand-wrapped in '...',
// so a ' in one ended the quoting and the rest ran as a command.
TEST(ProcessTest, AQuotedValueIsOneArgumentWhateverItHolds) {
    using nexus::system::shellQuote;
    const std::string hostile = "x'; touch /tmp/nexus-owned-$$; echo '";
    const auto result = runCommand("printf '%s' " + shellQuote(hostile), false);
    EXPECT_EQ(result.text, hostile);
    EXPECT_EQ(runCommand("printf '%s' " + shellQuote("$(id) `id` \"q\" \\"), false).text,
              "$(id) `id` \"q\" \\");
}

TEST(ProcessTest, PackageNamesAreOnlyPackageNames) {
    using nexus::system::isPackageName;
    for (const char* good : {"nmap", "java-21-openjdk", "gcc-c++", "python3.12", "lib32-mesa",
                             "org.mozilla.firefox", "libfoo:i386", "pkg@1", "1.0~rc1"})
        EXPECT_TRUE(isPackageName(good)) << good;
    for (const char* bad : {"", "-y", "--setopt=x", "a b", "x;id", "x'y", "$(id)", "a\nb", "a/b"})
        EXPECT_FALSE(isPackageName(bad)) << bad;
}

// Third outside review (November 2026): stderr went to a guessable
// /tmp/nexus-...-<pid>. Now a folder only this process can enter.
TEST(ProcessTest, AScratchFolderIsPrivateAndGoesAway) {
    std::string kept;
    {
        const nexus::system::ScratchDirectory scratch;
        ASSERT_TRUE(scratch.ok());
        kept = scratch.path();
        struct stat info {};
        ASSERT_EQ(::stat(kept.c_str(), &info), 0);
        EXPECT_EQ(info.st_mode & 0777, 0700u);
        EXPECT_EQ(scratch.file("stderr"), kept + "/stderr");
        std::ofstream(scratch.file("stderr")) << "x";
        // Two at once never share a name.
        const nexus::system::ScratchDirectory other;
        EXPECT_NE(other.path(), kept);
    }
    EXPECT_FALSE(std::filesystem::exists(kept));
}

TEST(ProcessTest, WithoutAFolderTheFileIsDevNullNotAGuess) {
    const char* old = std::getenv("TMPDIR");
    const std::string saved = old ? old : "";
    ::setenv("TMPDIR", "/nonexistent-nexus-test", 1);
    {
        const nexus::system::ScratchDirectory scratch;
        EXPECT_FALSE(scratch.ok());
        EXPECT_EQ(scratch.file("stderr"), "/dev/null");
    }
    if (old) ::setenv("TMPDIR", saved.c_str(), 1); else ::unsetenv("TMPDIR");
    EXPECT_TRUE(std::filesystem::exists("/dev/null"));
}

// An outside review (0.1.84): the WireGuard private key went through
// `sh -c "printf '<key>' | wg pubkey"`, and sh's arguments are readable
// by every local user in /proc. runProgram passes it on stdin, with no
// shell: a watcher reading every /proc/*/cmdline must never see it.
TEST(ProcessTest, ASecretOnStdinNeverAppearsInAnyCommandLine) {
    // Made at run time, so it is in no other process's command line
    // (a shell that started the test could hold a literal).
    std::string secret = "nexus-test-";
    std::srand(static_cast<unsigned>(::getpid()));
    for (int i = 0; i < 40; ++i) secret += static_cast<char>('a' + std::rand() % 26);
    std::atomic<bool> stop{false};
    std::atomic<int> seen{0};
    std::thread watcher([&] {
        while (!stop) {
            // Processes come and go while this walks /proc: no throwing.
            std::error_code ec;
            for (auto it = std::filesystem::directory_iterator("/proc", ec);
                 !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
                // A process that exits mid-read makes libstdc++ throw
                // (read() fails with ESRCH), whatever the stream's
                // exception mask: caught, as a process that is gone.
                std::string all;
                try {
                    std::ifstream in(it->path() / "cmdline", std::ios::binary);
                    all.assign(std::istreambuf_iterator<char>(in), {});
                } catch (const std::exception&) {
                    continue;
                }
                if (all.find(secret) != std::string::npos) ++seen;
            }
        }
    });
    for (int i = 0; i < 200; ++i) {
        const auto r = nexus::system::runProgram({"cat"}, secret);
        ASSERT_TRUE(r.ok);
        ASSERT_EQ(r.text, secret);
    }
    stop = true;
    watcher.join();
    EXPECT_EQ(seen.load(), 0);
}

TEST(ProcessTest, RunProgramParsesNothing) {
    // A shell would run the id; runProgram hands it to echo as text.
    const auto r = nexus::system::runProgram({"echo", "a;id", "$(id)"});
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.text, "a;id $(id)\n");
    EXPECT_FALSE(nexus::system::runProgram({"/nonexistent/program"}).ran);
    // A child that never reads its input does not take us down.
    const auto quick = nexus::system::runProgram({"true"}, std::string(1 << 20, 'x'));
    EXPECT_TRUE(quick.ran);
}

// Outside review, round 2 (0.1.85, N3): another disk's os-release kept
// OSC 52 (clipboard write) and "conceal" sequences on the way to a root
// terminal.
// 0.1.87 (audit): a program the kernel killed is not a success. The
// exit-code bits of such a status are 0; reading them alone said "ok".
TEST(ProcessTest, AKilledCommandIsNotASuccess) {
    using nexus::system::runCommand;
    // The shell itself is killed: nothing is left to report 137, so the
    // status popen hands back is "killed by signal 9".
    const auto shell = runCommand("kill -KILL $$", true);
    EXPECT_FALSE(shell.ok);
    EXPECT_EQ(shell.exitCode, 128 + SIGKILL);
    // What bash does with the last command of a line: it becomes that
    // command (exec), so its death is the status. `exec` makes every
    // shell do it.
    const auto last = runCommand("exec sh -c 'kill -SEGV $$'", false);
    EXPECT_FALSE(last.ok);
    EXPECT_EQ(last.exitCode, 128 + SIGSEGV);
}

TEST(ProcessTest, ExitCodesFromWaitStatuses) {
    using nexus::system::exitCodeFromStatus;
    EXPECT_EQ(exitCodeFromStatus(0), 0);
    EXPECT_EQ(exitCodeFromStatus(3 << 8), 3);        // exit 3
    EXPECT_EQ(exitCodeFromStatus(SIGKILL), 137);     // killed
    EXPECT_EQ(exitCodeFromStatus(SIGTERM), 143);
    EXPECT_EQ(exitCodeFromStatus(-1), -1);           // wait failed
    EXPECT_NE(exitCodeFromStatus(0x7f), 0);          // stopped, not exited
}

TEST(ProcessTest, RunProgramReportsAKilledProgram) {
    const auto r = nexus::system::runProgram({"sh", "-c", "kill -KILL $$"});
    ASSERT_TRUE(r.ran);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.exitCode, 128 + SIGKILL);
}

// 0.1.87 (audit): more than a pipe holds, both ways at once. Before,
// this waited for ever: cat stopped at 64 KiB of unread output while
// runProgram was still writing its input.
TEST(ProcessTest, RunProgramMovesLargeInputAndOutputTogether) {
    std::string big;
    big.reserve(3u << 20);
    for (std::size_t i = 0; big.size() < (3u << 20); ++i) {
        big += std::to_string(i);
        big += '\n';
    }
    const auto r = nexus::system::runProgram({"cat"}, big);
    ASSERT_TRUE(r.ran);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.text.size(), big.size());
    EXPECT_TRUE(r.text == big);
}

// A child that never reads: no SIGPIPE death, and the process's own
// SIGPIPE handling is exactly what it was.
TEST(ProcessTest, RunProgramLeavesSigpipeHandlingAlone) {
    struct sigaction before {}, after {};
    ::sigaction(SIGPIPE, nullptr, &before);
    sigset_t maskBefore, maskAfter, pending;
    ::pthread_sigmask(SIG_SETMASK, nullptr, &maskBefore);

    const auto r = nexus::system::runProgram({"true"}, std::string(1u << 20, 'x'));
    EXPECT_TRUE(r.ran);
    EXPECT_TRUE(r.ok);

    ::sigaction(SIGPIPE, nullptr, &after);
    EXPECT_EQ(before.sa_handler, after.sa_handler);
    ::pthread_sigmask(SIG_SETMASK, nullptr, &maskAfter);
    EXPECT_EQ(sigismember(&maskBefore, SIGPIPE), sigismember(&maskAfter, SIGPIPE));
    // Nothing left queued to arrive later.
    ::sigpending(&pending);
    EXPECT_EQ(sigismember(&pending, SIGPIPE), 0);
}

// 0.1.87 (audit): the stretch between "server stopped" and "server
// started again" must not be cut short by Ctrl-C.
TEST(ProcessTest, HeldSignalsAreRememberedNotFatal) {
    struct sigaction before {}, during {}, after {};
    ::sigaction(SIGINT, nullptr, &before);
    {
        nexus::system::HoldSignals hold;
        EXPECT_EQ(hold.arrived(), 0);
        ::sigaction(SIGINT, nullptr, &during);
        EXPECT_NE(during.sa_handler, SIG_DFL);
        EXPECT_NE(during.sa_handler, SIG_IGN);   // "ignore" would be inherited by tar
        ::raise(SIGINT);                         // would end the test program if not held
        EXPECT_EQ(hold.arrived(), SIGINT);
        ::raise(SIGTERM);
        EXPECT_EQ(hold.arrived(), SIGTERM);
    }
    ::sigaction(SIGINT, nullptr, &after);
    EXPECT_EQ(before.sa_handler, after.sa_handler);
    // A second hold starts clean.
    nexus::system::HoldSignals again;
    EXPECT_EQ(again.arrived(), 0);
}

// A signal that was ignored before (nohup) stays ignored, also for the
// programs started meanwhile.
TEST(ProcessTest, AnIgnoredSignalStaysIgnoredWhileHeld) {
    struct sigaction ignore {}, before {}, during {};
    ignore.sa_handler = SIG_IGN;
    ::sigaction(SIGHUP, &ignore, &before);
    {
        nexus::system::HoldSignals hold;
        ::sigaction(SIGHUP, nullptr, &during);
        EXPECT_EQ(during.sa_handler, SIG_IGN);
        ::raise(SIGHUP);
        EXPECT_EQ(hold.arrived(), 0);
    }
    ::sigaction(SIGHUP, &before, nullptr);
}

// A program started while signals are held is not shielded: it gets
// the default back, so the terminal's Ctrl-C still stops a long tar.
TEST(ProcessTest, ProgramsStartedWhileHeldCanStillBeInterrupted) {
    nexus::system::HoldSignals hold;
    const auto r = nexus::system::runCommand("kill -INT $$; sleep 5; echo survived", true);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.text.find("survived"), std::string::npos);
    EXPECT_EQ(hold.arrived(), 0);   // it was the child's signal, not ours
}

// ---- 0.1.88 (review of patch 113) ------------------------------------

namespace {

// Runs body in a child of its own, so that what it does to signal
// handling stays there; returns how the child ended (wait status).
template <typename Body>
int inAChild(Body body) {
    const pid_t child = ::fork();
    if (child == 0) {
        ::_exit(body());
    }
    int status = -1;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    return status;
}

std::size_t openDescriptors() {
    std::size_t count = 0;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator("/proc/self/fd", ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        ++count;
    }
    return count;
}

}  // namespace

// With SIGCHLD ignored the kernel collects children itself and waitpid
// fails with ECHILD. 0.1.87 then read its own untouched `status = 0`
// as "exit 0": `false` was a success. Not knowing is not success.
TEST(ProcessTest, AnEndingThatCannotBeCollectedIsNotASuccess) {
    const int status = inAChild([] {
        ::signal(SIGCHLD, SIG_IGN);
        int wrong = 0;
        const auto failing = nexus::system::runProgram({"false"});
        if (!failing.ran || failing.ok || failing.exitCode != -1) wrong |= 1;
        const auto passing = nexus::system::runProgram({"true"});
        if (!passing.ran || passing.ok || passing.exitCode != -1) wrong |= 2;
        // The same question through the shell.
        const auto shell = nexus::system::runCommand("exit 0", false);
        if (shell.ok || shell.exitCode != -1) wrong |= 4;
        // What it printed is still there: only the verdict is withheld.
        const auto said = nexus::system::runProgram({"echo", "hello"});
        if (said.text != "hello\n" || said.ok) wrong |= 8;
        return wrong;
    });
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0) << "bit 1: false; 2: true; 4: shell; 8: output";
}

// The ways starting a program can fail, and that none of them leaves a
// descriptor or a finished child behind.
TEST(ProcessTest, FailuresToRunAreReportedAndLeaveNothingBehind) {
    using nexus::system::runProgram;
    const std::size_t before = openDescriptors();

    for (int round = 0; round < 50; ++round) {
        const auto none = runProgram({});
        EXPECT_FALSE(none.ran);
        EXPECT_FALSE(none.ok);

        const auto missing = runProgram({"/nonexistent/nexus-test-program"}, "input");
        EXPECT_FALSE(missing.ran);
        EXPECT_FALSE(missing.ok);

        const auto notAProgram = runProgram({"/etc/hostname"});
        EXPECT_FALSE(notAProgram.ok);

        const auto three = runProgram({"sh", "-c", "exit 3"});
        EXPECT_TRUE(three.ran);
        EXPECT_FALSE(three.ok);
        EXPECT_EQ(three.exitCode, 3);

        // Closes its output and goes on: the reading ends, the waiting
        // does not hang, the exit code is the real one.
        const auto closes = runProgram({"sh", "-c", "exec 1>&-; exit 4"}, std::string(100000, 'x'));
        EXPECT_EQ(closes.exitCode, 4);

        const auto shell = nexus::system::runCommand("/nonexistent/nexus-test-program", true);
        EXPECT_FALSE(shell.ok);
        EXPECT_EQ(shell.exitCode, 127);
    }

    EXPECT_EQ(openDescriptors(), before);
    // Every child was collected: none is left to wait for.
    errno = 0;
    EXPECT_EQ(::waitpid(-1, nullptr, WNOHANG), -1);
    EXPECT_EQ(errno, ECHILD);
}

// Ctrl-\ is held with the others.
TEST(ProcessTest, QuitIsHeldToo) {
    const int status = inAChild([] {
        nexus::system::HoldSignals hold;
        ::raise(SIGQUIT);
        return hold.arrived() == SIGQUIT ? 0 : 1;
    });
    ASSERT_TRUE(WIFEXITED(status));     // not ended by the signal
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

// After the held stretch, the process can end the way the signal asked,
// and is seen to: a shell loop stops only then.
TEST(ProcessTest, EndingBySignalAfterAHoldIsSeenAsThatSignal) {
    for (int number : {SIGINT, SIGTERM, SIGHUP}) {
        const int status = inAChild([number]() -> int {
            int arrived = 0;
            {
                nexus::system::HoldSignals hold;
                ::raise(number);
                arrived = hold.arrived();
            }
            if (arrived != number) return 9;
            nexus::system::endBySignal(arrived);
        });
        ASSERT_TRUE(WIFSIGNALED(status)) << number;
        EXPECT_EQ(WTERMSIG(status), number);
    }
    // Also when the signal was blocked by the caller.
    const int status = inAChild([]() -> int {
        sigset_t block;
        sigemptyset(&block);
        sigaddset(&block, SIGTERM);
        ::pthread_sigmask(SIG_BLOCK, &block, nullptr);
        nexus::system::endBySignal(SIGTERM);
    });
    ASSERT_TRUE(WIFSIGNALED(status));
    EXPECT_EQ(WTERMSIG(status), SIGTERM);
}

TEST(ProcessTest, PrintableRemovesEveryControlCharacter) {
    using nexus::system::printable;
    EXPECT_EQ(printable("Linux Mint 22\x1b]52;c;aWQ=\x07\x1b[8m hidden"),
              "Linux Mint 22?]52;c;aWQ=??[8m hidden");
    EXPECT_EQ(printable("a\nb\tc\x7f"), "a?b?c?");
    EXPECT_EQ(printable("\xc2\x9b" "31m"), "?31m");                  // C1 CSI in UTF-8
    EXPECT_EQ(printable("Fedora Linux 44 (KDE) \xc3\xa9\xe2\x80\x94"), "Fedora Linux 44 (KDE) \xc3\xa9\xe2\x80\x94");
}

// Round 3 (0.1.86, R2): text that turns around or hides, "Trojan
// Source" style.
TEST(ProcessTest, PrintableReplacesBidiControls) {
    using nexus::system::printable;
    EXPECT_EQ(printable("Fire\xe2\x80\xae" "xof"), "Fire?xof");            // U+202E
    EXPECT_EQ(printable("a\xe2\x81\xa6" "b\xe2\x81\xa9"), "a?b?");         // U+2066, U+2069
    EXPECT_EQ(printable("\xd8\x9c" "x"), "?x");                               // U+061C
}

TEST(ProcessTest, PrintableReplacesZeroWidthCharacters) {
    using nexus::system::printable;
    EXPECT_EQ(printable("Ste\xe2\x80\x8b" "am"), "Ste?am");                // U+200B
    EXPECT_EQ(printable("\xef\xbb\xbf" "org.x.Y"), "?org.x.Y");             // U+FEFF
    EXPECT_EQ(printable("a\xe2\x81\xa0" "b"), "a?b");                        // U+2060
    // Neighbours that are ordinary text stay: U+2010 hyphen, U+2014
    // dash, and Arabic and Hebrew words themselves.
    EXPECT_EQ(printable("\xe2\x80\x90\xe2\x80\x94"), "\xe2\x80\x90\xe2\x80\x94");
    EXPECT_EQ(printable("\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 \xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d"),
              "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85 \xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d");
    // Cut short at the end: no reading past the text.
    EXPECT_EQ(printable("x\xe2\x80"), "x\xe2\x80");
}
