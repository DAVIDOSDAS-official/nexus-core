#include <nexus/system/language.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace nexus::system {

namespace {

std::string lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

}

std::vector<Language> parseLanguagePacks(const std::string& text) {
    std::vector<Language> out;
    std::istringstream lines(text);
    std::string line;
    const std::string prefix = "langpacks-";
    const std::string suffix = " langpacks meta-package";

    while (std::getline(lines, line)) {
        const auto bar = line.find('|');
        if (bar == std::string::npos) {
            continue;
        }

        const std::string package = trim(line.substr(0, bar));
        const std::string summary = trim(line.substr(bar + 1));

        if (package.rfind(prefix, 0) != 0) {
            continue;
        }

        const std::string code = package.substr(prefix.size());

        if (code.empty() || code.rfind("core-", 0) == 0 ||
            code.rfind("fonts-", 0) == 0) {
            continue;
        }

        std::string name = summary;
        if (name.size() > suffix.size() &&
            name.compare(name.size() - suffix.size(), suffix.size(),
                         suffix) == 0) {
            name = name.substr(0, name.size() - suffix.size());
        } else {
            name = code;
        }

        const bool seen = std::any_of(out.begin(), out.end(),
            [&](const Language& l) { return l.code == code; });
        if (!seen) {
            out.push_back({code, name});
        }
    }

    std::sort(out.begin(), out.end(),
        [](const Language& a, const Language& b) {
            return lower(a.name) < lower(b.name);
        });

    return out;
}

const Language* findLanguage(const std::vector<Language>& languages,
                             const std::string& typed) {
    const std::string want = lower(trim(typed));

    for (const Language& language : languages) {
        if (lower(language.code) == want || lower(language.name) == want) {
            return &language;
        }
    }

    return nullptr;
}

std::vector<std::string> localesFor(const std::string& listing,
                                    const std::string& codeOrLocale) {
    std::vector<std::string> out;
    std::istringstream lines(listing);
    std::string line;

    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        if (line == codeOrLocale) {
            return {line};
        }

        if (line.rfind(codeOrLocale + "_", 0) == 0 ||
            line.rfind(codeOrLocale + ".", 0) == 0 ||
            line.rfind(codeOrLocale + "@", 0) == 0) {
            out.push_back(line);
        }
    }

    return out;
}

std::string localeLanguage(const std::string& locale) {
    const auto end = locale.find_first_of("_.@");
    return locale.substr(0, end);
}

std::string describeLocale(const std::string& locale) {
    std::string words = localeLanguage(locale);

    const auto underscore = locale.find('_');
    if (underscore != std::string::npos) {
        const auto end = locale.find_first_of(".@", underscore);
        words += ", " + locale.substr(underscore + 1,
            end == std::string::npos ? std::string::npos
                                     : end - underscore - 1);
    }

    const auto at = locale.find('@');
    if (at != std::string::npos) {
        const std::string modifier = locale.substr(at + 1);
        if (modifier == "latin") {
            words += ", Latin script";
        } else if (modifier == "cyrillic") {
            words += ", Cyrillic script";
        } else {
            words += ", " + modifier;
        }
    }

    return words;
}

std::string localeConfLang(const std::string& text) {
    std::istringstream lines(text);
    std::string line;

    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.rfind("LANG=", 0) != 0) {
            continue;
        }

        std::string value = line.substr(5);
        if (value.size() >= 2 &&
            (value.front() == '"' || value.front() == '\'') &&
            value.back() == value.front()) {
            value = value.substr(1, value.size() - 2);
        }
        return value;
    }

    return {};
}

}
