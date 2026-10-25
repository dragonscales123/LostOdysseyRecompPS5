#!/usr/bin/env bash
# Lost Odyssey Recompiled - link the PS5 title and package its folder.
#
#   link-title.sh CONFIG OUTPUT [OBJECT | ARCHIVE | -o PATH | other link flags]...
#
# The PS5 build (cmake/LoPs5.cmake) makes this CMake's link rule for
# executables, so it receives the runtime's objects and CMake's fully resolved
# static libraries. The link itself is Mihawk's PS5_Vulkan recipe, as
# PS5_VulkanTemplate's ps5/tools/link-title.sh runs it: RADV's release archive,
# the platform layer of the payload SDK (heap, thread and libc wraps), the CRT,
# the AGC import stubs, then the native tool (ELF -> the console's fake SELF)
# and PS5_Vulkan's clean-room libc.prx. CONFIG (written at configure time)
# names those inputs and the title.
#
# The title folder (<dist>/<TITLE_ID>/) gets eboot.bin, sce_sys/ and
# sce_module/libc.prx; OUTPUT is the linked ELF, so CMake sees its target.

set -euo pipefail
[[ -n ${LINK_TRACE:-} ]] && set -x

config=$1 output=$2
shift 2
# shellcheck source=/dev/null
source "$config" # vulkan, sdk, title_id, title_name, dist, param_template, icon, work

objects=() archives=()
while (($#)); do
    case $1 in
        -o) output=$2; shift 2; continue ;;
        *.o | *.obj) objects+=("$1") ;;
        *.a) archives+=("$1") ;;
        # The SDK's stub libraries (every *.so below) stand for the console's
        # modules; libraries named to a desktop linker add nothing here.
        -l* | -pthread | -Wl,* | -rdynamic | -f* | -O* | -m* | --*) ;;
        *) echo "link-title.sh: ignoring link argument '$1'" >&2 ;;
    esac
    shift
done

archive=$vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a
tool=$vulkan/build/host/ps5-native-tool
native=$vulkan/tooling/native
for file in "$archive" "$tool" "$param_template" "$sdk/bin/prospero-lld" "$vulkan/tools/radv-link.sh" \
        "$vulkan/runtime/libc.prx"; do
    [[ -e $file ]] || { echo "link-title.sh: missing $file (see docs/PS5.md)" >&2; exit 2; }
done
mkdir -p "$work/link/obj" "$work/link/stubs"
export PS5_CLANG=${PS5_CLANG:-$(command -v clang-20 || command -v clang || true)}
cc() { PS5_PAYLOAD_SDK="$sdk" sh "$vulkan/tooling/prospero-clang18" "$@"; }

# The CRT; the C++ runtime is libc++abi's (the runtime throws and catches).
cc -std=c++20 -O2 -fno-exceptions -fno-rtti -c "$native/app_crt.cpp" -o "$work/link/obj/app_crt.o"
# RADV calls AGC, which the SDK has no stubs for: these name its imports.
stub() {
    local library=$1 source=$2
    cc -std=c11 -O2 -fPIC -c "$vulkan/$source" -o "$work/link/obj/${library}_stub.o"
    "$sdk/bin/prospero-lld" --shared -soname "${library}.prx" \
        -o "$work/link/stubs/${library}.so" "$work/link/obj/${library}_stub.o"
}
stub libSceAgc vendor/ps5/sdk/stubs/agc_canary_link_stub.c
stub libSceAgcDriver vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c

# shellcheck source=/dev/null
source "$vulkan/tools/radv-link.sh"
radv_link_recipe "$vulkan" "$sdk" "$archive" || exit 2

# libc names the recipe does not bind yet but the runtime calls: libc's
# getcwd asks __getcwd, which only libkernel_sys exports, so it faults in a
# title (std::filesystem::current_path, realpath, canonical). The platform
# layer has working versions; bind them and keep the names local, as the
# recipe keeps its own.
platform_symbols=$("$sdk/bin/llvm-nm" --defined-only "$sdk/target/lib/libps5platform.a" 2>/dev/null || true)
extra_local=()
for name in getcwd realpath; do
    if grep -q " T ps5_$name$" <<<"$platform_symbols" &&
            [[ " ${radv_link_flags[*]} " != *" --defsym=$name=ps5_$name "* ]]; then
        radv_link_flags+=("--defsym=$name=ps5_$name")
        extra_local+=("$name")
    fi
