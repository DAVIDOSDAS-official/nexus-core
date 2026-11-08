# Borrow from Bazzite, secureblue and Mint first

The distributions worth studying for Nexus-CORE are a short list, not the thousand or so that have ever existed. Three of them are Nexus's closest relatives and give the most portable features. **Bazzite/Universal Blue** gives the update, Portal and gaming stack on Fedora bootc. **secureblue** gives tiered hardening plus an audit tool on Fedora Atomic. **Linux Mint 22.3** gives the template for `nexus specs`, the system doctor and the GUI side of `nexus dualboot`. The architecture is sound: Fedora, KDE and Universal Blue are all moving to image-based bases with Flatpak/containers for apps. The one hard constraint is that **bootc officially does not support systemd-sysext/confext**, so add-ons must be built as a few GHCR image variants plus runtime containers and Flatpaks, not as overlay layers ([Fedora Discussion](https://discussion.fedoraproject.org/t/unsupported-systemd-confext-and-sysext-on-bootc/202199)). For the thin add-ons, the substantive upgrades are these. Gaming gets evidence-backed fixes (Intel split-lock penalty off, NTSYNC, gamescope session, MangoHud) instead of placebo daemons. Server becomes a `nexus server` command family built on Quadlet + Caddy + LinuxGSM. VPN and hosting share one WireGuard "VPS edge" that also delivers the "sites without exposing home IP" goal. Development gets an Aeon-style install ladder (Flatpak → toolbox → warned layering). The biggest traps are licences (Umbrel and Cosmos are not open source, and ZimaOS is freemium), expecting CachyOS-style benchmark gains on a 4-core Iris Xe laptop, and building antivirus around ClamAV.

## About 270 distros are active, and the newest winners are image-based

DistroWatch's last hard count (early 2024) listed **958 distributions, of which only 274 were active**, 47 dormant and 637 discontinued. The active count has been falling slowly from 323 in 2011 ([Linux Magazine](https://www.linux-magazine.com/Issues/2024/280/Digging-into-DistroWatch)). No 2025–2026 count was found. Almost all of them descend from a handful of roots, each built around one signature idea. The table summarises them. The "signature idea" column is well-established background, not re-verified in this research, except where a citation appears.

| Family | Notable members (Nov 2026) | Signature idea | Most relevant borrow for Nexus |
|---|---|---|---|
| Debian | Debian 13, Ubuntu 26.04, Mint 22.3, Pop!_OS 24.04, Zorin 18, Kicksecure/Whonix, Kali, Proxmox | apt/dpkg, a social contract, very stable releases | Mint's small GUI tools, Zorin's .exe helper, Kicksecure's `security-misc` |
| Fedora/RHEL | Fedora 43/44, RHEL 10, Alma/Rocky, Fedora Atomic, **Bazzite, Bluefin, Aurora, secureblue** | Upstream-first, SELinux, rpm-ostree → **bootc** | Nearly everything: this is Nexus's family |
| Arch | CachyOS, EndeavourOS, Manjaro, Garuda, SteamOS 3, KDE Linux (Arch packages) | Rolling release, AUR, ArchWiki | CachyOS schedulers (with caveats), ArchWiki-style docs |
| openSUSE/SUSE | Leap 16, Tumbleweed, Aeon/Kalpa | Snapper rollback, OBS, transactional-update | Aeon's install-preference ladder |
| Gentoo | Gentoo | USE flags (declarative feature toggles) | The concept behind a capability resolver |
| NixOS/Guix | NixOS, Guix System | One declarative config → generations | Manifest, generations, diff-before-switch |
| Independents | Alpine, Void, Solus, Chimera; Clear Linux (dead) | musl/minimalism; stateless config | Small footprint, Clear's "factory-resettable" goal |

