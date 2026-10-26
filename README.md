# Vita Block Client

A native PS Vita client targeting **Minecraft Java 1.19.4 (protocol 762)** for
self-hosted vanilla/Paper servers with `online-mode=false`. Verified against an
unmodified Mojang server on aarch64 Linux and a physical Vita.

Milestone 8 adds **sprint/crouch signaling, liquid movement, climbing, surface
physics, movement effects and world borders** to survival gameplay, creative
building, entity rendering and inventory/container UI.
Terrain, drops, inventory changes, damage and respawns remain server-owned.
Square opens a local inspection camera.

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
- Survival mining uses generated 1.19.4 hardness, harvest tools and tool-speed
  data, with Efficiency, Haste, Mining Fatigue, airborne and underwater penalties.
  Start/cancel/finish packets share the interaction sequence counter. A progress
  bar waits for server block updates; it never removes terrain speculatively.
- Entity targeting with block occlusion, attack cooldowns, right-click entity
  interactions, main/offhand use, food/potions, bow/shield/trident use/release and
  empty-hand block interaction. The server owns damage, projectiles and pickups.
- Server health, hunger, saturation, XP, armor, air, attributes and status effects;
  server knockback goes through collision. Death freezes movement and offers
  Cross to respawn; server immediate-respawn mode is also supported.
- Position, look and on-ground updates; server teleport correction, including
  relative corrections based on the last position sent.
- Creative flight toggling only when the server grants permission; permission
  revocation returns the player to gravity.
- Creative instant breaking and placement with a five-block targeting ray,
  exact hit faces/cursor coordinates, partial collision shapes, main-hand swings,
  shared interaction sequences and server acknowledgements. Terrain changes only
  when server block updates arrive; rejected actions leave no phantom blocks.
- Bounded entity tracking (512 entries), spawns, relative movement/rotation,
  absolute teleports, velocity, head turns, removal, player profiles, equipment,
  and metadata for visibility, pose, babies, health and dropped item stacks.
  Interpolated original cuboid models show humanoids, quadrupeds, generic entity
  silhouettes, spinning items and experience orbs. Up to 128 nearby entities
  are drawn, with player nameplates. No downloaded skins or Mojang assets.
- Nine-slot hotbar with original item icons, server selection corrections and
  full player/container inventories. Slot and cursor stacks retain their complete
  wire NBT. Pickup/place, splitting, swapping, shift transfer and dropping use
  server state IDs and wait for authoritative updates.
- Player inventory, armor/offhand and 2x2 crafting, chest grids, 3x3 crafting
  tables and furnace/blast-furnace/smoker layouts (including cooking progress).
  Generic menus expose their item slots; specialized trade/enchantment/recipe
  buttons remain to be added.
- Paged creative item catalog with all 1.19.4 items, correct stack limits and a
  clear-slot entry. Block icons reuse the original procedural textures; tools,
  armor and other items have original procedural icons. Existing slots change
  only on explicit user actions; the remote `blocknext` command retains the
  quick building palette.
- Separate local inspection camera, selectable rendering detail and diagnostics.
- Network worker separate from the UI; world mutation and rendering are
  serialized to keep chunk pointers valid.

No server plugin or proxy is required. Paper itself has not been tested yet.
PollyMC is a launcher; the target server protocol is standard Java 1.19.4.
Other Minecraft versions, Bedrock, Microsoft login and signed chat are unsupported.

## Prototype limits

Player movement is a basic implementation, rather than full vanilla physics:
there is no sprinting, crouching, swimming, climbing, full status-effect
movement, entity collision or world-border collision. Ice, fluids and special
surface behavior are not simulated. Sound, server lighting and biome tinting remain unimplemented.
Creative/peaceful is recommended for testing this prototype. Survival and creative block actions are supported; creative palette
assignment still requires creative game mode. The building palette
assigns 64 plain items using Creative Inventory Action; vanilla does not echo
accepted creative writes, but subsequent server inventory updates override them.
Targeting ignores fluids and uses collision shapes for partial blocks, with a
full-cell fallback for rendered plants. Blocks without render models cannot be
targeted. Inventory dragging, recipe-book selection, trade offers, enchantment selection
and other specialized container buttons are not implemented yet. Generic menu
slots remain accessible, with dedicated layouts for crafting and furnaces.
Inventories are bounded to 128 slots, 32 KB per encoded stack and 512 KB total
stack payloads; oversized custom items fail clearly rather than losing NBT.

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
- Cross jumps; on the death screen it requests respawn. Gravity and collision apply automatically.
- Triangle toggles flight when the server permits it. Up ascends and Cross
  descends while flying.
