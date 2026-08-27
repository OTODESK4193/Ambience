#!/bin/bash
# Measure what the Ambience engine costs, so the CPU budget on the device is a
# number rather than a guess.
#
#   lv2/tools/benchmark.sh            # host (native, fast, indicative)
#   lv2/tools/benchmark.sh --target   # aarch64 under qemu (NOT a timing result)
#
# The host build is the useful one. qemu emulates the ISA faithfully but not
# the timing, so --target only tells you the code runs - never read its
# microseconds as device numbers.
#
# The authoritative measurement is on the device: put the plugin in a
# pedalboard and watch mod-host's CPU in top and the JACK xrun counter.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
LV2DIR="$(cd "${HERE}/.." && pwd)"
REPO="$(cd "${LV2DIR}/.." && pwd)"
WORK="${LV2DIR}/build"

SOURCES=(
    "${HERE}/benchmark.cpp"
    "${REPO}/Source/DSP/UniversalEngine.cpp"
    "${REPO}/Source/DSP/BiquadFilters.cpp"
    "${REPO}/Source/DSP/MagnitudeResponseFitter.cpp"
    "${REPO}/Source/DSP/AcousticMetrics.cpp"
)

INCLUDES=(-I"${REPO}/Source" -I"${REPO}/Source/DSP" -I"${LV2DIR}/src")

mkdir -p "${WORK}"

if [ "${1:-}" = "--target" ]; then
    LOOPPAD_ROOT="${LOOPPAD_ROOT:-${HOME}/Sources/LoopPad_Jack}"
    OUT="${LOOPPAD_ROOT}/Buildroot/build/output"
    SYSROOT="${OUT}/staging"
    CXX="${OUT}/host/bin/aarch64-buildroot-linux-gnu-g++"

    command -v qemu-aarch64-static >/dev/null \
        || { echo "qemu-aarch64-static not installed"; exit 1; }

    "${CXX}" --sysroot="${SYSROOT}" -std=c++20 -O3 -mcpu=cortex-a72 \
        "${INCLUDES[@]}" "${SOURCES[@]}" -o "${WORK}/benchmark" -lm

    echo "Running under qemu - the code path is real, the timings are NOT."
    echo
    qemu-aarch64-static -L "${SYSROOT}" "${WORK}/benchmark" "$@"
else
    g++ -std=c++20 -O3 "${INCLUDES[@]}" "${SOURCES[@]}" \
        -o "${WORK}/benchmark" -lm
    "${WORK}/benchmark" "$@"
fi
