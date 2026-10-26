<div align="center">

<img src="assets/lost-odyssey-recomp.png" alt="Lost Odyssey Recomp logo" width="112">

# Lost Odyssey Recomp

**A native port of Lost Odyssey for the PS5.**

An experimental homebrew build of [LostOdysseyRecomp](https://github.com/freefrank/LostOdysseyRecomp) for PlayStation 5 consoles that run homebrew titles.

### [Download](https://github.com/dragonscales123/LostOdysseyRecompPS5/releases) · [Install guide](#getting-started) · [Upstream project](https://github.com/freefrank/LostOdysseyRecomp) · [License: GPL-3.0](LICENSE)

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

Download both files from the **[Releases page](https://github.com/dragonscales123/LostOdysseyRecompPS5/releases)**:

- `LostOdyssey-PS5.zip`: the ready-to-copy title folder.
- `LostOdyssey-PS5-ShaderCacheBuilder.zip`: builds the shaders from your own discs.

You don't need to compile anything. The only things you add yourself are the shaders and your discs, because neither can be distributed.

**You need:** a PS5 that runs homebrew titles (tested on firmware 13.60) with an enabler for fake-signed titles and ShadowMountPlus loaded, FTP access to the console, and a Windows or Linux PC with your extracted Lost Odyssey discs (one folder holding `disc1` to `disc4`).

1. **Copy the title.** Unzip `LostOdyssey-PS5.zip` and copy its `PPSA99212` folder to `/data/homebrew/PPSA99212/` on the PS5.
2. **Make the shader cache.** Unzip the shader cache builder on your PC and run it on your disc folder (Windows: double-click `Build PS5 shader cache.bat`; its `README.txt` has the details). It takes 30-90 minutes. Copy the `cache` folder it makes into `/data/homebrew/PPSA99212/`.
3. **Get the shader pack.** Download the game's shader pack from the upstream project into `/data/homebrew/PPSA99212/shaders/`, named `portable_vk.lospv`. The exact link is in `shaders/PUT_SHADER_PACK_HERE.txt`.
4. **Copy your discs.** Copy your extracted `disc1` to `disc4` folders into `/data/homebrew/PPSA99212/game/` over FTP. Don't use the in-game importer: the console writes slowly from inside a title.
5. **Play.** Launch **Lost Odyssey** from the home screen.

**Tips**

- **120 FPS:** use a 120 Hz display and choose 120 in Graphics → Frame rate.
- **HDR:** set the PS5 to HDR "On when supported", then turn on HDR in Graphics.
- **Bug reports:** logs are in `/data/homebrew/PPSA99212/logs/` (start-up steps in `boot.txt`). Please attach the newest one.
- **Optional switches** go in `ps5-env.txt` next to `eboot.bin`; [docs/PS5.md](docs/PS5.md#switches-ps5-envtxt) lists them.

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
