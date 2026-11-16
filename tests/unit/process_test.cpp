#include <gtest/gtest.h>

#include <csignal>

#include <sys/stat.h>
#include <sys/wait.h>
#include <cerrno>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <functional>
#include <iterator>
#include <string>
#include <utility>
#include <vector>
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

// Round-4 audit L3 (0.1.99): more characters that print as nothing or
// break a line where none shows. The emoji variation selector stays.
TEST(ProcessTest, PrintableReplacesTheRestOfTheInvisibleCharacters) {
    using nexus::system::printable;
    EXPECT_EQ(printable("Ste\xc2\xad" "am"), "Ste?am");                    // U+00AD soft hyphen
    EXPECT_EQ(printable("a\xe2\x80\xa8" "b\xe2\x80\xa9"), "a?b?");         // U+2028, U+2029
    EXPECT_EQ(printable("\xe3\x85\xa4" "x"), "?x");                         // U+3164
    EXPECT_EQ(printable("\xe1\x85\x9f\xe1\x85\xa0" "x"), "??x");           // U+115F, U+1160
    EXPECT_EQ(printable("\xef\xbe\xa0" "x"), "?x");                         // U+FFA0
    EXPECT_EQ(printable("a\xe1\xa0\x8e" "b"), "a?b");                       // U+180E
    EXPECT_EQ(printable("x\xf3\xa0\x80\x81\xf3\xa0\x81\xbf"), "x??");       // U+E0001, U+E007F
    // Kept: the emoji selector, a real Hangul syllable, an ordinary
    // character next to the tag range (U+E0100, a variation selector
    // of its own block), and a cut-short sequence at the end.
    EXPECT_EQ(printable("\xe2\x9d\xa4\xef\xb8\x8f"), "\xe2\x9d\xa4\xef\xb8\x8f");
    EXPECT_EQ(printable("\xed\x95\x9c"), "\xed\x95\x9c");
    EXPECT_EQ(printable("\xf3\xa0\x84\x80"), "\xf3\xa0\x84\x80");
    EXPECT_EQ(printable("x\xf3\xa0\x80"), "x\xf3\xa0\x80");
    EXPECT_EQ(printable("\xc2\xa0"), "\xc2\xa0");                           // no-break space stays
}

// ---------------------------------------------------------------------
// runProgram with options (0.2.1, ledger item 4): everything a command
// line handed to /bin/sh used to say with 2>&1, 2>/dev/null, >/dev/null,
// VAR=value and timeout, without a shell.
//
// `sh -c` appears below as the program under test: a small script is
// the plainest way to make a program that prints to both streams, exits
// with a chosen number or signals somebody. Nothing here builds a
// command out of a value.

namespace {

using nexus::system::ProgramOptions;
using nexus::system::runProgram;

// Runs `body` in a child with one of its standard streams (0, 1 or 2)
// pointed at a file, and gives back what the file then holds.
template <typename Body>
std::string withStreamInAFile(int stream, const std::string& before, Body body, int* exitCode = nullptr) {
    const std::string path = (std::filesystem::temp_directory_path() /
        ("nexus-stream-" + std::to_string(::getpid()) + "-" + std::to_string(stream))).string();
    { std::ofstream(path) << before; }
    const int status = inAChild([&]() -> int {
        const int fd = ::open(path.c_str(), stream == 0 ? O_RDONLY : (O_WRONLY | O_TRUNC));
        if (fd < 0 || ::dup2(fd, stream) < 0) return 99;
        return body();
    });
    if (exitCode != nullptr) *exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    std::ifstream in(path);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::filesystem::remove(path);
    return text;
}

const std::vector<std::string> kBothStreams{"sh", "-c", "echo out; echo err >&2; exit 3"};

}  // namespace

