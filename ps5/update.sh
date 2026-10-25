#!/usr/bin/env bash
# Lost Odyssey Recompiled - bring the PS5 port up to date with upstream and
# rebuild the title (docs/PS5.md, "Updating").
#
#   ps5/update.sh [--no-fetch] [--no-build] [--continue]
#
# The port lives on a local branch, ps5-port, as commits on top of upstream's
# main. Each run:
#   1. records any uncommitted PS5 work as a commit on ps5-port (first run:
#      creates the branch from the current checkout);
#   2. fetches upstream and rebases ps5-port onto origin/main; on a conflict it
#      stops and says which files to merge, then `ps5/update.sh --continue`;
#   3. resets the patched submodules to upstream's pins and applies the
#      project's patches plus tools/patches/sdl-ps5.patch;
#   4. regenerates the game code from your Disc 1 default.xex only when the
#      recompiler, its patch or its configuration changed;
#   5. fetches the portable Vulkan shader pack (and pipeline corpus) matching
#      the new runtime from the project's shader-packs release, checking size,
#      SHA-256 and the runtime contract;
#   6. builds the title with ps5/build.sh and puts the pack beside it.
#
# Afterwards, copy eboot.bin (and shaders/ when it changed) to the console.
# If upstream changed one of the runtime's own built-in shaders, the console
# saves it to cache/shaders/requests/ on the next launch; compile those with
# LoShaderPackTool compile-requests (docs/PS5.md, "Built-in shaders").
#
# Needs: git, cmake, ninja, python3 (3.11+), curl, clang/clang++ and ld.lld 20,
# a PS5 payload SDK and PS5_Vulkan with its RADV release archive (the same
# environment ps5/build.sh needs; PS5_PAYLOAD_SDK and PS5_VULKAN_DIR override
# where they are looked for).
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
fetch=true build=true resume=false
for argument in "$@"; do
    case $argument in
        --no-fetch) fetch=false ;;
        --no-build) build=false ;;
        --continue) resume=true ;;
        *) echo "usage: ${0##*/} [--no-fetch] [--no-build] [--continue]" >&2; exit 2 ;;
    esac
done

branch=ps5-port
upstream=origin/main
step() { printf '\n==> %s\n' "$*"; }
fail() { printf '\nupdate.sh: %s\n' "$*" >&2; exit 1; }
git_commit() { git -c user.name="${GIT_AUTHOR_NAME:-PS5 port}" -c user.email="${GIT_AUTHOR_EMAIL:-ps5-port@localhost}" commit "$@"; }

for tool in git cmake ninja python3 curl; do
    command -v "$tool" > /dev/null || fail "$tool is missing"
done
[[ -f LostOdysseyRecompLib/private/disc1/default.xex ]] ||
    fail "put your Disc 1 default.xex in LostOdysseyRecompLib/private/disc1/ first"

# ---------------------------------------------------------------- 1. the branch
if $resume; then
    step "continuing the rebase"
    if [[ -d .git/rebase-merge || -d .git/rebase-apply ]]; then
        git -c core.editor=true rebase --continue ||
            fail "the rebase still has conflicts: merge them, git add the files, and run ps5/update.sh --continue"
    fi
else
    step "recording the PS5 port on branch $branch"
    current=$(git rev-parse --abbrev-ref HEAD)
    if [[ $current != "$branch" ]]; then
        git rev-parse --verify -q "$branch" > /dev/null &&
            fail "branch $branch exists but $current is checked out; switch to $branch first"
        git checkout -q -b "$branch"
    fi
    # Submodule pointers are upstream's; their patches are files in tools/patches.
    git add -A -- . ':(exclude)thirdparty/SDL' ':(exclude)thirdparty/plume' ':(exclude)tools/XenonRecomp' \
        ':(exclude)tools/XenosRecomp'
    if ! git diff --cached --quiet; then
        git_commit -q -m "PS5 port: local changes ($(date -u +%Y-%m-%d))"
        echo "committed local PS5 changes"
    fi

    # ------------------------------------------------------- 2. rebase onto upstream
    if $fetch; then
        step "fetching upstream"
        git fetch -q origin main
    fi
    before=$(git merge-base HEAD "$upstream")
    if [[ $(git rev-parse "$upstream") == "$before" ]]; then
        echo "already on the latest upstream ($(git rev-parse --short "$upstream"))"
    else
        step "rebasing $branch onto $upstream ($(git rev-list --count "$before..$upstream") new upstream commits)"
        if ! git rebase -q "$upstream"; then
            echo
            echo "These files changed both upstream and in the PS5 port:"
            git diff --name-only --diff-filter=U | sed 's/^/  /'
            fail "merge them (keep upstream's change and the PS5 lines), git add them, then run ps5/update.sh --continue"
        fi
    fi
