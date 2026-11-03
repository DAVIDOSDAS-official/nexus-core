#include <nexus/system/vpn_check.hpp>

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace nexus::system {

namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::vector<std::string> words(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> out;
    std::string w;
    while (in >> w) out.push_back(w);
    return out;
}

bool startsWith(const std::string& s, const std::string& p) {
    return s.rfind(p, 0) == 0;
}

bool contains(const std::string& s, const std::string& p) {
    return s.find(p) != std::string::npos;
}

// "Protocols: ..." style key at the start of a resolvectl line. IPv6
// continuation lines ("fd00::1") have no "word: " key.
bool splitKey(const std::string& line, std::string& key, std::string& value) {
    const std::string t = trim(line);
    if (t.empty() || !std::isalpha(static_cast<unsigned char>(t[0]))) return false;
    const auto colon = t.find(':');
    if (colon == std::string::npos) return false;
    for (std::size_t i = 0; i < colon; ++i) {
        const char c = t[i];
        if (!(std::isalpha(static_cast<unsigned char>(c)) || c == ' ' || c == '/' || c == '-')) return false;
    }
    if (colon + 1 < t.size() && t[colon + 1] != ' ') return false;
    key = t.substr(0, colon);
    value = trim(t.substr(colon + 1));
    return true;
}

std::string bareServer(std::string s) {
    const auto hash = s.find('#');
    if (hash != std::string::npos) s = s.substr(0, hash);
    return s;
}

const NetLink* findLink(const std::vector<NetLink>& links, const std::string& name) {
    for (const auto& l : links) if (l.name == name) return &l;
    return nullptr;
}

}  // namespace

std::vector<NetLink> parseLinks(const std::string& text) {
    std::vector<NetLink> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        // "5: wg0: <POINTOPOINT,NOARP,UP,LOWER_UP> mtu 1420 ... wireguard ..."
        const auto c1 = line.find(": ");
        if (c1 == std::string::npos) continue;
        const auto c2 = line.find(':', c1 + 2);
        if (c2 == std::string::npos) continue;
        std::string name = trim(line.substr(c1 + 2, c2 - c1 - 2));
        const auto at = name.find('@');
        if (at != std::string::npos) name = name.substr(0, at);
        if (name.empty()) continue;

        NetLink link;
        link.name = name;
        const auto lt = line.find('<', c2), gt = line.find('>', c2);
        if (lt != std::string::npos && gt != std::string::npos && gt > lt) {
            std::string flags = "," + line.substr(lt + 1, gt - lt - 1) + ",";
            link.up = contains(flags, ",UP,");
        }
        const std::string rest = " " + (gt == std::string::npos ? line : line.substr(gt + 1)) + " ";
        std::string spaced = rest;
        std::replace(spaced.begin(), spaced.end(), '\\', ' ');
        link.wireguard = contains(spaced, " wireguard ");
        link.tun = contains(spaced, " tun ") || contains(spaced, " tap ");
        out.push_back(link);
    }
    return out;
}

std::string parseRouteDev(const std::string& text) {
    const auto w = words(text);
    for (std::size_t i = 0; i + 1 < w.size(); ++i) {
        if (w[i] == "dev") return w[i + 1];
    }
    return "";
}

DnsView parseResolvectl(const std::string& text) {
    DnsView view;
    std::istringstream in(text);
    std::string line;
    DnsLink* current = nullptr;
    bool inGlobal = false;
    std::string lastKey;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty()) { lastKey.clear(); continue; }
        if (t == "Global") {
            view.resolved = true; inGlobal = true; current = nullptr; lastKey.clear(); continue;
        }
        if (startsWith(t, "Link ") && t.find('(') != std::string::npos && t.back() == ')') {
            view.resolved = true; inGlobal = false;
            const auto a = t.find('('), b = t.rfind(')');
            view.links.push_back(DnsLink{});
            current = &view.links.back();
            current->name = t.substr(a + 1, b - a - 1);
            lastKey.clear();
            continue;
        }
        std::string key, value;
        if (splitKey(line, key, value)) {
            lastKey = key;
        } else if (lastKey == "DNS Servers" || lastKey == "DNS Domain") {
            value = t;          // continuation of the previous key
            key = lastKey;
        } else {
            continue;
        }

        if (key == "DNS Servers") {
            for (const auto& s : words(value)) {
                if (inGlobal) view.globalServers.push_back(bareServer(s));
                else if (current) current->servers.push_back(bareServer(s));
            }
        } else if (key == "DNS Domain" && current) {
            for (const auto& d : words(value)) if (d == "~.") current->catchAll = true;
        } else if (key == "Protocols" && current) {
            for (const auto& p : words(value)) if (p == "+DefaultRoute") current->defaultRoute = true;
        } else if (key == "DefaultRoute setting" && current) {
            current->defaultRoute = value == "yes";
        }
    }
    return view;
}

