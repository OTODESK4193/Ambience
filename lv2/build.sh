#!/bin/bash
# Cross-compile the Ambience LV2 bundle for the LoopPad_Jack MOD device and
# package it as a tar for /data/mod/lv2.
#
#   lv2/build.sh                       # build + package
#   lv2/build.sh --deploy root@HOST    # build, package, install over ssh
#   lv2/build.sh --no-regen            # skip the TTL/ports.h generator
#
# The toolchain is the SAME aarch64 gcc that built the device rootfs, so the
# result is ABI-identical to the image by construction. Point LOOPPAD_ROOT at
# the checkout if it is not in the default place:
#
#   LOOPPAD_ROOT=/path/to/LoopPad_Jack lv2/build.sh
#
# NOTHING in the LoopPad_Jack tree is written to - it is read for the compiler,
# the sysroot and the LV2 headers only.
#
# +---------------------------------------------------------------------------+
# | WHAT THE SANITY GATE IS FOR                                               |
# +---------------------------------------------------------------------------+
# A host-arch .so and a missing modgui are both completely silent at build
# time. The first shows up on the device as "plugin does not load", the second
# as a wall of generic sliders instead of a pedal. Both are cheap to check here
# and expensive to diagnose there, so the build FAILS on either rather than
# shipping. Same reasoning, same check as LoopPad_Jack's own
# plugins/build-plugins.sh.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "${HERE}/.." && pwd)"

LOOPPAD_ROOT="${LOOPPAD_ROOT:-${HOME}/Sources/LoopPad_Jack}"
OUT="${LOOPPAD_ROOT}/Buildroot/build/output"
SYSROOT="${OUT}/staging"
CXX="${OUT}/host/bin/aarch64-buildroot-linux-gnu-g++"
FILE_TOOL="${OUT}/host/bin/aarch64-buildroot-linux-gnu-readelf"

BUNDLE_NAME="AmbienceReverb.lv2"
SO_NAME="AmbienceReverb.so"
VERSION="1.1.0"

WORK="${HERE}/build"
STAGE="${WORK}/stage"
BUNDLE="${STAGE}/lv2/${BUNDLE_NAME}"
TARBALL="${WORK}/ambience-reverb-${VERSION}-aarch64.tar.gz"

DEVICE_LV2_DIR="/data/mod/lv2"

die() { echo "ERROR: $*" >&2; exit 1; }

# --- arguments -------------------------------------------------------------
DEPLOY_HOST=""
REGEN=1
while [ $# -gt 0 ]; do
    case "$1" in
        --deploy)   [ $# -ge 2 ] || die "--deploy needs a user@host"
                    DEPLOY_HOST="$2"; shift 2 ;;
        --no-regen) REGEN=0; shift ;;
        -h|--help)  sed -n '2,18p' "$0" | sed 's/^# \?//'; exit 0 ;;
        *)          die "unknown argument: $1" ;;
    esac
done

# --- preflight -------------------------------------------------------------
[ -x "${CXX}" ]     || die "cross g++ not found: ${CXX} (build the rootfs first)"
[ -d "${SYSROOT}" ] || die "staging sysroot missing: ${SYSROOT} (build the rootfs first)"
[ -f "${SYSROOT}/usr/include/lv2/core/lv2.h" ] \
    || die "LV2 headers missing from ${SYSROOT}/usr/include/lv2"
command -v tar >/dev/null || die "missing host tool: tar"

# --- regenerate the derived files ------------------------------------------
# ports.h, the TTLs, the presets and the modgui's algorithm table all come out
# of lv2/tools/. Running it every build is what keeps the C++ port indices and
# the metadata mod-ui reads from ever disagreeing.
if [ "${REGEN}" -eq 1 ]; then
    command -v python3 >/dev/null || die "missing host tool: python3 (or pass --no-regen)"
    echo "Regenerating bundle metadata:"
    python3 "${HERE}/tools/gen_bundle.py"
    echo
fi

# --- stage -----------------------------------------------------------------
rm -rf "${STAGE}"
mkdir -p "${BUNDLE}"

