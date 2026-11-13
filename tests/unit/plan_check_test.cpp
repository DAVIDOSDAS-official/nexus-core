#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

#include <nexus/system/plan_check.hpp>

using nexus::system::checkPlanWithDnf;
using nexus::system::PlanAgreement;

namespace {

// An executable `dnf` first on PATH, and a machine language, for the
// life of the object.
class FakeDnf {
public:
    FakeDnf(const std::string& script, const std::string& language) {
        dir_ = std::filesystem::temp_directory_path() /
               ("nexus-fake-dnf-" + std::to_string(::getpid()));
        std::filesystem::create_directories(dir_);

        const auto path = dir_ / "dnf";
        {
            std::ofstream out(path);
            out << "#!/bin/sh\n" << script;
        }
        std::filesystem::permissions(
            path, std::filesystem::perms::owner_all);

        save("PATH", path_);
        save("LC_ALL", all_);
        ::setenv("PATH", (dir_.string() + ":" + path_).c_str(), 1);
        ::setenv("LC_ALL", language.c_str(), 1);
    }

    ~FakeDnf() {
        ::setenv("PATH", path_.c_str(), 1);
        if (all_.empty()) { ::unsetenv("LC_ALL"); }
        else { ::setenv("LC_ALL", all_.c_str(), 1); }
        std::error_code error;
        std::filesystem::remove_all(dir_, error);
    }

private:
    static void save(const char* name, std::string& into) {
        const char* value = std::getenv(name);
        into = value == nullptr ? "" : value;
    }

    std::filesystem::path dir_;
    std::string path_;
    std::string all_;
};

// dnf5's table, the way it prints it in English -- and in Serbian when
// the locale says so. The words are real dnf5 translations' shape; what
// matters is that they are not "Installing".
const char* kDnf =
    "if [ \"$LC_ALL\" = C.UTF-8 ]; then\n"
    "  printf 'Package Arch Version Repository Size\\n"
    "Installing:\\n nmap x86_64 4:7.92-11.fc44 fedora 5.0 MiB\\n"
    "Installing dependencies:\\n libssh2 x86_64 1.11.1-9.fc44 fedora 1 MiB\\n'\n"
    "else\n"
    "  printf 'Пакет Арх Верзија Ризница Величина\\n"
    "Инсталирање:\\n nmap x86_64 4:7.92-11.fc44 fedora 5.0 MiB\\n"
    "Инсталирање зависности:\\n libssh2 x86_64 1.11.1-9.fc44 fedora 1 MiB\\n'\n"
    "  echo 'Корисник је прекинуо радњу.' >&2\n"
    "fi\n"
    "exit 1\n";

}

// The Serbian VM, 26 September: every plan at first boot was "refused",
// because the table dnf printed was not one the parser could read.
TEST(PlanCheckTest, DnfIsReadTheSameWhateverTheMachinesLanguage) {
    FakeDnf dnf(kDnf, "sr_RS.UTF-8");

    const auto check =
        checkPlanWithDnf("nmap", std::set<std::string>{"nmap", "libssh2"});

    EXPECT_EQ(check.agreement, PlanAgreement::Agrees);
    EXPECT_EQ(check.theirs.size(), 2u);
    EXPECT_TRUE(check.refusal.empty());
}

// ---------------------------------------------------------------------
// 0.1.91, round-4 audit M1-M3: the whole of what the package manager
// would do is read, and the list confirmed is the one that runs.

