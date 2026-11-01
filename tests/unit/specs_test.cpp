#include <gtest/gtest.h>

#include <nexus/system/specs.hpp>

using namespace nexus::system;

TEST(SpecsTest, CpuModelIsTheFirstModelName) {
    const std::string cpuinfo =
        "processor\t: 0\n"
        "vendor_id\t: GenuineIntel\n"
        "model name\t: Intel(R) Core(TM) i3-1115G4 @ 3.00GHz\n"
        "processor\t: 1\n"
        "model name\t: Intel(R) Core(TM) i3-1115G4 @ 3.00GHz\n";
    EXPECT_EQ(parseCpuModel(cpuinfo),
              "Intel(R) Core(TM) i3-1115G4 @ 3.00GHz");
}

TEST(SpecsTest, CpuModelEmptyWhenAbsent) {
    EXPECT_EQ(parseCpuModel("processor\t: 0\nflags\t: fpu\n"), "");
    EXPECT_EQ(parseCpuModel(""), "");
}

TEST(SpecsTest, CpuCoresCountsProcessorLines) {
    const std::string cpuinfo =
        "processor\t: 0\n"
        "model name\t: x\n"
        "processor\t: 1\n"
        "model name\t: x\n"
        "processor\t: 2\n"
        "processor\t: 3\n";
    EXPECT_EQ(parseCpuCores(cpuinfo), 4);
    // "processor" must be the whole key, not a prefix of another.
    EXPECT_EQ(parseCpuCores("processor_foo\t: 9\n"), 0);
    EXPECT_EQ(parseCpuCores(""), 0);
}

TEST(SpecsTest, MemoryReadsTotalAndAvailable) {
    const std::string meminfo =
        "MemTotal:        3813376 kB\n"
        "MemFree:          512000 kB\n"
        "MemAvailable:    1600000 kB\n"
        "Buffers:           20000 kB\n";
    std::uint64_t total = 0, avail = 0;
    parseMemKiB(meminfo, total, avail);
    EXPECT_EQ(total, 3813376ULL);
    EXPECT_EQ(avail, 1600000ULL);
}

TEST(SpecsTest, MemoryMissingFieldStaysZero) {
    std::uint64_t total = 0, avail = 0;
    parseMemKiB("MemTotal:        2048 kB\n", total, avail);
    EXPECT_EQ(total, 2048ULL);
    EXPECT_EQ(avail, 0ULL);
}

TEST(SpecsTest, UptimeSecondsFromFirstField) {
    EXPECT_EQ(parseUptimeSeconds("11564.72 90580.19\n"), 11564);
    EXPECT_EQ(parseUptimeSeconds("0.00 0.00\n"), 0);
    EXPECT_EQ(parseUptimeSeconds("garbage"), 0);
}

TEST(SpecsTest, UptimeFormatsLargestTwoUnits) {
    EXPECT_EQ(formatUptime(30), "up less than a minute");
    EXPECT_EQ(formatUptime(60), "1 min");
    EXPECT_EQ(formatUptime(125), "2 mins");
    EXPECT_EQ(formatUptime(3600), "1 hour");
    EXPECT_EQ(formatUptime(3 * 3600 + 12 * 60), "3 hours, 12 mins");
    // Largest two only: days + hours, minutes dropped.
    EXPECT_EQ(formatUptime(2 * 86400 + 4 * 3600 + 5 * 60),
              "2 days, 4 hours");
    // A zero middle unit is skipped, not shown as "0 hours".
    EXPECT_EQ(formatUptime(86400 + 5 * 60), "1 day, 5 mins");
}

TEST(SpecsTest, OsReleaseValueStripsQuotes) {
    const std::string os =
        "NAME=\"Nexus-CORE\"\n"
        "VERSION_ID=44\n"
        "VARIANT_ID=kde\n"
        "PRETTY_NAME=\"Fedora Linux 44 (Workstation Edition)\"\n";
    EXPECT_EQ(parseOsReleaseValue(os, "NAME"), "Nexus-CORE");
    EXPECT_EQ(parseOsReleaseValue(os, "VERSION_ID"), "44");
    EXPECT_EQ(parseOsReleaseValue(os, "VARIANT_ID"), "kde");
    EXPECT_EQ(parseOsReleaseValue(os, "PRETTY_NAME"),
              "Fedora Linux 44 (Workstation Edition)");
    EXPECT_EQ(parseOsReleaseValue(os, "MISSING"), "");
    // A key that is only a prefix of a line must not match.
    EXPECT_EQ(parseOsReleaseValue(os, "VERSION"), "");
}

TEST(SpecsTest, JsonHasEveryFieldAndEscapes) {
    SpecsInfo info;
    info.host = "nexus";
    info.model = "ASUS \"VivoBook\"";  // quotes must be escaped
    info.nexusVersion = "0.1.59";
    info.edition = "kde";
    info.cpuModel = "Intel i3";
    info.cpuCores = 4;
    info.gpus = {"Intel Iris Xe (i915)"};
    info.ramUsedMiB = 2000;
    info.ramTotalMiB = 3724;
    info.disks.push_back({"/var", 13ULL << 30, 229ULL << 30});
    info.ntsync = true;
    info.addons = {"gaming", "hosting"};
    info.secureBoot = "off";
    info.encryption = "on (LUKS)";
    info.firewall = "on";
    info.signature = "signed";

    const std::string json = specsJson(info);

    EXPECT_NE(json.find("\"host\": \"nexus\""), std::string::npos);
    EXPECT_NE(json.find("\"model\": \"ASUS \\\"VivoBook\\\"\""),
              std::string::npos);
    EXPECT_NE(json.find("\"cpu_cores\": 4"), std::string::npos);
    EXPECT_NE(json.find("\"ntsync\": true"), std::string::npos);
    EXPECT_NE(json.find("\"addons\": [\"gaming\", \"hosting\"]"),
              std::string::npos);
    EXPECT_NE(json.find("\"encryption\": \"on (LUKS)\""),
              std::string::npos);
    EXPECT_NE(json.find("\"used_bytes\": 13958643712"), std::string::npos);
    // ntsync false serialises as a bare boolean, not a string.
    SpecsInfo off;
    EXPECT_NE(specsJson(off).find("\"ntsync\": false"), std::string::npos);
}
