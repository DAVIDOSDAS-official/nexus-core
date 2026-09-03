#include <nexus/system/rpm_source.hpp>

#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

#include <nexus/constraint.hpp>
#include <nexus/requirement.hpp>
#include <nexus/system/xml.hpp>

namespace nexus::system {

namespace {

// An <rpm:entry>, which is used for provides, requires, conflicts and
// obsoletes alike.
struct Entry {
    std::string name;
    std::string flags;
    std::string epoch;
    std::string version;
    std::string release;
    bool pre = false;
};

// The version parts arrive as separate attributes rather than as a
// string, so there is nothing to parse -- only to reassemble.
std::string versionOf(const Entry& entry) {
    if (entry.version.empty()) {
        return "";
    }

    std::string text;

    if (!entry.epoch.empty() && entry.epoch != "0") {
        text += entry.epoch;
        text += ":";
    }

    text += entry.version;

    if (!entry.release.empty()) {
        text += "-";
        text += entry.release;
    }

    return text;
}

bool relationOf(const std::string& flags, VersionRelation& relation) {
    if (flags == "EQ") {
        relation = VersionRelation::Exactly;
        return true;
    }

    if (flags == "LT") {
        relation = VersionRelation::Earlier;
        return true;
    }

    if (flags == "LE") {
        relation = VersionRelation::EarlierOrEqual;
        return true;
    }

    if (flags == "GT") {
        relation = VersionRelation::Later;
        return true;
    }

    if (flags == "GE") {
        relation = VersionRelation::LaterOrEqual;
        return true;
    }

    return false;
}

bool isBoolean(const std::string& name) {
    return !name.empty() && name.front() == '(';
}

}

std::string normaliseRpmArchitecture(const std::string& architecture) {
    if (architecture == "x86_64") {
        return "amd64";
    }

    if (architecture == "i686" || architecture == "i586" ||
        architecture == "i486" || architecture == "i386") {
        return "i386";
    }

    if (architecture == "aarch64") {
        return "arm64";
    }

    if (architecture == "armv7hl") {
        return "armhf";
    }

    if (architecture == "noarch") {
        return kArchitectureAll;
    }

    if (architecture == "src") {
        return "source";
    }

    return architecture;
}

std::string toString(RpmGapKind kind) {
    switch (kind) {
        case RpmGapKind::BooleanDependency:
            return "boolean-dependency";
        case RpmGapKind::UnknownFlag:
            return "unknown-flag";
    }

    return "unknown";
}

RpmSourceResult parseRepodataPrimary(const std::string& document) {
    RpmSourceResult result;

    XmlReader reader(document);

    // Where in the document we are. Only the depth that matters is
    // tracked; the metadata has a fixed shape.
    std::string name;
    std::string architecture;
    std::string epoch;
    std::string version;
    std::string release;

    std::uint64_t downloadSize = 0;
    std::uint64_t installedSize = 0;

    std::string section;      // provides / requires / conflicts / ...
    std::vector<Entry> provides;
    std::vector<Entry> requires_;
    std::vector<Entry> conflicts;
    std::vector<std::string> files;

    bool inPackage = false;
    std::string textElement;
    std::string pendingText;

    const auto finishPackage = [&]() {
        if (name.empty()) {
            return;
        }

        result.packagesRead += 1;

        const std::string nativeArchitecture =
            normaliseRpmArchitecture(architecture);

        std::string fullVersion = version;

        if (!epoch.empty() && epoch != "0") {
            fullVersion = epoch + ":" + fullVersion;
        }

        if (!release.empty()) {
            fullVersion += "-" + release;
        }

        Component component(
            name,
            name,
            fullVersion,
            ComponentType::Application
        );

        component.setArchitecture(nativeArchitecture);
        component.setDownloadSize(downloadSize);
        component.setInstalledSize(installedSize);

        component.addProvidedCapability(Capability(name));

        for (const Entry& entry : provides) {
            if (isBoolean(entry.name)) {
                result.gaps.push_back(RpmGap{
                    name, RpmGapKind::BooleanDependency, entry.name
                });
                continue;
            }

            const std::string provided = versionOf(entry);

            component.addProvidedCapability(
                provided.empty()
                    ? Capability(entry.name)
                    : Capability(entry.name, provided)
            );
        }

        // A file is a capability too: rpm lets a requirement name a
        // path, satisfied by whichever package ships it.
        for (const std::string& file : files) {
            component.addProvidedCapability(Capability(file));
        }

        for (const Entry& entry : requires_) {
            if (isBoolean(entry.name)) {
                result.gaps.push_back(RpmGap{
                    name, RpmGapKind::BooleanDependency, entry.name
                });
                continue;
            }

            if (entry.name.find(".so") != std::string::npos) {
                result.sonameRequirements += 1;
            } else if (!entry.name.empty() && entry.name.front() == '/') {
                result.fileRequirements += 1;
            }

            Constraint constraint(entry.name);

            if (!entry.flags.empty()) {
                VersionRelation relation;

                if (relationOf(entry.flags, relation)) {
                    constraint.version =
                        VersionConstraint{relation, versionOf(entry)};
                } else {
                    result.gaps.push_back(RpmGap{
                        name, RpmGapKind::UnknownFlag,
                        entry.name + " " + entry.flags
                    });
                }
            }

            Requirement requirement(std::move(constraint));

            requirement.pre = entry.pre;

            component.addRequirement(std::move(requirement));
        }

        for (const Entry& entry : conflicts) {
            if (isBoolean(entry.name)) {
                result.gaps.push_back(RpmGap{
                    name, RpmGapKind::BooleanDependency, entry.name
                });
                continue;
            }

            Constraint constraint(entry.name);

            VersionRelation relation;

            if (!entry.flags.empty() &&
                relationOf(entry.flags, relation)) {
                constraint.version =
                    VersionConstraint{relation, versionOf(entry)};
            }

            component.addConflict(std::move(constraint));
        }

        result.components.push_back(std::move(component));

        name.clear();
        architecture.clear();
        downloadSize = 0;
        installedSize = 0;
        epoch.clear();
        version.clear();
        release.clear();
        provides.clear();
        requires_.clear();
        conflicts.clear();
        files.clear();
    };

    while (true) {
        const XmlReader::Event event = reader.next();

        if (event == XmlReader::Event::None) {
            break;
        }

        if (event == XmlReader::Event::Error) {
            result.error =
                "line " + std::to_string(reader.line()) + ": " +
                reader.error();
            return result;
        }

        if (event == XmlReader::Event::StartElement) {
            const std::string& element = reader.name();

            if (element == "package") {
                inPackage = true;
                continue;
            }

            if (!inPackage) {
                continue;
            }

            if (element == "name" || element == "arch" ||
                element == "file") {
                textElement = element;
                pendingText.clear();
                continue;
            }

            if (element == "version") {
                epoch = reader.attribute("epoch");
                version = reader.attribute("ver");
                release = reader.attribute("rel");
                continue;
            }

            // Both attributes are bytes here, unlike Debian.
            if (element == "size") {
                downloadSize = std::strtoull(
                    reader.attribute("package").c_str(), nullptr, 10);

                installedSize = std::strtoull(
                    reader.attribute("installed").c_str(),
                    nullptr, 10);

                continue;
            }

            if (element == "rpm:provides" ||
                element == "rpm:requires" ||
                element == "rpm:conflicts") {
                section = element;
                continue;
            }

            if (element == "rpm:entry") {
                Entry entry;

                entry.name = reader.attribute("name");
                entry.flags = reader.attribute("flags");
                entry.epoch = reader.attribute("epoch");
                entry.version = reader.attribute("ver");
                entry.release = reader.attribute("rel");
                entry.pre = reader.attribute("pre") == "1";

                if (section == "rpm:provides") {
                    provides.push_back(std::move(entry));
                } else if (section == "rpm:requires") {
                    requires_.push_back(std::move(entry));
                } else if (section == "rpm:conflicts") {
                    conflicts.push_back(std::move(entry));
                }

                continue;
            }

            continue;
        }

        if (event == XmlReader::Event::Text) {
            if (!textElement.empty()) {
                pendingText += reader.text();
            }

            continue;
        }

        // EndElement.
        const std::string& element = reader.name();

        if (element == "package") {
            finishPackage();
            inPackage = false;
            section.clear();
            continue;
        }

        if (element == textElement) {
            if (element == "name") {
                name = pendingText;
            } else if (element == "arch") {
                architecture = pendingText;
            } else if (element == "file") {
                files.push_back(pendingText);
            }

            textElement.clear();
            pendingText.clear();
            continue;
        }

        if (element == "rpm:provides" || element == "rpm:requires" ||
            element == "rpm:conflicts") {
            section.clear();
        }
    }

    return result;
}

}
