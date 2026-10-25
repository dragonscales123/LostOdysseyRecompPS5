<div align="center">

<img src="assets/lost-odyssey-recomp.png" alt="Lost Odyssey Recomp logo" width="112">

# Lost Odyssey Recomp

**A native port of Lost Odyssey for the PS5.**

An experimental homebrew build of [LostOdysseyRecomp](https://github.com/freefrank/LostOdysseyRecomp) for PlayStation 5 consoles that run homebrew titles.

[Build and install](docs/PS5.md) · [Upstream project](https://github.com/freefrank/LostOdysseyRecomp) · [License: GPL-3.0](LICENSE)

</div>

## What this is

LostOdysseyRecomp turns the Xbox 360 game into native code by static recompilation. This fork adds a PS5 target: the game runs as a homebrew title (`PPSA99212`) on the console's own CPU and GPU, rendering through Vulkan with Mesa's RADV driver built for the PS5.

You need your own copy of the game. This repository contains no game data, no shaders taken from the game and no Sony code.

## Status

Experimental. Tested on one console (firmware 13.60) with the USA/Europe four-disc set.

| Area | State |
| :--- | :--- |
| Boot, menus, movies | Working |
| Field and battles | Working, rendered fully |
| Saves | Saving and overwriting work |
| Frame rate | 120 FPS at 2560x1440 on a 120 Hz display, 60 FPS on 60 Hz |
| HDR | HDR10 output with peak-brightness calibration (needs the RADV patch) |
| Controller | DualSense, with PlayStation button prompts |
| Known issues | A few seconds of lower frame rate at the start of a battle and after some loads |
| Untested | Discs 2-4, long play sessions |

## Getting started

Everything is in **[docs/PS5.md](docs/PS5.md)**:

1. **Build** the title on Linux from your Disc 1 `default.xex`, with the PS5 SDK and Vulkan driver projects listed there (exact commits included).
2. **Build the shader cache** from your own discs with `LoShaderCacheBuilder` (Windows or Linux). It is not distributed because it is derived from the game.
3. **Install** the title folder to `/data/homebrew/PPSA99212/` and copy your disc files to its `game/` folder over FTP.
4. **Optional switches** go in `ps5-env.txt` next to `eboot.bin` (see [ps5/ps5-env.example.txt](ps5/ps5-env.example.txt)).

Logs are written to `/data/homebrew/PPSA99212/logs/`. Please include one with any bug report.

## Credits

- **[freefrank/LostOdysseyRecomp](https://github.com/freefrank/LostOdysseyRecomp)** and its contributors: the recompilation, runtime and renderer this port builds on.
- **[mihawk-99](https://github.com/mihawk-99)**: PS5_Vulkan, PS5_Mesa, PS5_PayloadSDK and PS5_VulkanTemplate, which make Vulkan homebrew on the PS5 possible.
- **[ps5-payload-dev](https://github.com/ps5-payload-dev/sdk)**: the PS5 payload SDK.
- **[hedge-dev](https://github.com/hedge-dev)**: XenonRecomp and XenosRecomp.
- **[SDL](https://github.com/libsdl-org/SDL)**, **[plume](https://github.com/renderbag/plume)** and the other libraries under `thirdparty/`.

## License and game data

The code is licensed under the [GNU GPL v3.0](LICENSE), like the upstream project.

Lost Odyssey is a trademark of its respective owners. This project is not affiliated with or endorsed by Microsoft, Mistwalker, feelplus or Sony Interactive Entertainment. It does not include or distribute any game assets: you must own the game and supply your own disc files.
