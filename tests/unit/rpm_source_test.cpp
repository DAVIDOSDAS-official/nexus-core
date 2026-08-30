#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include <nexus/system/rpm_source.hpp>

using nexus::Capability;
using nexus::Component;
using nexus::system::normaliseRpmArchitecture;
using nexus::system::parseRepodataPrimary;
using nexus::system::RpmGapKind;

namespace {

// A real package entry, copied from Fedora 42 repodata rather than
// invented. Writing metadata tests from memory is how the shape gets
// quietly wrong.
const char* kRealPackage = R"XML(<?xml version="1.0" encoding="UTF-8"?>
<metadata xmlns="http://linux.duke.edu/metadata/common" xmlns:rpm="http://linux.duke.edu/metadata/rpm" packages="1">
<package type="rpm">
  <name>mozilla-openh264</name>
  <arch>x86_64</arch>
  <version epoch="0" ver="2.5.1" rel="1.fc42"/>
  <summary>H.264 codec support for Mozilla browsers</summary>
  <format>
    <rpm:license>BSD-2-Clause</rpm:license>
    <rpm:provides>
      <rpm:entry name="mozilla-openh264" flags="EQ" epoch="0" ver="2.5.1" rel="1.fc42"/>
      <rpm:entry name="mozilla-openh264(x86-64)" flags="EQ" epoch="0" ver="2.5.1" rel="1.fc42"/>
    </rpm:provides>
    <rpm:requires>
      <rpm:entry name="libgcc_s.so.1()(64bit)"/>
      <rpm:entry name="libstdc++.so.6(GLIBCXX_3.4.32)(64bit)"/>
      <rpm:entry name="mozilla-filesystem(x86-64)"/>
      <rpm:entry name="openh264(x86-64)" flags="EQ" epoch="0" ver="2.5.1" rel="1.fc42"/>
      <rpm:entry name="rtld(GNU_HASH)"/>
    </rpm:requires>
    <file>/etc/profile.d/gmpopenh264.sh</file>
  </format>
</package>
</metadata>)XML";

bool provides(const Component& component, const std::string& name) {
    const auto& capabilities = component.providedCapabilities();

    return std::any_of(
        capabilities.begin(),
        capabilities.end(),
        [&name](const Capability& capability) {
            return capability.name() == name;
        }
    );
}

bool requires_(const Component& component, const std::string& name) {
    for (const auto& requirement : component.requirements()) {
        for (const auto& option : requirement.alternatives) {
            if (option.capability == name) {
                return true;
            }
        }
    }

    return false;
}

}

TEST(RpmSourceTest, NormalisesArchitectureNames) {
    EXPECT_EQ(normaliseRpmArchitecture("x86_64"), "amd64");
    EXPECT_EQ(normaliseRpmArchitecture("i686"), "i386");
    EXPECT_EQ(normaliseRpmArchitecture("aarch64"), "arm64");
    EXPECT_EQ(normaliseRpmArchitecture("noarch"), "all");
    EXPECT_EQ(normaliseRpmArchitecture("ppc64le"), "ppc64le");
}

TEST(RpmSourceTest, ReadsARealPackage) {
    const auto result = parseRepodataPrimary(kRealPackage);

    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_EQ(result.components.size(), 1u);

    const Component& component = result.components[0];

    EXPECT_EQ(component.id(), "mozilla-openh264");
    EXPECT_EQ(component.architecture(), "amd64");
    EXPECT_EQ(component.version(), "2.5.1-1.fc42");
}

// The version arrives as three attributes, not a string. There is
// nothing to parse here, only to reassemble.
TEST(RpmSourceTest, ReassemblesVersionFromAttributes) {
    const auto result = parseRepodataPrimary(R"XML(<metadata>
<package type="rpm"><name>a</name><arch>noarch</arch>
<version epoch="2" ver="1.0" rel="3.fc42"/>
<format></format></package></metadata>)XML");

    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_EQ(result.components[0].version(), "2:1.0-3.fc42");
}

TEST(RpmSourceTest, ProvidesCarryTheirVersion) {
    const auto result = parseRepodataPrimary(kRealPackage);

    ASSERT_EQ(result.components.size(), 1u);

    for (const auto& capability :
         result.components[0].providedCapabilities()) {

        if (capability.name() == "mozilla-openh264(x86-64)") {
            EXPECT_EQ(capability.version(), "2.5.1-1.fc42");
        }
    }
}