namespace {

using nexus::system::PackageChanges;
using nexus::system::PlanCheck;

// Stand-in programs first on PATH, added one at a time. (A constructor
// taking a list of pairs was ambiguous with one brace-enclosed pair on
// some compilers: the Acer, 13 November.)
class FakeTools {
public:
    FakeTools() {
        dir_ = std::filesystem::temp_directory_path() /
               ("nexus-test-tools-" + std::to_string(::getpid()));
        std::filesystem::create_directories(dir_);
        const char* old = std::getenv("PATH");
        path_ = old == nullptr ? "" : old;
        ::setenv("PATH", (dir_.string() + ":" + path_).c_str(), 1);
    }
    FakeTools(const FakeTools&) = delete;
    FakeTools& operator=(const FakeTools&) = delete;
    ~FakeTools() {
        ::setenv("PATH", path_.c_str(), 1);
        std::error_code error;
        std::filesystem::remove_all(dir_, error);
    }
    void add(const std::string& name, const std::string& script) {
        const auto path = dir_ / name;
        { std::ofstream out(path); out << "#!/bin/sh\n" << script; }
        std::filesystem::permissions(path, std::filesystem::perms::owner_all);
    }
private:
    std::filesystem::path dir_;
    std::string path_;
};

// dnf 5, as printed for an upgrade that obsoletes another package and
// takes dependents with it (shape of dnf5's table; the wording of a
// real Fedora 44 run is still to be captured on the Asus).
const char* kDnf5Mixed =
    "Package           Arch    Version        Repository  Size\n"
    "Installing:\n"
    " newtool          x86_64  2.0-1.fc44     updates     1 MiB\n"
    "   replacing oldtool x86_64 1.0-1.fc44   @System     1 MiB\n"
    "Upgrading:\n"
    " libfoo           x86_64  2.1-1.fc44     updates     1 MiB\n"
    "   replacing libfoo x86_64 2.0-1.fc44    @System     1 MiB\n"
    "Downgrading:\n"
    " libbar           x86_64  0.9-1.fc44     fedora      1 MiB\n"
    "Removing dependent packages:\n"
    " plugin-old       x86_64  1.0-1.fc44     @System     1 MiB\n"
    "Removing unused dependencies:\n"
    " leftover         noarch  1.0-1.fc44     @System     1 KiB\n"
    "\n"
    "Transaction Summary:\n"
    " Installing:         1 package\n"
    " Upgrading:          1 package\n"
    " Replacing:          2 packages\n"
    " Removing:           2 packages\n"
    " Downgrading:        1 package\n";

// dnf 4: rules of "=", an obsolete under Installing, no colon after
// "Transaction Summary".
const char* kDnf4Obsolete =
    "Dependencies resolved.\n"
    "================================================================\n"
    " Package       Arch     Version         Repository        Size\n"
    "================================================================\n"
    "Installing:\n"
    " newtool       x86_64   2.0-1.fc40      updates          10 k\n"
    "     replacing  oldtool.x86_64 1.0-1.fc40\n"
    "\n"
    "Transaction Summary\n"
    "================================================================\n"
    "Install  1 Package\n";

// The Asus, 13 November: `rpm-ostree install --dry-run vlc`, cut short.
const char* kRpmOstree =
    "Checking out tree 49857f4... done\n"
    "Enabled rpm-md repositories: updates fedora\n"
    "Importing rpm-md... done\n"
    "rpm-md repo 'updates' (cached); generated: 2026-11-13T01:23:21Z solvables: 32207\n"
    "Resolving dependencies... done\n"
    "Installing 5 packages:\n"
    "  gcc-16.2.1-3.fc44.x86_64 (updates)\n"
    "  vlc-1:3.0.24-1.fc44.x86_64 (updates)\n"
    "  vlc-plugin-pipewire-3-7.fc44.x86_64 (fedora)\n"
    "  daala-libs-0^20250205git24f889a-1.fc44.x86_64 (fedora)\n"
    "  perl-Text-Tabs+Wrap-2024.001-521.fc44.noarch (fedora)\n"
    "Exiting because of '--dry-run' option\n";

}

TEST(PlanCheckTest, NamesComeOutOfFullPackageVersions) {
    using nexus::system::packageNameFromNevra;
    EXPECT_EQ(packageNameFromNevra("vlc-1:3.0.24-1.fc44.x86_64"), "vlc");
    EXPECT_EQ(packageNameFromNevra("vlc-plugin-pipewire-3-7.fc44.x86_64 (fedora)"),
              "vlc-plugin-pipewire");
    EXPECT_EQ(packageNameFromNevra("daala-libs-0^20250205git24f889a-1.fc44.x86_64"),
              "daala-libs");
    EXPECT_EQ(packageNameFromNevra("perl-Text-Tabs+Wrap-2024.001-521.fc44.noarch"),
              "perl-Text-Tabs+Wrap");
    EXPECT_EQ(packageNameFromNevra("oldtool.x86_64"), "oldtool");
}