fi

# --------------------------------------------------------------- 3. submodules
step "submodules at upstream's pins, with the project's patches"
git submodule update --init --depth 1 tools/XenonRecomp tools/XenosRecomp thirdparty/plume thirdparty/SDL \
    thirdparty/o1heap thirdparty/unordered_dense thirdparty/SPIRV-Cross > /dev/null
git -C tools/XenonRecomp submodule update --init --depth 1 --recursive > /dev/null
git -C tools/XenosRecomp submodule update --init --depth 1 thirdparty/fmt thirdparty/smol-v thirdparty/xxHash \
    thirdparty/zstd thirdparty/dxc-bin > /dev/null
git -C thirdparty/plume submodule update --init --depth 1 contrib/volk contrib/Vulkan-Headers \
    contrib/VulkanMemoryAllocator > /dev/null
patch_submodule() {
    local submodule=$1; shift
    git -C "$submodule" reset -q --hard
    git -C "$submodule" clean -qfd
    local patch
    for patch in "$@"; do
        git -C "$submodule" apply --check "$root/tools/patches/$patch" 2> /dev/null ||
            fail "tools/patches/$patch no longer applies to $submodule (upstream moved it); see docs/PS5.md"
        git -C "$submodule" apply "$root/tools/patches/$patch"
        echo "  $submodule: $patch"
    done
}
patch_submodule tools/XenonRecomp XenonRecomp-lostodyssey.patch
patch_submodule thirdparty/plume plume-lostodyssey.patch plume-sdl3.patch plume-android.patch
patch_submodule thirdparty/SDL sdl-ps5.patch

