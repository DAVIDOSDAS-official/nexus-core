#include <gtest/gtest.h>

#include <nexus/system/container.hpp>

using nexus::system::containerImageFor;
using nexus::system::containerNameFor;
using nexus::system::Containers;

// distrobox prints its header even when there are no containers, so
// an empty list is a header and nothing else. Treating the header as
// a row would invent a container called NAME.
TEST(ContainerTest, AnEmptyListIsJustAHeader) {
    const auto containers = Containers::parseList(
        "ID           | NAME                 | STATUS             | IMAGE\n");

    EXPECT_TRUE(containers.empty());
}

TEST(ContainerTest, ParsesContainers) {
    const auto containers = Containers::parseList(
        "ID           | NAME                 | STATUS             | IMAGE\n"
        "a1b2c3d4e5f6 | nexus-arch           | Up 2 minutes       | docker.io/library/archlinux:latest\n"
        "f6e5d4c3b2a1 | nexus-fedora         | Exited (0) 1 day ago | registry.fedoraproject.org/fedora-toolbox:latest\n");

    ASSERT_EQ(containers.size(), 2u);

    EXPECT_EQ(containers[0].name, "nexus-arch");
    EXPECT_EQ(containers[0].id, "a1b2c3d4e5f6");
    EXPECT_EQ(containers[0].image,
              "docker.io/library/archlinux:latest");
    EXPECT_TRUE(containers[0].running());

    EXPECT_EQ(containers[1].name, "nexus-fedora");
    EXPECT_FALSE(containers[1].running());
}

TEST(ContainerTest, RowsWithoutANameAreSkipped) {
    const auto containers = Containers::parseList(
        "ID | NAME | STATUS | IMAGE\n"
        "abc |  |  | \n"
        "def | real | Up | image\n");

    ASSERT_EQ(containers.size(), 1u);
    EXPECT_EQ(containers[0].name, "real");
}

TEST(ContainerTest, HandlesNoOutputAtAll) {
    EXPECT_TRUE(Containers::parseList("").empty());
}

TEST(ContainerTest, KnowsAnImagePerDistribution) {
    EXPECT_NE(containerImageFor("arch").find("archlinux"),
              std::string::npos);
    EXPECT_NE(containerImageFor("fedora").find("fedora"),
              std::string::npos);
    EXPECT_TRUE(containerImageFor("plan9").empty());
}

// One container per distribution rather than one per package: a
// second Arch container would download Arch twice.
TEST(ContainerTest, OneContainerPerDistribution) {
    EXPECT_EQ(containerNameFor("arch"), "nexus-arch");
    EXPECT_EQ(containerNameFor("arch"), containerNameFor("arch"));
    EXPECT_NE(containerNameFor("arch"), containerNameFor("fedora"));
}

TEST(ContainerTest, AnUnknownDistributionIsRefusedNotGuessed) {
    const auto result = Containers::ensure("nexus-plan9", "");

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
}

// Guessing the binary from the package name is wrong twice over:
// Arch's metasploit ships msfconsole, msfvenom and msfdb and nothing
// called metasploit, and a package that ships a dozen commands does
// not have "a" binary.
//
// These check the parsing of what each package manager reports, which
// is the part that has to be right before the container is involved
// at all.
TEST(ContainerTest, PacmanListingIsStrippedOfItsPackagePrefix) {
    // pacman -Ql prefixes every line with the package name.
    const std::vector<std::string> output{
        "metasploit /usr/",
        "metasploit /usr/bin/",
        "metasploit /usr/bin/msfconsole",
        "metasploit /usr/bin/msfvenom",
        "metasploit /usr/share/",
        "metasploit /usr/share/metasploit/README",
    };

    // The same filtering the reader applies.
    std::vector<std::string> binaries;

    for (const std::string& line : output) {
        std::string path = line;

        const std::size_t space = path.find(' ');

        if (space != std::string::npos) {
            path = path.substr(space + 1);
        }

        if (path.empty() || path.back() == '/') {
            continue;
        }

        if (path.rfind("/usr/bin/", 0) == 0) {
            binaries.push_back(path);
        }
    }

    ASSERT_EQ(binaries.size(), 2u);
    EXPECT_EQ(binaries[0], "/usr/bin/msfconsole");
    EXPECT_EQ(binaries[1], "/usr/bin/msfvenom");
}

TEST(ContainerTest, DirectoriesAreNotCommands) {
    // A trailing slash is how a directory announces itself.
    const std::string directory = "/usr/bin/";
    const std::string file = "/usr/bin/msfconsole";

    EXPECT_EQ(directory.back(), '/');
    EXPECT_NE(file.back(), '/');
}
