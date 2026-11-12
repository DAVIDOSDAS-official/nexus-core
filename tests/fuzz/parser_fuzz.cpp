// Throws odd input at every text parser in Nexus and watches for a
// crash or an exception.
//
//     build/tests/parser_fuzz              a quick pass (ctest runs this)
//     build/tests/parser_fuzz 60           60 seconds per parser
//     build/tests/parser_fuzz 60 parseJson only that one
//
// Why (0.1.87, audit): these parsers read what Nexus does not control --
// other systems' os-release files and loader paths, package metadata,
// answers from ProtonDB, Steam and the registry, the output of forty
// tools. Unit tests check the inputs someone thought of; this checks
// the ones nobody did. It starts from a real sample for each parser
// and changes it: flipped bits, cut-off ends, repeated pieces, tokens
// that mean something to a parser (quotes, brackets, escapes, NUL
// bytes, numbers too long for any integer), up to 200 KB.
//
// Most useful in a build with the sanitizers on (see the Tests
// workflow), where reading one byte past a buffer stops the run; in an
// ordinary build it still catches crashes and stray exceptions. The
// random numbers are seeded with a fixed value, so a failure repeats.
// The input that failed is printed, escaped, to stderr.
//
// To add a parser: one line in the table below, with a real sample.

#include <nexus/architecture.hpp>
#include <nexus/system/alias_file.hpp>
#include <nexus/system/catalogue.hpp>
#include <nexus/system/clock.hpp>
#include <nexus/system/container.hpp>
#include <nexus/system/control_file.hpp>
#include <nexus/system/cyber.hpp>
#include <nexus/system/dependency_expression.hpp>
#include <nexus/system/dualboot.hpp>
#include <nexus/system/flatpak_source.hpp>
#include <nexus/system/gamecheck.hpp>
#include <nexus/system/gaming.hpp>
#include <nexus/system/json.hpp>
#include <nexus/system/language.hpp>
#include <nexus/system/nix_source.hpp>
#include <nexus/system/pacman_source.hpp>
#include <nexus/system/pins.hpp>
#include <nexus/system/portage_source.hpp>
#include <nexus/system/process.hpp>
#include <nexus/system/profile_file.hpp>
#include <nexus/system/protection.hpp>
#include <nexus/system/rollback.hpp>
#include <nexus/system/rpm_database.hpp>
#include <nexus/system/rpm_source.hpp>
#include <nexus/system/rpm_version.hpp>
#include <nexus/system/security_audit.hpp>
#include <nexus/system/services.hpp>
#include <nexus/system/snap_source.hpp>
#include <nexus/system/source_build.hpp>
#include <nexus/system/specs.hpp>
#include <nexus/system/update.hpp>
#include <nexus/system/version.hpp>
#include <nexus/system/vpn_check.hpp>
#include <nexus/system/xml.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using S = const std::string&;
namespace ns = nexus::system;

struct Target { const char* name; std::function<void(S)> run; std::vector<std::string> seeds; };

static std::mt19937_64 rng(20261112);
static const std::string TOKENS[] = {"{", "}", "[", "]", "\"", "\\", "\\u", "\\ud800", ":", ",", "<", ">", "</", "/>", "&", "&#", "&#x", ";", "=", "(", ")", "|", "~", "^", "-", ".", "0", "9999999999999999999999999", "-1", "\n", "\r", "\t", std::string(1, '\0'), "\x1b[", "\xe2\x80\xae", "\xff", "\xc2", "%", "%s", "<!--", "-->", "<![CDATA[", "]]>", "<?", "?>", "sha256:", "/", "@", " ", "  ", "null", "true", "e", "E+", "1e999", "::", "0x", ">=", "<<", "!", "*", "'"};

