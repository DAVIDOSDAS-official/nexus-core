#include <gtest/gtest.h>

#include <sys/stat.h>

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
