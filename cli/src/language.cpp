// nexus language.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

namespace {

// nexus language: what the system speaks, and adding or switching.
// See system/include/nexus/system/language.hpp for what Fedora offers
// and how that was checked.
//
// Every tool here is started directly, each value an argument of its
// own (0.2.1, ledger item 4): adding and setting a language run as
// root, and no command line here is read by a shell any more. A plain
// runProgram is what "2>/dev/null" was, mergedErrors() what "2>&1" was.
using nexus::system::mergedErrors;
using nexus::system::runProgram;

// Every language Fedora has a pack for. Asks dnf, which reads its own
// cache; empty when dnf is not here or cannot answer.
std::vector<nexus::system::Language> fedoraLanguages() {
    if (!nexus::system::commandExists("dnf")) {
        return {};
    }
    std::cerr << "Asking dnf which languages Fedora offers...\n";
    // "\n" as two characters: dnf turns it into the line end itself.
    const auto ran = runProgram(
        {"dnf", "-q", "repoquery", "--qf", "%{name}|%{summary}\\n", "langpacks-*"},
        nexus::system::ProgramOptions());
    return nexus::system::parseLanguagePacks(ran.text);
}

bool packageInstalled(const std::string& name) {
    if (!nexus::system::rpmDatabasePresent()) return false;
    return runProgram({"rpm", "-q", name}, nexus::system::nothingKept()).exitCode == 0;
}

bool packageOffered(const std::string& name) {
    const auto ran = runProgram(
        {"dnf", "-q", "repoquery", "--qf", "%{name}\\n", name},
        nexus::system::ProgramOptions());
    return ran.text.find(name) != std::string::npos;
}

std::vector<std::string> addedLanguageCodes() {
    std::vector<std::string> codes;
    if (!nexus::system::rpmDatabasePresent()) return codes;
    const auto ran = runProgram(
        {"rpm", "-qa", "--qf", "%{NAME}\\n", "langpacks-*"},
        nexus::system::ProgramOptions());
    std::istringstream lines(ran.text);
    std::string line;
    while (std::getline(lines, line)) {
        const std::string prefix = "langpacks-";
        if (line.rfind(prefix, 0) != 0) {
            continue;
        }
        const std::string code = line.substr(prefix.size());
        if (code.rfind("core-", 0) == 0 || code.rfind("fonts-", 0) == 0) {
            continue;
        }
        codes.push_back(code);
    }
    return codes;
}

// The desktop keyboard layouts, as the person types: labwc's file for
// the minimal edition, localectl for KDE.
std::string keyboardLayouts() {
    const char* home = std::getenv("HOME");
    if (home != nullptr) {
        std::istringstream lines(readWholeFile(
            std::string(home) + "/.config/lxqt/labwc/environment"));
        std::string line, layouts, variants;
        while (std::getline(lines, line)) {
            if (line.rfind("XKB_DEFAULT_LAYOUT=", 0) == 0) {
                layouts = line.substr(19);
            } else if (line.rfind("XKB_DEFAULT_VARIANT=", 0) == 0) {
                variants = line.substr(20);
            }
        }
        if (!layouts.empty()) {
            return variants.empty() || variants == ","
                ? layouts : layouts + " (variants: " + variants + ")";
        }
    }
    const auto ran = runProgram({"localectl", "status"},
                                nexus::system::ProgramOptions());
    std::istringstream lines(ran.text);
    std::string line;
    while (std::getline(lines, line)) {
        const auto at = line.find("X11 Layout:");
        if (at != std::string::npos) {
            return line.substr(line.find_first_not_of(' ', at + 11));
        }
    }
    return "unknown";
}

const nexus::system::Language* languageOrSuggest(
    const std::vector<nexus::system::Language>& languages,
    const std::string& typed
) {
    const auto* found = nexus::system::findLanguage(languages, typed);
    if (found != nullptr) {
        return found;
    }
    std::cerr << "No language called " << typed << ".\n";
    std::vector<std::string> near;
    std::string want = typed;
    for (char& c : want) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (const auto& language : languages) {
        std::string name = language.name;
        for (char& c : name) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if ((want.size() >= 3 && name.find(want) != std::string::npos) ||
            closeSpelling(name, want)) {
            near.push_back(language.name);
        }
    }
    if (!near.empty()) {
        std::cerr << "Did you mean:";
        for (const auto& name : near) {
            std::cerr << " " << name;
        }
        std::cerr << "?\n";
    } else {
        std::cerr << "'nexus language list' shows them all.\n";
    }
    return nullptr;
}

// glibc's own description of a locale, e.g. "Serbian locale for
// Serbia with Latin" -- every locale is installed (glibc-all-langpacks),
// so this is there; Nexus's short form when it is not.
std::string localeInWords(const std::string& locale) {
    // In that locale: LC_ALL for this one program.
    nexus::system::ProgramOptions inLocale;
    inLocale.environment = {{"LC_ALL", locale}};
    const auto ran = runProgram({"locale", "title"}, inLocale);
    std::string title = ran.text;
    while (!title.empty() && (title.back() == '\n' || title.back() == ' ')) {
        title.pop_back();
    }
    if (ran.exitCode != 0 || title.empty() ||
        title.find('\n') != std::string::npos || title == "C locale" ||
        title.rfind("ISO/IEC 14652", 0) == 0) {
        // The last one is glibc's fallback for a locale it cannot load.
        return nexus::system::describeLocale(locale);
    }
    return title;
}

}  // namespace

