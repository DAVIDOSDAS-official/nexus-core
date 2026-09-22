Profile: showcase
Description: Everything on display: effects, themes and polish, still safe to use
Requires: desktop-session,
 display-manager,
 compositor-effects,
 icon-theme,
 cursor-theme,
 font-collection,
 terminal-emulator,
 file-manager,
 text-editor,
 web-browser,
 image-viewer,
 audio-server
Prefers: audio-server=pipewire

# The gpu-vendor alternative that used to be the last requirement is
# gone. It was copied from gaming.profile, where it earns its place
# as the condition for a Prefers-When that picks a driver. Nothing
# here reads it.
#
# A hardware capability is not something that can be installed, so a
# profile that requires one can only be satisfied on a machine that
# already has it. `nexus image` runs inside a container, which has no
# GPU to detect, so the requirement could never resolve there -- and
# image/generate-lists.sh has been calling showcase incomplete ever
# since, over a line that was asking for nothing.
