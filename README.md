# Halo: Combat Evolved for Linux, Windows, Android and the web

[![Join our Discord](https://invidget.switchblade.xyz/9gqcHyr5km)](https://discord.gg/9gqcHyr5km)

This project is a port of the Halo: Combat Evolved decompilation to Linux,
Windows, Android and web browsers. The decompilation is of the Xbox build 2342
(`cachebeta.exe`, SHA-256
`4cc87b45f721270392a96f1674ed2b5cd4a7bb4355faeab4531d1cf1884d9520`).

<img width="1289" height="995" alt="The game on Linux" src="https://github.com/user-attachments/assets/0d3ad50f-f8b8-46cf-aef8-e3661da2a7d7" />

The port starts from the decompilation of [bnunu/halo-1](https://github.com/bnunu/halo-1).
That project is a fork of [punpckhdq/halo](https://github.com/punpckhdq/halo).

## Quake Edition movement

Controls Setup > Show: Movement has a Physics spinner beneath Crouch:
**Halo CE** (default) or **Quake 1**. Left/right and the arrow buttons cycle
the choices like Show. OK applies and saves the choice; Cancel discards it;
Defaults restores Halo CE. It controls movement, jump/gravity changes and
blast momentum (`game.quake_physics`, default false). Self damage uses the
same rocket/frag formula independently of the two selectors.
In network games each player chooses their own physics. Halo CE and Quake 1
players can share a match. The host confirms each player's active style and
shares it with clients, including late joiners. Changes while playing apply
on the next respawn; the current life keeps its style. Split-screen players
have separate choices, selected by the local player opening Controls
(`game.quake_physics_player2`, `player3`, `player4` for the other slots).
Controls Setup > Movement also has **Rocket/Grenade** directly below Physics:
**Halo CE** by default, or **Quake 1** (`game.quake_weapons`). This independently
selects rocket/frag launch, flight, grenade bounce/fuse and blast range.
All Quake distances, speeds and accelerations share 80 Quake units per Halo
world unit, matching the standing collider's width and height. Rocket/grenade
origins are 40/56 and 24/56 of the current body height, following the crouch blend; rockets add
8 scaled units forward. Grenades live 2.5 seconds from
release; rockets expire after 5 seconds without an explosion. Nearby
geometry clips the spawn offset so shots cannot originate beyond a wall. Both
choices retain the same rocket/frag self damage, with Halo shields. Normal
ammunition and animations remain.
Network players choose independently, with changes applied on respawn.

Quake 1 mode uses QuakeWorld's movement order and slide/step rules, with one
conversion: 80 Quake units per Halo world unit. Normal running is 320 Quake
units/s (4 world units/s), jump impulse is 270 units/s (3.375 world units/s)
and gravity is 800 units/s squared (10 world units/s squared). The old
CE-jump-height compensation is removed. Movement runs at 90 Hz, three
substeps per 30 Hz world tick. Input and network state still update at 30 Hz.

The solver includes directional acceleration, ground/edge friction, the
30-unit air directional cap, four-bump/five-plane sliding, crease clipping,
18-unit stairs and one-unit ground probes with a 0.7 walkable normal limit.
Jump is checked before friction, including after landing within a tick.
The release latch prevents automatic repeated jumps after a successful jump.
Ground probes resolve incoming velocity when snapping the Halo capsule onto
support, so buffered jumps do not inherit downward fall velocity. Incoming
landing speed is retained for Halo fall damage even on a buffered jump. The former
approximate NetQuake wall-friction hook is removed. Velocity has Quake's
2000-unit/s per-axis safety limit, not a cap at normal running speed.

Halo's capsule dimensions, crouch transitions, airborne crouch displacement,
stand-clearance checks, tagged ladder handling and crouched speed ratios remain. Plasma-rifle injury
stun retains Halo steering, turning and jump penalties; Halo speed modifiers
and powerups remain. AI, vehicles, dead bodies, flying/climbing units,
attached units, cinematics and scripted movement keep their original paths.

Rocket/grenade blast momentum uses raw Quake splash points times 8 before
armor/shields. Splash starts at 120 points, subtracts half the distance,
queries a 160-unit radius, and halves self damage. Falloff uses the Quake
box center (28 units above feet); direction uses its origin (24 units above
feet). All distances and impulses use the same world scale. One full Halo
body-health bar represents 100 Quake points; Halo shields remain. The Rocket/Grenade selector preserves Halo weapon flight/fuse/bounce/range by
default, or selects the independently implemented Quake projectile parameters
and five-ray blast obstruction check. Damage to other targets follows Quake
splash points in that weapon mode, while Halo shields remain. Blast momentum is independent of the
damage-effect acceleration magnitude and eventual health loss.

With display.fov = 0, Quake players get Quake's 90-degree horizontal view at
4:3 (73.74 degrees vertically, widened for other aspect ratios). Explicit
FOV settings, Halo scope zoom, eye/crouch camera heights, bob and weapon
animations remain. CE players retain their stock default view.

The implementation is independent C code in `port/linux/game/quake_movement.c`.
Behavioral references are id Software's [movement equations](https://github.com/id-Software/Quake/blob/master/WinQuake/sv_user.c),
[QuakeWorld player movement](https://github.com/id-Software/Quake/blob/master/QW/client/pmove.c),
and [damage logic](https://github.com/id-Software/Quake-Tools/blob/master/qcc/v101qc/combat.qc).
No Quake source is vendored or copied into this repository.

The motion pipeline is in `port/linux/game/quake_motion.c`; Halo collision
features supply traces, capsule overlap repair and dynamic object contacts.
Elevator bookkeeping remains Halo's. Quake-mode falls deal a fixed five points
when grounded after descending faster than 650 Quake units/s; ordinary-water
landings are exempt. Halo shields and immunities remain. Quake mode does not
apply Halo's midair fall-speed kill rule. This is a compatible
Quake movement mode, not an exact reproduction of Quake box-hull corner,
water or framerate-dependent input quirks. Live gameplay tuning is ongoing.

Network version 39 is required for this physics implementation. Each player can
select movement and rocket/frag behavior independently; the host confirms the
active choices and retains authority over velocity and damage. The release
contains production gameplay code and standard runtime files.

## Download

GitHub Actions builds the game for each commit. These links download the
builds of the latest release:

| Platform | Release | Debug |
| --- | --- | --- |
| Linux | [halo-linux-release.zip](https://github.com/XplosivBolts/OpenCE-Quake-Edition/releases/latest/download/halo-linux-release.zip) | [halo-linux-debug.zip](https://github.com/XplosivBolts/OpenCE-Quake-Edition/releases/latest/download/halo-linux-debug.zip) |
| Windows | [halo-windows-release.zip](https://github.com/XplosivBolts/OpenCE-Quake-Edition/releases/latest/download/halo-windows-release.zip) | [halo-windows-debug.zip](https://github.com/XplosivBolts/OpenCE-Quake-Edition/releases/latest/download/halo-windows-debug.zip) |
| Android | [halo-android-release.zip](https://github.com/XplosivBolts/OpenCE-Quake-Edition/releases/latest/download/halo-android-release.zip) | [halo-android-debug.zip](https://github.com/XplosivBolts/OpenCE-Quake-Edition/releases/latest/download/halo-android-debug.zip) |
| Web | [halocombatevolved.com](https://halocombatevolved.com/) (or [halo-web-release.zip](https://github.com/XplosivBolts/OpenCE-Quake-Edition/releases/latest/download/halo-web-release.zip) for your own server) | [halo-web-debug.zip](https://github.com/XplosivBolts/OpenCE-Quake-Edition/releases/latest/download/halo-web-debug.zip) |

Use the release build to play. The debug build stops at the first failed
assertion and writes it to the log. Use the debug build to find and report
problems.

The game updates itself. At start-up it looks for a newer release, and asks
if you want to install it. Refer to "Updates" in
[port/linux/README.md](port/linux/README.md#updates). The web site offers
each new build when it is out.

Each build of the `main` branch that passes on all four platforms is a new
release, and the web site then plays it. The [Releases](https://github.com/OpenCommunityEdition/OpenCE/releases)
page keeps the last five releases. If the latest build has a problem, get
an older build from that page.

## Game data

The port does not include the game data. Download an Xbox disc image
(`.xiso` or `.iso`) of Halo: Combat Evolved. All versions of the game
operate. The maps of the European (PAL) version were made for a slower
console. The port changes them to play as the North American (NTSC) maps do,
so players of the two versions can play together.

1. Start the game.
2. At the first start, the game asks for the disc image. Select it.
3. The game extracts the `maps/` folder. Then the game starts.

On Linux and Windows, the game puts `maps/` next to the executable. On
Android, copy the disc image to the phone first. The app puts `maps/` in its
data folder. Refer to [port/android/README.md](port/android/README.md). On
the web, the page copies `maps/` out of the disc image into the browser's
storage for the site; nothing is uploaded. Refer to
[port/web/README.md](port/web/README.md).

## Platforms

Each platform has its own instructions:

| Platform | Instructions |
| --- | --- |
| Linux (32-bit x86 executable, OpenGL 4.5, SDL3) | [port/linux/README.md](port/linux/README.md) |
| Windows (32-bit x86 executable, OpenGL 4.5, SDL3) | [port/windows/README.md](port/windows/README.md) |
| Android (arm64 app, OpenGL ES 3, SDL3) | [port/android/README.md](port/android/README.md) |
| Web (WebAssembly, WebGL 2, SDL3; desktop Chrome, Edge and Firefox) | [port/web/README.md](port/web/README.md) |

The Linux README also gives the controls, the settings and the multiplayer
functions. These are almost the same on all platforms.

## Multiplayer

The game can play system link games on a local network and on the internet:

- A system link game can have up to 128 players on up to 128 machines.
- Linux, Windows and Android machines and browsers can play in the same
  game. On a local network, the native builds play system link; browsers
  play each other in rooms, over WebRTC
  ([port/web/README.md](port/web/README.md)).
- An invite link, or the server browser, lets a machine join a game on the
  internet, browsers too: a native build takes a browser's WebRTC
  ([port/linux/README.md](port/linux/README.md), "Browsers"). No server of
  this project is necessary.
- The netcode is new. Each machine moves its own player at once,
  and the host makes the decisions for the game. Refer to
  [port/linux/NETCODE.md](port/linux/NETCODE.md).

## Build the game

You do not need the Xbox SDK. The port supplies the SDK declarations that
the game uses. Refer to [port/include/xdk](port/include/xdk/README.md).

To build the game:

1. Install Python and [ninja](https://ninja-build.org/).
2. Install the tools for your platform. Refer to the README for the
   platform.
3. In the root folder of the repository, enter `python configure.py`.
4. Enter `ninja` with the target for the platform:

| Target | Result |
| --- | --- |
| `ninja linux` | `build/linux/halo` |
| `ninja windows` (on Windows) | `build/windows/halo.exe` and `SDL3.dll` |
| `ninja android_apk` | `port/android/app/build/outputs/apk/debug/app-debug.apk` |
| `ninja web` | `build/web/site/`, the web site (`python tools/web_serve.py` serves it) |

If you enter `ninja` without a target, ninja builds the game for the
computer that you use.

`tools/ci_build.py` makes the same builds as GitHub Actions. For example,
enter `python tools/ci_build.py linux release`.

### Build options

Give these options to `configure.py`:

| Option | Result |
| --- | --- |
| (none) | A debug build. A failed assertion stops the game. |
| `--release` | A release build. The game does not examine assertions, as in the retail game. |
| `--portable` | The Linux and Windows builds operate on all x86-64 processors. The Linux build also operates on older distributions and on SteamOS: refer to "Portable build" in [port/linux/README.md](port/linux/README.md#portable-build). Use this option for builds that you give to other persons. |
| `--lto=thin`, `--lto=off` | Less link-time optimization. The link is faster. |
| `--pgo=off` | No profile-guided optimization. |
| `--pgo=train` | Records a new optimization profile. Refer to "Optimization profiles". |
| `--profile` | A profiling build. It records CPU times. Refer to "Profiling builds". |
| `--web-emcc PATH` | The Emscripten `emcc` for `ninja web` (else the one on the PATH, or `~/emsdk`'s). |

Without `--portable`, the Linux and Windows builds use all the instructions
of the processor that builds them (`-march=native`). Such a build does not
always start on a different computer.

### Optimization profiles

The builds use profiles of the game to optimize the code:

- `pgo/halo_linux.profdata` for Linux and Android.
- `pgo/halo_windows.profdata` for Windows.

The profiles need clang 22 or later. With an older clang, the builds do not
use the profiles.

To record a new profile:

1. Delete the profile.
2. Enter `python configure.py --pgo=train`.
3. Enter `ninja linux` or `ninja windows`.

The build then plays the main menu and the first minute of each campaign
level. This procedure continues for approximately 15 minutes. The game
data must be in `assets/`.

### Profiling builds

A profiling build records where the game spends its time.

1. Enter `python configure.py --profile`, then build as usual.
2. Start a recording. Enter `profile_record` in the console (or the telnet
   console), or set `debug.profile_record = true` in `config.toml`. To
   record each game of a session, also set
   `debug.profile_record_when = "game"`.
3. Stop the recording. Enter `profile_stop`, load another map, or quit.

The game writes numbered `.part<n>.json` files under `profiles/` in its data root
and logs their paths. Parts are retained after the recording stops. Logs go to
standard error; a Windows release build without standard error writes them to
`halo.log` next to `halo.exe`.

`tools/net_report.py` is a helper for developers. Enter
`python tools/net_report.py <recording.part1.json>` to read a recording
as tables. The command also writes `<recording>.summary.txt`, a short text
file.

| Command | Result |
| --- | --- |
| `profile_record [seconds]` | Starts a recording. With a number of seconds, the recording stops after that time. |
| `profile_stop` | Stops the recording. |

`debug.profile_memory` sets the memory of a recording in MB (default 256,
from 4 to 1024). A recording has no length limit:
the game writes a part each time the memory is full. Refer to "Settings" in
[port/linux/README.md](port/linux/README.md).

On Android, the files are in `/sdcard/Android/data/com.halo.decomp/files/profiles/`.
Enter `adb pull` to copy the recording parts to a computer.

A profiling build plays with normal builds. It does not use
`--pgo=train`.
