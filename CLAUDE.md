# Terrax

Terrax is a multiplayer voxel sandbox game written in C++17 on a custom OpenGL
engine. It features a procedurally generated voxel world with biomes, block
building/breaking, procedural houses and towns, NPCs (villagers, guards,
bandits), wildlife, vehicles/ferries, a day–night cycle, and a third-person
player. It is client–server: one process runs an authoritative server (embedded
on a background thread for hosting/singleplayer, or standalone via `--server`)
while every player runs a client.

## Tech stack

- **Language:** C++17
- **Graphics:** custom OpenGL 3.x renderer, GLSL shaders (`shaders/`)
- **Windowing/input:** GLFW
- **Math:** GLM
- **Networking:** Boost.Asio (TCP)
- **UI:** Dear ImGui (vendored in `third_party/imgui/`)
- **Dependencies:** managed by vcpkg (`vcpkg.json` → `glfw3`, `glm`, `boost-asio`)

## Building

The primary, supported build is **Windows / MSVC** via `Terrax.vcxproj`
(`Terrax.sln`). A `Makefile` also builds it with g++ on Linux.

### Windows (primary)

Open `Terrax.sln` in Visual Studio, or build from the command line:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
    Terrax.vcxproj /p:Configuration=Debug /p:Platform=x64 /m