TEST(PlanCheckTest, EveryKindOfChangeInADnf5TableIsRead) {
    const PackageChanges c = nexus::system::parseDnfTransaction(kDnf5Mixed);
    EXPECT_EQ(c.install, (std::set<std::string>{"newtool"}));
    EXPECT_EQ(c.upgrade, (std::set<std::string>{"libfoo"}));
    EXPECT_EQ(c.downgrade, (std::set<std::string>{"libbar"}));
    EXPECT_EQ(c.remove, (std::set<std::string>{"plugin-old", "leftover"}));
    // libfoo's own old version is the upgrade, not a replacement.
    EXPECT_EQ(c.replaced, (std::set<std::string>{"oldtool"}));
    EXPECT_TRUE(c.beyondInstalling());
    // The bug the audit found: "replacing" read as a package name.
    EXPECT_EQ(c.install.count("replacing"), 0u);
    EXPECT_EQ(c.upgrade.count("replacing"), 0u);
}

TEST(PlanCheckTest, ADnf4ObsoleteIsAReplacementNotAPackageCalledReplacing) {
    const PackageChanges c = nexus::system::parseDnfTransaction(kDnf4Obsolete);
    EXPECT_EQ(c.install, (std::set<std::string>{"newtool"}));
    EXPECT_EQ(c.replaced, (std::set<std::string>{"oldtool"}));
    EXPECT_TRUE(c.remove.empty());
}

TEST(PlanCheckTest, AnInstallThatAlsoRemovesDoesNotAgreeEvenWithTheSameNames) {
    FakeTools tools;
    tools.add("dnf",
        std::string("printf '%s' '") + kDnf5Mixed + "'\nexit 1\n");
    const auto check = checkPlanWithDnf(
        "newtool", std::set<std::string>{"newtool", "libfoo"});
    EXPECT_EQ(check.agreement, PlanAgreement::Differs);
    EXPECT_TRUE(check.onlyOurs.empty());
    EXPECT_TRUE(check.onlyTheirs.empty());
    EXPECT_TRUE(check.changes.beyondInstalling());
}

TEST(PlanCheckTest, RpmOstreesListLessWhatIsAlreadyHereIsWhatIsNew) {
    FakeTools tools;
    tools.add("rpm-ostree", std::string("cat <<'EOF'\n") + kRpmOstree + "EOF\n");
    tools.add("rpm", "for a in \"$@\"; do case \"$a\" in gcc) echo gcc;; -q|--qf|*NAME*) ;; "
                "*) echo \"package $a is not installed\";; esac; done\nexit 4\n");
    const auto check = nexus::system::checkPlanWithRpmOstree(
        "vlc", std::set<std::string>{"vlc", "vlc-plugin-pipewire"});
    EXPECT_EQ(check.agreement, PlanAgreement::Differs);
    EXPECT_EQ(check.refreshed, (std::set<std::string>{"gcc"}));
    EXPECT_EQ(check.theirs, (std::set<std::string>{
        "vlc", "vlc-plugin-pipewire", "daala-libs", "perl-Text-Tabs+Wrap"}));
    EXPECT_EQ(check.onlyTheirs,
              (std::vector<std::string>{"daala-libs", "perl-Text-Tabs+Wrap"}));
}

TEST(PlanCheckTest, RpmOstreeAddingNothingUnapprovedAgrees) {
    FakeTools tools;
    tools.add("rpm-ostree", std::string("cat <<'EOF'\n") + kRpmOstree + "EOF\n");
    tools.add("rpm", "echo gcc\nexit 4\n");
    const auto check = nexus::system::checkPlanWithRpmOstree(
        "vlc", std::set<std::string>{"vlc", "vlc-plugin-pipewire", "daala-libs",
                                     "perl-Text-Tabs+Wrap"});
    EXPECT_EQ(check.agreement, PlanAgreement::Agrees);
}