std::vector<std::string> parseResolvConf(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const auto w = words(line);
        if (w.size() >= 2 && w[0] == "nameserver") out.push_back(w[1]);
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> dnsInUse(const DnsView& view) {
    std::vector<std::pair<std::string, std::string>> out;
    std::set<std::string> seen;
    auto add = [&](const std::string& server, const std::string& link) {
        if (server.empty() || server == "127.0.0.53" || server == "127.0.0.54") return;
        if (seen.insert(server).second) out.emplace_back(server, link);
    };
    bool anyCatchAll = false;
    for (const auto& l : view.links) if (l.catchAll && !l.servers.empty()) anyCatchAll = true;
    if (anyCatchAll) {
        for (const auto& l : view.links)
            if (l.catchAll) for (const auto& s : l.servers) add(s, l.name);
        return out;
    }
    for (const auto& l : view.links)
        if (l.defaultRoute) for (const auto& s : l.servers) add(s, l.name);
    for (const auto& s : view.globalServers) add(s, "");
    return out;
}

std::string tunnelKind(const NetLink& link) {
    const std::string& n = link.name;
    // Proton's kill-switch and IPv6-leak helpers are dummy interfaces,
    // not tunnels.
    if (startsWith(n, "pvpnksintrf") || startsWith(n, "ipv6leakintrf")) return "";
    if (contains(n, "tailscale")) return "Tailscale";
    if (contains(n, "mullvad")) return "Mullvad";
    if (startsWith(n, "proton") || startsWith(n, "pvpn")) return "Proton VPN";
    if (n == "nordlynx") return "NordVPN";
    if (link.wireguard) return "WireGuard";
    if (link.tun || startsWith(n, "tun") || startsWith(n, "tap")) return "OpenVPN";
    return "";
}

bool rulesetBlocksOutside(const std::string& ruleset, const std::string& tunnel) {
    std::istringstream in(ruleset);
    std::string line;
    const std::string notOut = "oifname != \"" + tunnel + "\"";
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (startsWith(t, "table ") &&
            (contains(t, "mullvad") || contains(t, "killswitch") || contains(t, "kill-switch") ||
             contains(t, "kill_switch"))) {
            return true;
        }
        if (!tunnel.empty() && contains(t, notOut) &&
            (contains(t, " reject") || contains(t, " drop"))) {
            return true;
        }
    }
    return false;
}

std::string parseMullvadLockdown(const std::string& text) {
    std::string lower;
    for (char c : text) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    const auto colon = lower.rfind(':');
    const std::string tail = trim(colon == std::string::npos ? lower : lower.substr(colon + 1));
    if (startsWith(tail, "on") || startsWith(tail, "enabled")) return "on";
    if (startsWith(tail, "off") || startsWith(tail, "disabled")) return "off";
    return "";
}

