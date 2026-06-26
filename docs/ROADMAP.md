# Terrax — MMO Gameplay-Loop Roadmap

This is the working backlog for turning Terrax into a WoW-style MMO sandbox: a
core loop of **quest → travel → fight → loot → level → venture further**, with a
large bestiary, town quest-givers, dungeons and bosses, a town economy, an asset
catalog to drive world population, and an ongoing rendering-quality pass.

It is delivered in **small, sequential features**, each on the
`feature/mmo-gameplay-loop` branch with **one commit per feature**. After each
feature the project must build clean (`make` + `make test`) before moving on.

## Status legend
`[ ]` todo · `[~]` in progress · `[x]` done

---

## What already exists (baseline)

The engine is already far along — this roadmap *extends* it, it does not rebuild
it. Present today:

- Player **levels + XP**, **roles** (Tank/DPS/Healer), **skill tree**, **abilities**
  (cooldowns, mana/energy/rage, buffs) — a real WoW-style combat kit.
- **Dungeons** (Crypt/Cave/Ruins/Castle) with rooms, corridors, lights, spawn
  tables and **bosses** that drop legendary loot.
- **NPCs** (Villager/Enemy/Guard/Farmer/Skeleton/Brute/Cultist/Trainer) with
  server-authoritative AI, bandit camps and town raids.
- **Animals**, **item generation** (level-scaled, rarity tiers, themed sets,
  legendary boss drops), **loot drops**, **inventory**, **prop registry**.
- Cone-based **target selection** for abilities (`findTargetNpc`).

## What's missing (this roadmap)

Quests & quest-givers · distance-based **danger tiers** so far-out biomes hold
higher-level enemies · a **persistent target + target frame** (tab-target) ·
a much larger **bestiary & animal roster** · a **town economy** (vendors) · an
**asset catalog** to reference when populating the world · a **rendering pass**.

---

## Track A — Asset & content catalog  *(reference infra, build first)*

- [x] **A1. Asset catalog generator + `docs/ASSET_CATALOG.md`** — headless tool
  (links via `gl_stub` like the tests) that enumerates every prop in the registry,
  bakes its mesh, and records bounding-box **dimensions**, voxel count, light/
  interaction/wind metadata. Emits a Markdown catalog. `make catalog` target.
- [x] **A2. Item & weapon archetype catalog** — extend the tool to list weapon
  types, clothing slots/tiers, themed sets and their dimensions/palette.
- [x] **A3. Dungeon & building catalog** — list dungeon kinds, room purposes,
  building types and their footprints, as a placement reference.
- [ ] **A4. Catalog-driven placement helpers** — small API so dungeon/house
  furnishing can query "props that fit a W×D×H slot / tagged `tavern`" from the
  registry instead of hardcoding `PropType`s.

## Track B — World scaling & progression

- [x] **B1. Danger tiers** — `dangerTierAt(worldXZ)` = f(distance from spawn),
  surfaced in the F3 overlay and on the map. Spawn is a safe tier-1 zone.
- [x] **B2. Enemy levels** — every hostile NPC carries a `level` derived from the
  danger tier of its spawn point; serialize it in `NPCState`. (HP also scales at
  spawn; bosses are +3 levels above their pack.)
- [x] **B3. Level-scaled enemy stats** — HP/damage/XP-reward/loot-level scale
  with enemy level (re-use the item-generator `targetLevel`). Loot rolls at
  max(player, enemy) level; XP uses con-based scaling (grey kills give less).
- [ ] **B4. Biome-tier mapping** — bias biome placement so harsher biomes
  (volcanic, frozen, blighted) sit in the outer tiers; lush/temperate near spawn.
- [x] **B5. Level-up rewards & feel** — level-up grants a skill point + HUD toast and
  recomputes role stats (existing), and a periodic **region danger warning** fires when
  local foes far out-level the player (≥8), nudging them to gear up / turn back. _(A
  level-up particle burst could still be added.)_

## Track C — Bestiary & animals (variety)

- [ ] **C1. Enemy archetype framework** — data-driven enemy defs (model palette,
  size, behaviour flags, ability set, tier band) so new enemies are table rows.
- [x] **C2. Humanoids** — Bandit, Brigand, Cultist, Necromancer, Knight, Warlord.
  _(Added: `Knight` (plate sword+shield), `Brigand` (hardened axe/mace bandit, cave
  packs), and `Warlord` (heavy plate war-commander wielding a greatsword/axe — now the
  **Castle boss** + elite champions, replacing the generic Brute lord). Bandit/Cultist/
  Necromancer already present.)_
