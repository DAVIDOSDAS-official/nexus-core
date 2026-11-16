#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include <nexus/system/process.hpp>

#include <nexus/system/plan_apply.hpp>

using nexus::system::ApplyOutcome;
using nexus::system::layerWithRpmOstree;

namespace {

// Stand-in tools at the front of PATH for one test (0.2.1, ledger item
// 4). Each writes the arguments it was given, one per line, into
// <name>.args, prints a line to each stream and exits as told: what a
// package manager is called with can then be read back exactly.
struct StandIns {
    std::filesystem::path dir;
    std::string oldPath;
    StandIns() {
        static int made = 0;
        dir = std::filesystem::temp_directory_path() /
              ("nexus-standins-" + std::to_string(::getpid()) + "-" + std::to_string(++made));
        std::filesystem::create_directories(dir);
        oldPath = std::getenv("PATH") ? std::getenv("PATH") : "";
        ::setenv("PATH", (dir.string() + ":" + oldPath).c_str(), 1);
    }
    ~StandIns() {
        ::setenv("PATH", oldPath.c_str(), 1);
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
    }
    void tool(const std::string& name, int exitCode) const {
        const auto file = dir / name;
        {
            std::ofstream out(file);
            out << "#!/bin/sh\n"
                << "for a in \"$@\"; do printf '%s\\n' \"$a\"; done > '" << (dir / (name + ".args")).string() << "'\n"
                << "echo \"" << name << " says this\"\n"
                << "echo \"" << name << " complains\" >&2\n"
                << "exit " << exitCode << "\n";
        }
        std::filesystem::permissions(file, std::filesystem::perms::owner_all);
    }
    std::vector<std::string> calledWith(const std::string& name) const {
        std::vector<std::string> lines;
        std::ifstream in(dir / (name + ".args"));
        for (std::string line; std::getline(in, line);) lines.push_back(line);
        return lines;
    }
    bool called(const std::string& name) const {
        return std::filesystem::exists(dir / (name + ".args"));
    }
};

}  // namespace

// rpm-ostree is started directly, each package name an argument of its
// own (0.2.1). Until then the names were quoted into one line for
// /bin/sh. The words it is called with are the same; so is what comes
// back: output and errors together, and the exit code.
TEST(PlanApplyTest, LayeringCallsRpmOstreeWithEachNameAsOneArgument) {
    const StandIns standIns;
    standIns.tool("rpm-ostree", 0);

    const auto layered = layerWithRpmOstree({"vlc", "gstreamer1-plugins-bad-free", "libstdc++"});
    EXPECT_EQ(layered.outcome, ApplyOutcome::Staged);
    EXPECT_EQ(layered.exitCode, 0);
    EXPECT_EQ(standIns.calledWith("rpm-ostree"),
              (std::vector<std::string>{"install", "--idempotent", "--allow-inactive", "vlc",
                                        "gstreamer1-plugins-bad-free", "libstdc++"}));
    EXPECT_EQ(layered.output,
              std::vector<std::string>{"rpm-ostree says this\nrpm-ostree complains\n"});

    const auto removed = nexus::system::uninstallWithRpmOstree({"vlc", "gimp"});
    EXPECT_EQ(removed.outcome, ApplyOutcome::Staged);
    EXPECT_EQ(standIns.calledWith("rpm-ostree"),
              (std::vector<std::string>{"uninstall", "vlc", "gimp"}));

    // A failure is a failure, with what the tool said.
    standIns.tool("rpm-ostree", 3);
    const auto failed = layerWithRpmOstree({"vlc"});
    EXPECT_EQ(failed.outcome, ApplyOutcome::Failed);
    EXPECT_EQ(failed.exitCode, 3);
    EXPECT_EQ(failed.output.size(), 1u);
    EXPECT_EQ(nexus::system::uninstallWithRpmOstree({"vlc"}).outcome, ApplyOutcome::Failed);
}

// A name that is refused stops the call before the tool is started,
// and nothing given to the tool can become a second command.
TEST(PlanApplyTest, ARefusedNameStartsNothing) {
    const StandIns standIns;
    standIns.tool("rpm-ostree", 0);
    for (const char* hostile : {"x;touch /tmp/nexus-poc", "$(id)", "a b", "--install=x", ""}) {
        EXPECT_EQ(layerWithRpmOstree({"vlc", hostile}).outcome, ApplyOutcome::Refused) << hostile;
        EXPECT_EQ(nexus::system::uninstallWithRpmOstree({hostile}).outcome, ApplyOutcome::Refused) << hostile;
    }
    EXPECT_EQ(nexus::system::uninstallWithRpmOstree({}).outcome, ApplyOutcome::Refused);
    EXPECT_FALSE(standIns.called("rpm-ostree"));
    EXPECT_FALSE(std::filesystem::exists("/tmp/nexus-poc"));
}