static std::string mutate(std::string s) {
    const int rounds = 1 + static_cast<int>(rng() % 6);
    for (int r = 0; r < rounds; ++r) {
        const std::size_t at = s.empty() ? 0 : rng() % (s.size() + 1);
        switch (rng() % 9) {
            case 0: if (!s.empty()) s[rng() % s.size()] = static_cast<char>(rng()); break;
            case 1: s.insert(at, TOKENS[rng() % (sizeof TOKENS / sizeof *TOKENS)]); break;
            case 2: if (!s.empty()) s.erase(rng() % s.size(), 1 + rng() % 8); break;
            case 3: s.resize(at); break;
            case 4: if (!s.empty()) { const std::size_t a = rng() % s.size(); const std::size_t n = 1 + rng() % 32; s.insert(at > s.size() ? s.size() : at, s.substr(a, n)); } break;
            case 5: { const std::string& t = TOKENS[rng() % (sizeof TOKENS / sizeof *TOKENS)]; std::string rep; const std::size_t n = 1 + rng() % 3000; for (std::size_t i = 0; i < n; ++i) rep += t; s.insert(at, rep); } break;
            case 6: if (!s.empty()) s[rng() % s.size()] ^= static_cast<char>(1u << (rng() % 8)); break;
            case 7: s.insert(at, 1, '\0'); break;
            case 8: { std::string digits(1 + rng() % 40, static_cast<char>('0' + rng() % 10)); s.insert(at, digits); } break;
        }
        if (s.size() > 200000) s.resize(200000);
    }
    return s;
}

