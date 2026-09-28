#!/bin/sh
#
# Defaults for the minimal edition's LXQt desktop, applied when the
# image is built. Does nothing where LXQt is not installed.
#
# Found in David's VM test of 0.1.18 (28 September). (0.1.19 also
# pointed LXQt at labwc here; that was undone in 0.1.20 -- see
# minimal.profile.)
#
#   The panel. Its quick-launch area shipped empty and showed "Drop
#   application icons here". It gets the browser, the file manager and
#   the terminal, by whichever desktop-file names this Fedora uses.

set -eu

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