VpnReport assessVpn(const VpnFacts& f) {
    VpnReport r;
    auto isTunnel = [&](const std::string& dev) {
        const NetLink* l = findLink(f.links, dev);
        return l && !tunnelKind(*l).empty();
    };

    std::vector<const NetLink*> up;
    for (const auto& l : f.links) if (l.up && !tunnelKind(l).empty()) up.push_back(&l);

    if (isTunnel(f.route4)) {
        r.tunnel = f.route4;
        r.kind = tunnelKind(*findLink(f.links, f.route4));
    }

    if (r.tunnel.empty()) {
        VpnLine vpn{"VPN", VpnHealth::Info, "", ""};
        const NetLink* ts = nullptr;
        for (auto* l : up) if (tunnelKind(*l) == "Tailscale") ts = l;
        if (ts && up.size() == 1) {
            vpn.detail = "Tailscale is connected to your own devices, but internet traffic goes out"
                         " through " + (f.route4.empty() ? std::string("the normal connection") : f.route4) +
                         " directly (no exit node). Normal, unless you meant to use one.";
            vpn.suggestion = "tailscale set --exit-node=<device>";
        } else if (!up.empty()) {
            vpn.health = VpnHealth::Warning;
            vpn.detail = tunnelKind(*up.front()) + " on " + up.front()->name +
                         " is up, but internet traffic does not go through it: it goes out through " +
                         (f.route4.empty() ? std::string("no route") : f.route4) + ".";
            vpn.suggestion = "check the VPN's routes (WireGuard: AllowedIPs = 0.0.0.0/0, ::/0)";
        } else {
            vpn.detail = "None active. Traffic goes out through " +
                         (f.route4.empty() ? std::string("no route at all (offline?)") : f.route4) +
                         " directly.";
            vpn.suggestion = "nexus setup vpn   (the tools; the VPN itself is your provider or your own server)";
        }
        r.lines.push_back(vpn);
        return r;
    }

    {
        VpnLine vpn{"VPN", VpnHealth::Ok, r.kind + " on " + r.tunnel, ""};
        if (!f.configPath.empty()) vpn.detail += " (" + f.configPath + ")";
        if (r.kind == "Tailscale") vpn.detail += ", through an exit node";
        vpn.detail += ".";
        r.lines.push_back(vpn);
        r.lines.push_back({"Traffic", VpnHealth::Ok, "IPv4 goes through " + r.tunnel + ".", ""});
    }

    // IPv6: the classic leak -- a VPN that only tunnels IPv4.
    {
        VpnLine v6{"IPv6", VpnHealth::Ok, "", ""};
        if (!f.ipv6Reachable) {
            v6.detail = "No IPv6 route, so nothing can leak there.";
        } else if (f.route6 == r.tunnel || isTunnel(f.route6)) {
            v6.detail = "Goes through " + f.route6 + ".";
        } else {
            v6.health = VpnHealth::Problem;
            v6.detail = "Goes out through " + f.route6 +
                        ", outside the VPN: sites reachable over IPv6 see your real address.";
            if (r.kind == "Mullvad") v6.suggestion = "mullvad tunnel set ipv6 on";
            else if (r.kind == "WireGuard")
                v6.suggestion = "add ::/0 to AllowedIPs" +
                                (f.configPath.empty() ? std::string("") : " in " + f.configPath);
            else v6.suggestion = "turn on IPv6 (or IPv6 leak protection) in the VPN app";
        }
        r.lines.push_back(v6);
    }

    // DNS: where lookups go, and whether that path is the tunnel.
    {
        VpnLine dns{"DNS", VpnHealth::Ok, "", ""};
        std::vector<std::pair<std::string, std::string>> servers = dnsInUse(f.dns);
        std::vector<std::string> inside, outside;
        for (const auto& [server, link] : servers) {
            std::string dev;
            const auto pct = server.find('%');
            if (pct != std::string::npos) dev = server.substr(pct + 1);
            else {
                auto it = f.dnsRoute.find(server);
                if (it != f.dnsRoute.end()) dev = it->second;
            }
            if (dev.empty()) dev = link;
            if (dev == "lo" || dev == r.tunnel || isTunnel(dev)) inside.push_back(server + " through " + (dev.empty() ? "?" : dev));
            else outside.push_back(server + " through " + (dev.empty() ? "an unknown path" : dev));
        }
        auto join = [](const std::vector<std::string>& v) {
            std::string s;
            for (std::size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + v[i];
            return s;
        };
        if (servers.empty()) {
            dns.health = VpnHealth::Warning;
            dns.detail = "No DNS servers found, so where lookups go could not be checked.";
        } else if (outside.empty()) {
            dns.detail = "Lookups go to " + join(inside) + ".";
        } else {
            dns.health = VpnHealth::Problem;
            dns.detail = "Lookups go to " + join(outside) +
                         ", outside the VPN: that network can see every site you look up.";
            if (r.kind == "WireGuard")
                dns.suggestion = "set DNS = <a server inside the tunnel>" +
                                 (f.configPath.empty() ? std::string("") : " in " + f.configPath);
            else dns.suggestion = "the VPN app's DNS setting (turn off custom DNS, or use the provider's)";
        }
        r.lines.push_back(dns);
    }

    // Kill switch: would traffic stop, or go out directly, if the tunnel dropped?
    {
        VpnLine ks{"Kill switch", VpnHealth::Ok, "", ""};
        bool protonKs = findLink(f.links, "pvpnksintrf0") != nullptr;
        if (r.kind == "Proton VPN" && protonKs) {
            ks.detail = "On (Proton VPN's kill switch).";
        } else if (r.kind == "Mullvad") {
            if (f.mullvadLockdown == "on") {
                ks.detail = "On: Mullvad blocks traffic outside the tunnel, also when disconnected (lockdown mode).";
            } else if (f.mullvadLockdown == "off") {
                ks.health = VpnHealth::Warning;
                ks.detail = "Partly: Mullvad blocks leaks while connecting, but after you disconnect"
                            " traffic goes out directly.";
                ks.suggestion = "mullvad lockdown-mode set on";
            } else {
                ks.health = VpnHealth::Info;
                ks.detail = "Mullvad's own firewall protects while it runs; lockdown mode could not be read.";
                ks.suggestion = "mullvad lockdown-mode get";
            }
        } else if (r.kind == "Tailscale") {
            ks.health = VpnHealth::Info;
            ks.detail = "Tailscale has none: if the exit node drops, traffic goes out directly.";
        } else if (!f.rulesetRead) {
            ks.health = VpnHealth::Info;
            ks.detail = "Not checked: reading the firewall rules needs root.";
            ks.suggestion = "sudo nexus vpn check";
        } else if (rulesetBlocksOutside(f.ruleset, r.tunnel)) {
            ks.detail = "On: the firewall blocks traffic leaving outside " + r.tunnel + ".";
        } else {
            ks.health = VpnHealth::Warning;
            ks.detail = "Off: if " + r.tunnel + " drops, traffic goes out directly, unprotected.";
            if (r.kind == "WireGuard")
                ks.suggestion = "PostUp/PreDown reject rules in " +
                                (f.configPath.empty() ? std::string("the tunnel's config") : f.configPath) +
                                " (a Nexus command for it is planned)";
            else ks.suggestion = "the VPN app's kill-switch setting";
        }
        r.lines.push_back(ks);
    }
    return r;
}

}  // namespace nexus::system
