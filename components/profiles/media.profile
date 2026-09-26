Profile: media
Description: Play any video or music file
# What a person means by "it plays everything", and nothing past it:
# no editors, no recording, no streaming tools.
#
# VLC from Flathub rather than a package, for the reason Steam is:
# the full codec set lives in RPM Fusion, and an RPM Fusion package
# layered on an image system has to match the image's own libraries
# exactly. A Fedora update to gstreamer or ffmpeg makes it
# uninstallable until the next image build. The Flatpak carries its
# own codecs, and its runtime brings hardware video decoding with it.
# A VLC already installed as a package still counts.
#
# No H.264 plugin for Firefox here. Cisco's OpenH264 cannot be shipped
# in the image, so the image carries Fedora's stand-in, noopenh264, and
# the real one replaces it -- which on an image-based system is an
# override (rpm-ostree override remove noopenh264 --install openh264
# --install mozilla-openh264), not an addition. Asking for it as an
# addition failed the whole transaction on the Asus (26 September),
# Elisa included. Until Nexus can do replacements, the website gives the
# one command; VLC plays H.264 regardless.
Requires: vlc,
 music-player
Flatpak: vlc=org.videolan.VLC