Several things changed the landscape in 2024–2026. **Intel killed Clear Linux on 18 July 2025, effective immediately** ([Wikipedia](https://en.wikipedia.org/wiki/Clear_Linux_OS)). **CachyOS** now leads DistroWatch's 12-month page-hit ranking at about 3,789 hits/day, ahead of Mint, MX Linux, Debian and Pop!_OS, and **Ubuntu has dropped out of the top five** ([PBX Science](https://pbxscience.com/cachyos-tops-2026-distrowatch-rankings-as-ubuntu-falls-out-of-top-five/)). Page hits measure hype, not installs. Debian 13 (Aug 2025) dropped i386 and added riscv64 ([Debian](https://www.debian.org/News/2025/20250809.en.html)). Fedora 43 made Workstation **Wayland-only** and shipped RPM 6.0 with OpenPGP v6/post-quantum signing ([It's FOSS](https://itsfoss.com/news/fedora-43-release/)). openSUSE Leap 16 retired YaST for the Agama installer and the Myrlyn package GUI, and switched to SELinux ([Linuxiac](https://linuxiac.com/opensuse-leap-16-0-released-this-is-whats-new/)). Ubuntu 26.04 LTS shipped sudo-rs, Rust coreutils, dracut, TPM-backed FDE and **no cgroup v1** ([ComputingForGeeks](https://computingforgeeks.com/ubuntu-2604-lts-features/)). KDE launched **KDE Linux**, a read-only image OS with whole-image updates and rollback via boot-menu entries ([KDE blog](https://planet.kde.org/nate-graham-2025-09-06-announcing-the-alpha-release-of-kde-linux)). Fedora 44 is the current base for derivatives: secureblue moved to it in May 2026 ([secureblue releases](https://github.com/secureblue/secureblue/releases)) and Bazzite 44 shipped in August ([Linuxiac](https://linuxiac.com/bazzite-linux-44-lands-with-kernel-7-2-new-gaming-stack-and-gui-updater/)).

Two lessons follow for a one-person distro. First, **Nexus should assume Wayland-only and cgroup v2-only from day one**, as its upstreams already do. Second, sponsored niche distros die when the sponsor cuts costs, and hobby distros die from burnout. Building on Fedora bootc keeps packages, kernel and security updates someone else's job. The more Nexus's value lives in a thin delta (the resolver, the `nexus` CLI and the profiles), the more survivable it is.

## Update and image model: wrap bootc, and keep image variants few

bootc already does the heavy lifting. `bootc upgrade` stages a new image without touching the running system, and `--download-only` / `--from-downloaded` split download from apply. `bootc switch` changes the tracked image while preserving /etc and /var. `bootc rollback` reorders boot entries, **but /etc reverts to the older deployment's state**. Red Hat recommends `switch` to an older image when the problem is not in /etc, and warns that the auto-update timer can re-upgrade within 1–3 hours after a rollback ([Red Hat](https://docs.redhat.com/en/documentation/red_hat_enterprise_linux/10/html/using_image_mode_for_rhel_to_build_deploy_and_manage_operating_systems/managing-rhel-bootc-images)). Nexus's job is to wrap these primitives in the "inform and warn" UX that raw bootc lacks. The effort ratings below are judgement calls for a solo developer on Fedora bootc, not sourced figures.

| Feature | Source | How it works | Effort | Fit with principles |
|---|---|---|---|---|
| Gated, staged auto-updates | Universal Blue `uupd`; Vanilla OS ABRoot | One pass updates the image, Flatpaks, Distrobox and Homebrew. It only runs if **battery ≥20%, CPU ≤50%, memory ≤90%** and the network is idle, and each check can be toggled in `/etc/uupd/config.json` ([uupd README](https://github.gg22.workers.dev/ublue-os/uupd)) | Easy–Medium | Strong. Expose an on/off toggle, and mask `bootc-fetch-apply-updates.timer` so you don't run two updaters. uupd's licence is unchecked |
| `nexus rollback` / `nexus rebase` | bootc; Bazzite Portal | Thin wrappers over `bootc rollback`/`switch`. Detect locally modified /etc files and offer to stash them to /var first | Easy | Strong: this is exactly "warn before data loss" |
| Soft reboot | RHEL image mode | `--soft-reboot=auto` restarts userspace only, when the kernel is unchanged ([Red Hat](https://docs.redhat.com/en/documentation/red_hat_enterprise_linux/10/html/using_image_mode_for_rhel_to_build_deploy_and_manage_operating_systems/performing-soft-reboots-to-rhel-bootc-images)) | Medium | Answers the top immutable-distro complaint ("why reboot?") |
| Auto-rollback on failed boot | Bazzite 44 (Greenboot) | Health checks at boot. If they fail, it rolls back to the previous deployment automatically ([Linuxiac](https://linuxiac.com/bazzite-linux-44-lands-with-kernel-7-2-new-gaming-stack-and-gui-updater/)) | Easy–Medium | Strong. It is a bootc-native safety net, and important for a solo maintainer whose bad build ships to everyone |
| Signed images + SBOM + provenance | Bluefin, secureblue | Keyless cosign via GitHub OIDC/Rekor, SLSA v1 provenance attestations, and a Syft SPDX SBOM attached to each image. Users can pin a dated tag like `stable-20260501` ([Bluefin docs](https://docs.projectbluefin.io/supply-chain/)). secureblue verifies SLSA provenance on every upgrade ([secureblue releases](https://github.com/secureblue/secureblue/releases)) | Easy (standard Actions steps) | Strong. Start keyless, because cosign v3 no longer reads legacy `.sig` tags |
| Declarative manifest + generations + `nexus diff`/`plan` | blendOS `/system.yaml`; NixOS `nixos-rebuild build` + `nvd diff` | One file lists the chosen profiles and add-ons. A dry run shows what would change before switching ([blendOS](https://blendos.co/blog/2024/06/05/blendos-v4-released-arch-linux-made-immutable-declarative-and-atomic/); [tjll.net](https://blog.tjll.net/previewing-nixos-system-updates/)) | Medium | The single best fit for the C++20 resolver. Each transaction records the manifest, the resolved components, the image digest and the Flatpak list. Package-level diffs come free from the SBOMs |
| Install-preference ladder | openSUSE Aeon | Flatpak → user distrobox → root distrobox → host layering, with host layering reserved for drivers ([openSUSE wiki](https://wiki.opensuse.org/Portal:Aeon)) | Easy (it's policy) | Strong. `nexus install X` picks the lowest rung and *warns* rather than blocks when a host change is needed |
| Factory reset | bootc `install reset --experimental` | Creates a new stateroot with a clean /etc and an empty /var. The old one stays on disk until removed ([bootc docs](https://cgwalters.github.io/bootc/experimental-install-reset.html)) | Easy to wrap, risky | Later. Red Hat lists it as Technology Preview, it wipes homes, and it doesn't reclaim space |
| Logically bound images | RHEL image mode | Quadlet images declared inside the OS image are pulled during upgrade and roll back with it ([Red Hat](https://docs.redhat.com/en/documentation/red_hat_enterprise_linux/10/html/using_image_mode_for_rhel_to_build_deploy_and_manage_operating_systems/building-and-managing-logically-bound-images)) | Medium | Only for things baked into a specific image variant. They can't be chosen at runtime |
| Transient root/etc (kiosk/school) | bootc composefs backend (June 2026) | `setup-root-conf.toml` with `transient = true` throws away writes to / on reboot ([API Evangelist](https://blogs.apievangelist.com/blogs/bootc-2026-06-05-video-sealed-bootc-with-transient-etc-and-var/)) | Hard / experimental | Interesting for a future "reset on reboot" school mode. Too new now |

The architectural decision matters more than any single feature. Because sysext is unsupported on bootc, citing an overlay stacking-depth limit ([Fedora Discussion](https://discussion.fedoraproject.org/t/unsupported-systemd-confext-and-sysext-on-bootc/202199)), KDE Linux's "sysext overlays for developers" approach ([KDE blog](https://planet.kde.org/nate-graham-2025-09-06-announcing-the-alpha-release-of-kde-linux)) does not transfer. Nexus's resolver should therefore treat a transaction as four steps. First, pick the target image tag. Keep the variants few, for example base-minimal, base-full and gaming (Steam, RPM Fusion and kernel bits that must live in /usr). Second, stage it with `bootc switch`. Third, apply runtime add-ons (Flatpaks, toolboxes, Quadlets in /etc or /var). Fourth, record a generation. Universal Blue copes with variant explosion through a tag matrix. One person cannot maintain gaming × NVIDIA × desktop × security combinations, so anything that can be runtime should be runtime. Bootc's previous-deployment rollback gives only one step back. Recording image *digests* lets `bootc switch image@sha256:…` reach further back, as long as GHCR retains the old digests.

Upstream risk is real. In March 2026 an rpm-ostree 2026.1 regression broke upgrades, and secureblue told users to downgrade and pin ([secureblue releases](https://github.com/secureblue/secureblue/releases)). Frostyard reports bootc sometimes updating the wrong boot entry on its Debian-based images ([Frostyard](https://frostyard.github.io/docs/status/)). Greenboot and pinned-deployment advice are not optional polish.

## Beginner UX: one menu definition should drive both GUI and CLI

The closest existing analogue to "beginner mode + `nexus` CLI" is the **Bazzite Portal**. It launches automatically on first login ([Linuxiac](https://linuxiac.com/bazzite-linux-44-lands-with-kernel-7-2-new-gaming-stack-and-gui-updater/)). Its menu is a YAML file (`/usr/share/yafti/yafti.yml`). Each entry has a title, a description, a **status script** that reports whether the feature is currently on, and options that each run a `ujust` command such as `ujust toggle-iwd enable` ([Bazzite docs](https://docs.bazzite.gg/Installing_and_Managing_Software/Bazzite_Portal/)). That is a capability with a probe and actions, which is what Nexus's component model already is. The recommendation is to **generate the first-boot wizard, a settings app and `nexus` help text from the same component metadata**. That way "app and terminal command" is one code path, and re-running the wizard shows what is already applied. Garuda shows the cost of getting this wrong. Its first-boot Setup Assistant is praised for breadth, but has recurring reports of it **closing without installing anything** ([Garuda forum](https://forum.garudalinux.org/t/setup-assistant-installs-nothing/12450.md); [Garuda forum](https://forum.garudalinux.org/t/garuda-setup-assistant-asks-for-required-software-but-did-not-install-it-just-closes/9466)). Nexus's first-boot question must show progress, write a log, report every failure and be re-runnable. DietPi's model of first-boot settings in one file (`AUTO_SETUP_INSTALL_SOFTWARE_ID=…`), logged to a known path ([DietPi docs](https://dietpi.com/docs/usage/)), is a good template for an unattended "answer file" version of beginner mode.

No other verified distro asks "what is this computer for?" and configures itself accordingly. Garuda and Bazzite offer category checklists, not profiles. That makes Nexus's profile question a genuine differentiator. Ubuntu's, Fedora's and Endless's installers were not checked, so "unique" is unverified.

For **`nexus specs` and the dashboard doctor**, copy Linux Mint 22.3. Its System Information app added four pages. **USB** groups devices by controller so you can compare each device's speed and power against the controller maximum. **GPU** shows whether hardware acceleration works. **PCI** shows the driver in use per device. **BIOS** shows boot mode and Secure Boot status. A separate privileged **System Administration** tool exposes the GRUB menu, its timeout ("useful for dual-boot setups") and kernel parameters ([Linux Mint 22.3](https://linuxmint.com/rel_zena_whatsnew.php)). Keep that split: specs and doctor run unprivileged and read-only, and fixes go behind an explicit privileged command. The doctor should add one thing Mint can't have: a list of `bootc status` deployments with dates and one-click rollback, which gives Timeshift-like visibility for the image. Note that image rollback covers neither /home nor /etc, so a separate user-data backup story is still needed. The dual-boot GUI is worth getting right. Reviewers still say Pop!_OS 24.04 "does not play nicely with dual boot", and its installer refused a 100 MB ESP ([The Register](https://www.theregister.com/2025/12/22/popos_2404_cosmic_epoch_1/)). A tested `nexus dualboot` is a marketable strength, but only for the configurations actually tested.

Two cheap features have high beginner impact. **Zorin OS 18's Windows-installer detection** recognises over 170 known .exe installers and points to a native app, web app or alternative. Its **Web Apps** tool turns any site into a launcher entry ([Zorin blog](https://blog.zorin.com/2025/09/19/a-sneak-peek-at-zorin-os-18-test-the-beta-today/)). The .exe helper is a MIME handler plus a curated JSON map. It fits "inform, don't block" as long as it still offers Wine/Bottles. For **Nexus share**, adopt **Warpinator** rather than inventing a protocol. It is **GPL-3.0** (matching Nexus), auto-discovers peers on the LAN, encrypts transfers, separates groups by shared code and offers a manual-connect fallback ([LinuxLinks](https://www.linuxlinks.com/warpinator-share-files-across-lan/)). Mint 22.3 added IPv6 and text messages ([Linux Mint](https://linuxmint.com/rel_zena_whatsnew.php)), and there is an unofficial Android client on F-Droid ([F-Droid](https://f-droid.org/it/packages/slowscript.warpinator)). Interop with existing Mint and Android users beats a new island. Its weakness is that it is LAN-only and has no official iOS client. LocalSend is the cross-platform alternative to evaluate (unverified here).

For the **$1 unlock**, elementary's AppCenter is the ethical template. It is pay-what-you-want via Stripe, the suggested price is "not a price floor" so $0 is allowed, and it takes a **70/30 split with a $0.50 minimum platform cut** ([elementary docs](https://docs.elementary.io/develop/appcenter/monetizing-your-app)). Zorin sells a one-time **$47.99 Pro** that adds layouts, extra software and support, never security or core function ([It's FOSS](https://itsfoss.com/news/zorin-os-18-release/)). Two consequences follow. A flat $1 is mostly eaten by fixed card fees. The exact Stripe fee wasn't verified, so check it, but elementary's own $0.50 minimum shows the problem. A suggested $1–3 pay-what-you-want is more workable. And under GPL-3.0 anyone can rebuild the unlocked code, so the purchase is really a convenience and support payment. A signed offline licence file checked with a public key avoids running a licence server, in line with the self-hosted principle.

## Gaming: ship the measurable fixes and label the rest

CachyOS's headline numbers are real but don't transfer to Nexus's hardware. CachyOS beat Ubuntu 26.04 by ~12% and Fedora 44 by ~15% (geometric mean) on a **256-core dual EPYC** machine. The gains are credited to x86-64-v3/v4 rebuilds, Clang LTO/PGO/BOLT and kernel patches ([WebProNews](https://www.webpronews.com/cachyos-outpaces-ubuntu-26-04-and-fedora-44-benchmarks-expose-linux-speed-gaps-in-2026/)). Phoronix's earlier test of the x86-64-v3/v4 repos alone found small, workload-dependent gains and no difference in Firefox or Blender ([Phoronix](https://www.phoronix.com/review/cachyos-x86-64-v3-v4/4)). Rebuilding Fedora that way is impossible for a solo project and would do little for a 4-core Iris Xe laptop in games. Clear Linux's ~48% lead over Ubuntu was likewise a dual-Xeon, compiler-and-governor story ([Phoronix](https://www.phoronix.com/review/clear-linux-48p-ubuntu)). What does help is the following:

| Feature | Source | How it works | Effort | Evidence / fit |
|---|---|---|---|---|
| Split-lock penalty off | Kernel ≥6.2 sysctl | `kernel.split_lock_mitigate=0` keeps the warning but removes the deliberate slowdown that crippled God of War under Proton ([Phoronix](https://www.phoronix.com/news/Linux-Splitlock-Hurts-Gaming)) | Trivial (one sysctl.d file) | **Strong, and Intel-specific**, so it applies directly to the Iris Xe test laptops. Default-on in the gaming profile |
| NTSYNC | Mainline since 6.14; Wine 11 | Kernel NT sync primitives. Make sure `ntsync` loads and /dev/ntsync is accessible | Trivial | Mixed but real. In user tests, a Last of Us level load took seconds instead of ~120 s, while fsync still won FPS in Indiana Jones ([CachyOS forum](https://discuss.cachyos.org/t/proton-cachyos-ntsync-and-fsync-comparison-update/7932)). The "678%" headline is vs plain wineserver: don't repeat it |
| gamescope "Game Mode" session | Bazzite (from ChimeraOS) | Optional login session that boots into Steam's gamescope UI ([Bazzite README](https://github.com/ublue-os/bazzite)) | Medium | Capability rather than FPS. Its frame limiter and FSR upscaling are the best iGPU levers |
| MangoHud, vkBasalt, OBS VkCapture | Bazzite defaults | Preinstalled overlays ([Bazzite README](https://github.com/ublue-os/bazzite)) | Easy | MangoHud lets users *measure* every other tweak. Ship it |
| scx_lavd scheduler (opt-in) | Bazzite Deck/HTPC images; Valve/Igalia | sched_ext BPF scheduler that targets stutter and latency ([scx_lavd README](https://gitea.hillion.co.uk/JakeHillion/scx-upstream/raw/branch/main/scheds/rust/scx_lavd/README.md); [Bazzite README](https://github.com/ublue-os/bazzite)) | Medium (scx packaging on Fedora unverified) | Plausible but **unmeasured**: the README has no benchmarks. Offer it as an opt-in with "measure with MangoHud" advice |
| Kyber I/O scheduler, ZRAM tune, dedup | Bazzite | Kyber avoids I/O starvation during installs. The Deck uses 4 GB LZ4 ZRAM plus `bees` dedup of Wine prefixes ([Bazzite README](https://github.com/ublue-os/bazzite)) | Easy | Cheap, with weak evidence. Fedora already ships zram-generator |
| OGC kernel | Open Gaming Collective (Bazzite, Nobara, ChimeraOS…) | Upstream-first gaming kernel with Secure Boot support. Bazzite 44 uses it (Linux 7.2) ([GamingOnLinux](https://www.gamingonlinux.com/2026/01/open-gaming-collective-ogc-formed-to-push-linux-gaming-even-further); [Linuxiac](https://linuxiac.com/bazzite-linux-44-lands-with-kernel-7-2-new-gaming-stack-and-gui-updater/)) | Medium–Hard | The most bootc-proven way to get a gaming kernel if Fedora's falls short. Gaming image only |
| NVIDIA open kmods, prebuilt and signed | Bazzite `nvidia-open`, secureblue | Build kmods at image-build time, sign with a project key, enroll via a command (`ujust enroll-secure-boot-key`) ([Bazzite README](https://github.com/ublue-os/bazzite)) | Medium | Runtime akmods don't fit an immutable /usr. Hybrid Intel+NVIDIA laptops also need dGPU offload. Bazzite 44 switched to **Cardwire** for this ([Linuxiac](https://linuxiac.com/bazzite-linux-44-lands-with-kernel-7-2-new-gaming-stack-and-gui-updater/)) |
| Handheld stack | Bazzite 44, SteamOS | InputPlumber for controllers, SteamOS-Manager/PowerStation for TDP ([Linuxiac](https://linuxiac.com/bazzite-linux-44-lands-with-kernel-7-2-new-gaming-stack-and-gui-updater/)). HHD is being phased out ([GamingOnLinux](https://www.gamingonlinux.com/2026/01/open-gaming-collective-ogc-formed-to-push-linux-gaming-even-further)) | Later | Only if Nexus targets handhelds |

The placebo list needs saying plainly. **gamemode, ananicy-cpp and system76-scheduler** have no 2026 benchmark evidence of FPS gains. At best they help responsiveness under background load. **vm.max_map_count** has been 1048576 in Fedora since F39 ([Fedora](https://fedoraproject.org/wiki/Changes/IncreaseVmMaxMapCount)), so shipping it again does nothing. **LACT/CoreCtrl** are AMD/NVIDIA tools and irrelevant to Iris Xe. On an iGPU laptop, CPU and GPU share one power budget. The honest gaming profile is the split-lock and NTSYNC fixes, a "performance" power profile while gaming, gamescope/MangoHud frame limits, FSR upscaling, and less background CPU contention. Bazzite also keeps risky items opt-in (RyzenAdj undervolting, refresh overclocking, Decky Loader) ([Bazzite README](https://github.com/ublue-os/bazzite)). That matches "never forced-on". To make the gaming add-on feel substantive, ship a `nexus gaming doctor` that reports each tweak's state and links to how to measure it.

## Security and privacy: harden in tiers and make the audit the antivirus

secureblue is the nearest relative: hardened Fedora Atomic delivered via `ujust` recipes. Kicksecure's `security-misc` is the most thorough catalogue, with each item marked default or opt-in according to its breakage cost. Almost every item in both is a config file (sysctl.d, modprobe.d, kargs, SELinux modules, USBGuard rules). In a bootc Containerfile these can ship **inactive** and be switched on by a `nexus` command, which is exactly the opt-in model. Use the projects' own breakage ratings to sort the tiers. security-misc warns that disabling user namespaces "may cause widespread breakage", that module-load lockdown causes "major breakage", and that `hidepid` breaks pkexec ([security-misc README](https://www.github.com/Kicksecure/security-misc)). secureblue had to keep a `with-standard-malloc` escape hatch for hardened_malloc, and spent a 2026 release closing a file-manager bypass of its userns restriction ([secureblue releases](https://github.com/secureblue/secureblue/releases)). That is the maintenance tax a solo developer should not take on by default.

| Tier | Features (sources) | Effort |
|---|---|---|
| **Default, invisible** | firewalld deny-inbound; network sysctls (SYN cookies, no redirects/source routing), kptr/dmesg restriction; coredumps off; blacklist FireWire, floppy and obscure filesystems/protocols; HTTPS-only repos; chrony with **NTS** ([security-misc](https://www.github.com/Kicksecure/security-misc); [secureblue](https://secureblue.dev/features)); signed-image container policy | Easy |
| **"Protect" (one command, explained)** | hardened_malloc; USBGuard with the currently attached devices allowed at enable time; DNS-over-TLS selector with local DNSSEC via Unbound; per-connection MAC randomization; mask geoclue/cups/passim; ptrace restriction; Thunderbolt off; TPM2+PIN LUKS unlock ([secureblue](https://secureblue.dev/features)) | Easy–Medium |
| **"Paranoid" (re-ask with warning)** | SMT off + strict mitigations; userns restriction; module-load lockdown; hidepid; Xwayland off; io_uring off; sudo → `run0` ([security-misc](https://www.github.com/Kicksecure/security-misc); [secureblue](https://secureblue.dev/features)) | Medium, with high support cost |

TPM2+PIN deserves care, because Nexus already encrypts the system partition. secureblue offers it only on TPMs without known vulnerabilities ([secureblue](https://secureblue.dev/features)). Fedora still documents it as a manual `systemd-cryptenroll` walkthrough, and users report that BIOS updates break PCR bindings ([Zeli/apalrd](https://zeli.app/story/50194602)). It must force a recovery key.

**Nexus antivirus should be an auditor, not a ClamAV wrapper.** None of the security distros researched ship desktop antivirus. secureblue's answer is an audit tool that "checks hardening status and suggests changes" ([secureblue](https://secureblue.dev/features)). In 2026 it gained checks for printing services, disabled updates and dangerous groups ([secureblue releases](https://github.com/secureblue/secureblue/releases)). Community analysis says ClamAV trails commercial AV, targets mostly Windows malware on file servers, needs root for real-time scanning and has had repeated memory-safety bugs ([Privacy Guides forum](https://discuss.privacyguides.net/t/do-you-use-clamav/30105.md)). On bootc, /usr is image-delivered, so the useful checks are elsewhere. A credible spec has five parts: a red/yellow/green hardening audit with a one-line fix command per item; image signature/provenance status; a report of /etc drift and persistence locations (systemd user units, autostart, shell rc files); an optional on-demand ClamAV/YARA scan of Downloads, explicitly labelled "mainly catches Windows malware"; and optional fapolicyd. Being honest about these limits in the UI *is* the "inform" principle.

For the **Kali add-on**, Kali publishes `docker.io/kalilinux/kali-rolling` with no tools preinstalled. You add metapackages such as `kali-linux-headless`, and systemd is unsupported inside ([Kali docs](https://www.kali.org/docs/containers/using-kali-podman-images/)). `nexus security kali` should create a Distrobox from it on demand with a **separate home directory**, because Distrobox shares $HOME by default and is not a security boundary. It should warn that raw-socket and Wi-Fi tools need extra capabilities or host networking. The cost to the base image is zero beyond Podman/Distrobox. Exegol is a heavier alternative: Docker-based, 20–100 GB, with pre-loaded tooling ([Help Net Security](https://www.helpnetsecurity.com/2025/03/31/exegol-open-source-hacking-environment/)). Its current licensing wasn't verified.

On privacy, the **self-hosted WireGuard VPN is a real differentiator**: secureblue only offers `ujust` installers for commercial VPN provider apps ([secureblue](https://secureblue.dev/features)). To make the VPN add-on substantive, pair it with an opt-in killswitch (nftables/firewalld blocking non-WireGuard egress), DNS through the tunnel and peer QR codes for phones. Tails' automatic Tor-bridge suggestion when Tor is blocked ([Pipedot](https://pipedot.org/74NGS)) and Whonix 18's Wayland rewrite of **Kloak** keystroke-timing anonymization ([Linux Today](https://www.linuxtoday.com/blog/whonix-18-0-privacy-focused-linux-distro-released/)) are good NEXUS-PACK opt-ins. Keep CPU microcode on, because security-misc notes that full mitigations depend on it ([security-misc](https://www.github.com/Kicksecure/security-misc)). One unverified item matters for the privacy claim: Fedora's DNF/rpm-ostree "countme" ping and any inherited telemetry should be audited and documented before Nexus calls itself privacy-respecting.

## Server and self-hosting: one WireGuard edge serves VPN, hosting and game servers

The turnkey self-hosting products all share one pattern. There is an app catalogue of container templates. Every app automatically gets a subdomain, a certificate and optional SSO. Some add one-command backups. YunoHost 12 issues Let's Encrypt certificates straight from the app install form across ~500 packaged apps ([YunoHost forum](https://forum.yunohost.org/t/yunohost-12-0-bookworm-release-sortie-de-yunohost-12-0-bookworm/31673)). Cosmos bundles a reverse proxy, automatic certs, SSO with MFA, Restic backups and a no-open-ports VPN ([Cosmos README](https://raw.githubusercontent.com/azukaar/Cosmos-Server/master/readme.md)). Nexus can reproduce the pattern with FOSS parts that fit bootc, because everything lives in /etc and /var rather than /usr. **Caddy** enables HTTPS automatically when it knows the hostname. It supports HTTP-01, TLS-ALPN-01 and DNS-01 (DNS-01 needs no open ports and is required for wildcards), and falls back across issuers when renewal fails ([Caddy docs](https://caddyserver.com/docs/automatic-https)). **Podman Quadlet** files under /etc/containers/systemd should survive image updates by bootc's /etc merge (an inference, not verified). For game servers, **LinuxGSM** (MIT) gives one verb set (`install/update/monitor/backup/details/console`) across 150 games, restarts hung servers and sends alerts to ntfy, Gotify, Discord and others ([linuxgsm.com](https://linuxgsm.com/)). **Pelican**, a Pterodactyl fork, is the web-panel option: its AGPLv3 panel drives a Docker daemon called Wings (MIT), which builds servers from "Egg" templates ([pelican.dev](https://pelican.dev/docs)). It is heavier, so offer it as an add-on rather than the default.

That gives a concrete, substantive `nexus server` family. `init` sets up the firewall, Caddy, automatic updates and opt-in Cockpit. `app add|remove|list` works from a Quadlet catalogue. `site add <domain> --static <dir>|--port <n>` creates a Caddy site. `game add|update|backup|console` wraps LinuxGSM. `backup` uses restic. `status` and `doctor` complete the set. Cockpit, Fedora's web console with Podman and ostree plugins, is the obvious optional web UI, but its details weren't verified in this research. DietPi's numeric software IDs and single-file unattended install ([DietPi docs](https://dietpi.com/docs/usage/)) suggest a declarative `/etc/nexus/server.toml` that the resolver applies at first boot.

For **"Nexus-hosted sites without exposing the home IP"**, only one option avoids trusting a third party: a cheap VPS that the home machine dials out to. That costs about **€4–10/month** (e.g. Hetzner CX22 at €4.35) ([OSSAlt](https://ossalt.com/guides/best-open-source-alternatives-cloudflare-tunnels-2026)). Visitors resolve the VPS IP, and nothing opens at home. `nexus server expose setup --vps user@ip` can install WireGuard + Caddy on the VPS over SSH. `expose add` then writes a site block that proxies to the home box's WireGuard address. This reuses the WireGuard plumbing from the VPN add-on, so one investment makes the VPN, server and hosting stories substantive together. Two honest caveats belong in the UI. First, if TLS terminates on the VPS, the VPS can see plaintext. For end-to-end TLS, run Caddy at home and do SNI/TCP passthrough on the VPS with rathole or frp. **rathole** (Apache-2.0, Rust, Noise encryption) is the lightest; **frp** sends its auth token in plaintext unless control-channel TLS is enabled ([OSSAlt](https://ossalt.com/guides/best-open-source-alternatives-cloudflare-tunnels-2026)). Second, **game servers use UDP/non-HTTP traffic and effectively need the VPS route**: Tailscale Funnel is TLS-only on ports 443/8443/10000 ([Tailscale docs](https://tailscale.com/docs/features/tailscale-funnel.md)). **Pangolin** is the polished option. It is WireGuard-based, with a home connector that needs "no open ports or public IP", plus SSO, RBAC and geoblocking. Its Community Edition is AGPL-3 ([Pangolin GitHub](https://github.com/fosrl/pangolin)). The no-VPS fallbacks can be offered with warnings. Tailscale Funnel hides your IP and can't read content, but gives you only `*.ts.net` names and an unpublished bandwidth cap. With Cloudflare Tunnel, "every HTTP request… is logged and inspectable by Cloudflare" ([OSSAlt](https://ossalt.com/guides/best-open-source-alternatives-cloudflare-tunnels-2026)).

**Licensing traps** decide what Nexus can bundle under GPL-3.0, especially with a paid unlock planned.

| Project | Licence / model | Implication for Nexus |
|---|---|---|
| Umbrel / umbrelOS | PolyForm Noncommercial, "not FOSS" ([isitreallyfoss](https://isitreallyfoss.com/projects/umbrel)) | Do not bundle, fork or rebrand. A paid Nexus tier makes this worse |
| Cosmos Server | Apache-2.0 + **Commons Clause** (no selling it or SaaS on it) ([Cosmos README](https://raw.githubusercontent.com/azukaar/Cosmos-Server/master/readme.md)) | Not OSI-open. User-installed optional app at most |
| ZimaOS (CasaOS lineage) | Freemium since 1.5.0: free tier capped at 10 apps/4 disks/3 users; $29 Plus ([Notebookcheck](https://www.notebookcheck.net/ZimaOS-1-5-0-goes-paid-community-reacts-with-dissapointment.1122850.0.html)) | A cautionary tale of open-core drift, and CasaOS's maintenance status is unverified. Avoid as a foundation |
| Pangolin | AGPL-3 CE + commercial EE since Oct 2025 ([Linuxiac](https://linuxiac.com/pangolin-reverse-proxy-moves-to-dual-licensing-with-new-enterprise-edition/)) | CE is fine as a separate container. Don't depend on EE-only features |
| Pelican | Panel AGPLv3, Wings MIT ([pelican.dev](https://pelican.dev/docs)) | Fine as containers |
| LinuxGSM, rathole, frp, chisel | MIT / Apache-2.0 ([linuxgsm.com](https://linuxgsm.com/); [OSSAlt](https://ossalt.com/guides/best-open-source-alternatives-cloudflare-tunnels-2026)) | Safe to wrap and bundle |

Whole-OS appliances (Proxmox VE 9.2 on Debian 13.5, whose enterprise repo needs a subscription ([Proxmox forum](https://forum.proxmox.com/threads/proxmox-virtual-environment-9-2-available.183741/))) and API-only Talos, with ~12 binaries and no SSH ([InfoQ](https://www.infoq.com/news/2025/10/talos-linux-kubernetes)), are sources of ideas, not components. Talos's lockdown in particular contradicts "never limit the user".

## Prioritized shortlist: what to build next, later, or never

The ordering weighs user-visible value against solo-developer effort and against the risk of breaking Nexus's principles. Effort is a judgement call, not a measured figure.

| Priority | Feature | Why now / why not |
|---|---|---|
| **Next** | Signed images + SBOM + provenance in GHCR CI; Greenboot auto-rollback | Cheap, protects every user from a bad solo build |
| **Next** | `nexus rollback/rebase` with /etc-loss warning; gated staged updates with a toggle | Wraps bootc's sharp edges; core "inform and warn" |
| **Next** | yafti-style component metadata (status probe + actions) driving the first-boot wizard, settings GUI and CLI help | Unifies beginner and advanced modes; prevents Garuda-style silent failures |
| **Next** | Gaming baseline: split-lock sysctl, NTSYNC, MangoHud, gamescope session, `nexus gaming doctor` | Trivial effort, real evidence, and directly relevant to Intel Iris Xe |
| **Next** | Default security tier + `nexus security audit` (the core of Nexus antivirus) | Low breakage, honest replacement for the AV idea |
| **Next** | `nexus specs` / doctor on Mint 22.3's page model, plus a deployments view | Already planned; Mint gives the exact design |
| **Later** | Declarative manifest + generations + `nexus plan/diff` | The highest-value resolver feature, but it needs transactions first |
| **Later** | `nexus server` family (Caddy + Quadlet catalogue + LinuxGSM + restic) | Makes the server add-on substantive |
| **Later** | WireGuard VPS edge (`nexus server expose`), shared with the VPN add-on plus killswitch | Delivers private VPN and IP-hiding hosting with one codebase |
| **Later** | Kali Distrobox with separate home; "Protect" tier; DoT selector; Kloak | Opt-in add-ons with moderate effort |
| **Later** | Aeon install ladder in `nexus install`; Warpinator-based Nexus share; Zorin-style .exe helper | Beginner wins, each self-contained |
| **Later** | scx_lavd opt-in; OGC kernel for the gaming image; signed NVIDIA open kmods + Cardwire | Medium effort; needs hardware you may not have to test |
| **Later / experimental** | Factory reset; soft reboot; transient-root school mode | Upstream still experimental or composefs-only |
| **Skip** | systemd-sysext/confext add-ons | Officially unsupported on bootc; conflicts with the architecture |
| **Skip** | Client-side image builds (blendOS-style); x86-64-v3/LTO/PGO rebuilds of Fedora | Too heavy for Iris Xe laptops and for one maintainer; gains don't transfer |
| **Skip** | gamemode/ananicy/system76-scheduler as headline features; re-setting vm.max_map_count | Placebo or already done by Fedora |
| **Skip** | Real-time ClamAV "antivirus"; forced Paranoid hardening (userns off, sudo removal, Xwayland off) by default | Weak protection, or breakage that violates "never forced-on" |
| **Skip** | Bundling Umbrel, Cosmos or ZimaOS | Non-FOSS or freemium licences clash with GPL-3.0 and a paid tier |

Several gaps in the underlying research should be checked before committing. Fetches were blocked for nixos.org, several GitHub repos and parts of docs.fedoraproject.org. The following were **not verified**:

- whether Fedora ships `scx-scheds` in its main repos;
- how `/etc/bootc/kargs.d` changes apply on upgrade, and whether `--enforce-container-sigpolicy`, UKI or composefs are default in Fedora 44/45 bootc images;
- the licences of uupd and yafti;
- Warpinator's protocol details and LocalSend as an alternative;
- Cockpit, Caddy, restic, YunoHost and DietPi licences;
- current CasaOS and Exegol status;
- any Intel-iGPU-specific benchmark of any gaming tweak;
- Fedora telemetry (countme);
- Stripe's per-transaction fee.

The SteamOS, Nobara, CachyOS Hello/kernel-manager, Ubuntu installer, Endless OS and Manjaro tooling were also not researched. Treat any claim about them as background, not evidence.

## Conclusion

Nexus-CORE's real competition is not Mint or CachyOS but the Universal Blue family: Fedora bootc images with a recipe runner and a GUI portal. What Nexus can do that they don't is make the capability resolver the product. A single declarative intent ("this machine is for school and coding") expands into an image tag plus runtime add-ons, is previewed as a diff, and is recorded as a generation. bootc has the primitives but no such layer, and NixOS has the layer but not Fedora's ecosystem.

The thin add-ons are thin because they have been framed as package lists. Gaming becomes substantive by being *honest and measurable*: a few evidence-backed fixes plus a doctor that shows what is applied and how to verify it. Server, VPN and "hosting without exposing your IP" turn out to be one feature, a WireGuard edge, which becomes three user-facing add-ons. Security becomes credible by replacing the antivirus idea with an audit. In each case the principle Nexus already holds, inform and warn rather than restrict, is also the cheapest thing for one developer to build and maintain.


Searched manually by David:
-Fedora's main repositories do not ship the sched_ext schedulers; they must be built from source using a kernel configured with CONFIG_SCHED_CLASS_EXT=y.
-Default Boot Settings in Fedora 44

Fedora 44 comes with specific boot settings enabled by default to enhance user experience and system functionality. Below are the key settings:
Graphical Boot

    Graphical User Interface (GUI): Fedora 44 typically enables graphical boot by default, which means it attempts to start the GUI during the boot process.

Bootloader

    GRUB Bootloader: Fedora 44 uses the GRUB bootloader to manage boot options for the operating system. This allows users to select between different operating systems or kernel versions if multiple are installed.

Additional Features

    Kernel Parameters: Fedora may include default kernel parameters that optimize the boot process, although specific parameters can vary based on hardware and installation choices.

These settings are designed to provide a smooth and user-friendly boot experience, allowing users to access their systems quickly and efficiently.

-Licensing Information for uupd and yafti
uupd License

    License Type: Apache-2.0 License
    Details: This license allows users to freely use, modify, and distribute the software, provided that proper attribution is given to the original authors.

yafti License

    License Type: Typically follows similar open-source licensing practices as uupd.
    Details: While the specific license for yafti is not explicitly stated, it is associated with the Universal Blue project, which generally adheres to open-source principles similar to those of the Apache-2.0 license.

Summary Table
Tool	License Type	Key Features
uupd	Apache-2.0	Free use, modification, and distribution with attribution.
yafti	Similar to uupd	Likely follows open-source principles, specifics not detailed.

Both tools are part of the Universal Blue ecosystem, promoting community-driven development and collaboration.

-Stripe's Per-Payment Fees

Stripe charges different fees based on the payment method used. Below is a breakdown of the standard fees for various payment types in the U.S.
Online Card Payments

    Fee Structure: 2.9% of the transaction amount + $0.30 per successful payment.
    Example: For a $100 sale, the fee would be calculated as follows:
        Fee = ($100 x 0.029) + $0.30 = $3.20

In-Person Payments

    Fee Structure: 2.7% of the transaction amount + $0.05 per successful payment.
    Example: For a $100 sale, the fee would be:
        Fee = ($100 x 0.027) + $0.05 = $2.75

ACH Payments

    Direct Debit Fee: 0.8% of the transaction amount, capped at $5.
    Example: For a $500 transaction, the fee would be:
        Fee = $500 x 0.008 = $4.00 (within the cap)

Additional Fees
Payment Type	Fee Structure
International Cards	+1.5% on the transaction amount
Currency Conversion	+1% on the transaction amount
Manually Entered Cards	+0.5% on the transaction amount
Refunds and Disputes

    Refunds: Stripe does not charge a separate fee for issuing refunds, but the original processing fees are not returned.
    Dispute Fee: $15 for each disputed charge.

These fees are automatically deducted from the payment amount, so businesses receive the net revenue after fees are applied.
