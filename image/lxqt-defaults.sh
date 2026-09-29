#!/bin/sh
#
# Defaults for the minimal edition's LXQt desktop, applied when the
# image is built. Does nothing where LXQt is not installed.
#
# The compositor. LXQt's startlxqtwayland takes it from the first
# session.conf with a compositor= line: ~/.config/lxqt, then
# /etc/xdg/lxqt. With none at all it does not start the desktop -- it
# starts a chooser (lxqt-config-session on its own) -- probably what
# the 0.1.19 VM showed, where 0.1.19 had changed only
# /usr/share/lxqt/wayland/default-compositor, which that script never
# reads. So every user gets labwc from
# /etc/xdg unless they pick something else in Session Settings.
# Only where labwc and LXQt's labwc setup are both installed.
#
# The panel (David's VM test of 0.1.18, 28 September). Its quick-launch
# area shipped empty and showed "Drop application icons here". It gets
# the browser, the file manager and the terminal, by whichever
# desktop-file names this Fedora uses.

set -eu

session=/etc/xdg/lxqt/session.conf
if [ -x /usr/bin/labwc ] && [ -d /usr/share/lxqt/wayland/labwc ]; then
    mkdir -p /etc/xdg/lxqt
    if [ -f "${session}" ] && grep -q '^\[General\]' "${session}"; then
        sed -i '/^compositor[[:space:]]*=/d' "${session}"
        sed -i 's/^\[General\]$/[General]\ncompositor=labwc/' "${session}"
    else
        printf '[General]\ncompositor=labwc\n' > "${session}.new"
        if [ -f "${session}" ]; then
            cat "${session}" >> "${session}.new"
        fi
        mv "${session}.new" "${session}"
    fi
    echo "LXQt compositor: labwc ($(grep -c '^compositor=labwc$' "${session}") line)"
fi

panel=/etc/xdg/lxqt/panel.conf
[ -f "${panel}" ] || exit 0
grep -q '^\[quicklaunch\]' "${panel}" || exit 0

found=""
for pick in "org.mozilla.firefox firefox" "pcmanfm-qt" "qterminal"; do
    for name in ${pick}; do
        file="/usr/share/applications/${name}.desktop"
        if [ -f "${file}" ]; then
            found="${found} ${file}"
            break
        fi
    done
done

[ -n "${found}" ] || exit 0

tmp="$(mktemp)"
count=0
awk -v apps="${found}" '
    { print }
    $0 == "[quicklaunch]" {
        n = split(apps, list, " ")
        for (i = 1; i <= n; i++) printf "apps\\%d\\desktop=%s\n", i, list[i]
        printf "apps\\size=%d\n", n
    }
' "${panel}" > "${tmp}"
cat "${tmp}" > "${panel}"
rm -f "${tmp}"
echo "Panel quick launch:${found}"
