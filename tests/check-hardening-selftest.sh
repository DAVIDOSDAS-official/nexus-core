#!/usr/bin/env bash
#
# Does tools/check-hardening.sh catch what it says it catches?
#
#     tests/check-hardening-selftest.sh [<C++ compiler>]
#
# Builds one small program with every protection, then the same program
# with each protection taken away in turn, and requires the checker to
# pass the first and fail each of the others (0.1.88, review of patch
# 113: a check that is never seen to fail proves little). One case
# cannot be built with a compiler: a program with no GNU_STACK segment
# at all is made by blanking that entry in the program's header.
#
# Run by ctest. SKIPPED (77) without a compiler, python3 or readelf, or
# on a machine that is not x86-64 (the CET and stack-clash cases are
# x86 ones). A protection this compiler cannot show even when it is on
# (libstdc++ assertions before GCC 13) is said so and not counted.

set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
CHECK="${HERE}/../tools/check-hardening.sh"
CXX="${1:-c++}"

for tool in "${CXX}" python3 readelf objdump; do
    if ! command -v "${tool}" > /dev/null 2>&1; then
        echo "SKIPPED: ${tool} is not installed."
        exit 77
    fi
done
if [ "$(uname -m)" != x86_64 ]; then
    echo "SKIPPED: only meaningful on x86-64 (this is $(uname -m))."
    exit 77
fi

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

cat > "${WORK}/t.cpp" <<'CPP'
#include <cstdio>
#include <cstring>
#include <vector>
__attribute__((noinline)) int big(const char* s, std::size_t n) {
    char buffer[70000];                       // a frame larger than a page
    std::snprintf(buffer, sizeof buffer, "%s", s);
    char small[16];
    std::memcpy(small, s, n);                 // a size only known when run
    std::vector<int> v(n, 1);
    return buffer[0] + small[0] + v[n / 2];   // a checked subscript
}
int main(int argc, char** argv) { return big(argv[0], static_cast<std::size_t>(argc)); }
CPP

PIE="-fPIE -pie"
CANARY="-fstack-protector-strong"
CLASH="-fstack-clash-protection"
CET="-fcf-protection"
FORTIFY="-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3"
ASSERT="-D_GLIBCXX_ASSERTIONS"
RELRO="-Wl,-z,relro"
NOW="-Wl,-z,now"
NX="-Wl,-z,noexecstack"

# build <name> <flags...>
build() {
    local name="$1"; shift
    # shellcheck disable=SC2086,SC2068  # flag lists, split on purpose
    "${CXX}" -O2 $@ "${WORK}/t.cpp" -o "${WORK}/${name}" 2> "${WORK}/${name}.log"
}

failures=0
# verdict <name> <wanted exit code> [checker options...]
verdict() {
    local name="$1" wanted="$2"; shift 2
    bash "${CHECK}" "$@" "${WORK}/${name}" > "${WORK}/${name}.out" 2>&1
    local got=$?
    if [ "${got}" -ne "${wanted}" ]; then
        failures=$((failures + 1))
        echo "FAILED: ${name}: the checker exited ${got}, should have exited ${wanted}"
        sed 's/^/    | /' "${WORK}/${name}.out"
    fi
}

if ! build all ${PIE} ${CANARY} ${CLASH} ${CET} ${FORTIFY} ${ASSERT} ${RELRO} ${NOW} ${NX}; then
    echo "SKIPPED: this compiler does not take the hardening flags:"
    sed 's/^/    /' "${WORK}/all.log" | head -n 5
    exit 77
fi

# What can this compiler show when everything is on?
bash "${CHECK}" "${WORK}/all" > "${WORK}/all.out" 2>&1
if [ $? -ne 0 ]; then
    echo "FAILED: a program built with every protection does not pass:"
    sed 's/^/    | /' "${WORK}/all.out"
    exit 1