cp "${HERE}/bundle/manifest.ttl" \
   "${HERE}/bundle/AmbienceReverb.ttl" \
   "${HERE}/bundle/modgui.ttl" \
   "${HERE}/bundle/presets.ttl" \
   "${HERE}/bundle/default-preset.ttl" \
   "${BUNDLE}/"

cp -r "${HERE}/bundle/modgui" "${BUNDLE}/modgui"
# javascript.js.in is the template the generator consumes; the device only
# needs the generated javascript.js.
rm -f "${BUNDLE}/modgui/javascript.js.in"

# --- compile ---------------------------------------------------------------
# Source/DSP/*.cpp are compiled UNMODIFIED. lv2/src/JuceHeader.h stands in for
# the real JUCE umbrella header and lv2/src/JuceCompat.h supplies the handful
# of juce:: helpers the DSP uses - see that file for the full list.
#
# EarlyReflections.cpp and SAPFStage.cpp are deliberately absent: they are not
# in the VST3's CMakeLists.txt either and nothing includes them.
#
# +---------------------------------------------------------------------------+
# | WHY THE DSP AND THE WRAPPER ARE COMPILED WITH DIFFERENT FLAGS             |
# +---------------------------------------------------------------------------+
# The DSP gets -ffast-math. Measured on the device (a Pi 4B at 1.5 GHz, JACK at
# 64 frames), that is worth about 25%: 356 -> 265 us per block. Note this is
# aarch64-specific - the same flag makes the same code SLOWER on x86-64, so do
# not "verify" it on the host and conclude anything.
#
# The wrapper does NOT get it, and must not. -ffast-math implies
# -ffinite-math-only, which lets the compiler assume NaN never happens and
# silently deletes the `v == v` check in Ambience::ctl(). That check is what
# stops a NaN arriving on a control port from permanently poisoning the FDN
# state. Verified: with -ffast-math the guard compiles to a constant `false`.
#
# The split is safe because the DSP sources contain no NaN/Inf tests of their
# own - the only one in the whole plugin is in the wrapper.
#
# lv2/tools/smoke_test.sh covers this: it feeds NaN and out-of-range values to
# every control port, so if the guard is ever compiled away the test fails.
echo "Building ${BUNDLE_NAME} for aarch64:"
mkdir -p "${WORK}"
LOG="${WORK}/build.log"
OBJDIR="${WORK}/obj"
rm -rf "${OBJDIR}"; mkdir -p "${OBJDIR}"

COMMON=(--sysroot="${SYSROOT}" -std=c++20 -O3 -mcpu=cortex-a72
        -fPIC -fvisibility=hidden
        -DAMBIENCE_VERSION='"'"${VERSION}"'"'
        -I"${REPO}/Source" -I"${REPO}/Source/DSP" -I"${HERE}/src")

# Hot path: the reverb engine and its filter design code.
DSP_SOURCES=(
    "${REPO}/Source/DSP/UniversalEngine.cpp"
    "${REPO}/Source/DSP/BiquadFilters.cpp"
    "${REPO}/Source/DSP/MagnitudeResponseFitter.cpp"
    "${REPO}/Source/DSP/AcousticMetrics.cpp"
)

OBJECTS=()
compile() {
    local src="$1"; shift
    local obj="${OBJDIR}/$(basename "${src}" .cpp).o"
    if ! "${CXX}" "${COMMON[@]}" "$@" -c "${src}" -o "${obj}" >>"${LOG}" 2>&1; then
        cat "${LOG}" >&2
        die "compile failed: $(basename "${src}") (see ${LOG})"
    fi
    OBJECTS+=("${obj}")
}

for src in "${DSP_SOURCES[@]}"; do
    compile "${src}" -ffast-math
done
# Wrapper: plain -O3, so the NaN guard survives.
compile "${HERE}/src/ambience_lv2.cpp" -fno-math-errno -fno-trapping-math

# -lpthread for the LED thread: painting an HMI LED takes a mutex inside
# mod-host and writes a shared-memory ring, so it cannot run on the audio
# thread. --no-undefined because a shared object links happily with undefined
# symbols and only fails at dlopen, on the device.
if ! "${CXX}" --sysroot="${SYSROOT}" -shared -Wl,--no-undefined "${OBJECTS[@]}" \
        -o "${BUNDLE}/${SO_NAME}" -lm -lpthread >>"${LOG}" 2>&1; then
    cat "${LOG}" >&2
    die "link failed (see ${LOG})"