```

- Configurations: `Debug|x64`, `Release|x64`. Toolset `v145`, C++17.
- Output: `build\windows\<Configuration>\Terrax.exe`.
- **Run MSBuild from PowerShell or cmd, not Git Bash.** Git Bash rewrites
  `/p:...` switches into file paths and the build fails with `MSB1008`.

### Linux

```bash
make            # build  -> ./terrax
make run        # build + run
make clean
```

## Running

- `Terrax.exe` (no args) launches the game with menus (host, join, singleplayer,
  character/house editors).
- `Terrax.exe --server` runs a headless dedicated server.
- Default server port: `12345` (`DEFAULT_SERVER_PORT`).

## Launcher

`launcher/` holds a **separate** standalone app, `TerraxLauncher.exe`, that checks
the GitHub Releases page, installs/updates the game to the latest version, and
launches it — with an animated voxel cover-art scene and a frosted "liquid glass"
theme. It is **Windows-only** (WinHTTP for the GitHub API, DWM for the acrylic
window) and reuses the same vendored ImGui + vcpkg GLFW as the game.

- It is its own MSBuild project, **`TerraxLauncher.vcxproj`** (also in
  `Terrax.sln`), because it has its own `main()` and must not collide with the
  game's. Build it separately:
  ```powershell
  MSBuild.exe TerraxLauncher.vcxproj /p:Configuration=Debug /p:Platform=x64 /m
  ```
  Output: `build\windows\<Configuration>\launcher\TerraxLauncher.exe`.
- New launcher sources go in `launcher/` and are registered in
  `TerraxLauncher.vcxproj` (+ `.filters`) — **not** the game's project or the
  `Makefile` (the launcher does not build on Linux).
- Installs to `%LOCALAPPDATA%\Terrax\current\`; records the tag in
  `installed_version.txt`; logs to `launcher.log`. `--install` does a silent
  (no-window) install. `TERRAX_REPO_OWNER`/`TERRAX_REPO_NAME` env vars override
  the target repo. **The repo/releases must be public** — unauthenticated GitHub
  calls 404 on a private repo. See `launcher/README.md`.

## Testing

A small headless test suite lives in `tests/` and runs without GL/GLFW:

```bash
make test       # builds terrax_tests, runs it
```

- Framework: `tests/terrax_test.h` — `TEST_CASE(name) { ... }`, `CHECK(expr)`,
  `CHECK_EQ(a,b)`, `CHECK_NE(a,b)`. `run_all_tests()` returns the failure count.
- The test build defines `TERRAX_TESTING` and links `tests/gl_stub.cpp` so
  GL-dependent code links headlessly.
- Add a new test file to `TEST_SRCS` in the `Makefile`.

There is no automated GUI test. Gameplay/visual changes are verified by building
and running the game (in-game **F3** opens a debug overlay with FPS, entity
counts, and server stats).

## Repository layout

```
src/            engine + game implementation (.cpp)
include/         public headers (.h)   — exception: src/noise.h
launcher/        standalone GitHub-release launcher (own exe, Windows-only)
shaders/         GLSL shader pairs (chunk, water, sky, char, shadow, glass)
tests/           headless test harness
third_party/     vendored ImGui
vcpkg_installed/ vcpkg-resolved dependencies
build/           build output (build/windows/ for MSBuild)
Terrax.vcxproj          MSBuild project (game; Windows, primary)
TerraxLauncher.vcxproj  MSBuild project (launcher; Windows)
Makefile         g++ build (Linux)
vcpkg.json       dependency manifest
```

## Architecture

### Game loop & states (`src/main.cpp`)

`main()` creates the GLFW window, loads GL, builds an `AppContext`, then loops:
poll input → ImGui new-frame → `switch (ctx.state)` → render. `GameState` is
`MainMenu / SettingsMenu / JoinMenu / CharacterEditor / HouseEditor / Playing /
Paused`. In-game frames call `updateGameplay()` then `renderer.renderWorld()`.

`AppContext` (`include/app_context.h`) is the single mutable hub passed
everywhere — holds state, the local `World`, `Camera`, `ObjectManager`,
`NetworkClient*`, settings, and per-frame gameplay flags.

### World (`src/world.cpp`)

Chunk-based voxel terrain, procedurally generated (noise in `src/noise.h`),
with biomes. Blocks are the `BlockType` enum. The client keeps its own `World`
in `AppContext`; the server thread keeps a separate authoritative `World`.

### Entities — the GameObject system (`include/game_object.h`)

Dynamic things layered on the voxel terrain are `GameObject`s:

- `ObjectKind { Player, Prop, Vehicle, Door, NPC, Animal, Loot, Projectile }`.
- `GameObject` is abstract: `update(dt, world)`, `draw(modelLoc)`,
  `getAABB(mn, mx)`. It carries `position`, `yaw`, `id`, `dead`, and a
  `baseMatrix(scale)` helper.

### Polymorphic hierarchies — the design rule for content

Anything that exists as multiple variants (items, projectiles, future
spell effects) goes through a polymorphic base class, **never** a giant
`switch` on a type enum at the call site. New variants are added by
subclassing — callers stay the same.

The existing hierarchies that follow this rule:

- **`Item`** (`include/items.h`) — abstract base. `ClothingItem` and
  `WeaponItem` inherit. `WeaponItem` itself has subclasses for weapons
  that need unique behaviour (`StaffItem` overrides `onPrimaryAttack` to
  fire `MagicBoltProjectile` instead of a melee swing). Concrete items
  are built via the **`createWeaponItem(name, type)`** factory in
  `items.cpp` so call sites don't hardcode subclasses.
- **`Projectile`** (`include/projectile.h`) — abstract base extending
  `GameObject`. Concrete subclasses: `ArrowProjectile` (gravity, drag,
  sticks in terrain), `MagicBoltProjectile` (gravity=0, trail particles,
  faster). New projectile types add a subclass; everything else (spawn,
  physics, collision sweep) stays generic.
- **`CharacterRig`** (`include/voxel_model.h`) — `BipedalRig` and
  `QuadrupedRig` inherit. Rigs drive animal/npc/player animation via
  the virtual `update(dt, velocity)`.

**Rules of thumb for new content:**

1. New weapon ability → new `WeaponItem` subclass, override the
   relevant virtual method (`onPrimaryAttack`, `isInstantRanged`, etc).
2. New projectile type → new `Projectile` subclass; override
   `onHitGround` / `onHitNpc` / `update` as needed.
3. New item category that isn't a weapon/clothing → new `Item`
   subclass alongside the existing two. Implement `buildVoxelVolume`.
4. Avoid `switch (weapon.getType()) { case Sword: ...; case Bow: ... }`
   in call sites. If you find yourself writing one, push the behaviour
   into a virtual method.
- `ObjectManager` owns all of them as `unique_ptr<GameObject>`. The renderer
  iterates it kind-agnostically. The local player is the one exception — held
  directly on `AppContext`.
- `id == 0` means local-only; `id > 0` is a networked entity. Networked id
  ranges are kept disjoint per type (e.g. ferries `1000000+`, villagers
  `0x40000000+`, bandits `0x80000000+`, animals `0xC0000000+`).

### Voxel character models (`src/voxel_model.cpp`)

Characters/creatures are voxel rigs, not block terrain:

- `VoxelVolume` — a 3D grid of `Voxel{r,g,b,a}` that bakes itself into a GL mesh.
- `CharacterNode` — a scene-graph node with an optional `VoxelVolume*`, a local
  transform (`localPos/localRot/scale`) and a `pivot`. Drawn so the voxel at
  `pivot` lands on the node origin; children inherit the node transform.
- `CharacterRig` → `BipedalRig` (players/NPCs) and `QuadrupedRig` (animals).
  Rigs animate in `update()`. `animal.cpp`/`npc.cpp` build species/appearance.

### Client / server (`src/game_session.cpp`, `src/network.cpp`)

- The server runs in `serverThreadMain()`. `startEmbeddedServer()` spawns it on
  a background thread (hosting & singleplayer); `runDedicatedServer()` runs it
  on the main thread (`--server`).
- Server tick: fixed **20 Hz** (`SERVER_TICK_RATE`, `SERVER_TICK_DT`). Each tick
  it advances the world, ferries, `NpcDirector`, `AnimalDirector`, then
  broadcasts entity-state packets.
- Globals: `g_server` (`NetworkServer*`), `ctx.client` (`NetworkClient*`).
- The server thread has its **own** `World` and entity state — it does not share
  the client's `AppContext`. Cross-thread communication is packets, plus a few
  atomics (`g_serverStats`, `g_serverDayTimeSync`).

### Networking (`include/network.h`)

- All packet structs live between `#pragma pack(push,1)` / `#pragma pack(pop)`
  so client and server agree on byte layout — keep new packets inside it.