fi
shows() { grep 'verified in the file' "${WORK}/all.out" | grep -q "$1"; }
expect_all=""
shows 'FORTIFY' && expect_all="${expect_all},fortify"
shows 'assertions' && expect_all="${expect_all},assertions"
shows 'CET' && expect_all="${expect_all},cet"
shows 'stack clash' && expect_all="${expect_all},stack-clash"
expect_all="${expect_all#,}"
for item in fortify assertions cet stack-clash; do
    case ",${expect_all}," in
        *",${item},"*) ;;
        *) echo "note: this compiler shows no '${item}' even with it on; that case is not counted" ;;
    esac
done

verdict all 0 "--expect=${expect_all}"

# ---- the always-required ones: taken away, the checker must fail
build no-pie -no-pie ${CANARY} ${CLASH} ${CET} ${FORTIFY} ${ASSERT} ${RELRO} ${NOW} ${NX} && verdict no-pie 1
build no-relro ${PIE} ${CANARY} ${CLASH} ${CET} ${FORTIFY} ${ASSERT} -Wl,-z,norelro ${NOW} ${NX} && verdict no-relro 1
build lazy ${PIE} ${CANARY} ${CLASH} ${CET} ${FORTIFY} ${ASSERT} ${RELRO} -Wl,-z,lazy ${NX} && verdict lazy 1
build no-canary ${PIE} -fno-stack-protector ${CLASH} ${CET} ${FORTIFY} ${ASSERT} ${RELRO} ${NOW} ${NX} && verdict no-canary 1
build exec-stack ${PIE} ${CANARY} ${CLASH} ${CET} ${FORTIFY} ${ASSERT} ${RELRO} ${NOW} -Wl,-z,execstack && verdict exec-stack 1

# No GNU_STACK segment at all: that entry of the program header
# (type 0x6474e551) is made a blank one.
python3 -I - "${WORK}/all" "${WORK}/no-stack-segment" <<'PY'
import struct, sys
data = bytearray(open(sys.argv[1], "rb").read())
assert data[:4] == b"\x7fELF" and data[4] == 2, "not a 64-bit ELF file"
phoff, = struct.unpack_from("<Q", data, 0x20)
phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
changed = 0
for i in range(phnum):
    at = phoff + i * phentsize
    if struct.unpack_from("<I", data, at)[0] == 0x6474E551:
        struct.pack_into("<I", data, at, 0)
        changed += 1
assert changed == 1, "expected exactly one GNU_STACK entry"
open(sys.argv[2], "wb").write(data)
PY
if [ -f "${WORK}/no-stack-segment" ]; then
    verdict no-stack-segment 1
    grep -q 'no GNU_STACK segment' "${WORK}/no-stack-segment.out" || {
        failures=$((failures + 1)); echo "FAILED: no-stack-segment: the checker did not say why"; }
else
    failures=$((failures + 1)); echo "FAILED: could not make the program without a GNU_STACK segment"
fi

# ---- the expected ones: taken away, the checker fails when the
# protection is expected, and passes -- saying "not seen" -- when not
case ",${expect_all}," in *",fortify,"*)
    build no-fortify ${PIE} ${CANARY} ${CLASH} ${CET} -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 ${ASSERT} ${RELRO} ${NOW} ${NX} &&
        { verdict no-fortify 1 --expect=fortify; verdict no-fortify 0; grep -q 'not seen.*FORTIFY' "${WORK}/no-fortify.out" ||
          { failures=$((failures + 1)); echo "FAILED: no-fortify: not reported as not seen"; }; } ;;
esac
case ",${expect_all}," in *",assertions,"*)
    build no-assert ${PIE} ${CANARY} ${CLASH} ${CET} ${FORTIFY} -U_GLIBCXX_ASSERTIONS ${RELRO} ${NOW} ${NX} &&
        { verdict no-assert 1 --expect=assertions; verdict no-assert 0; } ;;
