# Vita Block Client

A native PS Vita client targeting **Minecraft Java 1.19.4 (protocol 762)** for
self-hosted vanilla/Paper servers with `online-mode=false`. Verified against an
unmodified Mojang server on aarch64 Linux and a physical Vita.

Milestone 2 renders real server terrain and provides a local inspection camera.
**Camera movement is local only: the server player stays at its spawn position.**
Player physics, synchronized movement, block interaction and inventory are next.

## Implemented

- Server status and measured ping; username login, compression, keepalives,
  play ping/pong, and spawn teleport acknowledgement.
- Dimension registry NBT, including the modern Overworld's `min_y=-64` and
  384-block height. Dimension selection and respawn clear the terrain cache.
- Single-value, indirect and direct chunk palettes, including packed values
  that do not cross 64-bit word boundaries. Biome palettes are consumed.
- Bounded 9 by 9 chunk cache, chunk unloads, single and multi-block updates.
  Maximum dimension height is 512 blocks; larger/custom registries fail clearly.
- First-person software voxel rendering with approximate material colors,
  original procedural surface variation, face shading, fog and a crosshair.
- Dual-stick local camera, vertical movement, spawn-camera reset, selectable
  rendering detail and connection diagnostics.
- Network worker separate from the UI; world mutation and rendering are
  serialized to keep chunk pointers valid.

No server plugin or proxy is required. Paper itself has not been tested yet.
PollyMC is a launcher; the target server protocol is standard Java 1.19.4.
Other Minecraft versions, Bedrock, Microsoft login and signed chat are unsupported.

## Prototype limits

The camera can fly through blocks and move only within the cached region around
where the server player stands. It does not request terrain around the camera.
There is no collision, gravity, entity rendering, inventory, block interaction,
sound, server lighting or biome tinting. Most visible blocks are approximated
as full cubes; small plants are omitted. Water and glass are opaque approximations.
No Mojang textures or game assets are bundled.

The software renderer is a starting point for verifying terrain, not the final
performance architecture. The default preview samples 120 by 68 rays and scales
to the screen; detailed mode samples 240 by 136. Open views cost more than nearby
surfaces. A hardware renderer is planned for substantially better performance.

## Build and tests

Host prerequisites: C compiler, CMake 3.20+, Python 3.

```sh
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host -j4
ctest --test-dir build-host --output-on-failure
```

Vita prerequisites: VitaSDK.

```sh
export VITASDK=/home/frost/opt/vitasdk
export PATH="$VITASDK/bin:$PATH"
cmake -S . -B build-vita -DVITA=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-vita -j4
```

Output: `build-vita/VitaBlockClient.vpk`, title ID `VBLK00001`.
CMake fetches zlib 1.3.1 with a pinned SHA-256 hash on first configure.

Tests cover VarInt bounds; fragmented TCP, compression and login; dimension
registries; palette widths and word padding; malformed/truncated chunks;
negative coordinates; block updates; cache lifetime; and renderer output bounds.
World/protocol tests have also been run under AddressSanitizer and UBSan.

## Run a server

Use a vanilla or Paper **1.19.4** server. For an isolated LAN test:

```properties
online-mode=false
enforce-secure-profile=false
server-port=25565
view-distance=2
simulation-distance=2
gamemode=creative
difficulty=peaceful
```

Offline mode does not authenticate usernames; use a trusted LAN. The client
logs in as `VitaPlayer`. Review and accept Minecraft's server EULA before starting.

The local verified server is in `.deps/server-1.19.4/`, outside Git:

```sh
python tools/server.py download
python tools/server.py status
python tools/server.py stop
python tools/server.py start
```

`start` preserves existing server properties and accepted EULA, verifies the
Mojang download hash, and refuses duplicate processes. On a fresh setup, `--bind`
sets the initial bind address; the default is loopback. `stop` verifies process
ownership and sends SIGTERM for Minecraft's save/shutdown handler.

