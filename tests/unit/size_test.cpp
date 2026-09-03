#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include <nexus/options.hpp>
#include <nexus/system/apt_source.hpp>
#include <nexus/system/control_file.hpp>
#include <nexus/system/dpkg_source.hpp>
#include <nexus/system/rpm_source.hpp>
#include <nexus/system/version.hpp>

using nexus::Capability;
using nexus::Component;
using nexus::ComponentType;
using nexus::ConflictDetector;
using nexus::Constraint;
using nexus::findOptions;
using nexus::Requirement;
using nexus::Solver;

namespace {

ConflictDetector detector() {
    return ConflictDetector(
        [](const std::string& left, const std::string& right) {
            return nexus::system::compareVersions(left, right);
        }
    );
}

}

TEST(SizeTest, UnknownIsNotZero) {
    const Component component(
        "tree", "tree", "1.0", ComponentType::Application);

    EXPECT_EQ(component.downloadSize(), 0u);
    EXPECT_EQ(component.installedSize(), 0u);
}

// Debian states Installed-Size in kibibytes and Size in bytes.
// Treating them alike understates a package by a factor of a
// thousand.
TEST(SizeTest, DpkgInstalledSizeIsKibibytes) {
    std::istringstream input(
        "Package: firefox\n"
        "Status: install ok installed\n"
        "Architecture: amd64\n"
        "Version: 1.0\n"
        "Installed-Size: 245760\n");

    const nexus::system::DpkgSource source;
    const auto result = source.loadFromStanzas(
        nexus::system::parseControlStream(input));

    ASSERT_EQ(result.components.size(), 1u);

    // 245760 KiB is 251,658,240 bytes, not 245,760.
    EXPECT_EQ(result.components[0].installedSize(), 251658240u);
}

TEST(SizeTest, AptSizeIsBytesAndInstalledSizeIsKibibytes) {
    const auto path =
        std::filesystem::temp_directory_path() /
        ("nexus-size-" + std::to_string(::getpid()));

    std::filesystem::create_directories(path);

    {
        std::ofstream out(path / "testrepo_Packages");
        out << "Package: firefox\n"
               "Architecture: amd64\n"
               "Version: 1.0\n"
               "Size: 71126\n"
               "Installed-Size: 524\n";
    }

    const nexus::system::AptSource source(path.string());
    const auto result = source.load();

    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_EQ(result.components[0].downloadSize(), 71126u);
    EXPECT_EQ(result.components[0].installedSize(), 524u * 1024);

    std::error_code error;
    std::filesystem::remove_all(path, error);
}

// rpm states both in bytes, unlike Debian.
TEST(SizeTest, RpmStatesBothInBytes) {
    const auto result = nexus::system::parseRepodataPrimary(
        "<metadata>"
        "<package type=\"rpm\"><name>firefox</name>"
        "<arch>x86_64</arch>"
        "<version epoch=\"0\" ver=\"1.0\" rel=\"1\"/>"
        "<size package=\"71126\" installed=\"536576\"/>"
        "<format></format></package></metadata>");

    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_EQ(result.components[0].downloadSize(), 71126u);
    EXPECT_EQ(result.components[0].installedSize(), 536576u);
}

namespace {

Component sized(
    const std::string& id,
    const std::string& provides,
    std::uint64_t download,
    std::uint64_t installed
) {
    Component component(id, id, "1.0", ComponentType::Application);

    component.addProvidedCapability(Capability(id));
    component.addProvidedCapability(Capability(provides));
    component.setDownloadSize(download);
    component.setInstalledSize(installed);

    return component;
}

}

TEST(SizeTest, OptionsSumOnlyWhatWouldBeAdded) {
    Component browser =
        sized("browser", "web-browser", 1000000, 4000000);

    browser.addRequirement(Requirement(Constraint("shared")));

    const std::vector<Component> universe{
        browser,
        sized("shared", "shared", 500000, 2000000)
    };

    // shared is already here, so it is not a cost of choosing.
    const std::vector<Component> installed{
        sized("shared", "shared", 500000, 2000000)
    };

    const auto report = findOptions(
        "web-browser", universe, installed,
        Solver(universe, detector()), detector());

    ASSERT_EQ(report.options.size(), 1u);
    EXPECT_TRUE(report.options[0].sizeKnown);
    EXPECT_EQ(report.options[0].downloadBytes, 1000000u);
    EXPECT_EQ(report.options[0].installBytes, 4000000u);
}

// A package with no recorded size must not look free.
TEST(SizeTest, AnUnknownSizeIsReportedAsUnknown) {
    const std::vector<Component> universe{
        sized("browser", "web-browser", 0, 0)
    };

    const auto report = findOptions(
        "web-browser", universe, {},
        Solver(universe, detector()), detector());

    ASSERT_EQ(report.options.size(), 1u);
    EXPECT_FALSE(report.options[0].sizeKnown);
}

// Count and size are different questions, and neither predicts the
// other: on a real archive a browser with 292 components was smaller
// than one with 83.
TEST(SizeTest, FewerComponentsDoesNotMeanSmaller) {
    Component few = sized("few", "web-browser", 40000000, 0);
    Component many = sized("many", "web-browser", 35000000, 0);

    many.addRequirement(Requirement(Constraint("extra")));

    const std::vector<Component> universe{
        few, many, sized("extra", "extra", 0, 0)
    };

    const auto report = findOptions(
        "web-browser", universe, {},
        Solver(universe, detector()), detector());

    ASSERT_EQ(report.options.size(), 2u);

    for (const auto& option : report.options) {
        if (option.component == "many") {
            EXPECT_EQ(option.componentCount, 2u);
            EXPECT_LT(option.downloadBytes, 40000000u);
        }
    }
}
