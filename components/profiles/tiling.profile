Profile: tiling
Description: A desktop built from parts: keyboard-driven, scriptable, no monolith
# Not a desktop environment. Each piece does one job and can be
# swapped without touching the others -- which is the whole reason
# advanced users deconstruct a desktop rather than configuring one.
#
# Every line here has real alternatives. "nexus options window-manager"
# lists them with what each costs.
Requires: window-manager,
 status-bar,
 application-launcher,
 notification-daemon,
 terminal-emulator,
 screen-locker,
 wallpaper-setter,
 audio-server,
 network-applet,
 desktop-portal
Prefers: window-manager=sway,
 status-bar=waybar,
 application-launcher=fuzzel,
 notification-daemon=mako,
 terminal-emulator=foot,
 screen-locker=swaylock,
 wallpaper-setter=swaybg,
 audio-server=pipewire
# On a machine already running X11 the Wayland preferences above are
# only tie-breakers, so an i3 setup still resolves to its own tools.
