#!/bin/sh
# Usage: ./build-shader-cache.sh <folder holding disc1..disc4> [--no-motion-replay]
# Downloads the game's shader pack (checked by SHA-256) and builds the shader
# cache into shader-cache/; copy its cache and shaders folders to the PS5.
set -e
here=$(cd "$(dirname "$0")" && pwd)
[ -n "$1" ] || { echo "Usage: $0 <folder holding disc1..disc4> [--no-motion-replay]"; exit 2; }
game=$1; shift
out=$here/shader-cache
release=https://github.com/freefrank/LostOdysseyRecomp/releases/download/shader-packs
mkdir -p "$out/shaders"

fetch() { # <file on the release> <name in shaders/> <sha256>
    target=$out/shaders/$2
    if [ -f "$target" ] && echo "$3  $target" | sha256sum -c --status; then
        echo "$2 is already there."; return 0
    fi
    echo "Downloading $2 (the game's shaders, from the upstream project) ..."
    curl -fL --retry 3 -o "$target.download" "$release/$1"
    echo "$3  $target.download" | sha256sum -c --status ||
        { rm -f "$target.download"; echo "$2 did not download correctly; run this again."; exit 1; }
    mv "$target.download" "$target"
}
fetch portable_vk-03ef078715ed8c8b.lospv portable_vk.lospv c637b19ee9e05e12055cd5b906d5d7116f3e8a3b69d9b749e7b37285f21cd722
fetch pipelines_corpus-183df2d935260e33.bin pipelines_corpus.bin 183df2d935260e331284fac54c4f630ef1b9bbc4e43dfb1ad89d3d1269bf0c4f

"$here/LoShaderCacheBuilder" "$game" "$out" "$@"
echo
echo "Done. Copy the cache and shaders folders from $out into /data/homebrew/PPSA99212/ on the PS5."