- L mines or attacks a targeted entity. Survival mining takes time according to
  the block and held tool; releasing L cancels. Creative breaking repeats every
  200 ms. R places/interacts, or uses a held item when aimed into air. Hold R to
  eat, draw a bow or raise a shield; release to stop use or fire a drawn bow.
  Empty hands can open containers; offhand use is selected when appropriate.
  A green
  crosshair and coordinates indicate a block within five metres. The server
  decides whether each action succeeds; the terrain waits for its response.
- D-pad Left/Right wraps through nine hotbar slots. Hold Up while walking
  forward to sprint; hold Down to crouch or descend in liquids. Crouching reduces
  movement speed, lowers the camera and prevents walking off ledges.
- Cross also swims upward or climbs ladders/vines. Sprinting underwater enables
  the swimming pose. The creative catalog assigns blocks to hotbar slots.
- Square toggles the local inspection camera. Its sticks fly/look and L/R change
  camera height. The actual player stays under the normal physics simulation.
- Select opens/closes the inventory. D-pad moves focus, Cross picks up/places,
  Triangle splits/places one, L transfers a stack and R drops one item.
- Square in the player inventory opens the creative catalog. D-pad browses,
  L/R changes pages, Triangle chooses the destination hotbar slot and Cross
  assigns the selected item. The first catalog entry clears that slot.
- Circle closes an inventory; outside inventories it disconnects.
- Select in the inspection camera toggles renderer statistics. The `debug`
  diagnostic command also toggles statistics during normal play.
- Start toggles the connection menu; the server connection and gravity continue.
- Circle disconnects. To exit, open the connection menu with Start, then Select.
  Select during normal play opens the inventory and leaves the app running.

`ux0:data/vitablock/server.txt` stores the selected numeric IPv4 and port,
e.g. `192.168.100.190 25565`. Edit it over FTP to set a custom port. Hostnames,
SRV and IPv6 are not implemented. Successful status JSON is saved to `status.json`.

The local diagnostic commands use VitaCompanion FTP, port 1337:

```sh
python tools/vita.py log
python tools/vita.py health
python tools/vita.py inventoryin
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
python tools/vita.py health
python tools/vita.py disconnect
```

Commands go through `ux0:data/vitablock/command.txt` while the app is foreground.
The helper uploads each command to a temporary file, then renames it atomically
and waits for the app to consume it. The app deletes each command after reading.
`runtime.log` records timestamped startup, initialization failure, connect/reset/
join/spawn/disconnect stages, framebuffer captures, menu exit and clean shutdown.
`health.txt` is refreshed every five seconds after frame submission, with the
connection state, position, cache and movement counters, system/vitaGL free memory,
battery level/charging/low-battery/voltage/temperature readings, GL error and
renderer queue. Retrieve it with `python tools/vita.py health` even
when a new screenshot cannot be captured. `capture.txt` records camera, input and connection counters at capture
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
and breaking were subsequently verified on the physical Vita using the
vita-control skill's Companion fallback: Control port 5000 refused connections,
so controller injection used Companion on port 1338 and screenshots used the
app's framebuffer capture. Right selected empty hotbar slot 4, Down assigned
stone, and `r1` placed stone at `(313,111,-178)`. A fresh host login decoded
state `1` there, with all 17 other inspected cells in the surrounding 3x2x3
region still air. Disconnecting and reconnecting the Vita displayed the same
stone and selected hotbar slot, confirming server persistence.

