#pragma once

#include <string>
#include <vector>

#include <nexus/system/rpm_source.hpp>

namespace nexus::system {

struct SkippedRepository {
    std::string path;
    std::string reason;
};

struct RpmRepositoryResult {
    std::vector<Component> components;
    std::vector<RpmGap> gaps;

    std::vector<std::string> repositoriesRead;
    std::vector<SkippedRepository> skipped;

    std::size_t packagesRead = 0;

    // A package present in more than one repository is not two
    // packages. The newest wins, the same way dnf resolves it, and
    // the rest are counted rather than silently discarded.
    std::size_t versionsSuperseded = 0;
    std::size_t sonameRequirements = 0;
    std::size_t fileRequirements = 0;
};

// Find the primary metadata file named by a repomd.xml.
//
// repomd lists several kinds, including "primary_db" -- a SQLite dump
// of the same data. Matching on a prefix would pick the wrong one, so
// the type is compared exactly.
//
// Returns the href as written, or an empty string when there is no
// primary entry.
std::string findPrimaryHref(const std::string& repomd);

// Read every repository under a cache directory.
//
// Strictly read-only: it reads what dnf already downloaded and never
// contacts the network.
class RpmRepository {
public:
    explicit RpmRepository(
        std::string cacheDirectory = "/var/cache/libdnf5"
    );

    RpmRepositoryResult load() const;

    const std::string& cacheDirectory() const;

private:
    std::string cacheDirectory_;
};

}
