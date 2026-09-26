Profile: kde
Description: The KDE Plasma desktop, kept simple: one tool per job, nothing duplicated
# The desktop every Nexus-CORE image is built from. It was called
# minimalism until 26 September; that name now belongs to an add-on
# (a look, not a package set), and a machine's edition should say what
# it is: KDE.
Requires: desktop-session,
 display-manager,
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
 network-applet,
 volume-applet,
 desktop-portal
# Coherence is a preference, and it has to be stated.
#
# Resolving each requirement on its own picks the cheapest provider
# for each, which is individually right and collectively absurd: a KDE
# session with a GNOME document viewer pulls both toolkits, and the
# profile that asked for nothing duplicated gets two of everything.
#
# On a machine that already has a desktop these are only tie-breakers,
# so a GNOME system still resolves to its own tools. When generating
# an image from nothing, they decide.
Prefers: desktop-session=plasma-desktop,
 display-manager=sddm,
 terminal-emulator=konsole,
 file-manager=dolphin,
 text-editor=kate,
 document-viewer=okular,
 image-viewer=gwenview,
 archive-manager=ark,
 system-settings=systemsettings,
 screenshot-tool=kde-spectacle,
 network-applet=plasma-nm,
 volume-applet=plasma-pa,
 desktop-portal=xdg-desktop-portal-kde,
 audio-server=pipewire
