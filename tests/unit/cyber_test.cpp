#include <gtest/gtest.h>

#include <algorithm>

#include <nexus/system/cyber.hpp>

using namespace nexus::system;

TEST(CyberTest, TiersMapToKaliBundles) {
    ASSERT_EQ(cyberTiers().size(), 3u);
    EXPECT_EQ(cyberTier("small")->metapackage, "kali-tools-top10");
    EXPECT_EQ(cyberTier("medium")->metapackage, "kali-linux-default");
    EXPECT_EQ(cyberTier("full")->metapackage, "kali-linux-everything");
    EXPECT_FALSE(cyberTier("huge").has_value());
}

TEST(CyberTest, ToolNamesThatMayGoIntoACommand) {
    EXPECT_TRUE(safeToolName("nmap"));
    EXPECT_TRUE(safeToolName("msfconsole"));
    EXPECT_TRUE(safeToolName("john.bin"));
    EXPECT_TRUE(safeToolName("python3.12"));
    EXPECT_FALSE(safeToolName(""));
    EXPECT_FALSE(safeToolName("-rf"));
    EXPECT_FALSE(safeToolName("nmap; rm -rf ~"));
    EXPECT_FALSE(safeToolName("../bin/sh"));
    EXPECT_FALSE(safeToolName("$(id)"));
}

TEST(CyberTest, FindsTheContainerInDistroboxList) {
    const std::string list =
        "ID           | NAME                 | STATUS             | IMAGE\n"
        "1a2b3c4d5e6f | fedora-dev           | Exited (0) 2 days  | registry.fedoraproject.org/fedora-toolbox:44\n"
        "3f2a9b8c7d6e | nexus-cyber          | Up 2 minutes       | docker.io/kalilinux/kali-rolling:latest\n";
    const auto st = parseDistroboxList(list, "nexus-cyber");
    EXPECT_TRUE(st.exists);
    EXPECT_TRUE(st.running);
    EXPECT_EQ(st.image, "docker.io/kalilinux/kali-rolling:latest");

    const auto other = parseDistroboxList(list, "fedora-dev");
    EXPECT_TRUE(other.exists);
    EXPECT_FALSE(other.running);

    // a name that only contains ours is not ours
    EXPECT_FALSE(parseDistroboxList("x | nexus-cyber-old | Up | y\n", "nexus-cyber").exists);
    EXPECT_FALSE(parseDistroboxList("", "nexus-cyber").exists);
}

// 0.2.3: the isolated toolbox. What it is handed is all it sees.
TEST(CyberTest, TheIsolatedBoxSeesOnlyItsOwnFolder) {
    const std::string work = isolatedWorkFolder("/var/home/ana");
    EXPECT_EQ(work, "/var/home/ana/.local/share/nexus-cyber-isolated/work");
    const auto args = isolatedCreateArgs("docker.io/kalilinux/kali-rolling@sha256:abc", work);
    ASSERT_GE(args.size(), 3u);
    EXPECT_EQ(args[0], "podman");
    EXPECT_EQ(args[1], "create");
    int volumes = 0;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--volume" || a == "-v" || a.rfind("--volume=", 0) == 0 || a == "--mount") {
            ++volumes;
            ASSERT_LT(i + 1, args.size());
            EXPECT_EQ(args[i + 1], work + ":/work:Z");
        }
        // Nothing that would hand it the machine.
        EXPECT_NE(a, "--privileged");
        EXPECT_EQ(a.find("--network=host"), std::string::npos);
        EXPECT_NE(a, "host");
        EXPECT_EQ(a.find("/run/host"), std::string::npos);
        EXPECT_EQ(a.find("/run/user"), std::string::npos);
        EXPECT_EQ(a.find("--device"), std::string::npos);
        EXPECT_EQ(a.find("--pid"), std::string::npos);
        EXPECT_EQ(a.find("--ipc"), std::string::npos);
        EXPECT_EQ(a.find("label=disable"), std::string::npos);
        EXPECT_EQ(a.find("--cap-add"), std::string::npos);
    }
    EXPECT_EQ(volumes, 1);
    EXPECT_NE(std::find(args.begin(), args.end(), "--network=pasta"), args.end());
    EXPECT_NE(std::find(args.begin(), args.end(), "no-new-privileges"), args.end());
    // The home folder appears nowhere, not even as a value.
    for (const auto& a : args)
        if (a != work + ":/work:Z") EXPECT_EQ(a.find("/var/home/ana"), std::string::npos) << a;
}

TEST(CyberTest, TheIsolatedBoxIsEnteredInItsFolder) {
    EXPECT_EQ(isolatedExecArgs({"nmap", "-sT", "192.168.0.1"}, true),
              (std::vector<std::string>{"podman", "exec", "-i", "-t", "--workdir", "/work",
                                        "nexus-cyber-isolated", "nmap", "-sT", "192.168.0.1"}));
    EXPECT_EQ(isolatedExecArgs({"bash"}, false),
              (std::vector<std::string>{"podman", "exec", "-i", "--workdir", "/work",
                                        "nexus-cyber-isolated", "bash"}));
    // An argument stays one argument, whatever is in it.
    const auto odd = isolatedExecArgs({"echo", "a b; $(id) `x`"}, false);
    EXPECT_EQ(odd.back(), "a b; $(id) `x`");
}

TEST(CyberTest, TheIsolatedBoxsStateIsReadFromPodman) {
    EXPECT_FALSE(parseIsolatedInspect(125, "").exists);
    EXPECT_FALSE(parseIsolatedInspect(0, "").exists);
    const auto running = parseIsolatedInspect(0, "running\n");
    EXPECT_TRUE(running.exists);
    EXPECT_TRUE(running.running);
    const auto stopped = parseIsolatedInspect(0, "exited\n");
    EXPECT_TRUE(stopped.exists);
    EXPECT_FALSE(stopped.running);
    EXPECT_EQ(stopped.status, "exited");
    EXPECT_FALSE(parseIsolatedInspect(0, "running\nexited\n").exists);  // two boxes, not one
}