- [~] **C3. Undead** — Skeleton (warrior/archer/mage), Zombie, Ghoul, Wraith, Lich.
  _(Added: `Zombie` (slow heavy melee), `Ghoul` (fast frenzied claws),
  `Necromancer` (ranged staff-bolt caster — Ruins boss), `Wraith` (fast
  spectral claws, crypt packs), and `Lich` (undead arch-caster — frost-bolt
  staff, now the **Crypt boss** + elite champions, reuses the ranged-caster AI;
  also gave the Necromancer its proper cast pose). Skeleton already existed.)_
- [ ] **C4. Beasts/monsters** — Wolf-pack, Bear, Spider, Slime, Golem, Troll, Ogre.
- [~] **C5. Elementals & exotics** — Fire/Ice/Earth elementals, Imp, Wisp, Drake.
  _(Added: **Fire Elemental** — a being of living fire (no clothing/weapon; the rig's
  `skinColor` recoloured molten-orange + `resetBaseBody`), a ranged caster
  (`stepRangedEnemy`) conjured among the arcane Ruins. And **Stone Elemental** — a grey,
  oversized (1.32×), slow but very tanky melee guardian (`stepBandit`) that stands sentinel
  in Castles. Two elemental archetypes (caster + melee). Ice would be another palette swap;
  Imp/Wisp/Drake need new rigs.)_
- [x] **C6. Animal roster** — 8 species already shipped: Sheep, Cow, Pig, Rabbit,
  Squirrel, Deer, Fox, Chicken, each with a voxel rig, per-species speed, skittish
  vs. calm AI, and biome-aware spawn tables. _(Predators/mounts — wolf, bear, boar,
  horse — could extend it later.)_
- [x] **C7. Elites & rares** — _(Added **elites**: ~12% of non-boss dungeon minions
  are promoted to elite — +2 levels, ~2x HP, a starred gold nameplate (`drawLevelTag`),
  1.8x XP and extra loot rolled a couple levels higher (`spawnLootForKill` elite arg). The
  elite flag rides NPCState `flags` bit 4. And **named rares**: ~3% become a "rare" —
  elite-tier + a further +2 levels, ~3x HP, a **purple name banner** ("Gorefang the Cruel",
  deterministic `rareName(seed)`), 2.5x XP and a "Rare slain: <name>!" toast. Rare flag rides
  `flags` bit 5. The hunt-the-rare loop.)_

## Track D — Targeting & combat UX (tab-target MMO feel)

- [x] **D1. Persistent target** — **T** cycles to the next nearby hostile in view;
  `ctx.targetNpcId` persists (auto-cleared on death / >70m). `findTargetNpc` prefers
  the locked target (ignores the aim cone) so abilities + attacks hit it.
- [x] **D2. Target frame UI** — top-centre frame with the target's name, con-coloured
  level, and health bar, plus a con-coloured selection chevron over its head.
- [x] **D3. Nameplates** — con-coloured "Lv N" tags over hostiles within 45m,
  level-aware health bars, and a con-coloured selection chevron over the locked
  target (D1/D2). _(The focused enemy's name shows in the target frame; per-plate
  name labels deferred as redundant for now.)_
- [x] **D4. Threat/aggro polish** — hostiles that have acquired a player target set an
  `aggro` flag (server, in `stepBandit`/`stepRangedEnemy`), ridden on NPCState `flags` bit 6
  (pack/unpack like elite/rare); their nameplate shows a red **"!"** alert, so you can see at
  a glance which foes in a pack have woken and are hunting you. (Tab/T cycling already done in D1.)
- [x] **D5. Floating combat text** — damage numbers rise off a struck enemy and fade
  out. Fully client-side: spawned from each enemy's per-tick synced health delta
  (`syncNPCObjects`), aged/pruned in `updateGameplay`, projected + drawn in the play HUD
  (`drawFloatingCombatText`). Elite hits show in gold. Now also **player-side**: red
  damage-taken numbers + green heal numbers (potions, chain-heal/sanctuary) float over the
  player via `spawnPlayerFloatText`. Makes melee/abilities feel responsive, no new packet.

## Track E — Quests & quest-givers  *(the core loop)*