The running development server is `192.168.100.190:25565` on aarch64 OpenJDK
25.0.4.1, bound to its LAN interface. No x86 machine or emulation is needed.
Mojang 1.19.4 server SHA-1: `8f3112a1049751cc472ec13e397eade5336ca7ae`.

Check the connection or decode/render it on the host:

```sh
./build-host/mcprobe status SERVER_IPV4 25565
./build-host/mcprobe login SERVER_IPV4 25565 HostProbe 40
./build-host/mcprobe view SERVER_IPV4 25565 TerrainProbe 10
```

`view` reports the dimension, cache size and render time and saves
`build-host/server-view.ppm`. The same decoder and renderer run on Vita.

## Vita controls

The development Vita is `192.168.100.226`. This is the device address, not the
server address. The app is installed and its executable uploads were read back
and compared byte for byte. No global taiHEN configuration was changed.

In the connection menu:

- Left/right selects an address octet; up/down changes it.
- Cross queries status and ping; Triangle logs in as `VitaPlayer`.
- Circle disconnects; Select exits.

After joining, terrain appears automatically:

- Left stick moves the local camera; right stick looks around.
- L lowers the camera; R raises it.
- Square resets the camera to the server spawn position.
- D-pad Up toggles preview/detailed rendering.
- Start toggles the connection menu; the server connection stays alive.
- Circle disconnects; Select exits.

`ux0:data/vitablock/server.txt` stores the selected numeric IPv4 and port,
e.g. `192.168.100.190 25565`. Edit it over FTP to set a custom port. Hostnames,
SRV and IPv6 are not implemented. Successful status JSON is saved to `status.json`.

The local diagnostic commands use VitaCompanion FTP, port 1337:

```sh
python tools/vita.py login
python tools/vita.py shot --output build-vita/shot.bmp
python tools/vita.py menu
python tools/vita.py reset
python tools/vita.py log
python tools/vita.py disconnect
```

Commands go through `ux0:data/vitablock/command.txt` while the app is foreground.
The helper uploads each command to a temporary file, then renames it atomically
and waits for the app to consume it. The app deletes each command after reading. `runtime.log` records startup
results. `capture.txt` records camera, input and connection counters at capture
time. Screenshot BMPs contain the app's actual framebuffer. The existing
Vita Control plugin has a missing library dependency in this homebrew process,
so the app provides its own capture path and needs no global plugin edits.

## Live validation

Milestone 1 (2026-10-26): host and physical Vita login, compression threshold
256, 57 terrain packets, spawn confirmation and keepalive replies. A live-server
state timing race was fixed by waiting for Join Game before sending settings.

Milestone 2: host decoded all 57 chunks with `min_y=-64`, height 384, and
11,206,656 bytes of terrain cache. The physical Vita rendered server terrain;
right-stick look and left-stick movement were inspected in framebuffer
captures. Shoulder height controls are implemented; Companion button injection
did not provide a reliable independent verification of those buttons. Vanilla's
bounded zero padding after chunk sections is accepted, while nonzero unexpected trailing bytes are rejected.

The detailed software view initially measured roughly 2-6 FPS depending on the
scene. The optimized default preview measured around 16-23 FPS in captures of
nearby terrain/sky; this is scene-dependent, not a fixed performance target.
The Vita stayed connected through four keepalive replies while rendering.
A wider, rescaled stick deadzone addresses the resting offsets observed on the
physical device, and failed/no-sample controller reads leave the camera neutral.

An idle-camera check was inconclusive because input during the test was not
confirmed; neutral stick samples are covered by automated deadzone tests.

## Next milestones

1. Synchronize player movement with collision, gravity and server corrections.
2. Replace the software preview with hardware rendering and chunk meshes.
3. Add block placement/breaking and hotbar selection.
4. Add entities, inventory, lighting and broader block models.

Protocol/data reference: [PrismarineJS minecraft-data](https://github.com/PrismarineJS/minecraft-data).
The generated state-ID/material table is reproducible with `tools/materials.py`;
its input SHA-256 is recorded in `src/materials.inc`. See `CREDITS.md`.
