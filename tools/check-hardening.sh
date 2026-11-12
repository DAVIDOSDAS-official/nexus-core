#!/usr/bin/env bash
#
# Is a built program hardened the way CMakeLists.txt asks for? Read from
# the program itself, not from the flags that were passed.
#
#     tools/check-hardening.sh build/cli/nexus
#     tools/check-hardening.sh --expect=fortify,assertions,cet,stack-clash build/cli/nexus
#
# Run by ctest on the nexus that was just built, and by the image build
# on the programs that go into the image. Nothing is run; the program
# is read with readelf and objdump. Exits 1 and says what is missing if
# anything required is; 77 if readelf is not installed (nothing could
# be checked, and it says so rather than passing).
#
# What each line means, and how far it goes (0.1.88, review of patch
# 113: 0.1.87 printed "PIE, full RELRO, ..." after checking five things
# and listed in CMakeLists.txt four more it had only asked the compiler
# for):
#
# Always required --
#   PIE               the file is type DYN: loaded at a random address
#   RELRO, BIND_NOW   the GNU_RELRO segment exists and everything is
#                     bound at start, so the tables of function
#                     addresses are read-only before main() runs
#   NX stack          the GNU_STACK segment EXISTS and is RW, not RWE.
#                     (A file with no GNU_STACK at all gets an
#                     executable stack on some systems; 0.1.87 only
#                     looked for "RWE" and passed such a file.)
#   stack protector   __stack_chk_fail is referenced: at least one
#                     function has a canary
#
# Required only when named in --expect (the build knows which it asked
# for; a Debug build has no FORTIFY, an ARM build has no CET) --
#   fortify           at least one checked C library call (__*_chk) is
#                     referenced: _FORTIFY_SOURCE was in effect where
#                     the compiler could use it
#   assertions        libstdc++'s assertion handler is referenced:
#                     _GLIBCXX_ASSERTIONS was in effect (GCC 13 or
#                     newer; older ones put the check inline, where it
#                     cannot be seen from outside)
#   cet               x86 only: the file is MARKED for Intel CET (IBT
#                     and shadow stack). The linker sets the mark only
#                     if every part of the program has it. Whether the
#                     protection is switched on is up to the kernel and
#                     the processor, not this file
#   stack-clash       x86-64 only: at least one function with a frame
#                     larger than a page probes its stack as it grows
#                     (the instruction -fstack-clash-protection adds).
#                     Evidence that the flag was in effect, from the
#                     code itself; a program with no large frame would
#                     show nothing either way
#
# "at least one" is the limit of what a finished program can show for
# the per-function protections: it proves the flag reached the build,
# not that every file was compiled with it. The build passes the flags
# to every file in one place (CMakeLists.txt).
#
# Anything not required is still looked for and reported, as "seen" or
# "not seen", so the output never names a protection it did not find.

set -u

expect=""
programs=()
for arg in "$@"; do
    case "${arg}" in
        --expect=*) expect="${expect},${arg#--expect=}" ;;
        "") ;;                      # an empty word from the build system
        -*) echo "Unknown option: ${arg}" >&2; exit 2 ;;
        *) programs+=("${arg}") ;;
    esac
done
for item in ${expect//,/ }; do
    case "${item}" in
        fortify | assertions | cet | stack-clash) ;;
        *) echo "Unknown protection in --expect: ${item}" >&2; exit 2 ;;
    esac
done
expected() { case ",${expect}," in *",$1,"*) return 0 ;; *) return 1 ;; esac; }

if [ "${#programs[@]}" -eq 0 ]; then
    echo "Use: $0 [--expect=fortify,assertions,cet,stack-clash] <program>..." >&2
    exit 2
fi
if ! command -v readelf > /dev/null 2>&1; then
    # Said, not passed off as checked.
    echo "SKIPPED: readelf (binutils) is not installed, so nothing was checked."
    exit 77
fi