- [x] **E1. Quest data model** — `Quest` (id, kind, title, text, objective,
  rewards, giver) + `QuestStatus` + `QuestTarget`. Pure `buildTownQuests()`
  (deterministic, headless-tested) + `getTownQuests()` that pulls real nearby
  dungeons + a wilderness region from the world plans. quest.{h,cpp}, 4 tests.
- [x] **E2. Quest-giver NPCs** — `NPCType::Questgiver`, one static giver spawned
  per town; pressing E opens a quest-board dialog (lists `getTownQuests`, maps the
  giver to its town via nearest-in-plan); Accept adds to `ctx.activeQuests`.
  _(Assignment is client-side for now; networked turn-in lands with E6.)_
- [x] **E3. Kill quests** — accepted `KillEnemies` quests track progress on each
  matching kill (foe type + kill within one danger tier of the target region; see
  `questKillCounts`), flip to Complete at the required count, and show live in a
  top-right **quest tracker** HUD. _(Tracked client-side via awardEnemyKill; the
  networked/server-authoritative path comes with E6.)_
- [x] **E4. Collection quests** — "gather N <collectible>"; in-region kills yield
  the collectible (~70% drop) toward the count via `questCollectCounts`, with a
  drop toast + the shared tracker/complete flow. _(Kill-drop proxy for now; ties
  to real droppable items later.)_
- [x] **E5. Quest log + tracker UI** — on-screen objective **tracker** (top-right,
  done in E3) + a full **journal panel** (J key) listing every active/complete quest
  with progress, target region/tier, recommended level and rewards. Active quest
  objectives also show as gold diamond markers (green when complete) on the world
  map (M), with the quest title — so "go to <dungeon/region>" has clear direction.
- [x] **E6. Rewards & turn-in** — returning to a quest-giver shows Complete
  quests with a "Turn in" button → grants `rewardXp` (shared `grantPlayerXp`),
  `rewardGold` (new `ctx.playerGold`), and a rolled item at `recommendedLevel`
  into the inventory; marks TurnedIn + drops from the active list. Giver window
  shows the player's gold. _(Quest chains TBD; rewards client-side for now.)_
- [~] **E7. Quest variety** — escort, boss-kill, exploration ("discover X"),
  delivery between towns. _(Added: **boss-kill** quests (`QuestKind::SlayBoss`,
  credited via `questBossKillCounts`) and **exploration** quests (`QuestKind::Explore`,
  "Scout <region/dungeon>" — completed the instant the player reaches the target via
  `questExploreReached`, checked each tick in updateGameplay; quick fair reward) and
  **delivery** quests (`QuestKind::Deliver`, "Deliver a parcel to <town>" — destination is
  the nearest other town, completed on arrival via `questDeliverReached`; gold-focused
  courier pay). Tracker/journal show Scout/Deliver objectives; map marker points the way.
  Escort still to come.)_

## Track F — Dungeons & boss loop

- [~] **F1. Boss mechanics** — dungeon bosses now do a **telegraphed ground-slam**
  (`stepBossSpecial`): a warning ring appears + the boss rears up (~0.95s), then a
  radius AoE booms on anyone still standing in it — dodgeable by stepping out.
  Server-resolved, broadcast as SpellEffect for the visual. Plus an **enrage**: once a
  boss drops below 30% HP it frenzies (melee +50% dmg / +30% speed / faster swing; caster
  bosses fire 50% harder + faster), flagged once with a red burst cue — a tense finish.
  And **per-type specials**: caster bosses (Lich / Necromancer / Cultist) hurl a telegraphed
  **5-bolt volley** (a fan, reuses `spawnEnemyProjectile`) from range instead of the melee
  slam, so a caster boss fights very differently from a melee one.
- [x] **F2. Boss loot** — boss kills drop 2–4 guaranteed **legendaries** *plus* a
  guaranteed themed **set piece** (`generateSetClothing`) — a recognisable trophy you
  can only earn from bosses. _(Per-dungeon-kind themed tables still possible later.)_
- [~] **F3. Dungeon discovery & re-pop** — dungeons show as **map markers** (with
  recommended level); crossing into one pops a **"Discovered: \<name\>"** banner
  (`ctx.discoveredDungeons`); and felling a dungeon's **boss marks it "Cleared"**
  (`ctx.clearedDungeons`) with a banner + a green "(Cleared)" map label. _(Timed
  re-population still to come.)_
- [x] **F4. Dungeon difficulty by tier** — a dungeon's foes already scale with the
  danger tier of its location (B2/B3), and the world map now labels each dungeon
  with its **recommended level** ("Lv ~N") so players can pick targets that match
  their level. Far-out dungeons are tougher and drop higher loot.

