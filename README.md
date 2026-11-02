<div align="center">

<img src="assets/lost-odyssey-recomp.png" alt="Lost Odyssey Recomp logo" width="112">

# Lost Odyssey Recomp

**A native port of Lost Odyssey for the PS5.**

A homebrew build of [LostOdysseyRecomp](https://github.com/freefrank/LostOdysseyRecomp) for PlayStation 5 consoles that run homebrew titles. **Now in beta.**

### [Download](https://github.com/dragonscales123/LostOdysseyRecompPS5/releases) · [Install guide](#getting-started) · [Upstream project](https://github.com/freefrank/LostOdysseyRecomp) · [License: GPL-3.0](LICENSE)

</div>

## What this is

LostOdysseyRecomp turns the Xbox 360 game into native code by static recompilation. This fork adds a PS5 target: the game runs as a homebrew title (`PPSA99212`) on the console's own CPU and GPU, rendering through Vulkan with Mesa's RADV driver built for the PS5.

You need your own copy of the game. This repository contains no game data, no shaders taken from the game and no Sony code.

## Status

**Beta** (build 47, [v0.2.0-beta.3](https://github.com/dragonscales123/LostOdysseyRecompPS5/releases/latest)), based on LostOdysseyRecomp **v1.1.0**. The port has moved from alpha to beta: everything needed to play is in place, and every crash and freeze found so far is fixed. What is left is testing on more consoles and the later discs, so bug reports are the most useful help right now ([how to report](#reporting-a-bug)).

Tested on one console (firmware 13.60) with the USA/Europe four-disc set.

| Area | State |
| :--- | :--- |
| Boot, menus, movies | Working |
| Field and battles | Working, rendered fully |
| Saves | Saving and overwriting work |
| Disc changes | Disc 1 to disc 2 works, in play and when loading a disc 2 save |
| Frame rate | 120 FPS at 2560x1440 on a 120 Hz display, 60 FPS on 60 Hz |
| HDR | HDR10 output with peak-brightness calibration |
| Controller | DualSense, with PlayStation button prompts |
| Settings menu | The original game's menu look, HD and European fonts, more languages and button icon sets (from upstream v1.1.0) |
| Cheats | Debug menu cheats (L1+R1), including Restore HP / MP on save; items given with the cheats show up in the item menu and the sell screen |
| Long play | Multi-hour sessions on discs 1 and 2 with many area loads, battles and cutscenes, without a crash or freeze |
| Known issues | A few seconds at about 70-80 FPS at the start of a battle |
| Untested | Discs 3 and 4 |

### Latest: build 47

- **Updated to LostOdysseyRecomp v1.1.0:** the settings menu gets the original game's look, game-style button icons with more icon sets, HD and European fonts, four more European languages and a Scene fades option. The renderer gets Vulkan stability fixes and resets occlusion on each map load.
- **Cheat items now show up properly.** The item menu and the sell screen list only the items on the game's held-item list, which the item cheats never updated, so a cheat-given item could be counted but not shown. Item cheats now keep that list in step. If an item went missing this way, set its quantity again from the cheat menu.
- **Cheat menu:** the second "Healing Herb" entry is now named Healing Tank.

### What changed from alpha to beta

- **Crashes in long cutscenes fixed.** Some cutscenes piled up repeated screen clears until the console ran out of memory (error, then the home screen) or the graphics chip stalled.
- **Freezes on disc 2 fixed.** Moving between some areas (for example from the Sorcerer's Shrine back to the Crimson Forest) or starting some battles froze the game for 20-80 seconds while the audio decoder rejected sound data. Unreadable sound data now plays as a moment of silence instead.
- **Disc changes:** files the game keeps open while switching discs now read from the new disc, as on the console's drive.
- **Smoother battles:** fewer frame-rate drops in battle, from a faster game clock and lock handling.
- **HDR calibration** uses the console's own HDR brightness setting when the display reports none.
- **Restore HP / MP on save** (new, optional): refills the party after every save, on every disc.
- **Build number on the stats overlay**, so reports can say which build was running.
- **Audio:** includes the upstream fix for XMA output buffer handling.
- **Diagnostics:** memory use in the log and in crash reports.

## Getting started

Download both files from the **[Releases page](https://github.com/dragonscales123/LostOdysseyRecompPS5/releases)**:

- `LostOdyssey-PS5.zip`: the ready-to-copy title folder.
- `LostOdyssey-PS5-ShaderCacheBuilder.zip`: makes the shaders on your PC, from your own discs.

You don't need to compile anything. The only things you add yourself are the shaders, which the builder makes for you, and your discs.

**You need:** a PS5 that runs homebrew titles (tested on firmware 13.60) with an enabler for fake-signed titles and ShadowMountPlus loaded, FTP access to the console, and a Windows or Linux PC with your extracted Lost Odyssey discs (one folder holding `disc1` to `disc4`).

1. **Copy the title.** Unzip `LostOdyssey-PS5.zip` and copy its `PPSA99212` folder to `/data/homebrew/PPSA99212/` on the PS5.
2. **Make the shaders.** Unzip the shader cache builder on your PC and run it on your disc folder (Windows: double-click `Build PS5 shader cache.bat`; its `README.txt` has the details). It downloads the game's shader pack from the upstream project and builds the rest from your discs, in 30-90 minutes. Copy the `cache` and `shaders` folders it makes into `/data/homebrew/PPSA99212/`.
3. **Copy your discs.** Copy your extracted `disc1` to `disc4` folders into `/data/homebrew/PPSA99212/game/` over FTP ([folder layout](#folder-layout) below). Don't use the in-game importer: the console writes slowly from inside a title.
4. **Play.** Launch **Lost Odyssey** from the home screen.

### Folder layout

When everything is copied, the title folder should look like this:

```
/data/homebrew/PPSA99212/              ← the title folder (copy it here from the release zip)
├── eboot.bin                          ← from the release zip
├── ps5-env.txt                        ← from the release zip (optional switches)
├── sce_module/
│   └── libc.prx                       ← from the release zip
├── sce_sys/                           ← from the release zip
│   ├── icon0.png
│   └── param.json
├── licenses/                          ← from the release zip
│
├── shaders/                           ← made by the shader cache builder
│   └── portable_vk.lospv
├── cache/                             ← made by the shader cache builder
│   └── shaders/
│       └── shaders_spirv.lostore
│
├── game/                              ← your own discs (all names lowercase)
│   ├── disc1/
│   │   ├── default.xex                ← must be exactly game/disc1/default.xex
│   │   ├── LO.fpi
│   │   ├── LO.fpd
│   │   ├── xenon_battle.fpd
│   │   ├── xenon_chr.fpd
│   │   ├── xenon_event.fpd
│   │   ├── xenon_field.fpd
│   │   ├── xenon_loc.fpd
│   │   ├── xenon_mov.fpd
│   │   ├── xenon_obj.fpd
│   │   ├── xenon_scr.fpd
│   │   ├── xenon_snd.fpd
│   │   ├── xenon_sys.fpd
│   │   ├── xenon_vfx.fpd
│   │   └── xenon_world.fpd
│   ├── disc2/                         ← same files, from Disc 2
│   ├── disc3/                         ← same files, from Disc 3
│   └── disc4/                         ← same files, from Disc 4
│
├── logs/                              ← created by the game; attach the newest runtime-*.log to bug reports
└── boot.txt                           ← created by the game; attach it if the game crashed
```

**Common mistakes**
- `Disc1/` or `Default.xex`: names must be lowercase; the PS5 treats them as different.
- An extra folder level, like `game/disc1/Lost Odyssey Disc 1/default.xex`. The disc's files go straight into `disc1/`.
- `default.xex` directly in `game/` instead of `game/disc1/`.
- Discs copied into a different `PPSA99212` folder from the one you launch.

**Tips**

- **120 FPS:** use a 120 Hz display and choose 120 in Graphics → Frame rate.
- **HDR:** set the PS5 to HDR "On when supported", then turn on HDR in Graphics.
- **Restore HP / MP on save:** press **L1+R1** for the debug menu, open **Cheats**, and turn it on in Quick tools. The party's HP and MP refill after every save.
- **Missing cheat items:** if an item you gave yourself with an older build doesn't show in your item menu, set its quantity again from the cheat menu.
- **Optional switches** go in `ps5-env.txt` next to `eboot.bin`; [docs/PS5.md](docs/PS5.md#switches-ps5-envtxt) lists them.

### Reporting a bug

Open an issue and attach:

- the newest log from `/data/homebrew/PPSA99212/logs/`,
- `/data/homebrew/PPSA99212/boot.txt` if the game crashed or went back to the home screen,
- the build number from the stats overlay (Graphics → Performance overlay), and which disc and area you were in.

### Building from source

Only needed if you want to change the code: [docs/PS5.md](docs/PS5.md) covers building the title on Linux, with the exact versions of the PS5 SDK and Vulkan driver projects it uses.

## Credits

- **[freefrank/LostOdysseyRecomp](https://github.com/freefrank/LostOdysseyRecomp)** and its contributors: the recompilation, runtime and renderer this port builds on.
- **[mihawk-99](https://github.com/mihawk-99)**: PS5_Vulkan, PS5_Mesa, PS5_PayloadSDK and PS5_VulkanTemplate, which make Vulkan homebrew on the PS5 possible.
- **[ps5-payload-dev](https://github.com/ps5-payload-dev/sdk)**: the PS5 payload SDK.
- **[hedge-dev](https://github.com/hedge-dev)**: XenonRecomp and XenosRecomp.
- **[SDL](https://github.com/libsdl-org/SDL)**, **[plume](https://github.com/renderbag/plume)** and the other libraries under `thirdparty/`.

## License and game data

The code is licensed under the [GNU GPL v3.0](LICENSE), like the upstream project.

Lost Odyssey is a trademark of its respective owners. This project is not affiliated with or endorsed by Microsoft, Mistwalker, feelplus or Sony Interactive Entertainment. It does not include or distribute any game assets: you must own the game and supply your own disc files.
