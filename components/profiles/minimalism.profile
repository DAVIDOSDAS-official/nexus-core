Profile: minimalism
Description: A full desktop kept simple: one tool per job, nothing duplicated
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
 audio-server
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
 audio-server=pipewire