- `PacketType` enum tags each packet. Server: `NetworkServer::broadcast(...)`.
  Client: `NetworkClient::send(...)`; incoming packets are drained into typed
  buffers each frame (e.g. `animalUpdates`, `npcUpdates`).

### Rendering (`src/renderer.cpp`, `src/ui.cpp`)

`Renderer` draws terrain, water, sky, shadow map, and all `ObjectManager`
entities. Shaders are loaded from `shaders/`. ImGui HUD/menus/overlays are in
`ui.cpp` (HUD, nametags, health bars, F3 debug overlay, menus).

## Adding a feature: server-authoritative entity recipe

Ferries, NPCs and animals all follow the same pattern. To add a new networked
entity type:

1. **Kind:** add an `ObjectKind` value in `game_object.h` if needed.
2. **Client entity:** a class deriving `GameObject` (its `draw`/`update` render
   and interpolate). See `Animal` in `include/animal.h` / `src/animal.cpp`.
3. **Server simulation:** a `Director`-style class that spawns, steps and
   despawns the entities authoritatively (see `AnimalDirector`).
4. **Packet:** add a `PacketType`, a state struct inside the `#pragma pack`
   block in `network.h`, and a drain buffer on `NetworkClient`.
5. **Server thread:** in `game_session.cpp`, create the director, call its
   `update()` each tick, and `broadcast()` a state packet per entity.
6. **Client sync:** in `gameplay.cpp`, add a `syncXObjects()` that turns
   incoming packets into `ObjectManager` entries (create / update / remove).
7. **Build wiring:** add any new `.cpp`/`.h` to **all three**: `Makefile`
   (`SRCS`), `Terrax.vcxproj`, and `Terrax.vcxproj.filters`.
8. Build after each phase and fix errors before moving on.

## Conventions

- Headers in `include/`, implementation in `src/`; one `.cpp` per major system.
- New source files must be registered in `Makefile`, `Terrax.vcxproj`, and
  `Terrax.vcxproj.filters` or they won't compile on one of the two builds.
- Large features are delivered in **sequential phases**, with a clean build as
  the gate between phases.
- Prefer `std::min`/`std::max` — `NOMINMAX` is defined, so the Windows
  `min`/`max` macros are unavailable.
- Keep client and server in sync: anything that affects gameplay state must go
  through packets, not shared memory.

## Gotchas

- **MSBuild + Git Bash:** Git Bash mangles `/p:` switches (`MSB1008` error).
  Build from PowerShell or cmd.
- **Locked output files:** if the game is running, the linker can't overwrite
  `Terrax.exe` / `Terrax.pdb` (`LNK1104` / `LNK1201`). Close the game first.
- **Packet layout:** a state struct outside the `#pragma pack(push,1)` block
  will desync client and server silently.
- **Server thread:** code in `serverThreadMain()` runs off the main thread —
  never touch `AppContext` or client GL state from there.