int main(int argc, char** argv) {
    const double seconds = argc > 1 ? std::atof(argv[1]) : 0.15;
    const std::string only = argc > 2 ? argv[2] : "";
    std::vector<Target> targets = {
{"parseJson", [](S s){ ns::parseJson(s); }, {R"({"a":[1,2.5e3,"xé\n",null,true,{"b":{}}],"c":-0.1})"}},
{"XmlReader", [](S s){ ns::XmlReader r(s); int n = 0; while (r.next() != ns::XmlReader::Event::None && r.current() != ns::XmlReader::Event::Error && ++n < 2000000) { (void)r.name(); (void)r.text(); (void)r.attribute("href"); } }, {"<?xml version=\"1.0\"?><a b=\"c&amp;d\"><!-- x --><e f='g'/>text &#65;&#x41;<![CDATA[z]]></a>"}},
{"decodeEntities", [](S s){ ns::decodeEntities(s); }, {"a &amp; &lt; &#65; &#x1F600; &bogus; &#99999999999;"}},
{"parseRepodataPrimary", [](S s){ ns::parseRepodataPrimary(s); }, {"<metadata><package type=\"rpm\"><name>a</name><arch>x86_64</arch><version epoch=\"0\" ver=\"1\" rel=\"2\"/><size installed=\"12\"/><format><rpm:provides><rpm:entry name=\"a\" flags=\"EQ\" epoch=\"0\" ver=\"1\"/></rpm:provides><rpm:requires><rpm:entry name=\"(b if c)\"/></rpm:requires></format></package></metadata>"}},
{"parseCatalogue", [](S s){ std::string e; ns::parseCatalogue(s, "flathub", e); }, {"<components><component type=\"desktop-application\"><id>org.a.B</id><name>B</name><summary>s</summary><icon type=\"cached\" height=\"64\">b.png</icon><categories><category>Game</category></categories><releases><release version=\"1\" timestamp=\"1\"/></releases><screenshots><screenshot><image>https://x/y.png</image></screenshot></screenshots></component></components>"}},
{"parseDependencyField", [](S s){ ns::parseDependencyField(s); ns::parseProvidesField(s); }, {"a (>= 1.0) | b:any [amd64] <!nocheck>, c (<< 2~rc1), d"}},
{"parseRpmVersion", [](S s){ auto v = ns::parseRpmVersion(s); ns::compareRpmVersions(v, v); ns::compareRpmVersions(s, "1:2.3~rc1^git-4.fc44"); }, {"1:2.3~rc1^git-4.fc44"}},
{"parseVersion", [](S s){ ns::parseVersion(s); }, {"1:2.3.4-5ubuntu1~rc"}},
{"validateVersion", [](S s){ ns::validateVersion(s); }, {"1.2.3"}},
{"parseOsRelease", [](S s){ ns::parseOsRelease(s); ns::parseOsReleaseValue(s, "PRETTY_NAME"); }, {"NAME=\"Fedora Linux\"\nPRETTY_NAME='Nexus \\\"x\\\"'\nID=fedora\n"}},
{"parseSystemUpdate", [](S s){ ns::parseSystemUpdate(s); }, {R"({"deployments":[{"booted":true,"container-image-reference":"ostree-image-signed:docker://ghcr.io/a/b:kde","container-image-reference-digest":"sha256:ab","version":"44.1","timestamp":1,"requested-packages":["a"],"packages":["a"]},{"staged":true,"version":"44.2"}],"transaction":null})"}},
{"parseImageInspect", [](S s){ ns::parseImageInspect(s); }, {R"({"Digest":"sha256:ab","Created":"2026-11-01T00:00:00Z","Labels":{"nexus.version":"0.1.86","org.opencontainers.image.version":"44.1"}})"}},
{"parseImageRef", [](S s){ ns::parseImageRef(s); }, {"ostree-image-signed:docker://ghcr.io/a/b:kde", "ostree-unverified-registry:ghcr.io/a/b@sha256:ab"}},
{"parseUpdateLines", [](S s){ ns::parseUpdateLines(s); }, {"system\tupdate\t0.1.86\t2026-11-01\napp\torg.a.B\t1.0\nstate\tbusy\n"}},
{"parseFlatpakUpdates", [](S s){ ns::parseFlatpakUpdates(s); }, {"org.example.App\t1.0\norg.example.B\n"}},
{"parseProtonSummary", [](S s){ ns::parseProtonSummary(s); }, {R"({"tier":"gold","confidence":"strong","score":0.7,"total":120,"trendingTier":"platinum","bestReportedTier":"platinum"})"}},
{"parseSteamSearch", [](S s){ ns::parseSteamSearch(s); }, {R"({"total":1,"items":[{"type":"app","name":"X","id":730,"platforms":{"linux":true}}]})"}},
{"parseLsblk", [](S s){ std::string e; ns::parseLsblk(s, e); }, {R"({"blockdevices":[{"name":"nvme0n1","path":"/dev/nvme0n1","type":"disk","size":"256G","children":[{"name":"p1","path":"/dev/nvme0n1p1","type":"part","fstype":"vfat","parttype":"C12A7328-F81F-11D2-BA4B-00A0C93EC93B","uuid":"AB-CD","size":123}]}]})"}},
{"parseOstreeAdminStatus", [](S s){ ns::parseOstreeAdminStatus(s); }, {"* default abc.0 (staged)\n    Version: 44.1\n    origin refspec: x\n  default def.0 (rollback)\n"}},
{"parseServiceState", [](S s){ ns::parseServiceState(s); ns::parseUnitFiles(s); }, {"a.service enabled enabled\nb.timer disabled -\n"}},
{"parseZoneInfo", [](S s){ ns::parseZoneInfo(s); ns::parseActiveZones(s); ns::wideOpenRange(s); }, {"public (default, active)\n  target: default\n  interfaces: wlan0\n  sources: 10.0.0.0/8\n  services: ssh mdns\n  ports: 8080/tcp 1025-65535/udp\n"}},
{"parseListeners", [](S s){ ns::parseListeners(s); ns::parseIpAddresses(s); }, {"tcp LISTEN 0 128 0.0.0.0:22 0.0.0.0:* users:((\"sshd\",pid=1,fd=3))\n2: wlan0    inet 192.168.0.17/24 brd 192.168.0.255 scope global\n"}},
{"parseResolvectl", [](S s){ ns::parseResolvectl(s); ns::parseResolvConf(s); ns::parseLinks(s); ns::parseRouteDev(s); ns::parseMullvadLockdown(s); }, {"Global\n  Protocols: +LLMNR\nLink 2 (wlan0)\n  Current DNS Server: 10.2.0.1\n  DNS Servers: 10.2.0.1 fe80::1%2\nnameserver 1.1.1.1\n1.1.1.1 dev wg0 src 10.2.0.2\n"}},
{"parsePins", [](S s){ ns::parsePins(s); }, {"docker.io/library/caddy:2 docker.io/library/caddy@sha256:ab\n# c\n"}},
{"parseChrony", [](S s){ ns::parseChronyOffset(s); ns::parseSysctlInt(s); ns::parseHumanSize(s); ns::localeConfLang(s); }, {"System time     : 0.000012 seconds slow of NTP time\n", "95.4G", "LANG=en_US.UTF-8\n"}},
{"specs", [](S s){ ns::parseCpuCores(s); ns::parseCpuModel(s); ns::parseUptimeSeconds(s); std::uint64_t a = 0, b = 0; ns::parseMemKiB(s, a, b); }, {"model name\t: Intel(R) N4020\nprocessor\t: 0\ncpu cores\t: 2\nMemTotal: 3800000 kB\nMemAvailable: 100 kB\n", "12345.67 9.9\n"}},
{"printable", [](S s){ auto p = ns::printable(s); for (unsigned char c : p) if (c == 0x1b || c == 0x07) std::abort(); ns::shellQuote(s); ns::urlEncode(s); ns::isPackageName(s); }, {"a\x1b]52;c;x\x07 \xe2\x80\xae b"}},
{"parseLanguagePacks", [](S s){ ns::parseLanguagePacks(s); }, {"langpacks-sr.noarch  4.2-1.fc44  fedora\nlangpacks-core-font-sr.noarch 1 2\n"}},
{"parseControlStream", [](S s){ std::istringstream in(s); ns::parseControlStream(in); }, {"Package: a\nVersion: 1\nDepends: b (>= 1),\n c\nDescription: x\n y\n\nPackage: b\n"}},
{"parseProfileStream", [](S s){ std::istringstream in(s); ns::parseProfileStream(in); std::istringstream in2(s); ns::parseAliasStream(in2); }, {"name: kde\ninherits: base\nrequires:\n  - desktop\n  - web-browser\nflatpaks:\n  - org.a.B\n", "web-browser: firefox chromium\n"}},
{"parseProtectionRules", [](S s){ ns::parseProtectionRules(s); }, {"protect kernel\nprotect glibc # x\nallow foo\n"}},
{"RpmDatabase::parse", [](S s){ ns::RpmDatabase::parse(s); }, {"a\t0\t1\t2\tx86_64\t123\tsummary\tb >= 1,c\td\n"}},
{"Container::parseList", [](S s){ ns::Containers::parseList(s); ns::parseDistroboxList(s, "nexus-dev"); }, {"ID | NAME | STATUS | IMAGE\nabc | nexus-dev | Up 1h | fedora-toolbox:44\n"}},
{"otherSources", [](S s){ ns::parsePacmanDatabase(s); ns::parseSnapList(s); ns::parseNixProfile(s); ns::parsePortageAtom(s); ns::parsePacmanDependency(s); ns::parsePortageEntry("app-misc", s, s); ns::parseSourceStanza("a", s); nexus::parseMultiArch(s); ns::comparePortageVersions(s, "1.2-r3"); ns::comparePortageVersions("1.2-r3", s); }, {"%NAME%\na\n\n%VERSION%\n1-1\n\n%DEPENDS%\nb>=1\n", ">=app-misc/foo-1.2.3_p4-r1:0/1[bar]", "Name Version Rev\ncore 16 1\n"}},
    };
    int failures = 0;
    for (auto& t : targets) {
        if (!only.empty() && only != t.name) continue;
        t.seeds.push_back("");
        std::vector<std::string> corpus = t.seeds;
        const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
        std::size_t runs = 0, thrown = 0;
        std::string firstThrow;
        while (std::chrono::steady_clock::now() < end) {
            std::string input = mutate(corpus[rng() % corpus.size()]);
            try {
                t.run(input);
            } catch (const std::exception& e) {
                if (thrown++ == 0) {
                    firstThrow = e.what();
                    std::fprintf(stderr, "%s threw \"%s\" on this input (%zu bytes, first 400 shown):\n", t.name, e.what(), input.size());
                    for (std::size_t i = 0; i < input.size() && i < 400; ++i) {
                        const unsigned char c = static_cast<unsigned char>(input[i]);
                        if (c >= 0x20 && c < 0x7f && c != '\\') std::fputc(c, stderr);
                        else std::fprintf(stderr, "\\x%02x", c);
                    }
                    std::fputc('\n', stderr);
                }
            }
            if (rng() % 16 == 0 && corpus.size() < 512) corpus.push_back(input);
            ++runs;
        }
        std::printf("%-28s %8zu runs  %s\n", t.name, runs, thrown ? ("THROWS x" + std::to_string(thrown) + ": " + firstThrow).c_str() : "ok");
        std::fflush(stdout);
        failures += thrown ? 1 : 0;
    }
    if (failures != 0) {
        std::printf("%d parser(s) threw on odd input.\n", failures);
        return 1;
    }
    return 0;
}