TEST(PlanCheckTest, ARemovalTakesWhatDnfSaysNotWhatNexusPlanned) {
    FakeTools tools;
    tools.add("dnf",
        "cat <<'EOF'\n"
        "Package  Arch  Version  Repository  Size\n"
        "Removing:\n"
        " target   x86_64 1-1 @System 1 MiB\n"
        "Removing dependent packages:\n"
        " needs-target x86_64 1-1 @System 1 MiB\n"
        "Removing unused dependencies:\n"
        " libonlyfortarget x86_64 1-1 @System 1 MiB\n"
        "EOF\nexit 1\n");
    const auto check = nexus::system::checkRemovalWithDnf(
        {"target", "libonlyfortarget"}, {"target", "libonlyfortarget"});
    EXPECT_EQ(check.agreement, PlanAgreement::Differs);
    EXPECT_EQ(check.onlyTheirs, (std::vector<std::string>{"needs-target"}));

    const auto asked = nexus::system::decideRemoval(
        {"target", "libonlyfortarget"}, check, false);
    EXPECT_TRUE(asked.allowed);
    EXPECT_EQ(asked.willChange.size(), 3u);   // the count confirmed is dnf's
    const auto unattended = nexus::system::decideRemoval(
        {"target", "libonlyfortarget"}, check, true);
    EXPECT_FALSE(unattended.allowed);         // more than planned, under -y
}

TEST(PlanCheckTest, DnfDecliningToAnswerWithoutRootIsNotARefusal) {
    FakeTools tools;
    tools.add("dnf",
        "echo 'This command has to be run with superuser privileges "
        "(under the root user on most systems).' >&2\nexit 1\n");
    const auto check = nexus::system::checkRemovalWithDnf({"target"}, {"target"});
    EXPECT_EQ(check.agreement, PlanAgreement::Unavailable);
    EXPECT_FALSE(nexus::system::decideRemoval({"target"}, check, false).allowed);
}

TEST(PlanCheckTest, AptSimulationGivesTheRemovals) {
    const auto c = nexus::system::parseAptSimulation(
        "NOTE: This is only a simulation!\n"
        "Remv target [1.0-1]\n"
        "Remv needs-target:amd64 [2.0-1]\n");
    EXPECT_EQ(c.remove, (std::set<std::string>{"target", "needs-target"}));
}

TEST(PlanCheckTest, TheInstallRuleConfirmsTheListThatRuns) {
    using nexus::system::decideInstall;
    PlanCheck dnf;
    dnf.agreement = PlanAgreement::Differs;
    dnf.theirs = {"vlc", "libvlc"};

    // Plain: dnf's list.
    auto d = decideInstall("vlc", {"vlc"}, dnf, nullptr, false);
    EXPECT_TRUE(d.allowed);
    EXPECT_EQ(d.willChange, dnf.theirs);

    // On the image: rpm-ostree's list, which is the one that runs.
    PlanCheck layer;
    layer.agreement = PlanAgreement::Differs;
    layer.theirs = {"vlc", "libvlc", "vlc-plugins-extra"};
    d = decideInstall("vlc", {"vlc"}, dnf, &layer, false);
    EXPECT_TRUE(d.allowed);
    EXPECT_EQ(d.willChange, layer.theirs);

    // rpm-ostree not asked, or refusing: nothing is layered.
    layer.agreement = PlanAgreement::Unavailable;
    EXPECT_FALSE(decideInstall("vlc", {"vlc"}, dnf, &layer, false).allowed);

    // Also removes something: typed confirmation, and never under -y.
    dnf.changes.remove = {"oldplayer"};
    d = decideInstall("vlc", {"vlc"}, dnf, nullptr, false);
    EXPECT_TRUE(d.allowed);
    EXPECT_TRUE(d.typedConfirmation);
    EXPECT_EQ(d.alsoChanges, (std::vector<std::string>{"remove oldplayer"}));
    EXPECT_FALSE(decideInstall("vlc", {"vlc"}, dnf, nullptr, true).allowed);

    // A plan without the requested package is someone else's plan.
    PlanCheck other;
    other.agreement = PlanAgreement::Differs;
    other.theirs = {"vlc-something"};
    EXPECT_FALSE(decideInstall("vlc", {"vlc"}, other, nullptr, false).allowed);
}

TEST(PlanCheckTest, ARemovalThatInstallsIsRefused) {
    PlanCheck check;
    check.agreement = PlanAgreement::Differs;
    check.changes.remove = {"target"};
    check.changes.install = {"replacement"};
    check.theirs = {"target"};
    EXPECT_FALSE(nexus::system::decideRemoval({"target"}, check, false).allowed);
}