// dnf and apt-get only run as root, so these only run as root: in a
// throwaway container, never on a machine with real packages -- though
// what runs is the stand-in either way.
TEST(PlanApplyTest, DnfAndAptAreCalledWithEachNameAsOneArgument) {
    if (!nexus::system::haveRootPrivileges()) {
        GTEST_SKIP() << "dnf and apt-get are only started as root";
    }
    const StandIns standIns;
    standIns.tool("dnf", 0);
    standIns.tool("apt-get", 0);

    EXPECT_EQ(nexus::system::applyWithDnf("vlc").outcome, ApplyOutcome::Applied);
    EXPECT_EQ(standIns.calledWith("dnf"),
              (std::vector<std::string>{"install", "-y", "--setopt=install_weak_deps=False", "vlc"}));
    const auto removed = nexus::system::removeWithDnf({"vlc", "gimp"});
    EXPECT_EQ(removed.outcome, ApplyOutcome::Applied);
    EXPECT_EQ(removed.output, (std::vector<std::string>{"dnf says this", "dnf complains"}));
    EXPECT_EQ(standIns.calledWith("dnf"), (std::vector<std::string>{"remove", "-y", "vlc", "gimp"}));

    EXPECT_EQ(nexus::system::applyWithApt("vlc").outcome, ApplyOutcome::Applied);
    EXPECT_EQ(standIns.calledWith("apt-get"),
              (std::vector<std::string>{"install", "-y", "--no-install-recommends", "vlc"}));
    EXPECT_EQ(nexus::system::removeWithApt({"vlc", "gimp"}).outcome, ApplyOutcome::Applied);
    EXPECT_EQ(standIns.calledWith("apt-get"), (std::vector<std::string>{"remove", "-y", "vlc", "gimp"}));

    standIns.tool("dnf", 1);
    const auto failed = nexus::system::applyWithDnf("vlc");
    EXPECT_EQ(failed.outcome, ApplyOutcome::Failed);
    EXPECT_EQ(failed.exitCode, 1);
}

// An outside review (0.1.84): names from repository metadata reached
// `rpm-ostree install` in a root shell as they were. A name that is not
// a package name now stops the call before anything runs.
TEST(PlanApplyTest, LayeringRefusesWhatIsNotAPackageName) {
    for (const char* hostile : {"langpacks-xx;id>/tmp/nexus-poc;#", "a b", "$(id)", "`id`", "x\ny", "--install=x"}) {
        const auto r = layerWithRpmOstree({"glibc-langpack-en", hostile});
        EXPECT_EQ(r.outcome, ApplyOutcome::Refused) << hostile;
        EXPECT_EQ(r.exitCode, 2) << hostile;
        ASSERT_FALSE(r.output.empty());
        EXPECT_NE(r.output[0].find("Not a package name"), std::string::npos);
    }
    EXPECT_EQ(layerWithRpmOstree({}).outcome, ApplyOutcome::Refused);
}

// 0.1.92: the Asus (bootc, composefs) has no /usr mount point, so
// "findmnt /usr" printed nothing and 0.1.91 took it for a mutable
// system. The marker ostree leaves, or the kernel's read-only flag,
// decides now.
TEST(PlanApplyTest, AnOstreeSystemIsImageBasedWithoutAUsrMountPoint) {
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() /
                          ("nexus-test-image-" + std::to_string(::getpid()));
    const fs::path bin = base / "bin", root = base / "root";
    fs::create_directories(bin);
    fs::create_directories(root / "run");
    fs::create_directories(root / "usr");   // writable: the test's own /tmp
    {
        std::ofstream tool(bin / "rpm-ostree");
        tool << "#!/bin/sh\nexit 0\n";
    }
    fs::permissions(bin / "rpm-ostree", fs::perms::owner_all);
    const std::string oldPath = std::getenv("PATH") ? std::getenv("PATH") : "";
    ::setenv("PATH", (bin.string() + ":" + oldPath).c_str(), 1);

    EXPECT_FALSE(nexus::system::isImageBasedAt(root.string()));   // writable, no marker
    { std::ofstream marker(root / "run" / "ostree-booted"); }
    EXPECT_TRUE(nexus::system::isImageBasedAt(root.string()));    // the Asus's case

    ::setenv("PATH", oldPath.c_str(), 1);
    fs::remove(bin / "rpm-ostree");
    // No rpm-ostree: nothing to layer with, so not "image-based" here.
    if (!nexus::system::commandExists("rpm-ostree")) {
        EXPECT_FALSE(nexus::system::isImageBasedAt(root.string()));
    }
    fs::remove_all(base);
}