esac
case ",${expect_all}," in *",cet,"*)
    build no-cet ${PIE} ${CANARY} ${CLASH} -fcf-protection=none ${FORTIFY} ${ASSERT} ${RELRO} ${NOW} ${NX} &&
        { verdict no-cet 1 --expect=cet; verdict no-cet 0; } ;;
esac
case ",${expect_all}," in *",stack-clash,"*)
    build no-clash ${PIE} ${CANARY} -fno-stack-clash-protection ${CET} ${FORTIFY} ${ASSERT} ${RELRO} ${NOW} ${NX} &&
        { verdict no-clash 1 --expect=stack-clash; verdict no-clash 0; } ;;
esac

# ---- stack clash judged by what the code does, not by one probe spelling
# (0.1.90: Fedora's compiler took every large frame a page at a time, yet
# the 0.1.88 checker looked for one exact instruction and failed it).
# -fstack-protector-all: these tiny programs have no buffer, and the
# stack protector is always required.
if [ "$(uname -m)" = x86_64 ]; then
    # A program with no frame over a page: nothing to probe, passes.
    printf 'int main(int c, char**) { return c - 1; }\n' > "${WORK}/small.cpp"
    "${CXX}" -O2 ${PIE} -fstack-protector-all ${CLASH} ${RELRO} ${NOW} ${NX} "${WORK}/small.cpp" \
        -o "${WORK}/small" 2> "${WORK}/small.log" && verdict small 0 --expect=stack-clash
    # Pages taken one at a time, each touched with a plain store instead
    # of "or": passes. The same frame taken in one jump: fails.
    cat > "${WORK}/steps.S" <<'ASM'
        .text
        .globl paged
paged:  sub $0x1000,%rsp
        movq $0,(%rsp)
        sub $0x1000,%rsp
        movq $0,(%rsp)
        add $0x2000,%rsp
        ret
        .globl jump
jump:   sub $0x2000,%rsp
        movq $0,(%rsp)
        add $0x2000,%rsp
        ret
        .section .note.GNU-stack,"",@progbits
ASM
    printf 'extern "C" void %s();\nint main() { %s(); return 0; }\n' paged paged > "${WORK}/paged.cpp"
    printf 'extern "C" void %s();\nint main() { %s(); return 0; }\n' jump jump > "${WORK}/jump.cpp"
    for name in paged jump; do
        "${CXX}" -O2 ${PIE} -fstack-protector-all ${CLASH} ${RELRO} ${NOW} ${NX} "${WORK}/${name}.cpp" \
            "${WORK}/steps.S" -o "${WORK}/${name}" 2> "${WORK}/${name}.log" || {
            failures=$((failures + 1)); echo "FAILED: could not build ${name}"; sed 's/^/    | /' "${WORK}/${name}.log"; }
    done
    verdict paged 1 --expect=stack-clash   # jump() is in it too: one jump anywhere fails
    printf 'extern "C" void paged();\nint main() { paged(); return 0; }\n' > "${WORK}/onlypaged.cpp"
    sed '/globl jump/,/ret/d' "${WORK}/steps.S" > "${WORK}/onlypaged.S"
    "${CXX}" -O2 ${PIE} -fstack-protector-all ${CLASH} ${RELRO} ${NOW} ${NX} "${WORK}/onlypaged.cpp" \
        "${WORK}/onlypaged.S" -o "${WORK}/onlypaged" 2> "${WORK}/onlypaged.log" &&
        verdict onlypaged 0 --expect=stack-clash
fi

# ---- things that are not programs, and a misspelt protection
printf 'not a program\n' > "${WORK}/text"
verdict text 1
verdict does-not-exist 1
verdict all 2 --expect=fortfy

if [ "${failures}" -ne 0 ]; then
    echo "${failures} case(s): the checker did not behave as it says."
    exit 1
fi
echo "ok: the checker passes a fully protected program and fails it with each protection removed (expected here: ${expect_all:-none})"