TEST(ProcessTest, OptionsSendErrorsWhereTheyAreAskedTo) {
    // The default: what "2>/dev/null" was.
    const auto dropped = runProgram(kBothStreams, ProgramOptions());
    EXPECT_TRUE(dropped.ran);
    EXPECT_FALSE(dropped.ok);
    EXPECT_EQ(dropped.exitCode, 3);
    EXPECT_EQ(dropped.text, "out\n");
    EXPECT_EQ(dropped.errors, "");
    ASSERT_EQ(dropped.lines.size(), 1u);
    EXPECT_EQ(dropped.lines[0], "out");

    // "2>&1": with the output, in the order written.
    const auto merged = runProgram({"sh", "-c", "echo a; echo b >&2; echo c; exit 3"},
                                   nexus::system::mergedErrors());
    EXPECT_EQ(merged.text, "a\nb\nc\n");
    EXPECT_EQ(merged.errors, "");
    EXPECT_EQ(merged.exitCode, 3);
    EXPECT_EQ(merged.lines, (std::vector<std::string>{"a", "b", "c"}));

    // On their own.
    ProgramOptions apart;
    apart.errors = ProgramOptions::Errors::Separate;
    const auto separate = runProgram(kBothStreams, apart);
    EXPECT_EQ(separate.text, "out\n");
    EXPECT_EQ(separate.errors, "err\n");
    EXPECT_EQ(separate.exitCode, 3);

    // "> /dev/null 2>&1": only how it ended.
    const auto nothing = runProgram(kBothStreams, nexus::system::nothingKept());
    EXPECT_TRUE(nothing.ran);
    EXPECT_EQ(nothing.text, "");
    EXPECT_EQ(nothing.errors, "");
    EXPECT_EQ(nothing.exitCode, 3);

    // No redirection at all: this process's own stderr gets them.
    int code = -1;
    const std::string seen = withStreamInAFile(2, "", [] {
        const auto shown = runProgram(kBothStreams, nexus::system::shownErrors());
        return shown.text == "out\n" && shown.errors.empty() && shown.exitCode == 3 ? 0 : 1;
    }, &code);
    EXPECT_EQ(code, 0);
    EXPECT_EQ(seen, "err\n");

    // Lines only when asked for.
    ProgramOptions noLines = nexus::system::mergedErrors();
    noLines.splitLines = false;
    const auto whole = runProgram(kBothStreams, noLines);
    EXPECT_EQ(whole.text, "out\nerr\n");
    EXPECT_TRUE(whole.lines.empty());
}

TEST(ProcessTest, OptionsCanLeaveTheOutputToTheTerminal) {
    // What std::system did: nothing collected, the program writes where
    // this process writes. Errors "merged" follow the output there.
    for (const bool merge : {false, true}) {
        int code = -1;
        const std::string seen = withStreamInAFile(1, "", [merge] {
            ProgramOptions options = nexus::system::handedOver(false);
            if (merge) options.errors = ProgramOptions::Errors::Merge;
            else options.errors = ProgramOptions::Errors::Discard;
            const auto ran = runProgram(kBothStreams, options);
            return ran.text.empty() && ran.lines.empty() && ran.exitCode == 3 ? 0 : 1;
        }, &code);
        EXPECT_EQ(code, 0) << merge;
        EXPECT_EQ(seen, merge ? "out\nerr\n" : "out\n") << merge;
    }
}

// Unless input is given, the program reads what this process reads
// (a command run through the shell did: dpkg asks its questions there).
TEST(ProcessTest, OptionsShareStdinUnlessInputIsGiven) {
    int code = -1;
    withStreamInAFile(0, "typed by the person\n", [] {
        int wrong = 0;
        const auto shared = runProgram({"cat"}, ProgramOptions());
        if (shared.text != "typed by the person\n") wrong |= 1;
        ProgramOptions given;
        given.input = "from nexus";
        if (runProgram({"cat"}, given).text != "from nexus") wrong |= 2;
        given.input = "";   // given, and empty: the end of input at once
        const auto empty = runProgram({"cat"}, given);
        if (!empty.text.empty() || !empty.ok) wrong |= 4;
        return wrong;
    }, &code);
    EXPECT_EQ(code, 0) << "bit 1: shared; 2: given; 4: given empty";
}

