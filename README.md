# Need for Speed Heat — FSR 4 + XeFG bridge

[简体中文](README.zh-CN.md) · [Downloads / experimental release](https://github.com/widmonstertony/nfs-heat-fsr4-xefg/releases)

An experimental, single-player rendering bridge for **Need for Speed Heat**, developed on a MacBookPro16,4 with a Radeon Pro 5600M under Windows. It connects the game's main temporal-AA inputs to **FSR upscaling and Intel XeFG frame generation** through OptiScaler. This is not a graphics driver update and is not an official AMD, Intel, NVIDIA or EA product.

## What it does

- Identifies a specific main-color TAA compute shader, not just a screen-capture overlay.
- Captures matching depth, motion vectors and camera constants; keeps the game's TAA history intact.
- Sends jittered linear, tone-mapped color to the upscaler, then passes its full-resolution result into the game's original UI composition.
- Supports FSR 3.1 or the locally tested **FSR 4.1.1.2740 Navi12 INT8** runtime. **Frame generation is XeFG, not FSR 4 FG.**
- Enables automatically about 10 seconds after the matching TAA pass appears. **F9 disables the bridge for this game session**; restart Heat to enable again. No two-minute test timeout.

## Status and limitations

**Experimental prerelease. Solo/offline only. Do not use in multiplayer or with anti-cheat.**

Tested game executable: **1.0.60.7040**. Hardware: `VEN_1002&DEV_7360&SUBSYS_021E106B`, MacBookPro16,4 / Radeon Pro 5600M. Other versions, GPUs and drivers are not validated. The installer defaults to this hardware ID; `-AllowUntestedHardware` bypasses that installer check only, and does not make another GPU supported.

Validation so far:

- FSR 3.1 + XeFG: 1,800 consecutive single-player driving frames, **856×480 → 1280×720**, followed by successful backend release and render-hook restoration.
- FSR 4.1.1 INT8: separate GPU tests confirmed the local FSR 4 model and valid upscaled output; FSR 4 + XeFG passed a 60-app-present test with 36 generated presents / 96 SDK-reported frames. These are **not an in-game FPS benchmark**.
- The owner reports that the installed game works. This is not broad compatibility or long-session quality certification.

Known gaps: loading/menu/resize transitions, ghosting and HUD interpolation, dynamic camera FOV (backend configured to45°), performance and latency across long sessions. UI is composed by the original game, but interpolated HUD quality is not guaranteed. FSR 4 may be slower than FSR 3.1 on this GPU. A safety failure stops replacement and releases the backend; restarting is required to retry. It does not automatically restore 100% render scale.

## Download and install

1. Download and extract the **bridge-kit ZIP** from Releases. GitHub's automatic “Source code” ZIP does not contain the compiled payload.
2. Start unmodified Heat once, then exit. Restore/remove other graphics injectors first: the installer refuses existing `d3d11.dll`, `dxgi.dll` or their configuration files instead of overwriting them.
3. Obtain a **working, separately licensed runtime folder** matching `manifest.json`. The public kit deliberately **does not redistribute OptiScaler nightly, Intel runtime DLLs, or the modified Navi12 FSR DLL**. It imports matching files from your own runtime folder, including subdirectories. An ordinary latest OptiScaler package is not guaranteed to contain these exact versions. If you do not already have the matching runtime, this release alone is not a standalone installer.
4. Double-click `INSTALL.cmd`. Enter the Heat installation folder and your runtime folder. If the game folder is protected, run the command prompt as administrator.
5. Launch Heat normally and select **Solo**. Keep the initial windowed setup while testing. Press **F9** if the effect misbehaves.

PowerShell alternative:

```powershell
.\Install.ps1 -GamePath 'D:\Games\Need for Speed Heat' -RuntimeSource 'D:\MyRuntime' -Upscaler FSR4
```

Use `-Upscaler FSR3` for the FSR3.1 path. To switch an installed kit, restore it first and reinstall with the other option. `-ValidateOnly` checks inputs without writing game/configuration files. No automatic downloads, driver installation, BCD changes, signature-policy changes or game EXE patching are performed.

The installer selects windowed mode, custom graphics, TAA and **66.6667% internal rendering**. Other graphics settings and the savegame are not deliberately changed. Paths are generated locally; there is no dependency on the developer's drive letters or home directory. Keep the extracted kit for recovery.

## Restore

Exit Heat, run `RESTORE.cmd`, and enter the same game folder. DLLs must still match recorded hashes. The installer-owned files and logs are moved into `HeatTemporalBridge-backups/<timestamp>` in the game folder, not permanently deleted. Only the four changed profile fields are restored; fields you changed again after installing are preserved. Unknown/modified DLLs cause restoration to stop for review.

After a partial failed installation, use the same recovery command. Inspect the retained backup if an error remains. Do not indiscriminately delete existing game files.

## Build and source

Build from an x64 Visual Studio2022 Developer PowerShell with MSVC and a Windows11 SDK:

```powershell
.\Build.ps1 -NgxInclude 'C:\SDKs\ngx-headers' -XeFgInclude 'C:\SDKs\XeSS\inc\xess_fg'
```

Supply NVIDIA NGX declarations and Intel XeFG SDK headers under their own terms; they are not vendored. No NVIDIA DLSS runtime is included or needed on the tested AMD GPU. MinHook source is vendored with its license. Public builds do not dump game shader bytecode, constant buffers or screenshots. Optional local logs remain in the installed plugin folder and are never uploaded automatically.

## Dependencies and credits

- [OptiScaler](https://github.com/optiscaler/OptiScaler), GNU GPLv3; tested runtime identifies as v10dev `b19cfd29` (20261027). Obtain its binaries/source from the upstream project. Hashes pin the tested binary, not the newest one.
- [Intel XeSS / XeFG SDK](https://github.com/intel/xess), Intel Simplified Software License; XeFG1.3.1 used by the tested path.
- [AMD FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK), relevant file licenses apply. The tested local Navi12 FSR4.1.1 payload is modified, unofficial and not bundled here.
- [FSR4 shader performance overrides](https://github.com/zgauthier2000/fsr4-shader-performance-overrides), source of the local Navi12 modification route; consult its GPL notices and AMD notices. No prebuilt shader captures are included here.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader), MIT; its unmodified loader is included in the release kit.
- [MinHook](https://github.com/TsudaKageyu/minhook), BSD-style license, included under `licenses/`.

Bridge source/scripts: MIT. Third-party components retain their own terms. Never describe this package as an official FSR4 driver for all Macs. Do not publish logs containing personal paths or account information without reviewing them first.
