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
# The OpenH264 plugin is Cisco's H.264 decoder for Firefox. It is only
# licensed when it comes from Cisco, which is why the image never
# carries it: the machine downloads it from Cisco's repository (on in
# Fedora by default) when this add-on is chosen.
Requires: vlc,
 music-player,
 browser-h264
Flatpak: vlc=org.videolan.VLC
