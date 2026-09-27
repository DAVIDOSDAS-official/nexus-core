# minimal edition — proposal for review

Status: **draft for David.** Nothing here is code yet.

## What it is for

A computer with little to spare: 2 GB of RAM, an old dual-core CPU, a
32 GB disk. It has to feel like a finished desktop, not a bare window
manager, and stay out of the machine's way.

Same foundations as the KDE edition, unchanged: installer, disk
encryption, signed weekly updates with rollback, Nexus first boot and
add-ons, wallpaper, boot splash.

## Where it stands today

`minimal.profile` asks for Openbox, foot, nano and PCManFM. It builds
(2.9 GB image) but a normal person cannot use it: no login screen, no
Wi-Fi menu, no sound control, no browser, no panel. That is the gap.

## The desktop — the main decision

| Option | Idle RAM (rough) | For | Against |
|---|---|---|---|
| **A. LXQt** (recommended) | ~400–500 MB | Complete: panel, menu, settings, Wi-Fi, sound, file manager. Qt, like KDE, so the future Nexus app looks right on both editions. Fedora has an LXQt edition, so it is maintained. | Plainer look than KDE |
| B. Xfce | ~500–600 MB | Very mature, familiar | GTK, so the Nexus app would look foreign; its Wayland support is still partial |
| C. Openbox + parts (today) | ~250 MB | Lightest | Every piece (panel, Wi-Fi, sound, settings) assembled and configured by us, and kept working by us |

Exact numbers get measured, not promised: see *Tests*.

## What it installs (with option A)

| Piece | Choice | Why |
|---|---|---|
| Desktop | LXQt | See above |
| Login screen | SDDM, Breeze theme | Same as KDE edition; graphical, known to work with our setup |
| Wi-Fi / network | NetworkManager + nm-tray | Wi-Fi from the panel, no terminal |
| Sound | PipeWire + pavucontrol-qt | Volume and devices from the panel |
| Files | PCManFM-Qt | LXQt's own |
| Terminal | QTerminal | LXQt's own |
| Text editor | FeatherPad | LXQt's own, light |
| Browser | Firefox | Heavy-ish, but every site works; a "light" browser that breaks sites is worse on old hardware than a slower one that works |
| PDF / images | qpdfview, LXImage-Qt | Light Qt viewers |
| Memory | zram on | Compressed swap in RAM: the single biggest help on 2 GB machines |

Left out: office, media players, games, extra themes. Those are add-ons.

## Add-ons offered at first boot

media, school, development, vpn, security. **Not gaming**: Steam alone
wants more RAM than this edition is for. (Still installable by hand.)

## Build and release

- Own image `nexus-core:minimal`, built weekly next to `:kde` by the
  same workflow, signed with the same key, checked by the same add-on
  check.
- Own ISO `nexus-core-minimal-preview1.iso` on archive.org, and a second
  download on the website ("KDE — full desktop" / "Minimal — for older
  computers").

## Tests before release

1. VM with **2 GB RAM, 2 CPUs**: install, encrypted boot, first boot,
   login, Wi-Fi/sound/browser work.
2. Measure after login, nothing open: `free -m` — target **under 600 MB
   used**. KDE measured the same way for comparison.
3. Firefox with 3 tabs open still usable on 2 GB.
4. The Asus (3.6 GB, UHD 600) as the real-hardware check.
5. An update from `:minimal` to the next weekly `:minimal`, signed.

## Questions for you

1. Desktop: **A (LXQt)**, B (Xfce) or C (Openbox + parts)?
2. Browser: Firefox (works everywhere, heavier) or a lighter one
   (Falkon — Qt, lighter, some sites misbehave)?
3. Add-ons: media, school, development, vpn, security — and no gaming?
