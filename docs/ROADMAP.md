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
- [ ] **A2. Item & weapon archetype catalog** — extend the tool to list weapon
  types, clothing slots/tiers, themed sets and their dimensions/palette.
- [ ] **A3. Dungeon & building catalog** — list dungeon kinds, room purposes,
  building types and their footprints, as a placement reference.
- [ ] **A4. Catalog-driven placement helpers** — small API so dungeon/house
  furnishing can query "props that fit a W×D×H slot / tagged `tavern`" from the
  registry instead of hardcoding `PropType`s.

## Track B — World scaling & progression

- [ ] **B1. Danger tiers** — `dangerTierAt(worldXZ)` = f(distance from spawn),
  surfaced in the F3 overlay and on the map. Spawn is a safe tier-1 zone.
- [ ] **B2. Enemy levels** — every hostile NPC carries a `level` derived from the
  danger tier of its spawn point; serialize it in `NPCState`.
- [ ] **B3. Level-scaled enemy stats** — HP/damage/XP-reward/loot-level scale
  with enemy level (re-use the item-generator `targetLevel`).
- [ ] **B4. Biome-tier mapping** — bias biome placement so harsher biomes
  (volcanic, frozen, blighted) sit in the outer tiers; lush/temperate near spawn.
- [ ] **B5. Level-up rewards & feel** — level-up FX, stat recompute, "you must be
  level N" gating hints when entering a higher-tier region.

## Track C — Bestiary & animals (variety)

- [ ] **C1. Enemy archetype framework** — data-driven enemy defs (model palette,
  size, behaviour flags, ability set, tier band) so new enemies are table rows.
- [ ] **C2. Humanoids** — Bandit, Brigand, Cultist, Necromancer, Knight, Warlord.
- [ ] **C3. Undead** — Skeleton (warrior/archer/mage), Zombie, Ghoul, Wraith, Lich.
- [ ] **C4. Beasts/monsters** — Wolf-pack, Bear, Spider, Slime, Golem, Troll, Ogre.
- [ ] **C5. Elementals & exotics** — Fire/Ice/Earth elementals, Imp, Wisp, Drake.
- [ ] **C6. Animal roster expansion** — deer, boar, fox, rabbit, sheep, cow,
  chicken, horse, bear (neutral), birds, fish — passive/skittish/territorial AI.
- [ ] **C7. Elites & rares** — occasional starred elite spawns with buffed stats
  and better loot, plus roaming named "rare" mobs.

## Track D — Targeting & combat UX (tab-target MMO feel)

- [ ] **D1. Persistent target** — click / Tab to lock a target; `ctx.targetNpcId`
  persists; abilities use it instead of re-coning each cast.
- [ ] **D2. Target frame UI** — selected enemy's name, level, health bar, and a
  cast bar; colour by hostility and level delta.
- [ ] **D3. Nameplates** — floating name + level + health over nearby enemies,
  with a selection highlight ring on the target.
- [ ] **D4. Threat/aggro polish** — show aggro state; tab cycles nearest hostiles.

## Track E — Quests & quest-givers  *(the core loop)*

- [ ] **E1. Quest data model** — `Quest` (id, title, text, objectives, rewards,
  giver, turn-in) + `QuestState` (offered/active/complete/turned-in). Deterministic
  generation from town/region seed. Headless-testable.
- [ ] **E2. Quest-giver NPCs** — a town NPC type that offers quests; interaction
  opens a quest dialog. Server-authoritative quest assignment.
- [ ] **E3. Kill quests** — "slay N <enemy> in <region/dungeon>"; objective
  progress tracked server-side from kill events.
- [ ] **E4. Collection quests** — "gather N <item>"; counts inventory / loot.
- [ ] **E5. Quest log + tracker UI** — journal panel + on-screen objective tracker
  with progress, and a map/compass marker to the target area.
- [ ] **E6. Rewards & turn-in** — XP, gold, and a rolled item on turn-in; level
  scales the reward; quest chains unlock the next quest.
- [ ] **E7. Quest variety** — escort, boss-kill, exploration ("discover X"),
  delivery between towns.

## Track F — Dungeons & boss loop

- [ ] **F1. Boss mechanics** — telegraphs, special abilities, enrage; distinct
  from trash mobs.
- [ ] **F2. Boss loot tables** — themed drops per dungeon kind + guaranteed
  rare/legendary; tie into collection quests.
- [ ] **F3. Dungeon discovery & re-pop** — entrance markers, "dungeon cleared"
  state, timed re-population.
- [ ] **F4. Dungeon difficulty by tier** — dungeon level follows its danger tier;
  far dungeons are harder and drop higher loot.

## Track G — Town economy

- [ ] **G1. Currency (gold)** — earned from kills/quests; shown in HUD; saved.
- [ ] **G2. Vendor NPCs** — buy/sell UI; vendors stock tier-appropriate gear.
- [ ] **G3. Repair / consumables** — potions (heal/mana), food, basic upgrades.
- [ ] **G4. Bank/stash** — per-character persistent storage in town.

## Track H — Rendering pass (screenshot-driven)

- [ ] **H1. In-game screenshot capture** — key (e.g. F2) → `glReadPixels` → PNG
  in a `screenshots/` dir, plus a `--screenshot-tour` headless-ish capture mode
  that flies the camera through varied scenes for the loop to review.
- [ ] **H2. Lighting & tone** — review shots; improve ambient/sky/fog, exposure
  and colour grading in the post pass.
- [ ] **H3. Water & reflections** — review and refine the water shader.
- [ ] **H4. Foliage & detail** — denser/varied vegetation, better LOD/fade.
- [ ] **H5. Shadows & SSAO** — soften shadows, add contact shadowing if cheap.
- [ ] **H6. Per-screenshot follow-ups** — each rendering review appends concrete,
  scoped items here for later iterations to work through.

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
