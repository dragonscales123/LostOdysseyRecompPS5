# PlayStation 5 (homebrew)

A native port, now in beta, for a PS5 that runs homebrew titles. It renders
through Mihawk's [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) (Mesa's
RADV built for the console and linked into the title) and builds with the
[PS5 payload SDK](https://github.com/ps5-payload-dev/sdk) as forked by
[PS5_PayloadSDK](https://github.com/mihawk-99/PS5_PayloadSDK). No game data,
Sony code or console firmware is involved; supply your own disc files.

Status (beta, build 42, 2026-10-29, one console, firmware 13.60, USA/Europe
discs): boots to the title menu, plays movies, field and battles render fully,
saves and overwrites saves, changes from disc 1 to disc 2. 120 FPS at
2560x1440 on a 120 Hz display (60 FPS on 60 Hz), HDR10 output with
calibration. Multi-hour sessions on discs 1 and 2 with many area loads,
battles and cutscenes ran without a crash or freeze, with the title heap
steady at about 0.5-0.7 GiB. Known: a few seconds at about 70-80 FPS at the
start of a battle (CPU-bound: the game's main thread waits on locks its
loading threads hold; see `LO_CS_SPIN` below). Discs 3 and 4 are untested.

## What the port changes

| Part | PS5 implementation |
| --- | --- |
| Build | `cmake/LoPs5.cmake`: Zen 2, no module scanning, no vendor upscalers; every executable is linked by `ps5/link-title.sh`, which runs PS5_Vulkan's link recipe (RADV, the SDK platform layer, the CRT) and writes the title folder (`eboot.bin`, `sce_sys/`, `sce_module/libc.prx`) |
| Window, input, audio | SDL3 with PS5 backends (`tools/patches/sdl-ps5.patch`): a Vulkan `VK_KHR_display` surface on the console's display, controllers through `scePad` (one per signed-in user, DualSense glyphs), `sceAudioOut` at 48 kHz stereo, paths under `/app0/` |
| Vulkan | plume through volk, initialised from RADV's `vk_icdGetInstanceProcAddr` (`os/ps5_title.cpp`) |
| Guest memory | `kernel/guest_address_space_ps5.cpp`: a 4 GiB reservation at 0x10_0000_0000 in direct memory, the 512 MiB physical block mapped at A and C; the E view stays unmapped as on macOS (16 KiB pages) |
| Shaders | the published portable Vulkan pack in `shaders/` for the game's shaders; the runtime's own built-in shaders from a cache compiled on a PC (below); there is no DXC on the console |
| Occlusion | `LO_ZPD_MODE=fake` by default: the console's RADV counts occlusion coarsely and its results read as zero, which made the game cull battle scenes |
| Display | `sce_sys/param.json` declares HDR (`attribute` 0x20000000) and high frame rate and video-out information (`attribute3` 0x44), so the driver offers 119.88 Hz where the TV takes it; the settings menu offers 90/120 FPS only then. HDR10: `ps5/patches/radv-videoout-hdr.patch` (PS5_Mesa `wsi_common_videoout.c`) offers A2B10G10R10/A2R10G10B10 in `HDR10_ST2084` while `sceVideoOutGetOutputStatus` reports HDR (byte 4 = 2, byte 8 bit 0), and registers those swapchains with the PQ formats 0x8100070422000000 / 0x8100070400000000. A console set to HDR "On when supported" keeps its output in HDR10, so a PQ buffer shows as HDR with no mode change. `LO_PS5_VIDEO_PROBE=1` (`os/ps5_video_probe.cpp`) shows SDR and PQ test patterns; `=2` searches the format codes (0x80.. 8-bit, 0x81.. 10-bit) |
| Menu | rows the console cannot use are hidden and their saved values reset (`settings/menu.cpp` `GraphicsRowHidden`, `SystemRowHidden`; `settings/config.cpp` `Validate`) |
| Settings | no environment on the console: `LO_*` switches go in `/app0/ps5-env.txt`, one `KEY=VALUE` per line |
| Diagnostics | `/app0/boot.txt` records each startup step and any crash (signal, `rip`, stack, memory figures). The runtime log gets the title heap and free direct memory every 30 s, and at once when C++ blocks of 256 KiB or more change by 256 MiB, with the code holding the most (`os/ps5_alloc_trace.cpp`; `anchor` gives the load bias) |
| Clears | EDRAM clears queue per target until its next use (`gpu/pending_clear.h`); a rectangle already queued with the same value is skipped and a queue of 256 rectangles runs at once. A target cleared every frame of a cutscene and never drawn used to queue one rectangle per frame, until the replay ran the title out of memory or stalled the GPU |
| Audio | a frame the XMA decoder rejects yields a frame of silence, and parsing resumes at the next packet (`apu/xma.cpp`). A voice that produced nothing never drained, and the guest's audio thread waited on it while holding a lock the main thread needs: 20-80 s freezes on some disc 2 area changes |
| Disc changes | a read-only disc file opened before `XamSwapDisc` reads the same file on the new disc on its next use (`kernel/io/file_system.cpp` `FollowDiscChange`); the guest keeps some archives open across the change |
| Saves | a title may not call `lstat` (EPERM) or rely on the SDK's `realpath`, so replacing a save slot checks for links with `readlink` and paths lexically (`kernel/xam.cpp`, `FileSystem::IsSymlink`/`RemoveTree`) |
| Files | the title folder `/app0` (= `/data/homebrew/<TITLE_ID>/`) is the portable install root: settings, saves, logs, caches, `game/` |
| Network | none yet (`updater/ps5_http.cpp`): no update check, no pack download, no log upload |
| Exit | the shell closes the title (`sceSystemServiceLoadExec("exit")`); `exit`, `_exit` and `_Exit` are wrapped at link time |
| Waits | guest futex waits use hashed mutex/condition-variable buckets (`os/atomic_ref.h`) |
| FFmpeg | `thirdparty/ffmpeg-config/ps5-x86_64/config.h` |

## Build

On Linux x86-64 with clang/lld 20 (Ubuntu clang 18 builds a RADV archive whose
entry points cannot be linked into a title):

1. Next to this checkout: `PS5_Vulkan`, `PS5_Mesa`, `PS5_PayloadSDK` and
   `PS5_VulkanTemplate` from github.com/mihawk-99; run
   `PS5_VulkanTemplate/ps5/tools/bootstrap.sh` (it builds the RADV release archive).
   The port was built and tested with these commits:

   | Repository | Commit |
   | --- | --- |
   | [PS5_PayloadSDK](https://github.com/mihawk-99/PS5_PayloadSDK) | `b5efad528e0ac1b8289f39d72a0e890ff68ff0a6` (2026-10-04) |
   | [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) | `252bf912a745e3566a93b69b098bdc8ec4048837` (2026-10-10) |
   | [PS5_VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate) | `91179df760df1e6c6f2d4d172bc435319d79ff3c` (2026-10-10) |
   | [PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa) | `47e9eb61ee7e1112194557e1e047178562827bc6` (2026-10-10) |

   For HDR, rebuild RADV with `ps5/patches/radv-videoout-hdr.patch` (below);
   without it the title runs in SDR. `ps5/build.sh` looks for the SDK at
   `../PS5_VulkanTemplate/.deps/native/ps5-payload-sdk` and for PS5_Vulkan at
   `../PS5_Vulkan`; `PS5_PAYLOAD_SDK` and `PS5_VULKAN_DIR` override them.
2. Submodules and patches: as in [BUILDING.md](BUILDING.md), plus
   `git -C thirdparty/plume apply ../../tools/patches/plume-android.patch`,
   `git -C thirdparty/SDL apply ../../tools/patches/sdl-ps5.patch`, and
   `git -C thirdparty/plume submodule update --init contrib/volk contrib/Vulkan-Headers contrib/VulkanMemoryAllocator`.
3. Your Disc 1 `default.xex` at `LostOdysseyRecompLib/private/disc1/`, then
   `xexdump` and `python3 -B tools/ppc_codegen.py generate` as in BUILDING.md.
4. `ps5/build.sh` writes `out/build/ps5/dist/PPSA99212/`.
5. Add the shaders: run the shader cache builder (below), which downloads
   `shaders/portable_vk.lospv` and `shaders/pipelines_corpus.bin` and builds
   `cache/`. By hand, the pack is the one whose contract
   `LoShaderPackTool contract image_disc1.bin` prints, from upstream's
   `shader-packs` release; `ps5/update.sh` fetches it too.

## Updating

`ps5/update.sh` keeps the port as commits on a local `ps5-port` branch and
rebases them onto upstream's `main`, then reapplies the submodule patches,
regenerates the game code only when the recompiler or its configuration
changed, downloads the matching Vulkan shader pack (checked by size, SHA-256
and runtime contract), and rebuilds. On a merge conflict it stops and lists the
files; merge them, `git add` them, and run `ps5/update.sh --continue`.
`--no-fetch` rebuilds without pulling; `--no-build` stops before the title
build. Then copy `eboot.bin` (and `shaders/` when it says the pack changed) to
the console, launch once, and compile any new built-in shader requests.

### Rebuilding RADV with the HDR patch

The title links `PS5_Vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a`.
After `PS5_Vulkan/tools/build-radv.sh release` (or on its extracted source,
`.deps/work/radv-src`): `patch -p1 < ps5/patches/radv-videoout-hdr.patch` in
the source, `PATH=.deps/work/radv-clc-bin:$PATH ninja -C
.deps/work/radv-build-ps5-release src/amd/vulkan/libvulkan_radeon.a`, and copy
the archive over the installed one. `PS5_VIDEOOUT_HDR=0` (in `ps5-env.txt`)
turns the HDR formats off.

## Install and run

Players use the release zips and build nothing but the shaders; the README's
[Getting started](../README.md#getting-started) has the same steps.

1. Copy the title folder to `/data/homebrew/PPSA99212/` (FTP or ps5upload):
   `PPSA99212` from `LostOdyssey-PS5.zip`, or `out/build/ps5/dist/PPSA99212/`
   from your own build. `ps5-env.txt` (from `ps5/ps5-env.example.txt`) holds
   the optional switches below.
2. Run the shader cache builder on your PC (below) and copy the `cache` and
   `shaders` folders it makes into `/data/homebrew/PPSA99212/`. It downloads
   the game's Vulkan shader pack and builds the rest from your discs.
3. Copy your extracted disc files to `/data/homebrew/PPSA99212/game/disc1/`
   (and `disc2/` to `disc4/`). Do this over FTP rather than with the in-game
   importer: a title's own writes slow to about 2 MiB/s after 1.3 GiB.
4. With an enabler that runs fake-signed titles and ShadowMountPlus loaded,
   launch **Lost Odyssey** from the home screen.

### Switches (`ps5-env.txt`)

The console gives a title no environment, so `LO_*` switches go in
`/app0/ps5-env.txt`, one `KEY=VALUE` per line (`#` starts a comment). The
runtime log's first lines list the switches it read.

| Switch | Effect |
| --- | --- |
| `LO_FRAME_TIMING=1` | once a second: frame rate, GPU wait, pacing, and how long the game took between frames |
| `LO_PROFILE=1` | samples every guest thread each millisecond and logs the busiest threads' states and return addresses once a second, and thread creation every 10 s (`kernel/imports.cpp`) |
| `LO_CS_SPIN=<n>` | pauses a thread waiting for a guest critical section spins before it sleeps (default 2500, about 50 us; `0` sleeps at once). The console has no futex, so a sleeping waiter wakes slowly (`kernel/critical_section.h`) |
| `LO_GUEST_CLOCK=0` | the guest clock (`mftb`, `KeTimeStampBundle`) behind one mutex and `clock_gettime`, as before; the default reads the TSC without a lock while the game runs at 1x (`debug/fast_forward.h`) |
| `LO_PS5_MMAP_THRESHOLD_MB=<n>` | blocks of n MiB or more get a heap segment of their own (dlmalloc's default 256 KiB); the title heap holds at most 1024 segments. Off by default |
| `LO_PS5_PIN=0/1/2` | guest worker threads kept off the first CPU (1, default), and the main thread held there (2) (`os/ps5_title.cpp`) |
| `LO_PS5_VIDEO_PROBE=1/2` | the HDR display probe instead of the game |
| `PS5_VIDEOOUT_HDR=0` | RADV offers no HDR formats |

### Built-in shaders

The runtime compiles about 16 small shaders of its own (presentation, SMAA,
blits, overlays, rect lists) with DXC. On the console it looks them up in
`cache/shaders/builtin_v28_spirv.lostore`, keyed as compiled by `dxc-1.8` (the
bundled `dxc-bin`). Any it cannot find it saves to `cache/shaders/requests/`;
on a PC, with the same runtime source:

```bash
LO_DXC_PATH=tools/XenosRecomp/thirdparty/dxc-bin/lib/x64/libdxcompiler.so \
  LoShaderPackTool compile-requests <requests folder> <out>/cache/shaders
```

and copy `<out>/cache/shaders/builtin_v28_spirv.lostore` back to the title
folder. A change to the runtime's shader sources changes their keys.

Most of them never need a console request: `LoShaderPackTool builtin-cache
<out>/cache/shaders --sources <dir> --expand` compiles, from the runtime's own
sources, every shader the runtime builds through `CompileCachedHlsl`:

- the fixed built-ins: presentation, SMAA, TAA, AO, overlays, blits, EDRAM
  transfer, scene-copy promotion, rect-list geometry, movie, motion masks
  (`tools/shader_pack/extract_builtin_sources.py` copies the function-local
  literals verbatim);
- per game shader in `<dir>` (`vs_*.bin`, `ps_*.bin`), and per vertex shader
  the renderer's source expansion makes from them (`--expand`): the rect-list
  vertex shader, the HDR tone map, and TAA's motion-replay programs
  (`--no-motion-replay` leaves those out: about 28,000 large shaders, used only
  with TAA).

`--dump <dir>` writes each request as `<key>-<entry>-<profile>-spirv.hlsl`;
every request a console had saved (16, 2026-10-21 to 2026-10-23) matched one
byte for byte. `--no-compile` only counts and dumps.

The game shaders come from `LoShaderScan [--full] <folder> <out>`
(`tools/shader_scan`, a static Linux program; no GPU, compiler or game run):
the runtime's resource scanner over a disc folder's `.fpd` files and `LO.fpi`
(`--full` decodes every CPX package, as `LO_SHADER_FULL_SCAN`). A folder named
`disc1` also scans `disc2` to `disc4` beside it; one disc takes about a minute.
Add the four shaders `default.xex` holds (`gpu/shader/resource_xex.h`, from
`image_disc1.bin`). For the USA/Europe discs: 20,686 shaders on the four discs
plus 4 in the executable, 28,484 with expansion (upstream's count for the game
files), and `LoShaderPackTool missing <pack> <image_disc1.bin> <dir>
<manifest.tsv>` finds all of them in the published Vulkan pack, so no game
shader is compiled on the console. Its manifest feeds `LoShaderPackTool merge`
when a pack does lack some.

### Shader cache builder (for players)

The release's `LostOdyssey-PS5-ShaderCacheBuilder.zip` makes every shader the
console needs, in one run on a Windows or Linux PC (`Build PS5 shader
cache.bat` or `linux/build-shader-cache.sh`; its `README.txt` has the steps).
Its output folder holds both:

- `shaders/portable_vk.lospv` and `shaders/pipelines_corpus.bin`: the game's
  own shaders, the upstream Vulkan pack and pipeline corpus this build was made
  for, downloaded from upstream's `shader-packs` release and checked by
  SHA-256 (skipped when already present and correct);
- `cache/shaders/builtin_v28_spirv.lostore`: the runtime's built-in shaders
  and the ones it derives from the game's, compiled from the player's own
  discs. It is derived from the game's shaders, so it is not published.

Players copy `cache/` and `shaders/` into the title folder and nothing else.
The download is done by the package's scripts
(`tools/shader_cache_builder/package/`); update the file names and hashes in
both scripts when the runtime moves to a new pack.

The compiler itself is `LoShaderCacheBuilder <folder with disc1..disc4>
[output] [--no-motion-replay]` (`tools/shader_cache_builder`). It decrypts
`default.xex` with XenonUtils (as `xexdump`) for the four shaders it holds,
scans the four discs (as `LoShaderScan --full`), runs `builtin-cache --expand`,
and deletes the extracted game shaders. About 36,000 shaders with motion replay
(1.9 GB), 8,000 without (600 MB).

Build it for Linux with `ninja LoShaderCacheBuilder` in the pack tool build
(`cmake -S tools/shader_pack`), and for Windows from Linux with
`-DCMAKE_TOOLCHAIN_FILE=tools/shader_cache_builder/mingw-w64.cmake` (clang 20
targeting MinGW-w64 with POSIX threads; static, needs only the bundled
`dxcompiler.dll` and `dxil.dll`). A Windows build run under Wine wrote records
identical, key, compiler digest and SPIR-V, to the Linux build's.

Logs are in `/data/homebrew/PPSA99212/logs/`, and standard error goes to klog
(port 3232 with klogsrv).
