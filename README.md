# Vita Block Client

A native PS Vita client targeting **Minecraft Java 1.19.4 (protocol 762)**,
starting with a small networking milestone. Intended for self-hosted vanilla
or Paper servers with `online-mode=false`. No server plugin or proxy is needed
by the protocol implementation. Verified on a physical Vita against Mojang's vanilla 1.19.4 server running on
aarch64 Linux. Paper has not been tested yet.

1.19.4 meets the requested minimum of Java 1.19. Its modern world format will
require a small chunk cache and short render distance on Vita. Other Java
protocol versions and Bedrock are outside the initial target.
PollyMC is a launcher, rather than a server implementation.

## Current milestone

- Vita application with controller-operated IPv4 server selection.
- Java handshake, server status query and measured ping/pong.
- Username login, compression negotiation and bounded zlib decompression.
- Login and Join Game detection, terrain packet counting, keepalive replies.
- Server teleport acknowledgement and absolute/relative position handling.
- Network worker keeps the Vita menu responsive during connection attempts.
- Host CLI uses the same network and protocol implementation for testing.

**This is a connection diagnostic build, not a playable game yet.** Terrain
packets are counted and discarded. There is no world renderer, movement,
inventory, entities, block interaction, sound, or Microsoft account login.
Unexpected/irrelevant play packets are discarded. Login to an online-mode server
stops with an authentication-required message.

## Build

Host (C compiler, CMake 3.20+, Python 3):

```sh
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build-host -j4
ctest --test-dir build-host --output-on-failure
```

Vita (VitaSDK):

```sh
export VITASDK=/home/frost/opt/vitasdk
export PATH="$VITASDK/bin:$PATH"
cmake -S . -B build-vita -DVITA=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-vita -j4
```

Output: `build-vita/VitaBlockClient.vpk`, title ID `VBLK00001`.
CMake fetches zlib 1.3.1 with a pinned SHA-256 hash on first configure.

## First server and Vita test

Run your own vanilla or Paper **1.19.4** server. Review and accept the server's
EULA yourself before starting it. For an isolated LAN test, configure:

```properties
online-mode=false
enforce-secure-profile=false
server-port=25565
view-distance=2
gamemode=creative
difficulty=peaceful
```

Offline mode does not authenticate usernames; keep this test server on your
trusted LAN. This client currently logs in as `VitaPlayer`.

Check it from the development machine first:

```sh
./build-host/mcprobe status SERVER_IPV4 25565
./build-host/mcprobe login SERVER_IPV4 25565 VitaPlayer 40
```

A successful login run should show `joined=1`, terrain packets, at least one
confirmed teleport, and keepalive replies during a 40-second session. The server
should log the player's join and clean disconnect. This does not test rendering.

Install the VPK through VitaShell, or extract/upload/promote through
VitaCompanion if its version supports that. The Vita address supplied for the
upcoming device test is `192.168.100.226`; this is the **device address**, not the
Minecraft server address. The client is installed on that device and has been tested successfully.

On the Vita:

- Left/right selects an address octet; up/down changes it.
- Cross queries status and ping.
- Triangle logs in as `VitaPlayer`.
- Circle disconnects; Select exits.

The selected address is saved at `ux0:data/vitablock/server.txt`. It contains an
IPv4 address and optional port separated by whitespace, e.g. `192.168.100.10 25565`.
You can edit it over FTP to set a custom port. Numeric IPv4 is supported;
hostnames, SRV records and IPv6 are not implemented. Successful server status
JSON is saved to `ux0:data/vitablock/status.json`.

## Validation and next milestones

`ctest` checks VarInt bounds and uses local TCP server fixtures to exercise
fragmented responses, ping verification, login, compression disabled/zero/256, profile properties, play ping/pong,
relative teleports, keepalive contents, disconnects, authentication-required
responses, and oversized/truncated/corrupt packets. Fixtures are independent of
Mojang's server, so they do not by themselves establish vanilla/Paper compatibility. A live
vanilla test is recorded below.

1. Completed: status/login on real Vita against a self-hosted 1.19.4 server.
2. Decode dimension NBT and chunk palettes into a bounded chunk cache; render
   nearby blocks with simple original materials and a deliberately short view.
3. Add dual-stick camera, movement, collision and server position correction.
4. Add block placement/breaking and hotbar selection.
5. Add entity rendering, inventory, lighting and broader block models.

Protocol reference: [PrismarineJS minecraft-data](https://github.com/PrismarineJS/minecraft-data),
with the 1.19.4 version metadata and packet schema. No Mojang game
assets or server binaries are included. See `CREDITS.md` for dependencies.

## Live validation — 2026-10-26

- Server: unmodified Mojang Java 1.19.4, SHA-1
  `8f3112a1049751cc472ec13e397eade5336ca7ae`, running on aarch64 Linux with
  ARM64 OpenJDK 25.0.4.1. No x86 host or emulation was needed.
- LAN endpoint: `192.168.100.190:25565`, bound to the LAN interface, offline
  mode, secure-profile enforcement disabled, creative mode, view distance 2.
- Host CLI: 40-second session, joined, 57 terrain packets, one confirmed
  teleport, two keepalive replies, clean disconnect.
- Physical Vita `192.168.100.226`: status/pong measured 56 ms; joined as
  `VitaPlayer`, compression threshold 256, 57 terrain packets, one confirmed
  teleport, two keepalive replies observed. Confirmed Cross and Triangle input
  through VitaCompanion and inspected screenshots from the actual framebuffer.
- A real-server timing race was fixed: client settings are sent after Join
  Game, rather than immediately after Login Success. The TCP fixture now checks
  that no play packet is sent before Join Game.
- The installed Vita Control plugin could not be loaded in this homebrew
  process (`0x8002d003`, missing library). Global taiHEN configuration was not
  changed. Testing uses the app's own local command file and BMP capture.

The server remains in `.deps/server-1.19.4/` (ignored by Git):

```sh
python tools/server.py status
python tools/server.py stop
python tools/server.py start
```

`start` preserves existing server properties and accepted EULA, checks the
Mojang download hash, and refuses a duplicate process. On a fresh setup, review
and record the EULA before starting; `--bind` applies only to initial properties.
`stop` checks process ownership and sends SIGTERM for Minecraft's save/shutdown
handler. World files, jars, logs, PIDs and build outputs remain outside Git.

For device diagnostics, `tools/vita.py` uses FTP on port 1337:

```sh
python tools/vita.py status
python tools/vita.py login
python tools/vita.py shot --output build-vita/shot.bmp
python tools/vita.py log
python tools/vita.py disconnect
```

Commands go through `ux0:data/vitablock/command.txt` while the app is in the
foreground. The app deletes a command after reading it. Runtime startup results
are recorded in `runtime.log`. Screenshots contain only this app's framebuffer.
