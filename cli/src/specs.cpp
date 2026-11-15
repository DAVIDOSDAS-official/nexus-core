// nexus specs.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

namespace {

// ---------------------------------------------------------------------
// `nexus specs` -- what this machine is, in one screen.
//
// The pure parsing and the JSON live in nexus/system/specs; this is
// the live reading (files under /proc, /sys and os-release, and the
// few things that take a command) and the drawing, logo and colours
// included. Nothing here is guessed: a reading that fails is named in
// "unreadable", never shown as if it were a value.

// One line of a sysfs/proc file, trimmed; "" if it cannot be read.
std::string readOneLine(const std::string& path) {
    std::ifstream in(path);
    std::string line;
    if (!std::getline(in, line)) {
        return {};
    }
    const auto first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = line.find_last_not_of(" \t\r\n");
    return line.substr(first, last - first + 1);
}

// Intel/AMD/NVIDIA from the PCI vendor id, with the kernel driver in
// brackets: "Intel (i915)". The driver is the symlink's target name.
std::string describeGpu(const std::filesystem::path& cardDevice) {
    namespace fs = std::filesystem;
    std::string vendorId = readOneLine((cardDevice / "vendor").string());
    std::string vendor;
    if (vendorId == "0x8086") vendor = "Intel";
    else if (vendorId == "0x10de") vendor = "NVIDIA";
    else if (vendorId == "0x1002") vendor = "AMD";
    else if (vendorId == "0x1af4" || vendorId == "0x1234") vendor = "virtual";
    else vendor = vendorId.empty() ? "GPU" : ("GPU " + vendorId);

    std::error_code ec;
    std::string driver;
    const fs::path link = cardDevice / "driver";
    if (fs::exists(link, ec)) {
        const fs::path target = fs::read_symlink(link, ec);
        if (!ec) {
            driver = target.filename().string();
        }
    }
    return driver.empty() ? vendor : (vendor + " (" + driver + ")");
}

struct Palette {
    const char* reset = "";
    const char* node = "";    // the logo's nodes
    const char* edge = "";    // the logo's lines
    const char* label = "";   // field names
    const char* dim = "";     // notes
};

Palette paletteFor(bool colour) {
    if (!colour) {
        return Palette{};
    }
    return Palette{
        "\033[0m",
        "\033[96m",          // bright cyan
        "\033[38;5;31m",     // teal
        "\033[1m",           // bold
        "\033[38;5;244m",    // grey
    };
}

// The Nexus node-graph N: four corner nodes and one in the middle,
// the same mark as the logo, drawn so the terminal's colours can be
// seen to render.
void drawLogo(const Palette& p) {
    const std::string n = p.node, e = p.edge, r = p.reset;
    std::cout
        << "   " << n << "●" << e << "╲      " << n << "●" << r << "\n"
        << "   " << e << "│" << n << "╲" << e << "     │" << r << "\n"
        << "   " << e << "│ " << n << "●" << e << "    │" << r << "\n"
        << "   " << e << "│  " << n << "╲" << e << "  │" << r << "\n"
        << "   " << n << "●" << e << "   ╲" << n << "●" << r << "\n";
}

// The 16 terminal colours as blocks, then a smooth true-colour sweep:
// if these look right, the terminal is showing colour properly.
void drawColourBar() {
    for (int c = 0; c < 8; ++c) {
        std::cout << "\033[48;5;" << c << "m  ";
    }
    std::cout << "\033[0m\n";
    for (int c = 8; c < 16; ++c) {
        std::cout << "\033[48;5;" << c << "m  ";
    }
    std::cout << "\033[0m\n";
    for (int i = 0; i < 32; ++i) {
        const double h = 360.0 * i / 32.0;
        const double x = 1 - std::fabs(std::fmod(h / 60.0, 2.0) - 1);
        double rr = 0, gg = 0, bb = 0;
        if (h < 60)       { rr = 1;  gg = x;  bb = 0; }
        else if (h < 120) { rr = x;  gg = 1;  bb = 0; }
        else if (h < 180) { rr = 0;  gg = 1;  bb = x; }
        else if (h < 240) { rr = 0;  gg = x;  bb = 1; }
        else if (h < 300) { rr = x;  gg = 0;  bb = 1; }
        else              { rr = 1;  gg = 0;  bb = x; }
        std::cout << "\033[48;2;" << int(rr * 255) << ";" << int(gg * 255)
                  << ";" << int(bb * 255) << "m ";
    }
    std::cout << "\033[0m\n";
}

nexus::system::SpecsInfo collectSpecs(const std::string& sysfsRoot) {
    namespace fs = std::filesystem;
    using nexus::system::SpecsInfo;
    using nexus::system::DiskUse;
    SpecsInfo info;

    auto note = [&](const char* what) { info.unreadable.push_back(what); };

    info.host = readOneLine("/proc/sys/kernel/hostname");

    // Model: DMI product name, with the vendor if it adds anything.
    const std::string vendor = readOneLine(
        sysfsRoot + "sys/class/dmi/id/sys_vendor");
    const std::string product = readOneLine(
        sysfsRoot + "sys/class/dmi/id/product_name");
    if (!product.empty()) {
        info.model =
            (!vendor.empty() &&
             product.rfind(vendor, 0) != 0)
                ? vendor + " " + product
                : product;
    }

    info.nexusVersion = NEXUS_VERSION;
    info.edition = osReleaseValue("VARIANT_ID");
    info.osPretty = osReleaseValue("PRETTY_NAME");
    info.kernel = readOneLine("/proc/sys/kernel/osrelease");

    const long up = nexus::system::parseUptimeSeconds(
        readWholeFile("/proc/uptime"));
    info.uptime = nexus::system::formatUptime(up);

    if (const char* d = std::getenv("XDG_CURRENT_DESKTOP")) {
        info.desktop = d;
    }
    if (const char* s = std::getenv("SHELL")) {
        info.shell = fs::path(s).filename().string();
    }

    const std::string cpuinfo = readWholeFile("/proc/cpuinfo");
    info.cpuModel = nexus::system::parseCpuModel(cpuinfo);
    info.cpuCores = nexus::system::parseCpuCores(cpuinfo);

    // GPUs and the preferred resolution, from /sys/class/drm.
    std::error_code ec;
    const fs::path drm = fs::path(sysfsRoot) / "sys/class/drm";
    if (fs::exists(drm, ec)) {
        std::vector<std::string> cards;
        for (const auto& entry : fs::directory_iterator(drm, ec)) {
            const std::string name = entry.path().filename().string();
            // "card0", not "card0-eDP-1".
            if (name.rfind("card", 0) == 0 &&
                name.find('-') == std::string::npos) {
                cards.push_back(name);
            }
            if (info.resolution.empty()) {
                const std::string mode =
                    readOneLine((entry.path() / "modes").string());
                if (!mode.empty()) {
                    info.resolution = mode;
                }
            }
        }
        std::sort(cards.begin(), cards.end());
        for (const auto& card : cards) {
            const fs::path dev = drm / card / "device";
            if (fs::exists(dev, ec)) {
                info.gpus.push_back(describeGpu(dev));
            }
        }
    }

    std::uint64_t totalKiB = 0, availKiB = 0;
    nexus::system::parseMemKiB(readWholeFile("/proc/meminfo"),
                               totalKiB, availKiB);
    if (totalKiB > 0) {
        info.ramTotalMiB = totalKiB / 1024;
        info.ramUsedMiB = (totalKiB - availKiB) / 1024;
    } else {
        note("memory");
    }

    // Disks: where files and system versions live, not "/" (the
    // read-only image, always full). Each filesystem once.
    struct Place { const char* path; };
    const Place places[] = {{"/var"}, {"/boot"}, {"/home"}};
    std::set<unsigned long> seen;
    for (const auto& place : places) {
        struct statvfs st{};
        if (::statvfs(place.path, &st) != 0 || st.f_blocks == 0) {
            continue;
        }
        if (!seen.insert(st.f_fsid).second) {
            continue;
        }
        DiskUse d;
        d.mount = place.path;
        d.totalBytes = static_cast<std::uint64_t>(st.f_blocks) * st.f_frsize;
        d.usedBytes =
            static_cast<std::uint64_t>(st.f_blocks - st.f_bfree) * st.f_frsize;
        info.disks.push_back(d);
    }

    // Battery, if there is one.
    const fs::path power = fs::path(sysfsRoot) / "sys/class/power_supply";
    if (fs::exists(power, ec)) {
        for (const auto& entry : fs::directory_iterator(power, ec)) {
            if (readOneLine((entry.path() / "type").string()) != "Battery") {
                continue;
            }
            const std::string cap =
                readOneLine((entry.path() / "capacity").string());
            const std::string status =
                readOneLine((entry.path() / "status").string());
            if (!cap.empty()) {
                info.battery = cap + "%";
                if (!status.empty()) {
                    std::string s = status;
                    for (char& c : s) c = std::tolower(c);
                    info.battery += ", " + s;
                }
            }
            break;
        }
    }

    // What only Nexus knows.
    const auto secure =
        nexus::hardware::SecureBootDetector(sysfsRoot).detect();
    switch (secure.state) {
        case nexus::hardware::SecureBootState::Enabled:
            info.secureBoot = "on"; break;
        case nexus::hardware::SecureBootState::Disabled:
            info.secureBoot = "off"; break;
        case nexus::hardware::SecureBootState::SetupMode:
            info.secureBoot = "setup mode"; break;
        case nexus::hardware::SecureBootState::NotSupported:
            info.secureBoot = "not supported (no UEFI)"; break;
        default:
            info.secureBoot = "unknown";
    }

    const auto enc =
        nexus::hardware::EncryptionDetector(sysfsRoot).detect();
    switch (enc.root) {
        case nexus::hardware::Encrypted::Yes:
            info.encryption = enc.method.empty()
                                  ? "on"
                                  : "on (" + enc.method + ")";
            break;
        case nexus::hardware::Encrypted::No:
            info.encryption = "off"; break;
        default:
            info.encryption = "unknown";
    }

    if (nexus::system::commandExists("firewall-cmd")) {
        info.firewall =
            nexus::system::runCommand(
                "firewall-cmd --state 2>/dev/null", false).ok
                ? "on"
                : "off";
    } else {
        info.firewall = "unknown";
    }

    info.ntsync =
        fs::exists("/dev/ntsync") ||
        fs::exists(kNtsyncConf) ||
        nexus::system::runCommand(
            "grep -qsx ntsync /etc/modules-load.d/*.conf "
            "/usr/lib/modules-load.d/*.conf", false).ok;
    if (info.ntsync) {
        info.addons.push_back("gaming");
    }

    // Hosting: Cockpit present, or a Nexus server defined.
    bool hosting = nexus::system::commandExists("cockpit-bridge");
    const fs::path quadlets = "/etc/containers/systemd";
    std::vector<std::string> servers;
    if (fs::exists(quadlets, ec)) {
        for (const auto& entry : fs::directory_iterator(quadlets, ec)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("nexus-", 0) == 0 &&
                entry.path().extension() == ".container") {
                hosting = true;
                servers.push_back(
                    name.substr(6, name.size() - 6 - 10));  // strip ".container"
            }
        }
    }
    if (hosting) {
        info.addons.push_back("hosting");
    }