A 100 ms `l1` press then removed the stone, with exactly one break request and
sequence/acknowledgement `1/1` on that connection. Another fresh host login
confirmed state `0` at the test position and the original ground state `13`
at `(313,110,-178)`. Framebuffer captures during the test measured 58–59 FPS.
Use `l1`/`r1` with Companion: the short `r` press did not generate a placement
request. The held `r1` test generated 14 acknowledged placement attempts, but
the independent region check confirmed only the one intended stone was placed.
All synthetic inputs were released after testing. After the removal and fresh
server verification, a final Vita reconnect attempt received a Companion
connection reset; subsequent checks timed out on ports 1338, 1337 and 5000.
The user subsequently clarified that the whole Vita rebooted or powered off,
rather than only returning to LiveArea. Recorded system uptime also reset. On the next inspection the services
were reachable, the runtime log had no clean shutdown entry, and the standard
`ux0:data` crash-dump directory had no new dump from this run. No reboot or taiHEN
configuration change was attempted.

Local evidence is in `build-vita/interaction-{before,stone,r1,reloaded,broken}`
(PNG/framebuffer and text diagnostics), plus `interaction-server-placed.txt`
and `interaction-server-restored.txt`. The sanitizer run clears the host
desktop's injected `LD_PRELOAD` library so the ASan runtime loads first.


Crash follow-up: the same deployed gameplay build completed two file-command
reconnect cycles and one Companion Circle/Triangle reconnect cycle without
reproducing the failure. The diagnostic build adds connection/capture stage logs
and a five-second health snapshot; these changes improve evidence collection and
do not establish a crash fix. The diagnostic build also completed a reconnect
and remained connected through 648 movement packets with an empty mesh queue,
`gl_error=0`, approximately 55 MB free in vitaGL's VRAM pool and 171 MB in its
RAM pool. Local follow-up captures, health/runtime logs and the crash-dump
listing are under `build-vita/crash-check/`. Read-only checks found the installed
Companion modules match their saved local binaries; no kernel plugin or taiHEN
configuration was changed. The health snapshot now also records battery and
power readings. An app log cannot establish the cause of a whole-device reset;
the cause remains unresolved.

## Hardware renderer

Measured on the physical Vita (2026-10-26) against the local server: 57-60 FPS
at 960x544 with 4x MSAA in open terrain over water (~46,000 quads drawn) and in
caves, with ~2 ms of main-thread CPU per frame. Section meshing takes 1.5-2 ms
on the worker core. The previous software preview measured 16-23 FPS at 120x68.

Milestone 5: seven host suites pass normally and under ASan/UBSan. The deployed
Vita received 98 spawns, 9,098 movement/rotation/velocity updates, 160 metadata
updates, one equipment update and 97 removals during live vanilla testing. With
controls neutral, `EntityActor` flew to `(327.9,121,-179.5)`; the physical
framebuffer showed its interpolated humanoid, held-item marker and nameplate at
53.2 FPS. The capture reported one cached/drawn entity. Evidence is in
`build-vita/entities-player.png` and `entities-player.txt`.

Entity support is an initial model implementation: species-specific textures,
player skins, detailed animations, mounts/passenger rendering and combat effects
are not implemented. Equipment is tracked for all six slots; held items, helmets
and chest armor currently use simple cuboid markers. Display entities and block
entities need dedicated models. Unknown types use a generic silhouette. Overflow
spawns are counted and ignored while the cache is full.

Entity registry data is reproducible with `python tools/entities.py`; the input
hash is recorded in `src/entity_data.inc`. Entity tests cover lifecycle, relative
fixed-point movement, interpolation, teleports, metadata including NBT stacks,
equipment, player-info optional fields, malformed packet transactionality, cache
limits and model vertex bounds. An opt-in live second-player fixture is available:

```sh
./build-host/entity_actor SERVER_IPV4 25565 X Y Z
```

It logs in as `EntityActor`, equips stone, flies above terrain to the requested
point, moves nearby for 30 seconds and disconnects. Choose a clear destination;
this fixture does not perform collision avoidance during its final descent.

Milestone 6: eight host suites pass, including compressed TCP inventory/cursor
fixtures, and the suites pass under ASan/UBSan. The live vanilla probe completed
17 server-confirmed clicks: pickup, swapping, splitting, merging, crafting one
oak log into four planks, transfers into/out of a chest, and smelting raw iron
with coal into an iron ingot. The temporary chest and furnace were removed,
with interaction acknowledgement 6 and the original supporting block preserved.
The dedicated `InventoryProbe` account's test inventory is reset on each run:

```sh
./build-host/live_inventory SERVER_IPV4 25565
```

On the physical Vita, controller clicks moved 64 glass from hotbar slot 4 to
slot 5 and restored it, with four authoritative clicks and state ID 7. The
creative catalog assigned stone to an empty slot and then cleared it. The
original hotbar and selected glass slot were restored. Captures are under
`build-vita/inventory-{bag,moved,restored,catalog,assigned,final}`. Item definitions
and icon mappings are reproducible with `python tools/items.py`; hashes are
recorded in `src/item_data.inc`.

Milestone 7: nine host suites cover survival timing, cancellation, tool tiers,
Efficiency NBT, Adventure CanDestroy entries, mining penalties/effects, entity
occlusion, cooldowns, dead/spectator guards, knockback and wire-level
vitals/attributes/effects/death/respawn. A separate real vanilla survival server
verified dirt mining and pickup, damage and hunger, eating an apple, bow
projectile/arrow consumption, offhand shield use, cow combat and player death/
respawn. Final server state restored health 20 and food 20, with one death and
five teleport acknowledgements; the fixture also exercised server knockback.
Evidence is recorded in `build-host/live-survival.jsonl`.

```sh
python tools/test_server.py start
python tests/live_survival.py
python tools/test_server.py stop
```

This isolated server uses loopback ports 25566/25576 and its own flat world under
`.deps/server-survival/`, reuses the verified vanilla jar and previously accepted
EULA, and keeps its RCON password in permission-restricted ignored files. It does
not modify the normal development server or VitaPlayer save. The fixture resets
the dedicated SurvivalProbe account and creates/removes test entities/blocks.
Game data is regenerated with `python tools/gameplay.py`; source hashes are in
`src/gameplay_data.inc`. Physical survival controls still need their own check;
the complete survival flow above was tested on the host using the shared code.
The Vita VPK builds successfully, but its deployment was deferred when Companion
became unreachable. The last confirmed device deployment contains Milestone 6.

## Next milestones

1. Extend movement with sprinting, crouching, swimming, climbing, effects,
   surface behavior, vehicles and world transitions.
2. Add server lighting, broader models, sound, chat and specialized server UI.
3. Add authenticated login and additional server-version coverage.

Protocol/data reference: [PrismarineJS minecraft-data](https://github.com/PrismarineJS/minecraft-data).
The generated state-ID/material table is reproducible with `tools/materials.py`;
its input SHA-256 is recorded in `src/materials.inc`. Collision data is generated
by `tools/collisions.py`: 317 unique shapes / 699 boxes, with exact 1/32-block
coordinates and source hashes in `src/collision_data.inc`. See `CREDITS.md`.


Milestone 8 extends the fixed 20 Hz movement simulation with acceleration and
friction, sprint and crouch commands, crouch/crawl/swim dimensions and eye height,
water/lava buoyancy and flow, ladders/vines/scaffolding, ice/slime/honey/soul sand,
bubble columns, and world-border initialization, resizing and collision. Status
effects and boots influence jumping, falling, swimming and powder-snow collision.
Server sprint attribute modifiers are excluded from the base movement speed to
avoid applying sprint twice. Hunger and active item use restrict sprinting.
Player nameplate/model and action targeting now use the corrected pose heights.

Movement host tests cover speed ratios, crouch clearance/ledge safety, source
fluids and waterlogged slabs, climbing, ice inertia, slime landing, slow falling,
levitation, border interpolation and malformed border updates. Compressed TCP
checks assert sprint/crouch command encoding and suppress duplicate commands.
`python tests/live_movement.py` runs the movement and dimension-transition fixture
on the isolated vanilla server; evidence is in `build-host/live-movement.jsonl`.

The Vita package builds, but Milestones 7 and 8 still need physical-device testing
once the Vita is reachable. New controls are Up to sprint and Down to crouch;
creative flight keeps Up to ascend and Cross to descend. Vehicles, elytra flight,
sleeping controls, exact liquid exit boosts and specialized movement animation
remain unfinished. These changes do not resolve the deferred console reboot.