## Track G — Town economy

- [x] **G1. Currency (gold)** — `ctx.playerGold`, earned from quest turn-ins,
  spent at vendors; shown in a bottom-left HUD chip + the giver/vendor windows.
  _(Not yet persisted in character_save — needs a save-version bump; deferred.)_
- [x] **G2. Vendor NPCs** — `NPCType::Vendor`, one merchant per town; pressing E
  opens a buy/sell shop. Stock is deterministic, tier-scaled generated gear
  (`vendorStock` cache); buy deducts gold + adds a fresh copy, sell removes an
  item for ~35% of value. `vendorBuy`/`vendorSell`/`itemSellPrice` in gameplay.
- [~] **G3. Repair / consumables** — _(Added: **Health + Mana potions** — new
  `ItemKind::Consumable` + `ConsumableItem` (items.{h,cpp}); restore a % of max
  health/resource when right-clicked in the bag (`useConsumable`, client-side); every
  town apothecary stocks both at a fixed price (`vendorStock`/`vendorBuy`); bottle icon +
  liquid colour + tooltip in the inventory UI. A shared **12s "potion sickness" cooldown**
  (`ctx.potionCooldown`) blocks chain-quaffing to full mid-fight. Plus **food** — a "Hearty
  Meal" (`ConsumableKind::FoodRation`) grants a timed **+25% ability-power "Well Fed" buff**
  (5 min, an `ActiveBuff` shown in the buff bar); also vendor-stocked, with its own roast icon
  and no potion cooldown. Repair still to come.)_
- [ ] **G4. Bank/stash** — per-character persistent storage in town.

## Track H — Rendering pass (screenshot-driven)

- [x] **H1. In-game screenshot capture** — **F2** → `glReadPixels` → PNG in
  `screenshots/` (dependency-free PNG writer, no libpng/zlib), plus
  `./terrax --screenshot-tour` which boots singleplayer and flies the camera out
  through the danger tiers (0/1500/4000/9000/20000m), dropping a tagged PNG at each.
- [ ] **H2. Lighting & tone** — review shots; improve ambient/sky/fog, exposure
  and colour grading in the post pass.
- [ ] **H3. Water & reflections** — review and refine the water shader.
- [ ] **H4. Foliage & detail** — denser/varied vegetation, better LOD/fade.
- [ ] **H5. Shadows & SSAO** — soften shadows, add contact shadowing if cheap.
- [ ] **H6. Per-screenshot follow-ups** — each rendering review appends concrete,
  scoped items here for later iterations to work through.

### Rendering review #7 (2026-06-26, clean tour)

With HUD-free framing (R11) the review kept landing the tour camera over ponds /
coastal spits, so far shots read as flat top-down water. Two tour-vista fixes:
- **R12. Inland vista anchoring** — `findLand` now seeks a genuinely *inland*
  column (a margin above sea level with land neighbours), falling back to any dry
  land — so far stages frame terrain, not a pond.
- The tour camera now sits above the **tallest nearby column** (sampled in a small
  ring), not just the anchor's surface, so it clears mesas/cliffs instead of
  clipping into a wall. Verified: all five stages now give clean elevated vistas.

Renderer verdict (again): **strong** — AO (R1) + hemispheric ambient (R2) give
terrain real form, water (fresnel/foam/planar reflection) reads well at play
angles (the flat grey is only the near-vertical top-down view, atypical in play).
No urgent shader work; the open R3/R4/R5/R6 polish items remain low-priority.

### Rendering review #5 (2026-06-25, clean tour)

Reviewed fresh vistas across tiers (spawn town, rolling hills, autumn forest,
snowy biome). Terrain had good AO crevice depth but its larger *form* read soft
at high sun (slope tops all caught similar light). Applied **R2** — hemispheric
sky ambient (above) — and re-ran the tour to verify: hill slopes and building
walls now show clear top-vs-side dimension, with no muddying or over-darkening.
Tour-quality follow-ups noted below (R10, R11) — the captures themselves can be
improved so future reviews are cleaner.

- [x] **R10. Tour far-waypoint clipping** — fixed: the tour camera now sits `gy+32`
  above the land column (was `gy+16`) and looks down at `-32°` (was `-24°`), so far
  stages on tall/coastal terrain read as clean aerial vistas instead of burying the
  camera in a hill. Verified — the 20000m stage is now a clean overhead shot.
