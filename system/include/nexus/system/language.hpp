#pragma once

// nexus language: which languages the system speaks, and adding one.
//
// A language is not a keyboard. The keyboard is what the keys type
// (Alt+Shift switches it); the language is what menus, messages, dates
// and numbers are written in. Somebody with a Serbian keyboard can
// still have every menu in English, and that is the usual case.
//
// What Fedora 44 offers, checked on the Asus on 4 October:
//   - 101 languages, one package each: langpacks-<code>, summary
//     "<Name> langpacks meta-package". It brings langpacks-core-<code>
//     and langpacks-fonts-<code>, and recommends nothing.
//   - The spell checker is separate: hunspell-<code>.
//   - Dates and numbers for every language are already there
//     (glibc-all-langpacks is in the image).
//   - Most programs already carry their own translations: 96 for
//     Serbian (Cyrillic), 46 for Serbian (Latin), 23 for Macedonian in
//     /usr/share/locale. So switching the display language does most
//     of the work; the pack adds fonts, spell checking and the rest.
//
// Pure functions over text, so they are tested without dnf; the CLI
// runs the commands and passes the output in.

#include <string>
#include <vector>

namespace nexus::system {

struct Language {
    std::string code;   // "sr"
    std::string name;   // "Serbian"
};

// Lines of `dnf repoquery --qf '%{name}|%{summary}\n' 'langpacks-*'`.
// The core and fonts packages are parts of a language, not languages,
// and are left out. Sorted by name.
std::vector<Language> parseLanguagePacks(const std::string& text);

// A language by code ("sr") or name ("serbian", any case). Null when
// there is none.
const Language* findLanguage(const std::vector<Language>& languages,
                             const std::string& typed);

// The locales from `localectl list-locales` that belong to a language
// code: "sr" gives sr_ME.UTF-8, sr_RS.UTF-8, sr_RS.UTF-8@latin -- not
// "srn_..." or "en_..". A full locale typed exactly is returned alone.
std::vector<std::string> localesFor(const std::string& listing,
                                    const std::string& codeOrLocale);

// A locale in words: "sr_RS.UTF-8@latin" -> "sr, RS, Latin script".
std::string describeLocale(const std::string& locale);

// The value of LANG in /etc/locale.conf text, quotes removed.
std::string localeConfLang(const std::string& text);

// The language code of a locale: "sr_RS.UTF-8@latin" -> "sr".
std::string localeLanguage(const std::string& locale);

}
