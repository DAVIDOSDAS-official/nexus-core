Profile: minimal
Description: A light, complete desktop for older computers: LXQt, and nothing it does not need
# The least that still works -- and "works" means a person can use it:
# log in on a screen, join Wi-Fi, change the volume, open a browser.
# Until 27 September this was Openbox, foot and nano with no login
# screen, no network menu and no sound control, which was light and
# unusable. LXQt is the lightest desktop that is complete, it is Qt
# like the KDE edition (so the Nexus app will look at home on both),
# and Fedora maintains it.
#
# Named piece by piece, session and shortcuts and theme included,
# because weak dependencies are off in image builds: nothing arrives
# because it was merely recommended.
#
# Wayland, with labwc as the compositor: X11 is on its way out of
# Fedora, and labwc is the small one LXQt supports. SDDM's greeter runs
# on Weston here (sddm-wayland-generic) because there is no KWin.
#
# Combining this with anything is a different request, not a smaller
# one, so it is refused with a pointer to the edition that does combine.
Exclusive: yes
Instead-Use: kde
Requires: desktop-session,
 session-manager,
 global-shortcuts,
 qt-desktop-integration,
 desktop-theme,
 window-manager,
 display-manager,
 greeter-compositor,
 desktop-panel,
 application-launcher,
 polkit-agent,
 notification-daemon,
 power-manager,
 terminal-emulator,
 file-manager,
 text-editor,
 web-browser,
 document-viewer,
 image-viewer,
 archive-manager,
 system-settings,
 screenshot-tool,
 audio-server,
 pulseaudio-compat,
 network-applet,
 volume-applet,
 desktop-portal,
 icon-theme
# Stated for the same reason kde states its own: without them, building
# from nothing picks the cheapest provider per capability, and a light
# desktop ends up with KDE's file manager because nothing said
# otherwise. On a machine that already has tools these only break ties.
Prefers: desktop-session=lxqt-labwc-session,
 session-manager=lxqt-session,
 global-shortcuts=lxqt-globalkeys,
 qt-desktop-integration=lxqt-qtplugin,
 desktop-theme=lxqt-themes,
 window-manager=labwc,
 display-manager=sddm,
 greeter-compositor=sddm-wayland-generic,
 desktop-panel=lxqt-panel,
 application-launcher=lxqt-runner,
 polkit-agent=lxqt-policykit,
 notification-daemon=lxqt-notificationd,
 power-manager=lxqt-powermanagement,
 terminal-emulator=qterminal,
 file-manager=pcmanfm-qt,
 text-editor=featherpad,
 web-browser=firefox,
 document-viewer=qpdfview,
 image-viewer=lximage-qt,
 archive-manager=lxqt-archiver,
 system-settings=lxqt-config,
 screenshot-tool=screengrab,
 audio-server=pipewire,
 pulseaudio-compat=pipewire-pulseaudio,
 network-applet=network-manager-applet,
 volume-applet=pavucontrol-qt,
 desktop-portal=xdg-desktop-portal-lxqt,
 icon-theme=breeze-icon-theme
