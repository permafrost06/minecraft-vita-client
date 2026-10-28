# Credits

- `vendor/debugScreen.c`, `vendor/debugScreen.h` and
  `vendor/debugScreen_custom.h`: VitaSDK samples,
  https://github.com/vitasdk/samples, CC0 1.0 Universal. The upstream README
  states: "All code and build scripts in this repo is licensed under the terms
  of CC0 1.0 Universal."
  License: https://creativecommons.org/publicdomain/zero/1.0/
- vitaGL and vitaShaRK by Rinnegatamante, LGPL-3.0, statically linked from the
  VitaSDK packages: https://github.com/Rinnegatamante/vitaGL and
  https://github.com/Rinnegatamante/vitaShaRK. The application's object files
  can be relinked against modified versions using the CMake build in this repo.
- The HUD font glyphs in `vendor/debugScreenFont.c` retain the PSPSDK
  copyright notices and BSD license declared in that file. See
  `vendor/PSPSDK-LICENSE` for the full upstream license; the samples' general
  CC0 declaration does not replace this file's notice.
- zlib 1.3.1: Jean-loup Gailly and Mark Adler, zlib license, fetched at build
  time. License is included in the dependency source's `zlib.h`.
- Protocol references, collision shapes and generated block state-ID/name ranges:
  PrismarineJS minecraft-data, MIT, https://github.com/PrismarineJS/minecraft-data.
  See `vendor/MINECRAFT-DATA-LICENSE`. The approximate material colors and
  procedural surface patterns are original to this project.

- Physics constants were cross-checked with PrismarineJS prismarine-physics
  (MIT), https://github.com/PrismarineJS/prismarine-physics. Movement is implemented
  locally in C.

This is an independent homebrew project, unaffiliated with Mojang or Microsoft.
