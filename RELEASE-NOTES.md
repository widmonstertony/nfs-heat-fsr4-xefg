# v0.1.0-experimental — Heat FSR4/XeFG bridge kit

English / 简体中文说明均已提供。

- Main-color TAA interception with depth/motion/camera inputs; FSR3.1 or local Navi12 FSR4.1.1 INT8 output, with XeFG frame generation.
- Automatic session activation, F9 emergency off, integrity checks and recoverable restoration.
- Portable installer; no developer-specific paths, driver changes or game EXE patches.
- Public build disables shader/constant-buffer/image dumping.

**This is a prerelease for single-player testing on the documented5600M configuration.** Long-session stability and performance are not certified.

**Download `Heat-FSR4-XeFG-v0.1.0-experimental-bridge-kit.zip`, not GitHub's automatic Source code ZIP.** This kit includes the bridge and ASI loader, but **not the third-party upscaler/frame-generation runtimes**. Installation requires an existing working runtime matching `manifest.json`; there are no automatic downloads. See both READMEs before installing. Without those exact runtime files the kit cannot install independently.

**本包不是包含全部第三方运行库的离线整合包。** 需提供与manifest匹配的已有运行库。仅单人测试，不是官方驱动；FSR4负责超分，XeFG负责插帧。安装器拒绝覆盖已有MOD，恢复会保留备份。

Checksums are provided in `SHA256SUMS.txt`. Source is in this repository; third-party licensing/credits are documented separately. No game files, captured shaders, saves, personal logs or graphics drivers are distributed.
