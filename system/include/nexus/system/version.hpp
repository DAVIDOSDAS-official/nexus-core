#pragma once

#include <string>

#include <nexus/system/dependency_expression.hpp>

namespace nexus::system {

// A Debian version, in the form:
//
//     [epoch:]upstream_version[-debian_revision]
//
// The epoch defaults to 0 and the revision defaults to empty, which is
// what dpkg does. Both defaults matter for comparison: "1.0" and
// "0:1.0-" are not textually equal but compare equal.
struct Version {
    unsigned long epoch = 0;
    std::string upstream;
    std::string revision;
};

// Split a version string into its three parts.
//
// This is deliberately lenient: malformed input is parsed as best it
// can be rather than rejected, so that reading a package database
// never fails because of one bad field. Use validateVersion when you
// need to know whether the string was well formed.
Version parseVersion(const std::string& text);

// Returns an empty string when the version is well formed, or a
// human-readable reason when it is not.
std::string validateVersion(const std::string& text);

std::string toString(const Version& version);

// Returns a negative value when a sorts before b, zero when they are
// equal, and a positive value when a sorts after b.
//
// This implements dpkg's algorithm, including the rule that '~' sorts
// before everything else including the end of a string, so:
//
//     1.0~rc1  <  1.0  <  1.0a  <  1.0-1  <  1.0-2
int compareVersions(const Version& left, const Version& right);
int compareVersions(const std::string& left, const std::string& right);

// Evaluate a parsed constraint against an actual version.
//
//     satisfies("2.39", {LaterOrEqual, "2.38"})  ->  true
//
// This is what turns a recorded version constraint from text that was
// parsed into a condition that can be checked.
bool satisfies(
    const std::string& version,
    const VersionConstraint& constraint
);

bool satisfies(
    const Version& version,
    const VersionConstraint& constraint
);

}
