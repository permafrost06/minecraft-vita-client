# Credits

Original project code and documentation are covered by the root `LICENSE`
(MIT). Third-party material retains the separate terms below. Generated
minecraft-data tables are covered by the minecraft-data notice as well.

- `vendor/debugScreen.c`, `vendor/debugScreen.h` and
  `vendor/debugScreen_custom.h`: VitaSDK samples,
  https://github.com/vitasdk/samples, CC0 1.0 Universal. The upstream README
  states: "All code and build scripts in this repo is licensed under the terms
  of CC0 1.0 Universal."
  License: `vendor/CC0-1.0-LICENSE`
  (https://creativecommons.org/publicdomain/zero/1.0/).
- vitaGL and vitaShaRK by Rinnegatamante, LGPL-3.0, statically linked from the
  VitaSDK packages: https://github.com/Rinnegatamante/vitaGL and
  https://github.com/Rinnegatamante/vitaShaRK. These libraries are fetched
  separately, not vendored here. For binary distribution, see the README's
  release requirements; source availability alone is not a complete release bundle.
- The HUD font glyphs in `vendor/debugScreenFont.c` retain the PSPSDK
  copyright notices and BSD license declared in that file. See
  `vendor/PSPSDK-LICENSE` for the full upstream license; the samples' general
  CC0 declaration does not replace this file's notice.
- zlib 1.3.1: Jean-loup Gailly and Mark Adler, zlib license, fetched at build
  time. License is included in the dependency source's `zlib.h`.
- Protocol references and generated block, item, entity, enchantment, gameplay
  and collision tables:
  PrismarineJS minecraft-data, MIT, https://github.com/PrismarineJS/minecraft-data.
  See `vendor/MINECRAFT-DATA-LICENSE`. The approximate material colors and
  procedural surface patterns are original to this project.

- Physics constants were cross-checked with PrismarineJS prismarine-physics
  (MIT), https://github.com/PrismarineJS/prismarine-physics. Movement is implemented
  locally in C.

Sound and particle identifier/ID tables are generated from vanilla server
registry reports; they contain no recordings, textures, models or server code.
Their generators are `tools/sounds.py` and `tools/particles.py`.

This is an independent homebrew project, unaffiliated with Mojang or Microsoft.
