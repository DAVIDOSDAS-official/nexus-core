#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

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
