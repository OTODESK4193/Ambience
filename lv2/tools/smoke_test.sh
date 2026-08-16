#!/bin/bash
# Run lv2/tools/smoke_test.c against the built aarch64 bundle, on the host,
# under qemu-aarch64.
#
#   lv2/tools/smoke_test.sh
#
# Requires qemu-aarch64-static (Debian/Ubuntu: qemu-user-static). If it is not
# installed this exits 0 with a notice rather than failing the build - the
# device-side check with LoopPad_Jack's tools/lv2chain is the authoritative
# one, this is just the fast local gate.
#
# qemu emulates the instruction set faithfully, including the FPCR
# flush-to-zero bit ScopedNoDenormals sets, so the numbers here are the numbers
# the Pi produces. What it does NOT tell you is anything about timing - see the
# CPU/xrun step in lv2/README.md for that.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
LV2DIR="$(cd "${HERE}/.." && pwd)"

LOOPPAD_ROOT="${LOOPPAD_ROOT:-${HOME}/Sources/LoopPad_Jack}"
OUT="${LOOPPAD_ROOT}/Buildroot/build/output"
SYSROOT="${OUT}/staging"
CXX="${OUT}/host/bin/aarch64-buildroot-linux-gnu-g++"

SO="${LV2DIR}/build/stage/lv2/AmbienceReverb.lv2/AmbienceReverb.so"
BIN="${LV2DIR}/build/smoke_test"

die() { echo "ERROR: $*" >&2; exit 1; }

if ! command -v qemu-aarch64-static >/dev/null; then
    echo "qemu-aarch64-static not installed - skipping the host smoke test."
    echo "Verify on the device instead:  /tmp/lv2chain -t <uri>"
    exit 0
fi

[ -f "${SO}" ]  || die "no built bundle at ${SO} - run lv2/build.sh first"
[ -x "${CXX}" ] || die "cross g++ not found: ${CXX}"

# The test includes lv2/src/ports.h, so port count, ranges and defaults come
# from the same generated table the plugin and the TTL use.
"${CXX}" --sysroot="${SYSROOT}" -O1 -std=c++20 \
    -I"${LV2DIR}/src" \
    "${HERE}/smoke_test.cpp" -o "${BIN}" -ldl -lm

qemu-aarch64-static -L "${SYSROOT}" "${BIN}" "${SO}"