- [x] **R11. Tour HUD suppression** — fixed: `main.cpp` skips `renderPlayUI` entirely
  in `--screenshot-tour`, so review frames are completely HUD-free (no hotbar / bars /
  gold / nameplates covering the scene). Verified across all tour stages.

### Rendering review #4 (2026-06-24, clean tour)

Reviewed clean vistas (forest canopy, snowy coast). Applied **R8** (eased water
saturation 1.45→1.30 — calmer turquoise vs snow, verified). Also confirmed the
B5 region danger warning fires in-context ("foes here are around level 19…").
Renderer remains in good shape; no further urgent items.

### Rendering review #2 (2026-06-23, clean tour — overlay now off)

`--screenshot-tour` now disables the F3 debug overlay so review frames are
unobstructed (the old shots hid ~40% of the frame). Reviewed clean vistas
(snowy conifer coast, mountains, spawn town):

- **Verdict: the renderer reads well.** AO (R1) gives terrain + stone steps +
  building edges real depth; the post pass (shafts, fog, contrast, saturation,
  vignette) is well-tuned. No urgent visual gaps.
- [x] **R8. Water saturation** — eased the water shader's saturation boost
  (1.45→1.30) so open water reads less neon next to muted snow/sand while keeping
  its turquoise character (verified in review #4's snowy-coast shot).
- [x] **R9. Foliage density** — bumped open-biome ground-cover coverage (Plains
  0.62→0.72, Forest 0.62→0.68, Jungle 0.66→0.72, Savanna 0.50→0.58) so meadows read
  lusher and less flat (verified in review #3). The vegetation system already has
  ~22 detail types; distinct *tree* silhouettes remain a larger future item.

### Rendering review #3 (2026-06-24, clean tour)

Reviewed fresh clean vistas (plains, town, mountains). **Renderer + vegetation are
both already strong** — AO depth, tuned post/water/chunk shaders, ~22 vegetation
types. Only actioned R9 (denser open-biome cover). No other urgent gaps; remaining
ideas (distinct tree models, biome-aware water saturation) are low-priority polish.

### Rendering review #1 (2026-06-23, tour shots tier 1 & tier 9)

From the first `--screenshot-tour` (spawn savanna town; tier-9 ocean). Findings,
scoped as actionable items:

- [x] **R1. Voxel ambient occlusion** — classic per-vertex AO baked in the chunk
  mesher (`vertexAO`: 2 edge + 1 corner neighbour solidity → 4-level darkening
  table), carried on a new `Vertex.ao` attribute (loc 9) through chunk.vert →
  chunk.frag where it multiplies the lighting. Concave corners/joints now read 3D
  (verified in review #3's town shot). No per-frame cost (baked at mesh time).
- [x] **R2. Stronger directional shading** — addressed with **hemispheric sky
  ambient** in `chunk.frag`: indirect light now scales with the face normal's
  up-component (`mix(0.62,1.0,upFace)`), so voxel slopes keep their form even at
  high sun while top faces (and overall brightness) stay unchanged. Verified in
  review #5 — hill slopes + building walls read with clear top-vs-side dimension.
- [ ] **R3. Water material** — water is flat and near-opaque. Add transparency +
  fresnel + a little specular/normal ripple in `water.frag`.
- [ ] **R4. Weather particles** — vertical streaks are prominent even over open
  ocean; review rain density/opacity and whether it should gate on biome/weather.
- [ ] **R5. Tone & colour grade** — very bright/saturated; add a gentle
  exposure + filmic-ish tonemap and slight desaturation in `post.frag` for mood.
- [ ] **R6. Distance fog blend** — far terrain fades to a flat band; blend fog
  colour toward the sky gradient so the horizon reads cleanly.
- [x] **R7. Tour land waypoints** — waypoints now nudge to the nearest land
  column (`findLand`) and shoot elevated, angled vistas (noclip + raised camera
  + downward pitch), so reviews see terrain, not ocean. One clean shot per stage.

---

## Working agreement (for the loop)

1. Pick the next `[ ]` item (top-down, respecting track order where there are
   dependencies). Mark it `[~]`.
2. Implement it in the smallest coherent slice. Register new files in **all**
   builds (`Makefile`, `Terrax.vcxproj`, `Terrax.vcxproj.filters`).
3. `make` and `make test` must pass.
4. Mark it `[x]`, commit with a short one-line message (no AI attribution).
5. For rendering items, capture and review screenshots, then append findings to
   Track H before moving on.
