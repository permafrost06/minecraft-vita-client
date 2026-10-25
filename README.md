# Vita Block Client

A native PS Vita client targeting **Minecraft Java 1.19.4 (protocol 762)** for
self-hosted vanilla/Paper servers with `online-mode=false`. Verified against an
unmodified Mojang server on aarch64 Linux and a physical Vita.

Milestone 4 adds **creative block breaking, hotbar selection and placement**,
with server-confirmed terrain updates. Walking, gravity, jumping, collision and
permitted creative flight remain synchronized. Square opens a local inspection camera.

## Implemented

- Server status and measured ping; username login, compression, keepalives,
  play ping/pong, and spawn teleport acknowledgement.
- Dimension registry NBT, including the modern Overworld's `min_y=-64` and
  384-block height. Dimension selection and respawn clear the terrain cache.
- Single-value, indirect and direct chunk palettes, including packed values
  that do not cross 64-bit word boundaries. Biome palettes are consumed.
- Bounded 9 by 9 chunk cache, chunk unloads, single and multi-block updates.
  Maximum dimension height is 512 blocks; larger/custom registries fail clearly.
- Hardware rendering on Vita (vitaGL) at the native 960x544 with 4x MSAA:
  per-section chunk meshes with hidden-face culling, ambient occlusion, a cheap
  heightmap skylight (caves and overhangs darken), translucent water, cutout
  leaves/glass/plants, partial shapes (slabs, stairs, fences, beds...) from the
  collision boxes, frustum culling, distance fog and a sky gradient. Meshing runs
  on a separate core; the camera is interpolated between 20 Hz physics ticks.
- Original procedural 16px block textures (474 tiles) generated at startup from
  recipes in `tools/blockinfo.py`. The host keeps the older software raycaster
  for `mcprobe view` and tests.
- A fixed 20 Hz player simulation: walking, gravity, jumping, landing, 0.6-block
  step-up, and collision against generated block-state shapes (including slabs,
  stairs and fences). Missing chunk columns pause movement instead of allowing
  the player to fall into unloaded terrain.
- Position, look and on-ground updates; server teleport correction, including
  relative corrections based on the last position sent.
- Creative flight toggling only when the server grants permission; permission
  revocation returns the player to gravity.
- Creative instant breaking and placement with a five-block targeting ray,
  exact hit faces/cursor coordinates, partial collision shapes, main-hand swings,
  shared interaction sequences and server acknowledgements. Terrain changes only
  when server block updates arrive; rejected actions leave no phantom blocks.
- Nine-slot hotbar, server selection corrections, full player inventory and slot
  updates (including NBT-bearing stacks). Down cycles a small building palette in
  the selected creative slot: stone, dirt, grass, cobble, planks, oak log, glass,
  bricks and sand. Existing slots are preserved until explicitly replaced.
- Separate local inspection camera, selectable rendering detail and diagnostics.
- Network worker separate from the UI; world mutation and rendering are
  serialized to keep chunk pointers valid.

No server plugin or proxy is required. Paper itself has not been tested yet.
PollyMC is a launcher; the target server protocol is standard Java 1.19.4.
Other Minecraft versions, Bedrock, Microsoft login and signed chat are unsupported.

## Prototype limits

Player movement is a basic implementation, rather than full vanilla physics:
there is no sprinting, crouching, swimming, climbing, knockback, status-effect
movement, entity collision or world-border collision. Ice, fluids and special
surface behavior are not simulated. Full inventory/container UI, survival digging, health/death controls, entities,
sound, server lighting and biome tinting remain unimplemented.
Creative/peaceful is recommended for testing this prototype. Block actions require
creative game mode; hotbar selection works in other modes. The building palette
assigns 64 plain items using Creative Inventory Action; vanilla does not echo
accepted creative writes, but subsequent server inventory updates override them.
Targeting ignores fluids and uses collision shapes for partial blocks, with a
full-cell fallback for rendered plants. Blocks without render models cannot be
targeted. Interactive containers can open on the server, but have no client UI yet.

Partial blocks are drawn from their collision shapes, so blocks without
collision (signs, buttons, banners, heads, wall torches, vines) are not drawn,
and log/pillar rotation is honoured but other orientations are not. Waterlogged
stairs/slabs show no water. Lighting is approximate: there is no block light
from torches or lava beyond the lava itself. No Mojang textures or game assets
are bundled. The inspection camera can move through blocks, but its
motion is local and does not move the server player or request distant chunks.

The renderer needs `libshacccg.suprx` on the Vita (`ur0:data/libshacccg.suprx`,
as for other vitaGL homebrew) because shaders are compiled at startup. The fog
distance follows the server's view distance, capped by the 9 by 9 chunk cache.

## Build and tests

Host prerequisites: C compiler, CMake 3.20+, Python 3.

```sh
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host -j4
ctest --test-dir build-host --output-on-failure
```