int commandLanguage(
    const std::vector<std::string>& words,
    bool apply,
    bool assumeYes,
    const std::string& invocation
) {
    const std::string action = words.empty() ? "" : words[0];
    const std::string what = words.size() > 1 ? words[1] : "";

    if (action.empty()) {
        const std::string lang = nexus::system::localeConfLang(
            readWholeFile("/etc/locale.conf"));

        std::cout
            << "Language     " << (lang.empty() ? "not set" : lang);
        if (!lang.empty()) {
            std::cout << "  (" << localeInWords(lang) << ")";
        }
        std::cout
            << "\n             menus, messages, dates and numbers\n"
            << "Keyboard     " << keyboardLayouts() << "\n"
            << "             what the keys type; Alt+Shift switches\n";

        const auto added = addedLanguageCodes();
        std::cout << "Packs added  ";
        if (added.empty()) {
            std::cout << "none";
        }
        for (std::size_t i = 0; i < added.size(); ++i) {
            std::cout << (i ? ", " : "") << added[i];
        }
        std::cout
            << "\n\nThe keyboard and the language are separate: a Serbian "
               "keyboard\nwith English menus is normal. To change the "
               "language:\n\n"
            << "    nexus language list            every language Fedora offers\n"
            << "    nexus language add <name>      fonts, spell checking, the rest\n"
            << "    nexus language set <name>      switch menus and formats to it\n";
        return 0;
    }

    if (action == "list") {
        const auto languages = fedoraLanguages();
        if (languages.empty()) {
            std::cerr << "dnf gave no list (no dnf here, or no metadata yet:"
                         " 'sudo dnf makecache').\n";
            return 1;
        }
        const auto added = addedLanguageCodes();
        for (const auto& language : languages) {
            const bool have = std::find(added.begin(), added.end(),
                                        language.code) != added.end();
            std::cout << "    " << std::left << std::setw(8) << language.code
                      << language.name << (have ? "   [added]" : "") << "\n";
        }
        std::cout << "\n" << languages.size() << " languages. Add one: "
                     "nexus language add <name>\n";
        return 0;
    }

    if (action == "add") {
        if (what.empty()) {
            std::cerr << "Usage: nexus language add <name or code>\n";
            return 2;
        }
        const auto languages = fedoraLanguages();
        if (languages.empty()) {
            std::cerr << "dnf gave no list (no dnf here, or no metadata yet:"
                         " 'sudo dnf makecache').\n";
            return 1;
        }
        const auto* language = languageOrSuggest(languages, what);
        if (language == nullptr) {
            return 1;
        }

        const std::string pack = "langpacks-" + language->code;
        const std::string speller = "hunspell-" + language->code;
        std::vector<std::string> wanted;
        if (!packageInstalled(pack)) {
            wanted.push_back(pack);
        }
        if (!packageInstalled(speller) && packageOffered(speller)) {
            wanted.push_back(speller);
        }

        std::cout << "\nAdd " << language->name << "\n\n";

        // Names come from dnf's answer about the repositories, not from
        // the person: only package names go on (0.1.84, outside review).
        wanted.erase(std::remove_if(wanted.begin(), wanted.end(), [](const std::string& n) {
            if (nexus::system::isPackageName(n)) return false;
            std::cerr << "Left out, not a package name: " << n << "\n";
            return true;
        }), wanted.end());
        if (wanted.empty()) {
            std::cout << "Already added. To switch the menus to it:\n"
                      << "    nexus language set " << language->code << "\n";
            return 0;
        }

        std::cout << "  Brings      ";
        for (std::size_t i = 0; i < wanted.size(); ++i) {
            const std::string& name = wanted[i];
            std::cout << (i ? "              " : "") << name
                      << (name == pack
                              ? "  (with its core and fonts packages)"
                              : "  (spell checking)")
                      << "\n";
        }
        std::cout
            << "  Note        most programs already carry their own "
               "translations;\n"
            << "              this adds the fonts, the spell checker and "
               "the rest\n";

        const bool image = std::filesystem::exists("/run/ostree-booted");
        if (image) {
            std::cout << "  Restart     needed: added packages take effect "
                         "the next time the machine starts\n";
        }
        std::cout << "  Menus       unchanged; switching is a separate "
                     "step: nexus language set " << language->code << "\n";

        if (!apply) {
            std::cout << "\nNothing has been changed. To add it:\n"
                      << "    sudo " << invocation << " --apply\n";
            return 0;
        }
        if (!nexus::system::haveRootPrivileges()) {
            std::cout << "\nAdding a language needs root. Re-run with sudo:\n"
                      << "    sudo " << invocation << "\n";
            return 1;
        }
        if (!confirmed(assumeYes)) {
            std::cout << "Nothing has been changed.\n";
            return 1;
        }

        std::string names;
        for (const auto& name : wanted) {
            names += (names.empty() ? "" : " ") + name;
        }

        nexus::system::TransactionRecord record;
        record.when = nexus::system::currentTimestamp();
        record.request = "language " + language->code;
        record.resolved = names;
        record.packages = std::set<std::string>(wanted.begin(), wanted.end());
        const std::string log = nexus::system::defaultTransactionLog();
        const std::string marker = nexus::system::beginTransaction(log, record);

        std::cout << "\n";
        const auto applied = image
            ? nexus::system::layerWithRpmOstree(wanted)
            : [&] {
                  nexus::system::ApplyResult result;
                  std::vector<std::string> argv{"dnf", "install", "-y"};
                  argv.insert(argv.end(), wanted.begin(), wanted.end());
                  const auto ran = runProgram(argv, mergedErrors());
                  result.exitCode = ran.exitCode;
                  result.output.push_back(ran.text);
                  result.outcome = !ran.ran ? nexus::system::ApplyOutcome::Unavailable
                      : ran.exitCode == 0 ? nexus::system::ApplyOutcome::Applied
                                          : nexus::system::ApplyOutcome::Failed;
                  return result;
              }();
        for (const auto& line : applied.output) {
            std::cout << line << "\n";
        }

        record.outcome = toString(applied.outcome);
        record.exitCode = applied.exitCode;
        record.succeeded =
            applied.outcome == nexus::system::ApplyOutcome::Applied;
        nexus::system::finishTransaction(log, marker, record);

        if (applied.outcome == nexus::system::ApplyOutcome::Staged) {
            std::cout << "\n" << language->name << " is staged: restart to "
                         "finish (systemctl reboot).\nThen, to switch the "
                         "menus: nexus language set " << language->code
                      << "\n";
            return 0;
        }
        if (applied.outcome == nexus::system::ApplyOutcome::Applied) {
            std::cout << "\nAdded. To switch the menus: nexus language set "
                      << language->code << "\n";
            return 0;
        }
        std::cout << "\nThat did not complete (exit " << applied.exitCode
                  << "). Nothing was added.\n";
        return 1;
    }

    if (action == "set") {
        if (what.empty()) {
            std::cerr << "Usage: nexus language set <name, code or locale>\n";
            return 2;
        }

        // A code ("sr") or a locale ("sr_RS.UTF-8@latin") is used as it
        // is; a name ("serbian") is looked up in Fedora's list.
        std::string code = what;
        std::string shown = what;
        const bool looksLikeCode =
            what.find('_') != std::string::npos ||
            (what.size() <= 3 &&
             std::all_of(what.begin(), what.end(), [](char c) {
                 return std::islower(static_cast<unsigned char>(c));
             }));
        if (!looksLikeCode) {
            const auto languages = fedoraLanguages();
            const auto* language = languageOrSuggest(languages, what);
            if (language == nullptr) {
                return 1;
            }
            code = language->code;
            shown = language->name;
        }

        const auto listing = runProgram({"localectl", "list-locales"},
                                        nexus::system::ProgramOptions());
        const auto locales = nexus::system::localesFor(listing.text, code);

        if (locales.empty()) {
            std::cerr << "This system has no locale for " << shown << ".\n";
            return 1;
        }
        if (locales.size() > 1) {
            std::cout << shown << " comes in " << locales.size()
                      << " forms (language, country, script):\n\n";
            for (const auto& locale : locales) {
                std::cout << "    " << std::left << std::setw(22) << locale
                          << localeInWords(locale) << "\n";
            }
            std::cout << "\nPick one, for example:\n    nexus language set "
                      << locales.back() << "\n";
            return 0;
        }

        const std::string& locale = locales.front();
        const std::string current = nexus::system::localeConfLang(
            readWholeFile("/etc/locale.conf"));

        if (locale == current) {
            std::cout << "The system language is already " << locale << ".\n";
            return 0;
        }

        std::cout
            << "\nSwitch the system language\n\n"
            << "  From        " << (current.empty() ? "not set" : current) << "\n"
            << "  To          " << locale << "  ("
            << localeInWords(locale) << ")\n"
            << "  Changes     menus, messages, dates and numbers, from the "
               "next login\n"
            << "  Keeps       the keyboard layouts, and everybody's files\n";
        if (std::filesystem::exists("/usr/bin/plasmashell")) {
            std::cout << "  KDE         a language chosen in System Settings -> "
                         "Region & Language\n              wins over this one, "
                         "for that person\n";
        }
        if (!packageInstalled("langpacks-" +
                              nexus::system::localeLanguage(locale))) {
            std::cout << "  Tip         nexus language add "
                      << nexus::system::localeLanguage(locale)
                      << " adds its fonts and spell checker\n";
        }
        std::cout << "  Back        nexus language set "
                  << (current.empty() ? "en_US.UTF-8" : current) << "\n";

        if (!apply) {
            std::cout << "\nNothing has been changed. To switch:\n"
                      << "    sudo " << invocation << " --apply\n";
            return 0;
        }
        if (!nexus::system::haveRootPrivileges()) {
            std::cout << "\nChanging the system language needs root. "
                         "Re-run with sudo:\n    sudo " << invocation << "\n";
            return 1;
        }
        if (!confirmed(assumeYes)) {
            std::cout << "Nothing has been changed.\n";
            return 1;
        }

        const auto ran = runProgram({"localectl", "set-locale", "LANG=" + locale},
                                    mergedErrors());
        if (ran.exitCode != 0) {
            std::cout << "localectl refused (exit " << ran.exitCode << "): "
                      << ran.text << "\nNothing has been changed.\n";
            return 1;
        }
        std::cout << "\nSet. Log out and back in to see it.\n";
        return 0;
    }

    std::cerr << "Unknown: nexus language " << action << "\n";
    // "set_RS.UTF-8@latin": a space lost after set (Asus, 4 October).
    for (const std::string verb : {"set", "add"}) {
        if (action.size() > verb.size() && action.rfind(verb, 0) == 0) {
            std::string rest = action.substr(verb.size());
            if (rest[0] == '_' || rest[0] == '-') {
                rest = rest.substr(1);
            }
            std::cerr << "Did you mean: nexus language " << verb << " "
                      << (verb == "set" && action[verb.size()] == '_'
                              ? "<language>_" + rest
                              : rest)
                      << " ?\n";
            return 2;
        }
    }
    std::cerr << "Use: nexus language [list | add <name> | set <name>]\n";
    return 2;
}

}  // namespace nexus_cli