// More than a pipe holds (64 KiB), in every direction at once.
TEST(ProcessTest, OptionsMoveMoreThanAPipeHoldsEveryWay) {
    const std::string big(300000, 'x');

    ProgramOptions in;
    in.input = big;
    const auto echoed = runProgram({"cat"}, in);
    EXPECT_TRUE(echoed.ok);
    EXPECT_EQ(echoed.text, big);

    // In, out and errors, all three larger than a pipe.
    ProgramOptions three;
    three.input = big;
    three.errors = ProgramOptions::Errors::Separate;
    const auto all = runProgram(
        {"sh", "-c", "head -c 200000 /dev/zero | tr '\\0' e >&2; cat; head -c 200000 /dev/zero | tr '\\0' E >&2"},
        three);
    EXPECT_TRUE(all.ok);
    EXPECT_EQ(all.text, big);
    EXPECT_EQ(all.errors, std::string(200000, 'e') + std::string(200000, 'E'));

    ProgramOptions merged = nexus::system::mergedErrors();
    merged.input = big;
    const auto both = runProgram(
        {"sh", "-c", "cat; head -c 200000 /dev/zero | tr '\\0' e >&2"}, merged);
    EXPECT_EQ(both.text.size(), 500000u);

    // In only: nothing is read back, all of it is still written.
    ProgramOptions swallowed = nexus::system::nothingKept();
    swallowed.input = big;
    const auto counted = runProgram({"sh", "-c", "test \"$(wc -c)\" -eq 300000"}, swallowed);
    EXPECT_TRUE(counted.ok);
    EXPECT_EQ(counted.text, "");
}