problems=0
for program in "${programs[@]}"; do
    if [ ! -f "${program}" ]; then
        echo "PROBLEM: ${program} does not exist"
        problems=$((problems + 1))
        continue
    fi
    header="$(readelf -hW "${program}" 2>/dev/null)"
    segments="$(readelf -lW "${program}" 2>/dev/null)"
    dynamic="$(readelf -dW "${program}" 2>/dev/null)"
    symbols="$(readelf -sW --dyn-syms "${program}" 2>/dev/null)"
    notes="$(readelf -nW "${program}" 2>/dev/null)"
    if [ -z "${header}" ] || [ -z "${segments}" ]; then
        echo "PROBLEM: ${program} could not be read as an ELF program"
        problems=$((problems + 1))
        continue
    fi
    machine="$(printf '%s\n' "${header}" | sed -n 's/^ *Machine: *//p')"
    x86_64=no
    case "${machine}" in *X86-64*) x86_64=yes ;; esac

    missing=""
    verified=""
    unseen=""

    # ---- always required
    if printf '%s\n' "${header}" | grep -Eq 'Type:[[:space:]]+DYN'; then
        verified="${verified}, PIE"
    else
        missing="${missing}, PIE"
    fi
    if printf '%s\n' "${segments}" | grep -q 'GNU_RELRO'; then
        verified="${verified}, RELRO"
    else
        missing="${missing}, RELRO"
    fi
    if printf '%s\n' "${dynamic}" | grep -Eq 'BIND_NOW|FLAGS_1.*NOW'; then
        verified="${verified}, BIND_NOW"
    else
        missing="${missing}, BIND_NOW"
    fi
    # The segment's flags are the field before its alignment: "RW " for
    # a stack that cannot be run, "RWE" for one that can.
    stack_line="$(printf '%s\n' "${segments}" | grep 'GNU_STACK' | head -n 1)"
    if [ -z "${stack_line}" ]; then
        missing="${missing}, NX stack (no GNU_STACK segment at all)"
    else
        stack_flags="$(printf '%s\n' "${stack_line}" | awk '{ f = ""; for (i = 7; i < NF; i++) f = f $i; print f }')"
        case "${stack_flags}" in
            RW) verified="${verified}, NX stack" ;;
            *E*) missing="${missing}, NX stack (GNU_STACK is ${stack_flags}: executable)" ;;
            *) missing="${missing}, NX stack (GNU_STACK flags are '${stack_flags}', expected RW)" ;;
        esac
    fi
    if printf '%s\n' "${symbols}" | grep -q '__stack_chk_fail'; then
        verified="${verified}, stack protector"
    else
        missing="${missing}, stack protector"
    fi

    # ---- required when expected, reported either way
    note() {   # note <name> <found: yes|no|n/a> <detail>
        if [ "$2" = yes ]; then
            verified="${verified}, $3"
        elif [ "$2" = n/a ]; then
            unseen="${unseen}, $3"
        elif expected "$1"; then
            missing="${missing}, $3"
        else
            unseen="${unseen}, $3"
        fi
    }

    fortified="$(printf '%s\n' "${symbols}" | grep -cE ' __[a-z0-9_]+_chk(@|$)' || true)"
    if [ "${fortified}" -gt 0 ]; then
        note fortify yes "FORTIFY (${fortified} checked calls)"
    else
        note fortify no "FORTIFY (no checked C library call referenced)"
    fi

    if printf '%s\n' "${symbols}" | grep -q 'glibcxx_assert_fail'; then
        note assertions yes "libstdc++ assertions"
    else
        note assertions no "libstdc++ assertions (handler not referenced)"
    fi

    if [ "${x86_64}" = yes ]; then
        feature="$(printf '%s\n' "${notes}" | grep 'x86 feature:' | head -n 1)"
        case "${feature}" in
            *IBT*SHSTK* | *SHSTK*IBT*) note cet yes "marked for CET (IBT, SHSTK)" ;;
            *) note cet no "CET mark (IBT and SHSTK not both set)" ;;
        esac
        if command -v objdump > /dev/null 2>&1; then
            probes="$(objdump -d --no-show-raw-insn "${program}" 2>/dev/null \
                | grep -cE 'orq?[[:space:]]+\$0x0,\(%rsp\)' || true)"
            if [ "${probes}" -gt 0 ]; then
                note stack-clash yes "stack clash probes (${probes} large frames)"
            else
                note stack-clash no "stack clash probes (none found)"
            fi
        elif expected stack-clash; then
            missing="${missing}, stack clash probes (objdump is not installed, so not checked)"
        else
            unseen="${unseen}, stack clash probes (objdump not installed: not checked)"
        fi
    else
        note cet n/a "CET (not an x86 program: does not apply)"
        note stack-clash n/a "stack clash probes (only checked on x86-64)"
    fi

    if [ -n "${missing}" ]; then
        echo "PROBLEM: ${program} is missing: ${missing#, }"
        problems=$((problems + 1))
    fi
    echo "${program}"
    echo "    verified in the file: ${verified#, }"
    [ -z "${unseen}" ] || echo "    not seen, not required here: ${unseen#, }"
done

[ "${problems}" -eq 0 ]