done
# The libc names LostOdysseyRecomp/os/ps5_libc.cpp defines stay local too: a
# console module defines some of them, and a title publishes no exports.
extra_local+=(pathconf ffs tempnam)
# A title must not exit(): it asks the shell to close it
# (LostOdysseyRecomp/os/ps5_title.cpp defines the wrappers).
radv_link_flags+=(--wrap=exit --wrap=_exit --wrap=_Exit)
if ((${#extra_local[@]})); then
    {
        printf '{\n    local:\n'
        printf '        %s;\n' "${extra_local[@]}"
        printf '};\n'
    } > "$work/link/lo-local.map"
    radv_link_flags+=(--version-script "$work/link/lo-local.map")
fi

"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
    --version-script "$native/app-symbols.map" --exclude-libs=ALL \
    -e _start -o "$work/link/llvm-pie.elf" \
    "$work/link/obj/app_crt.o" "${objects[@]}" \
    --start-group "${archives[@]}" --end-group \
    "$work/link/stubs/libSceAgc.so" "$work/link/stubs/libSceAgcDriver.so" \
    "${radv_link_inputs[@]}" \
    --as-needed "$sdk"/target/lib/*.so

# An import that no module a title loads exports is null at run time, and its
# first call jumps to address 0: refuse the title here, not on the console.
null_imports=$(comm -23 \
    <("$sdk/bin/llvm-nm" -D --undefined-only "$work/link/llvm-pie.elf" |
        awk '$1 == "U" { sub(/@.*/, "", $2); print $2 }' | sort -u) \
    <(for library in "$sdk"/target/lib/*.so "$work/link/stubs/libSceAgc.so" "$work/link/stubs/libSceAgcDriver.so"; do
        case ${library##*/} in libkernel_sys.so | libScePosixForWebKit.so) continue ;; esac
        "$sdk/bin/llvm-nm" -D --defined-only "$library" 2>/dev/null | awk '{ print $NF }'
    done | sort -u))
if [[ -n $null_imports ]]; then
    echo "link-title.sh: imports that no module a title loads exports (null at run time): ${null_imports//$'\n'/ }" >&2
    if [[ -z ${LO_PS5_ALLOW_NULL_IMPORTS:-} ]]; then
        echo "link-title.sh: bind them to the platform layer, or set LO_PS5_ALLOW_NULL_IMPORTS=1 to accept them" >&2
        exit 1
    fi
fi

"$tool" link --in "$work/link/llvm-pie.elf" --out "$work/eboot.elf.new" \
    --stub-dir "$sdk/target/lib" --stub "$work/link/stubs/libSceAgc.so" \
    --stub "$work/link/stubs/libSceAgcDriver.so" --module-sdk 0x02000009 \
    --companion-sdk 0x08050001 --file-name eboot.elf

# The title folder.
app="$dist/$title_id"
mkdir -p "$app/sce_sys" "$app/sce_module"
"$tool" self --sign --in "$work/eboot.elf.new" --out "$app/eboot.bin" --magic 0x1D3D154F
"$tool" self --inspect --file "$app/eboot.bin" > /dev/null
python3 - "$param_template" "$app/sce_sys/param.json" "$title_id" "$title_name" <<'PY'
import json, sys
template, out, title_id, name = sys.argv[1:]
param = json.load(open(template))
param["titleId"] = title_id
param["conceptId"] = title_id[4:]
param["contentId"] = f"UP9000-{title_id}_00-LOSTODYSSEYRECMP"
param["localizedParameters"]["en-US"]["titleName"] = name
json.dump(param, open(out, "w"), indent=2)
PY
cp "$icon" "$app/sce_sys/icon0.png"
(cd "$vulkan/runtime" && sha256sum --check --strict --quiet libc.prx.sha256)
cp "$vulkan/runtime/libc.prx" "$app/sce_module/libc.prx"
mv "$work/eboot.elf.new" "$output"
radv_revision=$(sed -n 's/^revision: //p' "$(dirname "$(dirname "$archive")")/PROVENANCE.txt" 2>/dev/null || true)
printf '==> %s: %s (eboot.bin %s bytes; RADV %s)\n' "$title_id" "$app" \
    "$(stat -c %s "$app/eboot.bin")" "${radv_revision:0:11}"