// A program that ends without reading: the write would raise SIGPIPE
// and end nexus. It must not, whichever way the output goes.
TEST(ProcessTest, OptionsSurviveAProgramThatExitsWhileInputIsWritten) {
    const int status = inAChild([] {
        int wrong = 0;
        const std::string big(4 * 1024 * 1024, 'x');
        ProgramOptions given;
        given.input = big;
        const auto early = runProgram({"sh", "-c", "exit 5"}, given);
        if (!early.ran || early.exitCode != 5) wrong |= 1;
        const auto some = runProgram({"head", "-c", "10"}, given);
        if (some.text != std::string(10, 'x') || !some.ok) wrong |= 2;
        ProgramOptions quiet = nexus::system::nothingKept();
        quiet.input = big;
        if (runProgram({"true"}, quiet).exitCode != 0) wrong |= 4;
        ProgramOptions merged = nexus::system::mergedErrors();
        merged.input = big;
        const auto said = runProgram({"sh", "-c", "echo bye >&2; exit 6"}, merged);
        if (said.text != "bye\n" || said.exitCode != 6) wrong |= 8;
        // And no SIGPIPE left waiting for whoever unblocks it next.
        sigset_t pending;
        sigemptyset(&pending);
        ::sigpending(&pending);
        if (sigismember(&pending, SIGPIPE) == 1) wrong |= 16;
        return wrong;
    });
    ASSERT_TRUE(WIFEXITED(status)) << "ended by signal " << WTERMSIG(status);
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(ProcessTest, OptionsReportAKilledProgramAs128PlusTheSignal) {
    for (const auto& [name, number] : {std::pair{"TERM", SIGTERM}, std::pair{"KILL", SIGKILL},
                                       std::pair{"INT", SIGINT}, std::pair{"HUP", SIGHUP}}) {
        const auto killed = runProgram(
            {"sh", "-c", std::string("echo before; kill -") + name + " $$; echo after"},
            nexus::system::mergedErrors());
        EXPECT_TRUE(killed.ran) << name;
        EXPECT_FALSE(killed.ok) << name;
        EXPECT_EQ(killed.exitCode, 128 + number) << name;
        EXPECT_EQ(killed.text, "before\n") << name;
    }
}

// A program that is not there: the numbers a shell gave, never 0, and
// the reason where the shell's "not found" line used to be.
TEST(ProcessTest, OptionsSayWhenAProgramCannotBeStarted) {
    const std::string missing = "nexus-no-such-program-for-the-test";
    const std::size_t before = openDescriptors();

    const auto merged = runProgram({missing, "x"}, nexus::system::mergedErrors());
    EXPECT_FALSE(merged.ran);
    EXPECT_FALSE(merged.ok);
    EXPECT_EQ(merged.exitCode, 127);
    EXPECT_EQ(merged.text, missing + ": no such program\n");
    ASSERT_EQ(merged.lines.size(), 1u);

    ProgramOptions apart;
    apart.errors = ProgramOptions::Errors::Separate;
    const auto separate = runProgram({missing}, apart);
    EXPECT_EQ(separate.exitCode, 127);
    EXPECT_EQ(separate.text, "");
    EXPECT_EQ(separate.errors, missing + ": no such program\n");

    const auto dropped = runProgram({missing}, ProgramOptions());
    EXPECT_EQ(dropped.exitCode, 127);
    EXPECT_EQ(dropped.text, "");

    // Shown to the person, as the shell's line was.
    const std::string seen = withStreamInAFile(2, "", [&missing] {
        return runProgram({missing}, nexus::system::shownErrors()).exitCode == 127 ? 0 : 1;
    });
    EXPECT_EQ(seen, missing + ": no such program\n");

    // A folder in PATH that cannot be looked into does not turn "not
    // there" into "not allowed" (the C library says the latter; a shell
    // says the former). Root can look into anything, so only otherwise.
    if (::geteuid() != 0) {
        const auto locked = std::filesystem::temp_directory_path() /
                            ("nexus-locked-" + std::to_string(::getpid()));
        std::filesystem::create_directories(locked);
        std::filesystem::permissions(locked, std::filesystem::perms::none);
        const std::string oldPath = std::getenv("PATH") ? std::getenv("PATH") : "";
        ::setenv("PATH", (locked.string() + ":" + oldPath).c_str(), 1);
        const auto behind = runProgram({missing}, nexus::system::mergedErrors());
        EXPECT_EQ(behind.exitCode, 127);
        EXPECT_EQ(behind.text, missing + ": no such program\n");
        // And what is there further along PATH still runs.
        EXPECT_EQ(runProgram({"echo", "found"}, ProgramOptions()).text, "found\n");
        ::setenv("PATH", oldPath.c_str(), 1);
        std::filesystem::permissions(locked, std::filesystem::perms::owner_all);
        std::filesystem::remove(locked);
    }

    // There, and not a program: 126.
    const auto notAProgram = runProgram({"/etc/hostname"}, nexus::system::mergedErrors());
    EXPECT_FALSE(notAProgram.ran);
    EXPECT_EQ(notAProgram.exitCode, 126);
    EXPECT_NE(notAProgram.text.find("could not be started"), std::string::npos) << notAProgram.text;

    // Nothing to run at all.
    EXPECT_EQ(runProgram({}, ProgramOptions()).exitCode, 127);
    EXPECT_FALSE(runProgram({}, ProgramOptions()).ran);
    EXPECT_EQ(runProgram({""}, ProgramOptions()).exitCode, 127);
    EXPECT_EQ(runProgram({"/"}, ProgramOptions()).exitCode, 126);
    // The form without options too: it said 0 until 0.2.0.
    EXPECT_EQ(runProgram({missing}).exitCode, 127);
    EXPECT_FALSE(runProgram({missing}).ok);

    EXPECT_EQ(openDescriptors(), before);
    EXPECT_EQ(::waitpid(-1, nullptr, WNOHANG), -1);   // no child left uncollected
}

// The point of all this: an argument is one argument, whatever it
// holds. Nothing is split, expanded, globbed or run.
TEST(ProcessTest, OptionsPassEveryArgumentAsItIs) {
    const std::vector<std::string> hostile{
        "",                           // an empty argument stays an argument
        "two words",
        "it's",
        "say \"hi\"",
        "$HOME",
        "${PATH}",
        "`id`",
        "$(id)",
        "a;b",
        "a|b",
        "a&b",
        "> /tmp/nexus-should-not-exist",
        "*",
        "~",
        "line one\nline two",
        "tab\there",
        "-n",
        "--",
        "\\",
        "caf\xc3\xa9",
    };
    // Prints each argument it was given with a zero byte after it: no
    // argument can hold one, so where each ends is beyond doubt.
    std::vector<std::string> argv{"sh", "-c", "printf '%s\\0' \"$@\"", "sh"};
    argv.insert(argv.end(), hostile.begin(), hostile.end());
    std::string expected;
    for (const auto& one : hostile) {
        expected += one + std::string(1, '\0');
    }
    const auto echoed = runProgram(argv, ProgramOptions());
    EXPECT_TRUE(echoed.ok);
    EXPECT_EQ(echoed.text, expected);
    EXPECT_FALSE(std::filesystem::exists("/tmp/nexus-should-not-exist"));

    // And with nothing in between that could count them wrongly.
    const auto count = runProgram({"sh", "-c", "echo $#", "sh", "", "a b", "", "$(id)"}, ProgramOptions());
    EXPECT_EQ(count.text, "4\n");
}

TEST(ProcessTest, OptionsSetTheLocaleAndVariablesForTheProgramOnly) {
    const char* oldAll = std::getenv("LC_ALL");
    const char* oldLanguage = std::getenv("LANGUAGE");
    const std::string keptAll = oldAll ? oldAll : "";
    const std::string keptLanguage = oldLanguage ? oldLanguage : "";
    ::setenv("LC_ALL", "sr_RS.UTF-8", 1);
    ::setenv("LANGUAGE", "sr", 1);
    ::setenv("NEXUS_TEST_KEPT", "kept", 1);

    const std::vector<std::string> show{"sh", "-c",
        "printf '%s|%s|%s|%s' \"$LC_ALL\" \"${LANGUAGE-unset}\" \"$NEXUS_TEST_KEPT\" \"$NEXUS_TEST_SET\""};

    // As runCommand always did: LC_ALL=C.UTF-8, LANGUAGE set and empty.
    EXPECT_EQ(runProgram(show, ProgramOptions()).text, "C.UTF-8||kept|");

    ProgramOptions own;
    own.plainLocale = false;
    EXPECT_EQ(runProgram(show, own).text, "sr_RS.UTF-8|sr|kept|");
    // The form without options keeps the caller's locale, as before.
    EXPECT_EQ(runProgram(show).text, "sr_RS.UTF-8|sr|kept|");

    // "LC_ALL=de_DE.UTF-8 locale title": one variable over the plain locale.
    ProgramOptions one;
    one.environment = {{"LC_ALL", "de_DE.UTF-8"}, {"NEXUS_TEST_SET", "a b;$(id)"}};
    EXPECT_EQ(runProgram(show, one).text, "de_DE.UTF-8||kept|a b;$(id)");

    // The last setting of a name wins; a name that is not one is left out.
    ProgramOptions twice;
    twice.environment = {{"NEXUS_TEST_SET", "first"}, {"NEXUS_TEST_SET", "second"},
                         {"NEXUS_TEST_KEPT=x", "smuggled"}, {"", "nameless"}};
    EXPECT_EQ(runProgram(show, twice).text, "C.UTF-8||kept|second");

    // This process's own variables are as they were.
    EXPECT_STREQ(std::getenv("LC_ALL"), "sr_RS.UTF-8");
    EXPECT_STREQ(std::getenv("LANGUAGE"), "sr");
    EXPECT_EQ(std::getenv("NEXUS_TEST_SET"), nullptr);

    if (oldAll) ::setenv("LC_ALL", keptAll.c_str(), 1); else ::unsetenv("LC_ALL");
    if (oldLanguage) ::setenv("LANGUAGE", keptLanguage.c_str(), 1); else ::unsetenv("LANGUAGE");
    ::unsetenv("NEXUS_TEST_KEPT");
}

namespace {

double secondsFor(const std::function<void()>& work) {
    const auto start = std::chrono::steady_clock::now();
    work();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

TEST(ProcessTest, OptionsStopAProgramWhenItsTimeIsUp) {
    // In time: nothing changes.
    ProgramOptions limited;
    limited.timeoutSeconds = 20;
    const auto quick = runProgram({"echo", "hi"}, limited);
    EXPECT_TRUE(quick.ok);
    EXPECT_FALSE(quick.timedOut);
    EXPECT_EQ(quick.text, "hi\n");

    // Too long: SIGTERM, exit code 124 as timeout(1), what it had
    // printed is kept.
    limited.timeoutSeconds = 1;
    nexus::system::ProcessResult slow;
    double took = secondsFor([&] {
        slow = runProgram({"sh", "-c", "echo first; exec sleep 30"}, limited);
    });
    EXPECT_TRUE(slow.ran);
    EXPECT_TRUE(slow.timedOut);
    EXPECT_FALSE(slow.ok);
    EXPECT_EQ(slow.exitCode, 124);
    EXPECT_EQ(slow.text, "first\n");
    EXPECT_GE(took, 0.9);
    EXPECT_LT(took, 15.0);   // not the thirty seconds; loose for a busy machine

    // With nothing to read from it the limit holds as well.
    ProgramOptions silent = nexus::system::nothingKept();
    silent.timeoutSeconds = 1;
    took = secondsFor([&] { slow = runProgram({"sleep", "30"}, silent); });
    EXPECT_TRUE(slow.timedOut);
    EXPECT_EQ(slow.exitCode, 124);
    EXPECT_LT(took, 15.0);

    // One that refuses SIGTERM gets SIGKILL two seconds later.
    took = secondsFor([&] {
        slow = runProgram({"sh", "-c", "trap '' TERM; echo deaf; exec sleep 30"}, limited);
    });
    EXPECT_TRUE(slow.timedOut);
    EXPECT_EQ(slow.exitCode, 124);
    EXPECT_EQ(slow.text, "deaf\n");
    EXPECT_GE(took, 2.9);    // SIGTERM at one second, SIGKILL two later
    EXPECT_LT(took, 15.0);

    // Something the program started keeps the pipe open after it is
    // gone: nexus does not wait for that for ever.
    took = secondsFor([&] {
        slow = runProgram({"sh", "-c", "sleep 8 & echo started; wait"}, limited);
    });
    EXPECT_TRUE(slow.timedOut);
    EXPECT_EQ(slow.exitCode, 124);
    EXPECT_EQ(slow.text, "started\n");
    EXPECT_LT(took, 15.0);

    EXPECT_EQ(::waitpid(-1, nullptr, WNOHANG), -1);   // every program was collected
}

// Work handed over in the foreground (what std::system was): Ctrl-C
// goes to everything in the terminal's foreground, nexus included.
// nexus must still be there afterwards to write down how it ended.
TEST(ProcessTest, HandedOverWorkTakesCtrlCItselfAndNexusLivesOn) {
    // The program interrupts its parent, as a terminal's Ctrl-C would.
    const std::vector<std::string> interrupts{"sh", "-c", "kill -INT $PPID; kill -QUIT $PPID; exit 7"};

    int status = inAChild([&] {
        const auto ran = runProgram(interrupts, nexus::system::handedOver(false));
        if (ran.exitCode != 7) return 1;
        // Afterwards Ctrl-C is nexus's own again.
        struct sigaction now {};
        ::sigaction(SIGINT, nullptr, &now);
        if (now.sa_handler != SIG_DFL) return 2;
        ::sigaction(SIGQUIT, nullptr, &now);
        return now.sa_handler == SIG_DFL ? 0 : 3;
    });
    ASSERT_TRUE(WIFEXITED(status)) << "nexus was ended by signal " << WTERMSIG(status);
    EXPECT_EQ(WEXITSTATUS(status), 0);

    // Without it, that is what happens: the test is about something real.
    status = inAChild([&] {
        ProgramOptions plain = nexus::system::nothingKept();
        runProgram(interrupts, plain);
        return 0;
    });
    EXPECT_TRUE(WIFSIGNALED(status));

    // The program itself is interrupted as usual: it is not shielded...
    status = inAChild([] {
        const auto ran = runProgram({"sh", "-c", "kill -INT $$; exit 7"},
                                    nexus::system::handedOver(false));
        return ran.exitCode == 128 + SIGINT ? 0 : 1;
    });
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);

    // ...unless nexus itself was started with Ctrl-C ignored (nohup, a
    // background job): then so is what it starts, as with system(3).
    status = inAChild([] {
        ::signal(SIGINT, SIG_IGN);
        const auto ran = runProgram({"sh", "-c", "kill -INT $$; exit 7"},
                                    nexus::system::handedOver(false));
        struct sigaction now {};
        ::sigaction(SIGINT, nullptr, &now);
        return ran.exitCode == 7 && now.sa_handler == SIG_IGN ? 0 : 1;
    });
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

// Started with stdin, stdout or stderr closed, a process gets those
// numbers back from the next pipe(). The pipes must still reach the
// program the right way round.
TEST(ProcessTest, OptionsWorkWithTheStandardStreamsClosed) {
    const int status = inAChild([] {
        ::close(0);
        ::close(1);
        ::close(2);
        int wrong = 0;
        ProgramOptions given = nexus::system::mergedErrors();
        given.input = "hello";
        if (runProgram({"cat"}, given).text != "hello") wrong |= 1;
        ProgramOptions apart;
        apart.input = "";
        apart.errors = ProgramOptions::Errors::Separate;
        const auto both = runProgram({"sh", "-c", "echo o; echo e >&2"}, apart);
        if (both.text != "o\n" || both.errors != "e\n") wrong |= 2;
        if (runProgram({"echo", "x"}, "").text != "x\n") wrong |= 4;
        // And 0, 1 and 2 are closed again afterwards, as they were.
        for (int fd = 0; fd <= 2; ++fd) {
            if (::fcntl(fd, F_GETFD) != -1) wrong |= 8;
        }
        return wrong;
    });
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0) << "bit 1: input; 2: three pipes; 4: old form; 8: descriptors";
}

TEST(ProcessTest, OptionsLeaveNoDescriptorOpen) {
    const std::size_t before = openDescriptors();
    ProgramOptions apart;
    apart.errors = ProgramOptions::Errors::Separate;
    apart.input = "x";
    ProgramOptions limited = nexus::system::nothingKept();
    limited.timeoutSeconds = 5;
    for (int i = 0; i < 20; ++i) {
        runProgram(kBothStreams, ProgramOptions());
        runProgram(kBothStreams, nexus::system::mergedErrors());
        runProgram(kBothStreams, apart);
        runProgram(kBothStreams, nexus::system::nothingKept());
        runProgram({"true"}, limited);
        runProgram({"nexus-no-such-program-for-the-test"}, apart);
    }
    EXPECT_EQ(openDescriptors(), before);
    EXPECT_EQ(::waitpid(-1, nullptr, WNOHANG), -1);
}

// "| head -2", done here.
TEST(ProcessTest, FirstLinesIsWhatHeadKept) {
    using nexus::system::firstLines;
    EXPECT_EQ(firstLines("State: busy\nDeployments:\n  one\n", 2), "State: busy\nDeployments:\n");
    EXPECT_EQ(firstLines("a\nb\nc\n", 1), "a\n");
    EXPECT_EQ(firstLines("a\nb", 5), "a\nb");
    EXPECT_EQ(firstLines("a", 2), "a");
    EXPECT_EQ(firstLines("\n\nx\n", 2), "\n\n");
    EXPECT_EQ(firstLines("", 2), "");
    EXPECT_EQ(firstLines("a\nb\n", 0), "");
}
