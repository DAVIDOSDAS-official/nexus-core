# What advanced users want

Notes gathered while deciding what Nexus should be, kept as the
reasoning behind two decisions rather than as a plan.

The useful thing about this document is what it does not say. Six
pillars of what experienced Linux users want, and not one of them is
"packages from several distributions in one root filesystem". They
want control, transparency, reproducibility and a small footprint --
all of which are achievable on a single base. That is the evidence
behind decision 2.

Pillar 1 also arrives independently at the model in decision 2:
*"Native PM + AUR/Flatpak + automated build scripts"*. Not a merged
system -- one base, plus additional sources.

Pillar 3 is what the `tiling` profile implements.

Pillar 2 -- auditing what actually runs -- is the honest gap. Nexus
has no concept of a service.

---

For beginners, the "perfect" distro is defined by **out-of-the-box convenience**: sensible defaults, graphical software stores, and pre-configured drivers.

For advanced users, the "perfect" distro is defined by **control, transparency, and friction removal**. It isn't a specific distribution name; it is an environment that gets out of your way, lets you automate your workflow, and doesn't make decisions for you behind the scenes.

Achieving your personal "perfect" distro comes down to several core pillars:

---

### 1. The Core Ecosystem & Package Management

A power user's system lives or dies by how easily, reliably, and directly software can be acquired, built, and updated.

* **Repository Reach:** How vast is the native package library? Advanced setups favor distros with massive community-maintained repositories (like the Arch User Repository/AUR, Nix ecosystem, or Fedora's COPR) over manually managing third-party PPAs or random binaries.
* **Declarative System Management:** Higher-tier control involves defining your entire operating system, installed software, and configs in a single code file (e.g., NixOS or Guix). If your system breaks or you buy a new machine, one command rebuilds your entire exact setup.
* **Update Cadence:** Decide between a **Rolling Release** (bleeding-edge software, instant kernel/driver updates, higher maintenance awareness) or a **Rock-Solid Base** (Debian/Ubuntu LTS base with sandboxed Flatpaks/containers for modern apps).

---

### 2. Full Control Over the Init System & Services

Beginner distros run dozens of background services by default (telemetry helpers, graphical updater daemons, indexers). Advanced customization means auditing what actually runs.

* **Minimal Footprint:** Starting from a bare-bones base (Arch, Gentoo, Debian Minimal, Void) where you explicitly enable only the `systemd` (or `runit`/`dinit`) services you actually need.
* **Resource Predictability:** Zero unexpected background CPU spikes or disk usage during heavy workloads (compiling code, rendering, gaming).

---

### 3. Modular Desktop Architecture (WM vs. DE)

Instead of accepting a monolithic Desktop Environment (like stock GNOME or KDE Plasma), advanced users usually deconstruct their graphical interface.

* **Tiling Window Managers (TWMs):** Replacing a floating window manager with a keyboard-driven tiling WM (e.g., `i3`, `Sway`, `Hyprland`, `bspwm`).
* **Unix Philosophy UI:** Building the desktop out of tiny, independent, highly scriptable tools:
* **Bar:** `waybar` or `polybar`
* **Launcher:** `rofi`, `fuzzel`, or `dmenu`
* **Compositor:** `picom` or native Wayland compositing
* **Notifications:** `dunst` or `mako`



---

### 4. Portability & Reproducibility (Dotfiles)

A truly perfect setup isn't tied to a specific hard drive or install media.

* **Git-Managed Configs:** All configuration files (`~/.config/`) are stored in a Git repository.
* **Dotfile Managers:** Using tools like `GNU Stow`, `Chezmoi`, or custom shell scripts to symlink configurations instantly onto any fresh installation.

---

### 5. Deep Terminal & Shell Ergonomics

Since advanced workflows rely heavily on the terminal, your shell configuration becomes your primary development tool.

* **Modern Terminal Alternatives:** Moving from legacy tools to high-performance GPU-accelerated terminals (`Kitty`, `Alacritty`, `WezTerm`).
* **Enhanced Shell Tooling:**
* Shell choice (`zsh`, `fish`, or tuned `bash`).
* Modern CLI replacements for classic Unix tools: `eza` (instead of `ls`), `bat` (`cat`), `ripgrep` (`grep`), `fd` (`find`), and `fzf` for fuzzy history/file searching.


* **Multiplexers:** Session persistence using `tmux` or Zellij for multi-pane terminal setups.

---

### 6. Storage & File System Control

Advanced users tailor the storage layer to prevent data loss and optimize performance:

* **Modern File Systems:** Utilizing **Btrfs** or **ZFS** rather than standard `ext4`.
* **Instant Snapshots:** Configuring automated pre-update snapshots (e.g., via `Snapper` or `Timeshift` with Btrfs root subvolumes). If a kernel update or bad config breaks boot, you rollback to a functional state in under 10 seconds right from the GRUB/systemd-boot menu.

---

### Comparative Architecture: Beginner vs. Advanced

| Aspect | Beginner Ideal | Advanced Ideal |
| --- | --- | --- |
| **System State** | Configured via graphical settings panels | Defined in text files (`dotfiles`, `Nix` expressions) |
| **Workflow** | Mouse-heavy floating windows | Keyboard-driven modal tiling layout |
| **Install Base** | Everything pre-installed "just in case" | Absolute bare minimal base; zero bloat |
| **Package Source** | GUI App Stores, official repos | Native PM + AUR/Flatpak + automated build scripts |
| **Maintenance** | Automatic / GUI notification updates | Scripted updates paired with Btrfs/ZFS snapshots |