fi
echo "  ${SO_NAME}  $(du -h "${BUNDLE}/${SO_NAME}" | cut -f1)  (DSP -ffast-math, wrapper -O3)"

# --- sanity gate -----------------------------------------------------------
echo
echo "Checking the bundle:"

arch="NOT-aarch64"
if command -v file >/dev/null; then
    file -b "${BUNDLE}/${SO_NAME}" | grep -q 'ARM aarch64' && arch="ARM aarch64"
elif [ -x "${FILE_TOOL}" ]; then
    "${FILE_TOOL}" -h "${BUNDLE}/${SO_NAME}" | grep -q 'AArch64' && arch="ARM aarch64"
fi

if [ -d "${BUNDLE}/modgui" ] && [ -f "${BUNDLE}/modgui.ttl" ]; then gui=yes; else gui=NO; fi

presets="$(grep -c 'a pset:Preset' "${BUNDLE}/manifest.ttl" || true)"
uri="$(grep -v '^@prefix' "${BUNDLE}/manifest.ttl" \
       | grep -o '<https\?://[^>]*>' | head -1 | tr -d '<>')"

printf '  %-14s %s\n' arch    "${arch}"
printf '  %-14s %s\n' modgui  "${gui}"
printf '  %-14s %s\n' presets "${presets}"
printf '  %-14s %s\n' uri     "${uri}"

[ "${arch}" = "ARM aarch64" ] || die "the .so is not aarch64 - it will not load on the device"
[ "${gui}" = "yes" ]          || die "no modgui - mod-ui would render generic sliders"
[ "${presets}" -ge 1 ]        || die "manifest.ttl declares no presets"
[ -n "${uri}" ]               || die "could not read the plugin URI out of manifest.ttl"

# Deeper RDF cross-checks: modgui port indices against the real ports, and
# every preset complete and reachable. Skips itself if rdflib is missing.
if command -v python3 >/dev/null; then
    echo
    python3 "${HERE}/tools/validate_bundle.py" "${BUNDLE}" || die "bundle validation failed"
fi

# --- package ---------------------------------------------------------------
# Members are bundle-relative so the tar drops straight into place with
#     tar xzf ambience-reverb-*.tar.gz -C /data/mod/lv2
# which is the same shape as LoopPad_Jack's own deploy step.
tar czf "${TARBALL}" -C "${STAGE}/lv2" "${BUNDLE_NAME}"
echo
echo "Packaged ${TARBALL}  ($(du -h "${TARBALL}" | cut -f1))"

# --- deploy ----------------------------------------------------------------
# Untar over the top; never rm -rf the target. mod-ui writes USER presets into
# /data/mod/lv2 as new bundles, so wiping it would destroy them.
if [ -n "${DEPLOY_HOST}" ]; then
    echo
    echo "Deploying to ${DEPLOY_HOST}:${DEVICE_LV2_DIR} ..."
    ssh -n "${DEPLOY_HOST}" "mkdir -p ${DEVICE_LV2_DIR}"
    tar cf - -C "${STAGE}/lv2" "${BUNDLE_NAME}" \
        | ssh "${DEPLOY_HOST}" "tar xf - -C ${DEVICE_LV2_DIR}"
    echo "  installed ${BUNDLE_NAME}"
fi

# Restart mod-host BEFORE mod-ui: S66modui refuses to start while mod-host is
# down, and each builds its lilv world once at process start, so neither picks
# up a new bundle without a restart.
cat <<EOF

Install on a device:

    scp ${TARBALL} root@HOST:/tmp/
    ssh root@HOST 'mkdir -p ${DEVICE_LV2_DIR} \\
        && tar xzf /tmp/$(basename "${TARBALL}") -C ${DEVICE_LV2_DIR} \\
        && /etc/init.d/S65modhost restart && /etc/init.d/S66modui restart'
EOF
