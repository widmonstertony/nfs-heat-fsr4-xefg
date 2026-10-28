# Third-party notices

The compiled bridge contains MinHook; its complete BSD-style notices are in
`MinHook.txt`. The release kit also contains the unmodified Ultimate ASI Loader
binary; its MIT notice is in `Ultimate-ASI-Loader.txt`.

Other runtimes are **not distributed by this repository/release**. They must be
obtained separately and imported from an existing working installation under
their respective terms:

- OptiScaler: https://github.com/optiscaler/OptiScaler — GPL-3.0.
- Intel XeSS/XeFG: https://github.com/intel/xess — Intel Simplified Software License.
- AMD FidelityFX: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK —
  file-specific licenses. Some SDK files have binary-only redistribution terms;
  other named files are MIT. Consult the exact SDK version's license.
- FSR4 shader performance overrides:
  https://github.com/zgauthier2000/fsr4-shader-performance-overrides —
  GPL-2.0-or-later scripts and accompanying AMD notices. The tested Navi12
  upscaler is modified and unofficial; this release does not rehost that DLL.

NVIDIA NGX and Intel XeFG declarations are build-time dependencies supplied by
the builder; their SDK headers are not vendored. No NVIDIA DLSS runtime, game
shader bytecode, game executable or driver is distributed.

No dependency author or hardware vendor endorses this project.
