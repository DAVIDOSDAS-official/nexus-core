# /etc/profile.d/nexus-dnf-hint.sh -- when dnf cannot change this system, say what can.
#
# Nexus-CORE is an image: /usr is read-only, so `sudo dnf install x`
# fails with Fedora's own error, which does not say what to do instead
# (the Asus, 1 November). This changes nothing about dnf: it runs
# exactly as typed, and only when it FAILS to install, remove or
# upgrade does one hint follow, pointing at the Nexus command for it.
# Interactive shells only. Bypass completely: `command sudo dnf ...`.

[ -e /run/ostree-booted ] || return 0
case $- in *i*) ;; *) return 0 ;; esac

__nexus_dnf_hint() {
    __nx_sub=""; __nx_pkg=""
    for __nx_a in "$@"; do
        case "$__nx_a" in
            -*) ;;
            *) if [ -z "$__nx_sub" ]; then __nx_sub=$__nx_a
               elif [ -z "$__nx_pkg" ]; then __nx_pkg=$__nx_a; fi ;;
        esac
    done
    case "$__nx_sub" in
        install|in|reinstall|localinstall)
            echo "" >&2
            echo "Nexus: this system is an image, so dnf cannot add packages to it." >&2
            echo "  Instead:  sudo nexus install ${__nx_pkg:-<name>}     (shows the plan; add --apply to do it)" >&2
            echo "  Apps:     Nexus Shop, or: nexus app install <id>" >&2 ;;
        remove|rm|erase)
            echo "" >&2
            echo "Nexus: this system is an image, so dnf cannot remove packages from it." >&2
            echo "  Instead:  sudo nexus remove ${__nx_pkg:-<name>}      (shows the plan; add --apply to do it)" >&2 ;;
        upgrade|update|up|distro-sync|downgrade)
            echo "" >&2
            echo "Nexus: this system updates as a whole image, not package by package." >&2
            echo "  Instead:  sudo nexus update --apply" >&2 ;;
    esac
    unset __nx_sub __nx_pkg __nx_a
}

dnf() {
    command dnf "$@"; __nx_rc=$?
    [ "$__nx_rc" -ne 0 ] && __nexus_dnf_hint "$@"
    return "$__nx_rc"
}

sudo() {
    case "${1:-}" in
        dnf|dnf5|/usr/bin/dnf|/usr/bin/dnf5)
            command sudo "$@"; __nx_rc=$?
            if [ "$__nx_rc" -ne 0 ]; then shift; __nexus_dnf_hint "$@"; fi
            return "$__nx_rc" ;;
        *) command sudo "$@" ;;
    esac
}
