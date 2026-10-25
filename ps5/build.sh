#!/usr/bin/env bash
# Lost Odyssey Recompiled - build the PS5 homebrew title (docs/PS5.md).
#
#   ps5/build.sh [--sdk DIR] [--vulkan DIR] [--title-id PPSAxxxxx]
#
# Needs: the generated game code (python3 tools/ppc_codegen.py generate, from
# your own Disc 1 default.xex), the dependency patches applied (docs/PS5.md),
# a PS5 payload SDK and PS5_Vulkan with its RADV release archive built, and
# clang/lld 20 on the host. Writes out/build/ps5/dist/<TITLE_ID>/.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
parent=$(dirname "$root")
sdk=${PS5_PAYLOAD_SDK:-$parent/PS5_VulkanTemplate/.deps/native/ps5-payload-sdk}
vulkan=${PS5_VULKAN_DIR:-$parent/PS5_Vulkan}
title_id=PPSA99212
while (($#)); do
    case $1 in
        --sdk) sdk=$2; shift 2 ;;
        --vulkan) vulkan=$2; shift 2 ;;
        --title-id) title_id=$2; shift 2 ;;
        *) echo "usage: ${0##*/} [--sdk DIR] [--vulkan DIR] [--title-id PPSAxxxxx]" >&2; exit 2 ;;
    esac
done
[[ -f $sdk/toolchain/prospero.cmake ]] || { echo "no PS5 payload SDK at $sdk" >&2; exit 2; }
[[ -f $vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a ]] ||
    { echo "no RADV release archive in $vulkan (tools/build-radv.sh release)" >&2; exit 2; }
compgen -G "$root/LostOdysseyRecompLib/ppc/ppc_recomp.*.cpp" > /dev/null ||
    { echo "no generated game code: python3 tools/ppc_codegen.py generate" >&2; exit 2; }

build=$root/out/build/ps5
cmake -S "$root" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$sdk/toolchain/prospero.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG" -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG" \
    -DCMAKE_VERBOSE_MAKEFILE=OFF \
    -DLO_PS5_VULKAN_DIR="$vulkan" -DLO_PS5_TITLE_ID="$title_id"
cmake --build "$build" --target LostOdysseyRecomp
echo "title folder: $build/dist/$title_id"
echo "add shaders/portable_vk.lospv (and pipelines_corpus.bin) and game/disc1/ before copying it to /data/homebrew/"
