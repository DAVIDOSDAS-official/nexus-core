#include <nexus/system/rpm_repo.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <utility>

#include <nexus/system/compression.hpp>
#include <nexus/system/identity.hpp>
#include <nexus/system/rpm_version.hpp>
#include <nexus/system/xml.hpp>

namespace nexus::system {

std::string findPrimaryHref(const std::string& repomd) {
    XmlReader reader(repomd);

    bool inPrimary = false;

    while (true) {
        const XmlReader::Event event = reader.next();

        if (event == XmlReader::Event::None ||
            event == XmlReader::Event::Error) {
            break;
        }

        if (event == XmlReader::Event::StartElement) {
            if (reader.name() == "data") {
                // Exactly "primary": "primary_db" is a SQLite dump of
                // the same content and must not be picked up.
                inPrimary = reader.attribute("type") == "primary";
                continue;
            }

            if (inPrimary && reader.name() == "location") {
                return reader.attribute("href");
            }

            continue;
        }

        if (event == XmlReader::Event::EndElement &&
            reader.name() == "data") {
            inPrimary = false;
        }
    }

    return "";
}

RpmRepository::RpmRepository(std::string cacheDirectory)
    : cacheDirectory_(std::move(cacheDirectory)) {
}

const std::string& RpmRepository::cacheDirectory() const {
    return cacheDirectory_;
}

RpmRepositoryResult RpmRepository::load() const {
    RpmRepositoryResult result;

    // name:architecture -> position, so the same package appearing in
    // several repositories collapses to its newest version.
    std::map<std::string, std::size_t> seen;

    std::error_code error;

    if (!std::filesystem::is_directory(cacheDirectory_, error)) {
        result.skipped.push_back(SkippedRepository{
            cacheDirectory_,
            "not a directory"
        });

        return result;
    }

    std::vector<std::filesystem::path> repositories;

    for (const auto& entry :
         std::filesystem::directory_iterator(cacheDirectory_, error)) {

        if (entry.is_directory(error)) {
            repositories.push_back(entry.path());
        }
    }

    std::sort(repositories.begin(), repositories.end());

    for (const std::filesystem::path& repository : repositories) {
        const std::filesystem::path repomdPath =
            repository / "repodata" / "repomd.xml";

        if (!std::filesystem::exists(repomdPath, error)) {
            continue;
        }

        std::string repomd;
        std::string reason;

        if (!readPossiblyCompressed(
                repomdPath.string(), repomd, reason)) {
            result.skipped.push_back(SkippedRepository{
                repomdPath.string(), reason
            });
            continue;
        }

        const std::string href = findPrimaryHref(repomd);

        if (href.empty()) {
            result.skipped.push_back(SkippedRepository{
                repomdPath.string(),
                "no primary metadata listed"
            });
            continue;
        }

        // The href is written relative to the repository root, and
        // repomd.xml itself lives one level down in repodata/.
        std::filesystem::path primaryPath = repository / href;

        // repomd.xml describes the published repository; the cache
        // holds whatever dnf actually downloaded. For large
        // repositories that is a zchunk file, saved under a .zck name
        // while repomd still names the .zst. So when the named file
        // is absent, look for the primary metadata that is really
        // there rather than reporting the repository as broken.
        if (!std::filesystem::exists(primaryPath, error)) {
            const std::filesystem::path data = repository / "repodata";

            std::vector<std::filesystem::path> candidates;

            for (const auto& file :
                 std::filesystem::directory_iterator(data, error)) {

                const std::string name =
                    file.path().filename().string();

                if (name.find("primary.xml") == std::string::npos) {
                    continue;
                }

                // primary_db is a SQLite dump of the same content.
                if (name.find("primary_db") != std::string::npos ||
                    name.find("sqlite") != std::string::npos) {
                    continue;
                }

                candidates.push_back(file.path());
            }

            std::sort(candidates.begin(), candidates.end());

            if (candidates.empty()) {
                result.skipped.push_back(SkippedRepository{
                    primaryPath.string(),
                    "named in repomd.xml but not present, and no "
                    "primary metadata found in the cache"
                });
                continue;
            }

            primaryPath = candidates.front();
        }

        std::string primary;

        if (!readPossiblyCompressed(
                primaryPath.string(), primary, reason)) {
            result.skipped.push_back(SkippedRepository{
                primaryPath.string(), reason
            });
            continue;
        }

        RpmSourceResult parsed = parseRepodataPrimary(primary);

        if (!parsed.error.empty()) {
            result.skipped.push_back(SkippedRepository{
                primaryPath.string(), parsed.error
            });
            continue;
        }

        result.repositoriesRead.push_back(
            repository.filename().string()
        );

        result.packagesRead += parsed.packagesRead;
        result.sonameRequirements += parsed.sonameRequirements;
        result.fileRequirements += parsed.fileRequirements;

        for (Component& component : parsed.components) {
            const std::string key =
                component.name() + ":" + component.architecture();

            const auto existing = seen.find(key);

            if (existing == seen.end()) {
                seen[key] = result.components.size();
                result.components.push_back(std::move(component));
                continue;
            }

            result.versionsSuperseded += 1;

            Component& kept = result.components[existing->second];

            if (compareRpmVersions(
                    component.version(), kept.version()) > 0) {
                kept = std::move(component);
            }
        }

        for (RpmGap& gap : parsed.gaps) {
            result.gaps.push_back(std::move(gap));
        }
    }

    // Identity is settled over everything kept, so that a name built
    // for two architectures still gets distinct ids.
    qualifyAmbiguousIds(result.components);

    return result;
}

}
