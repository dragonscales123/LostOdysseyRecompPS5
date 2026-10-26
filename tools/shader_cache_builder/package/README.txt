Lost Odyssey PS5 - shader cache builder
=======================================

The PS5 port has no shader compiler on the console, so its shaders are made
on your PC. This package does both parts in one run:

- shaders/portable_vk.lospv: the game's own shaders, translated for Vulkan.
  Downloaded from the upstream LostOdysseyRecomp project (the exact file this
  PS5 build was made for, checked by SHA-256), with pipelines_corpus.bin.
- cache/shaders/builtin_v28_spirv.lostore: the port's own shaders and the
  ones it derives from the game's, compiled from YOUR OWN game discs.

This package contains no game data. The tool reads your discs on your PC,
keeps the shaders it extracts in a temporary folder, and deletes them when
it finishes. Nothing is uploaded.

What you need
-------------
- Your extracted Lost Odyssey discs (USA/Europe edition): one folder holding
  disc1, disc2, disc3 and disc4, each with default.xex, LO.fpi and the
  xenon_*.fpd files. disc1 alone also works but covers less.
- Windows 10/11 x64, or Linux x86-64 (glibc 2.38 or newer, e.g. Ubuntu 24.04).
- About 4 GB of free space and 30-90 minutes (it uses every CPU core).
- An internet connection for the 225 MB shader pack download (Windows 10
  1803 or later has the curl it uses; on Linux, curl and sha256sum).

Windows
-------
1. Unzip this package somewhere with a plain (English-letter) path, for
   example C:\LOShaderCache.
2. Open the windows folder and double-click "Build PS5 shader cache.bat"
   (or drop your game folder onto it).
3. Drag the folder holding disc1..disc4 into the window and press Enter.
4. Answer Y to include TAA's motion-replay shaders (about 1.9 GB) or N for
   a smaller file (about 600 MB) if you never turn on TAA.
5. When it says Done, copy the "cache" and "shaders" folders from
   windows\shader-cache into /data/homebrew/PPSA99212/ on the PS5 (FTP),
   replacing the old files.

Linux
-----
  cd linux
  ./build-shader-cache.sh /path/to/folder-with-disc1..disc4 [--no-motion-replay]
Then copy linux/shader-cache/cache and linux/shader-cache/shaders to
/data/homebrew/PPSA99212/.

Notes
-----
- Running it again reuses what it already compiled, and skips the download
  when the shader pack is already there and correct.
- The cache matches one version of the runtime's shader sources. If a newer
  PS5 build changes them, use the builder that comes with that build.
- If cache/shaders/requests/ on the PS5 ever gets new .hlsl files, the cache
  is missing something: report it with the files.

Licenses
--------
The builder is part of the Lost Odyssey PS5 port of LostOdysseyRecomp and is
licensed under the GNU GPL v3 (licenses/); its source is published with the
port. It includes DirectXShaderCompiler (dxcompiler.dll / libdxcompiler.so,
LLVM release license), XenonRecomp's XEX loader (MIT), libmspack (LGPL 2.1),
tiny-AES-c (Unlicense), zstd (BSD), fmt (MIT) and the SMAA shader (MIT).