// The shape that has no Debian equivalent: most requirements name a
// symbol set rather than a package.
TEST(RpmSourceTest, SonameRequirementsAreOrdinaryCapabilities) {
    const auto result = parseRepodataPrimary(kRealPackage);

    ASSERT_EQ(result.components.size(), 1u);

    const Component& component = result.components[0];

    EXPECT_TRUE(requires_(component, "libgcc_s.so.1()(64bit)"));
    EXPECT_TRUE(
        requires_(component, "libstdc++.so.6(GLIBCXX_3.4.32)(64bit)"));
    EXPECT_TRUE(requires_(component, "rtld(GNU_HASH)"));
    EXPECT_EQ(result.sonameRequirements, 2u);
}

// rpm lets a requirement name a path, satisfied by whichever package
// ships it, so a file is a capability too.
TEST(RpmSourceTest, FilesAreProvidedCapabilities) {
    const auto result = parseRepodataPrimary(kRealPackage);

    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_TRUE(provides(
        result.components[0], "/etc/profile.d/gmpopenh264.sh"));
}

TEST(RpmSourceTest, ReadsComparisonFlags) {
    const auto result = parseRepodataPrimary(R"XML(<metadata>
<package type="rpm"><name>a</name><arch>x86_64</arch>
<version epoch="0" ver="1.0" rel="1"/>
<format><rpm:requires>
<rpm:entry name="glibc" flags="GE" epoch="0" ver="2.34"/>
</rpm:requires></format></package></metadata>)XML");

    ASSERT_EQ(result.components.size(), 1u);

    const auto& requirements = result.components[0].requirements();

    ASSERT_EQ(requirements.size(), 1u);

    const auto& option = requirements[0].alternatives[0];

    ASSERT_TRUE(option.version.has_value());
    EXPECT_EQ(option.version->relation, nexus::VersionRelation::LaterOrEqual);
    EXPECT_EQ(option.version->version, "2.34");
}

TEST(RpmSourceTest, ReadsPreRequirements) {
    const auto result = parseRepodataPrimary(R"XML(<metadata>
<package type="rpm"><name>a</name><arch>x86_64</arch>
<version epoch="0" ver="1.0" rel="1"/>
<format><rpm:requires>
<rpm:entry name="early" pre="1"/>
<rpm:entry name="normal"/>
</rpm:requires></format></package></metadata>)XML");

    ASSERT_EQ(result.components.size(), 1u);

    const auto& requirements = result.components[0].requirements();

    ASSERT_EQ(requirements.size(), 2u);
    EXPECT_TRUE(requirements[0].pre);
    EXPECT_FALSE(requirements[1].pre);
}

TEST(RpmSourceTest, ReadsConflicts) {
    const auto result = parseRepodataPrimary(R"XML(<metadata>
<package type="rpm"><name>a</name><arch>x86_64</arch>
<version epoch="0" ver="1.0" rel="1"/>
<format><rpm:conflicts>
<rpm:entry name="b" flags="LT" epoch="0" ver="2.0"/>
</rpm:conflicts></format></package></metadata>)XML");

    ASSERT_EQ(result.components.size(), 1u);
    ASSERT_EQ(result.components[0].conflicts().size(), 1u);
    EXPECT_EQ(result.components[0].conflicts()[0].capability, "b");
}

// Boolean dependencies are not represented. They are recorded rather
// than dropped, so a system using them does not look like one that
// does not.
TEST(RpmSourceTest, RecordsBooleanDependenciesAsGaps) {
    const auto result = parseRepodataPrimary(R"XML(<metadata>
<package type="rpm"><name>a</name><arch>x86_64</arch>
<version epoch="0" ver="1.0" rel="1"/>
<format><rpm:requires>
<rpm:entry name="(python3-foo if python3)"/>
<rpm:entry name="plain"/>
</rpm:requires></format></package></metadata>)XML");

    ASSERT_EQ(result.components.size(), 1u);
    ASSERT_EQ(result.gaps.size(), 1u);
    EXPECT_EQ(result.gaps[0].kind, RpmGapKind::BooleanDependency);
    EXPECT_EQ(result.components[0].requirements().size(), 1u);
}

TEST(RpmSourceTest, ReadsSeveralPackages) {
    const auto result = parseRepodataPrimary(R"XML(<metadata>
<package type="rpm"><name>one</name><arch>x86_64</arch>
<version epoch="0" ver="1.0" rel="1"/><format></format></package>
<package type="rpm"><name>two</name><arch>noarch</arch>
<version epoch="0" ver="2.0" rel="1"/><format></format></package>
</metadata>)XML");

    ASSERT_EQ(result.components.size(), 2u);
    EXPECT_EQ(result.packagesRead, 2u);
    EXPECT_EQ(result.components[0].id(), "one");
    EXPECT_EQ(result.components[1].architecture(), "all");
}

TEST(RpmSourceTest, ReportsMalformedMetadata) {
    const auto result = parseRepodataPrimary("<metadata><package name=x>");

    EXPECT_FALSE(result.error.empty());
}