    // The running image: whether it was signature-checked, and when it
    // was built (the nearest thing to "last updated" without guessing).
    const auto deployments = readDeployments();
    for (const auto& d : deployments) {
        if (d.booted) {
            // The same answer as the summary and the audit (0.1.95,
            // round-4 audit L1; this went by the prefix alone).
            info.signature = signingWords(updateSigningOf(d.image));
            info.lastUpdate = dayOf(d.date);
            break;
        }
    }

    return info;
}

}  // namespace

int commandSpecs(const std::string& sysfsRoot, bool plain, bool json) {
    const nexus::system::SpecsInfo info = collectSpecs(sysfsRoot);

    if (json) {
        std::cout << nexus::system::specsJson(info) << "\n";
        return 0;
    }

    const bool colour = !plain && ::isatty(STDOUT_FILENO);
    const Palette p = paletteFor(colour);

    if (colour) {
        drawLogo(p);
        std::cout << "\n";
        drawColourBar();
        std::cout << "\n";
    } else {
        std::cout << "Nexus specs\n\n";
    }

    auto row = [&](const std::string& label, const std::string& value) {
        if (value.empty()) {
            return;
        }
        std::cout << "  " << p.label << std::left << std::setw(12) << label
                  << p.reset << value << "\n";
    };

    row("Host", info.host);
    row("Model", info.model);
    std::string version = "Nexus " + info.nexusVersion;
    if (!info.edition.empty()) version += " (" + info.edition + ")";
    row("Nexus", version);
    row("Base", info.osPretty);
    row("Kernel", info.kernel);
    row("Uptime", info.uptime);
    row("Desktop", info.desktop);
    row("Resolution", info.resolution);
    row("Shell", info.shell);

    std::string cpu = info.cpuModel;
    if (info.cpuCores > 0) {
        cpu += (cpu.empty() ? "" : " ") + ("(" + std::to_string(info.cpuCores) +
               (info.cpuCores == 1 ? " thread)" : " threads)"));
    }
    row("CPU", cpu);
    for (std::size_t i = 0; i < info.gpus.size(); ++i) {
        row(i == 0 ? "GPU" : "", info.gpus[i]);
    }

    if (info.ramTotalMiB > 0) {
        row("Memory", std::to_string(info.ramUsedMiB) + " / " +
                      std::to_string(info.ramTotalMiB) + " MiB");
    }
    for (std::size_t i = 0; i < info.disks.size(); ++i) {
        const auto& d = info.disks[i];
        const auto gib = [](std::uint64_t b) {
            return std::to_string(b / (1024ULL * 1024 * 1024));
        };
        row(i == 0 ? "Disk" : "",
            d.mount + "  " + gib(d.usedBytes) + " / " + gib(d.totalBytes) +
            " GiB");
    }
    row("Battery", info.battery);

    std::cout << "\n";
    row("Secure Boot", info.secureBoot);
    row("Encryption", info.encryption);
    row("Firewall", info.firewall);
    row("NTSync", info.ntsync ? "on" : "off");
    if (!info.addons.empty()) {
        std::string list;
        for (const auto& a : info.addons) {
            list += (list.empty() ? "" : ", ") + a;
        }
        row("Add-ons", list);
    }
    row("Image", info.signature);
    row("Built", info.lastUpdate);

    if (!info.unreadable.empty()) {
        std::string list;
        for (const auto& u : info.unreadable) {
            list += (list.empty() ? "" : ", ") + u;
        }
        std::cout << "\n  " << p.dim << "Could not read: " << list
                  << p.reset << "\n";
    }

    if (!colour && !plain) {
        std::cout << "\n  (Colours are shown when the output is a"
                     " terminal; --json for scripts.)\n";
    }

    return 0;
}

}  // namespace nexus_cli