Vita prerequisites: VitaSDK with vitaGL (`vdpm install vitaGL`, which pulls in
vitaShaRK, libmathneon, SceShaccCgExt and taihen).

```sh
export VITASDK=/home/frost/opt/vitasdk
export PATH="$VITASDK/bin:$PATH"
cmake -S . -B build-vita -DVITA=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-vita -j4
```

Output: `build-vita/VitaBlockClient.vpk`, title ID `VBLK00001`.
`python tools/deploy.py` quits the app through VitaCompanion, uploads
`eboot.bin` to a temporary file, verifies it, keeps the previous binary in
`.deps/device-backups/`, renames it into place and relaunches.
CMake fetches zlib 1.3.1 with a pinned SHA-256 hash on first configure.

Tests cover VarInt bounds; fragmented TCP, compression and login; dimension
registries; palette widths and word padding; malformed/truncated chunks;
negative coordinates; block updates; cache lifetime; and renderer output bounds.
Mesh tests cover face culling, quad winding, missing-neighbor edges, water
height and culling, ambient occlusion, heightmaps, plants and the per-section
quad limit; `build-host/mesh_tests atlas.ppm` also writes the texture atlas.
Player tests cover gravity, jump height/landing, walls, ceilings, slab steps,
flight, streaming boundaries and yaw/strafe direction. Packet fixtures cover
movement fields, permitted flight, permission revocation and relative server
corrections. A rendered west-side wall regression checks the horizontal camera
basis. Player/world/protocol tests also pass under AddressSanitizer and UBSan.

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
./build-host/mcprobe walk SERVER_IPV4 25565 MoveProbe 8
python tools/playerdata.py MoveProbe
```

`view` reports the dimension, cache size and render time and saves
`build-host/server-view.ppm`. The same decoder and renderer run on Vita.
`walk` runs the shared player simulation, walks forward and attempts a jump.
After it disconnects, `playerdata.py` independently reads the local server's
saved player position, orientation and grounded state.

## Vita controls

The development Vita is `192.168.100.226`. This is the device address, not the
server address. The app is installed and its executable uploads were read back
and compared byte for byte. No global taiHEN configuration was changed.

In the connection menu:

- Left/right selects an address octet; up/down changes it.
- Cross queries status and ping; Triangle logs in as `VitaPlayer`.
- Circle disconnects; Select exits.

After joining, terrain appears automatically:

- Left stick walks/strafe-moves; right stick looks around.
- Cross jumps. Gravity and collision apply automatically.
- Triangle toggles flight when the server permits it. Up ascends and Cross
  descends while flying.
- L breaks and R places the selected block. Hold to repeat every 200 ms. A green
  crosshair and coordinates indicate a block within five metres. The server
  decides whether each action succeeds; the terrain waits for its response.
- D-pad Left/Right wraps through nine hotbar slots. Down cycles building blocks
  in the selected slot (creative only); it replaces that slot's existing stack.
- Square toggles the local inspection camera. Its sticks fly/look and L/R change
  camera height. The actual player stays under the normal physics simulation.
- Select toggles renderer statistics (sections, quads, mesh queue, CPU time).
- Start toggles the connection menu; the server connection and gravity continue.
- Circle disconnects. To exit, open the connection menu with Start, then Select.
  Select during play toggles statistics and leaves the app running.

`ux0:data/vitablock/server.txt` stores the selected numeric IPv4 and port,
e.g. `192.168.100.190 25565`. Edit it over FTP to set a custom port. Hostnames,
SRV and IPv6 are not implemented. Successful status JSON is saved to `status.json`.

The local diagnostic commands use VitaCompanion FTP, port 1337:

```sh
python tools/vita.py login
python tools/vita.py shot --output build-vita/shot.bmp
python tools/vita.py menu
python tools/vita.py jump
python tools/vita.py fly
python tools/vita.py inspect
python tools/vita.py blocknext
python tools/vita.py slotnext
python tools/vita.py break
python tools/vita.py place
python tools/vita.py reset
python tools/vita.py log
python tools/vita.py disconnect
```

Commands go through `ux0:data/vitablock/command.txt` while the app is foreground.
The helper uploads each command to a temporary file, then renames it atomically
and waits for the app to consume it. The app deletes each command after reading.
`runtime.log` records timestamped startup, initialization failure, menu exit and
clean shutdown events. `capture.txt` records camera, input and connection counters at capture
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

The user confirmed they were using the controls during the earlier idle-camera
check, so that movement was not evidence of stick drift. Neutral stick samples
are covered by automated deadzone tests.

Milestone 3: the host `MoveProbe` test sent 158 movement packets over eight
seconds, fell from Y=145 to terrain at Y=136, and walked from Z=-39.5 to -34.3,
stopping at an obstacle. Mojang's saved player data independently confirmed
`Pos=[102.5,136.0,-34.3]` and `OnGround=1`; no server teleport corrections occurred.

On the physical Vita, the user confirmed that right-stick turning and jumping
both work. A capture showed 1,627 movement packets, five keepalive replies,
one initial teleport, and matching player/sent positions at
`[84.058,128.0,-47.406]`, grounded and with no missing-chunk wait. The view ran
at 18.7 FPS in that capture. No additional server position corrections occurred.
This validates that movement is synchronized, rather than only changing the
inspection camera.


Streaming verification: a physical Vita flight covered 125 metres across eight
chunk boundaries, received 126 chunks in total (69 after the initial region),
unloaded 72 old chunks, and retained the chunk under the player. It completed
with two keepalives and only the initial teleport. The bounded cache stays near
57 chunks while its received total increases; the HUD now shows **cached /
received** separately. Chunk center, view distance, unloads and memory usage
are recorded in `capture.txt`.

A cache regression moves a full window across positive and negative coordinates,
including new arrivals before delayed old unloads. It verifies current coverage
and bounded allocation throughout. The server's initial view distance is also
read from Join Game; later view-center and distance packets update diagnostics.

To repeat the automated physical streaming check:

```sh
python tools/vita.py streamtest
```

It temporarily logs in as `VitaStream`, flies a scripted route using permitted
creative-flight movement, writes `ux0:data/vitablock/stream-test.txt`, then
reconnects `VitaPlayer`. Allow roughly 35 seconds; there is a 90-second timeout.
The route tests packet streaming and cache replacement, rather than collision.
The host equivalent is `mcprobe stream SERVER_IPV4 25565 StreamProbe 35`.

A later traversal reproduced the missing-terrain disconnect at chunk `(25,-30)`.
Its section population hint was 4,107, which the old decoder incorrectly capped
at 4,096. The hint is not an array size; decoding still reads exactly 4,096 block
states and validates all palette and packed-data lengths. The invalid population
cap was removed. The exact captured packet fails on the prior decoder and
succeeds after the fix; regression cases cover 4,107 and 8,192 hints.
Host login at the player's affected coordinates now progresses beyond the
previous failing chunk instead of disconnecting. The deployed Vita fix then
loaded 45 chunks in that region and stayed connected through two keepalives,
with the same player location and no palette rejection.

Reported returns to LiveArea overlapped remote deployment stops. No separate
unexpected process crash was reproduced and no new crash dump was found. Select now
exits only from the connection menu; timestamped clean-exit records distinguish
that path from an unexpected termination. Deployment now uploads to a temporary
executable, verifies it, and preserves the previous binary before renaming it.

The prototype server view distance remains two. The hardware renderer fits fog
to that distance; distant terrain is limited even while streaming is working.

Milestone 4: the opt-in host compatibility probe placed stone at `(87,136,-30)`
on the local unmodified vanilla 1.19.4 server, observed the inbound block update
and sequence acknowledgement 1, then broke the new block and observed air plus
acknowledgement 2. The tested space was restored. Run it with a dedicated creative
player (it temporarily assigns slot 3 and moves that player above nearby ground):

```sh
./build-host/live_interaction SERVER_IPV4 25565
```

Automated tests additionally check six targeting faces, five-block reach, slabs,
negative coordinates, unloaded columns, NBT-bearing inventory updates, both slot
numbering schemes, server-held-slot corrections, creative item packets, exact
break/place fields, acknowledgements, malformed stacks and game-mode revocation.
All six suites pass on the host and under AddressSanitizer/UBSan. The Vita build
was deployed with binary read-back verification. Its capture showed the hotbar,
58.5 FPS, and two break requests with sequence/acknowledgement `2/2`. Placement
has been verified by the host vanilla probe; physical placement remains to be
confirmed. The sanitizer run clears the host desktop's injected `LD_PRELOAD`
library so the ASan runtime loads first.

## Hardware renderer

Measured on the physical Vita (2026-10-26) against the local server: 57-60 FPS
at 960x544 with 4x MSAA in open terrain over water (~46,000 quads drawn) and in
caves, with ~2 ms of main-thread CPU per frame. Section meshing takes 1.5-2 ms
on the worker core. The previous software preview measured 16-23 FPS at 120x68.

## Next milestones

1. Add entities, full inventory/container UI, lighting and broader block models.
2. Extend movement with swimming, climbing, surface effects and survival UI.

Protocol/data reference: [PrismarineJS minecraft-data](https://github.com/PrismarineJS/minecraft-data).
The generated state-ID/material table is reproducible with `tools/materials.py`;
its input SHA-256 is recorded in `src/materials.inc`. Collision data is generated
by `tools/collisions.py`: 317 unique shapes / 699 boxes, with exact 1/32-block
coordinates and source hashes in `src/collision_data.inc`. See `CREDITS.md`.