# ------------------------------------------------------------ 4. the game code
stamp_file=LostOdysseyRecompLib/ppc/.ps5-codegen-stamp
stamp=$( {
    git -C tools/XenonRecomp rev-parse HEAD
    cat tools/patches/XenonRecomp-lostodyssey.patch tools/ppc_codegen.py LostOdysseyRecompLib/config/*
    sha256sum LostOdysseyRecompLib/private/disc1/default.xex
} | sha256sum | cut -d' ' -f1)
if [[ -f $stamp_file && $(cat "$stamp_file") == "$stamp" ]] &&
        compgen -G "LostOdysseyRecompLib/ppc/ppc_recomp.*.cpp" > /dev/null; then
    step "game code: unchanged (recompiler and configuration are the same)"
else
    step "game code: regenerating from your default.xex"
    cc=$(command -v clang-20 || command -v clang)
    cxx=$(command -v clang++-20 || command -v clang++)
    cmake -S tools/XenonRecomp -B out/build/tools -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" > out-tools-configure.log 2>&1 ||
        fail "configuring XenonRecomp failed (out-tools-configure.log)"
    ninja -C out/build/tools XenonRecomp > /dev/null
    rm -f out-tools-configure.log
    python3 -B tools/ppc_codegen.py generate --executable out/build/tools/XenonRecomp/XenonRecomp |
        grep -v "Recompiling functions" || true
    printf '%s\n' "$stamp" > "$stamp_file"
fi
if [[ ! -f LostOdysseyRecompLib/private/image_disc1.bin ||
        LostOdysseyRecompLib/private/disc1/default.xex -nt LostOdysseyRecompLib/private/image_disc1.bin ]]; then
    step "decrypted image for the import stubs and the shader contract"
    cmake -S tools/xexdump -B out/build/xexdump -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER="$(command -v clang-20 || command -v clang)" \
        -DCMAKE_CXX_COMPILER="$(command -v clang++-20 || command -v clang++)" > /dev/null
    ninja -C out/build/xexdump xexdump > /dev/null
    "$(find out/build/xexdump -name xexdump -type f | head -1)" LostOdysseyRecompLib/private/disc1/default.xex \
        LostOdysseyRecompLib/private/image_disc1.bin | tail -1
fi

# --------------------------------------------------------- 5. the shader pack
step "shader pack for this runtime"
cmake -S tools/shader_pack -B out/build/packtool -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$(command -v clang-20 || command -v clang)" \
    -DCMAKE_CXX_COMPILER="$(command -v clang++-20 || command -v clang++)" > /dev/null
ninja -C out/build/packtool LoShaderPackTool > /dev/null
packtool=out/build/packtool/LoShaderPackTool
contract=$("$packtool" contract LostOdysseyRecompLib/private/image_disc1.bin |
    python3 -c 'import json,sys; print(json.load(sys.stdin)["vulkan"])')
packs=out/ps5-shaders
mkdir -p "$packs"
if [[ -f $packs/portable_vk.lospv ]] &&
        "$packtool" verify-runtime "$packs/portable_vk.lospv" LostOdysseyRecompLib/private/image_disc1.bin > /dev/null 2>&1; then
    echo "the pack you have matches (contract ${contract:0:16})"
    pack_changed=false
else
    pack_changed=true
    release=https://github.com/freefrank/LostOdysseyRecomp/releases/download/shader-packs
    curl -fsSL "$release/index.json" -o "$packs/index.json" || fail "could not read the shader-packs index"
    file= size= sha=
    read -r file size sha < <(python3 - "$packs/index.json" "$contract" <<'PY'
import json, sys
index, contract = json.load(open(sys.argv[1])), sys.argv[2]
for p in index["packs"]:
    if p["renderer"] == "vulkan" and p["contract"] == contract:
        print(p["file"], p["size"], p["sha256"]); break
PY
) || true
    [[ -n ${file:-} ]] || fail "no Vulkan shader pack is published yet for contract ${contract:0:16}; wait for the project to publish it (the old build keeps working)"
    echo "downloading $file ($((size / 1048576)) MiB)"
    curl -fsSL "$release/$file" -o "$packs/portable_vk.lospv.download"
    [[ $(stat -c %s "$packs/portable_vk.lospv.download") == "$size" ]] || fail "$file: wrong size"
    [[ $(sha256sum "$packs/portable_vk.lospv.download" | cut -d' ' -f1) == "$sha" ]] || fail "$file: wrong SHA-256"
    mv "$packs/portable_vk.lospv.download" "$packs/portable_vk.lospv"
    "$packtool" verify-runtime "$packs/portable_vk.lospv" LostOdysseyRecompLib/private/image_disc1.bin > /dev/null ||
        fail "$file does not verify against this runtime"
    pack_changed=true
fi
# The newest pipeline recipe corpus (optional: prebuilds pipelines while a scene loads).
if [[ ! -f $packs/index.json ]]; then
    curl -fsSL https://github.com/freefrank/LostOdysseyRecomp/releases/download/shader-packs/index.json \
        -o "$packs/index.json" || true
fi
if [[ -f $packs/index.json ]]; then
    corpus_file= corpus_sha=
    read -r corpus_file corpus_sha < <(python3 - "$packs/index.json" <<'PY'
import json, sys
corpora = [p for p in json.load(open(sys.argv[1]))["packs"] if p["renderer"] == "pipeline-corpus"]
if corpora: print(corpora[-1]["file"], corpora[-1]["sha256"])
PY
) || true
    if [[ -n ${corpus_file:-} ]] && ! { [[ -f $packs/pipelines_corpus.bin ]] &&
            [[ $(sha256sum "$packs/pipelines_corpus.bin" | cut -d' ' -f1) == "$corpus_sha" ]]; }; then
        curl -fsSL "https://github.com/freefrank/LostOdysseyRecomp/releases/download/shader-packs/$corpus_file" \
            -o "$packs/pipelines_corpus.bin" && echo "pipeline corpus: $corpus_file"
    fi
fi

# ---------------------------------------------------------------- 6. the title
if $build; then
    step "building the title"
    ps5/build.sh
    title_id=$(sed -n "s/^title_id='\(.*\)'$/\1/p" out/build/ps5/ps5-link.conf)
    dist=out/build/ps5/dist/$title_id
    mkdir -p "$dist/shaders"
    cp "$packs/portable_vk.lospv" "$dist/shaders/"
    [[ -f $packs/pipelines_corpus.bin ]] && cp "$packs/pipelines_corpus.bin" "$dist/shaders/"
    step "done: $dist"
    echo "copy to /data/homebrew/$title_id/ on the console:"
    echo "  eboot.bin"
    $pack_changed && echo "  shaders/portable_vk.lospv (new pack for this runtime)"
    echo "then launch once; if cache/shaders/requests/ gets new files, compile them (docs/PS5.md, \"Built-in shaders\")"
fi
echo "upstream: $(git rev-parse --short "$upstream")  port: $(git rev-list --count "$upstream..HEAD") commit(s) on $branch"
