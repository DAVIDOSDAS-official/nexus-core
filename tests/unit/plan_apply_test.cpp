#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

#include <nexus/system/process.hpp>

#include <nexus/system/plan_apply.hpp>

using nexus::system::ApplyOutcome;
using nexus::system::layerWithRpmOstree;

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
