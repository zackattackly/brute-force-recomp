# Brute Force recomp: reverse-engineering notes

Game: **Brute Force** (Digital Anvil / Microsoft, Xbox, USA). `default.xbe` built 2003-05-07, certified 2003-05-09,
title ID `4D53001E`, `version.bin` = 2092, SHA-1 `98bf07467da835531dbcb6601b3a45d4c6aed53a`.
Microsoft XDK libraries 1.0.5233 (XAPI, D3D8LTCG, DSOUND, XONLINES), plus Bink video and zlib 1.1.3.

## Milestone 1 (done): boot sequence + file/asset loading

### What's in this folder

| Path | What it is |
| --- | --- |
| `ghidra_scripts/BruteForce_ApplyNames.java` | Ghidra script: names 640 functions and 251 data labels (milestones 1–8), with comments describing each. Safe to run more than once. |
| `tools/` | Python 3 tools (standard library only) that read the game's data formats. |
| `convert_all.bat` | Double-click to unpack and convert all of the game's data (see below). |
| `xml/` | Readable XML converted from `common.tgz`, `mp_common.tgz` and `m01_a.tgz` (130 files, all decoded byte-exact). |

### Run the Ghidra script
1. In the CodeBrowser with `default.xbe` open: **Window → Script Manager**.
2. Click the **Manage Script Directories** button (list icon, top right), add this folder's `ghidra_scripts`, close the dialog.
3. Find **BruteForce_ApplyNames** under the *BruteForce* category and double-click it. The console prints how many names were applied.

### Convert the rest of the game data
Needs Python 3 on the PC (python.org, with "Add python.exe to PATH" ticked) and about 10 GB of free space.
**Double-click `convert_all.bat`** in this folder. It reads the game's data folder next to this one
(`iso\Brute Force (USA) (En,Fr,De,Es,It).xiso\data`) and writes everything to `C:\Users\<you>\Brute Force data`
(outside OneDrive, so gigabytes don't get synced). Or run it by hand from the `tools` folder:

```
python extract_and_convert.py "<game folder>\data" "<output folder>"
```

Each `.tgz` is unpacked into its own folder (as the game does on its Z: cache drive), then an `.xml` is written next to
every `.xmb`. Names that could not be recovered show as `_xxxxxxxx` (the hash).

Or **double-click `export_all.bat`** to run every exporter at once and get a start page (`index.html`, Milestone 42).

After that, **double-click `export_textures.bat`** to turn every texture into a PNG image (Milestone 15), and
**`export_models.bat`** to turn the 3D models into OBJ files (Milestone 16), and **`export_animations.bat`** for the
animations as JSON and as BVH files that open in Blender (Milestones 19–20).

## How the game boots

`mainCRTStartup` (0x2F5D1F) → main thread (0x2F5CAB) → **`main` (0x697F0)**:
`App_PreInit` (0x67EB0) → `App_Init` (0x68340) → `App_RunFrame` (0x69270) in an endless loop.

`App_Init` calls **`Game_LoadStartupData` (0x1EA2E0)**, which:
- fills the data-path globals (`InitDataPathGlobals`, 0x1E8160): `d:\data\`, `common\`, `mp_common\`, `schemas\`, `.xmb`, `.xsb`, and the per-kind name prefixes `animations-`, `objects-`, `textures-`, `sounds-`, `terrainmaps-`...
- checks the **Z: cache** (`Cache_Validate`, 0x1E48E0): compares `d:\version.bin` with `z:\cacheinfo.bin`
- loads the schemas between memory checkpoints `"pre_schema"` and `"post_schema"`.

## Data pipeline

```
DVD  d:\data\<name>.tgz   (gzip + tar, "max compression")
  └─ unpacked to the Xbox Z: utility-drive cache by the "cache_program" (Cache_RunCacheProgram, 0x1E4E30)
       z:\<name>\...      .xmb  binary XML (almost all game data)
                          .xsb  binary XML schemas (types for .xmb values)
                          .chnl animation channels, .ale effects, .ipn/.ivd objects, .tex textures,
                          .mem/.xwb sound, .gscr ...  (not decoded yet)
```

Archives: `common`, `mp_common`, episode archives `e01`–`e40`, mission archives `m01_a` ... (361 `.tgz` total, 2.2 GB),
plus per-language sound archives `ml-sounds\<lang>\<name>-<lang>.tgz`. Downloadable content is listed in `t:\packages`.

## Name hash

Every element, attribute, type and namespace name is stored as a 32-bit hash:
**case-insensitive CRC-32 with the game's own table** (`BF_Crc32NoCase` 0x238370, table 0x3BC150, lowercase map 0x3BC550).
`tools/bfhash.py` reproduces it exactly. Names were recovered by hashing every string in the executable and by
word-combination search: 1,028 of the 2,292 distinct names used in the schemas and the three converted archives (45%)
are known (`tools/names.json`); the rest remain as hashes. A few recovered names may be wrong guesses (hash collisions).

## .xmb format (BXML), all little-endian

| Offset | Size | Field |
| --- | --- | --- |
| 0x00 | 4 | `"BXML"` |
| 0x04 | 2 | version minor, major (files use 1.1) |
| 0x06 | 1 | flags: bit 0 = names stored as hashes; bits 0-1 enable special string types |
| 0x08 | 4 | file offset of the name table |
| 0x0C | 4 | checksum (0 in shipped data; written for saves by `BXML_WriteChecksum`) |
| 0x10 | varint + n×8 | namespace table: (prefix hash, URI hash) pairs. Prefix 0 = default namespace |
| name table | varint + n×4 | name hashes; tokens refer to them by index |

Then a **token stream of 16-bit words**, LZ-compressed in blocks:
- block = u16 length in words (including itself), u32 total word count (u16 if version ≠ 1.1), data words
- each group of 16 items is preceded by a flag word: bit 0 = literal word, bit 1 = back-reference
  (`offset = w & 0x1FF`, `length = (w >> 9) + 2`) into a 512-word ring buffer starting at position 1 for each block.

**Element values are stored raw between token blocks**, in the order the tokens need them.

Token = `type (top 4 bits) | name index (low 12 bits)`:

| Type | Meaning |
| --- | --- |
| `0x0` | element with children; nested tokens follow until `0xC` |
| `0xA` | element with one value (read from the raw data) |
| `0xB` | empty element |
| `0xD` | value list for the current element; count in the low 12 bits |
| `0x9` | "next element repeats N times" hint (informational) |
| `0xC` | end of element |

Counts above 0x7FF continue in following words (15 bits each, top bit = more).
The pseudo-element hash `0x1B8535F0` holds an element's **attributes**.

Value encoding depends on the schema type: `float`/`int`/`unsignedInt` 4 bytes, `boolean` 1 byte, `unsignedShort` 2,
`unsignedByte` 1, `string` = varint length + bytes, **`stringid`** = u32 name hash, **`enum-value`** = varint − 1.

## .xsb format (BXSD)

Header `"BXSD"`, version, flag byte (hashed names), u32 body offset. Then the target namespace (QName) and
(prefix, URI) pairs. The body is a tree: varint child count; each node = u16 kind, QName name, QName type, children.
QName = `00` none | `21` u32 name | `22` u32 namespace, u32 name.

Node kinds: `0x1` complex type · `0x101` complex type extending a base · `0x2` simple type / attribute ·
`0x6` list type · `0x9` element (inline type) · `0xA` simple element · `0x18` element reference ·
`0x19` typed element · `0x80` attribute group.

Seven schemas live in `common/schemas`: `bfns` (game), `daxns` (engine), `bfens`, `gameflags`,
`gameflags-downloadable`, `global-gameflags`, `limp`.

## Milestone 2 (done): the game loop

`main` calls **`App_RunFrame` (0x69270)** forever. Each frame:

1. **Timing.** Reads the high-resolution timer, accumulates elapsed time, caps one frame at 100 ms (unless a debug flag disables the cap).
2. **`App_UpdateSimulation` (0x68D20): fixed-rate simulation.** Converts accumulated time into whole ticks at
   `g_simTicksPerSecond` = **30** (**25** on a PAL 50 Hz console: `App_Init` checks `XGetVideoStandard()` and the PAL60 flag), caps the count (`Session_GetMaxTicksPerFrame`), asks the session how many may run
   (`Session_AcquireTicks`), then for each tick:
   - `g_simTickCount++`, game time += one tick (33 ms, or 40 ms at 25 Hz)
   - `Session_TickBegin`, `Session_Tick`: network session
   - `Input_Tick(dt)`: 33 input actions, reaches the controller (XPP) library
   - service at 0x443314 → vtable+0x50(dt)
   - the game object (`app+8`) → vtable+0x0C(dt): **the gameplay update** (`Game_Tick`, see Milestone 4)

   After the ticks: `App_CheckAttractTimeout` (15 s idle in session state 5 → `game->vtable+0x254`,
   the same call made when the network cable is pulled) and `Input_Update` (once per rendered frame).
3. **`App_Render` (0x69340).** `app->vtable+0x2C` draws, then `App_PresentFrame` (0x69390) limits frames queued on the GPU
   with a ring of 8 Direct3D fences, calls `game->vtable+0x14`, and presents.
4. **`App_UpdateFpsCounter` (0x68CB0).** Frames per second, about once a second.

### Lockstep multiplayer
`g_pSession` (0x46995C, built by `Session_ctor`) has four player slots. `Session_GetSlowestPeerTick` returns the lowest tick
any active player has confirmed, and the simulation never runs past it: classic **deterministic lockstep**, which is why
the simulation is fixed-rate. `Session_CheckNetworkLink` watches the cable (`PhyGetLinkState`).
Session state lives at `+0x1C` (values 0–5; meanings not yet confirmed).

### Main services (created in `Game_LoadStartupData`)
| Global | Object |
| --- | --- |
| `0x469940` `g_pSoundManager` | sound: `sounds\*.xwb` wave banks, `.mem` |
| `0x469948` `g_pInputManager` | input (33 actions) |
| `0x46995C` `g_pSession` | network/game session, lockstep |
| `0x4432F8` `g_pFileSystem` | `IFileManager` |
| `0x4432FC` `g_pSchemaLibrary` | `ISchemaLibrary` |

The engine is built from COM-style components. 73 interface names are embedded, e.g. `IEngine`, `IDatabase`,
`IScriptEngine`, `IFileManager`, `IMemCache`, `IFxRuntime`, `IAlchemy` (effects), `ISoundControl`, `ICameraControl`, `IIKControl`.

### Lua
The game embeds **Lua 3.2** (`luaB_predefine` 0x299210, `g_luaState` 0x469888). The `.gscr` files in the level archives
are **plain-text Lua scene/cutscene scripts** (`duration = ...; entities = { ... }`), readable as-is. Besides Lua's own
built-ins, `Script_InitLua` registers `_ERRORMESSAGE` and copies some enum names (event types, axes) into Lua as constants.
No other native functions are exposed, so gameplay logic is C++ driven by the `.xmb` data, not by scripts.

## Milestone 3 (done): enum names

The executable holds **93 enum name tables** (about 1,300 names), mostly as `{int nameLength; char *name; int value}`
entries. Examples: AI goals `GOAL_*` (135), overlay motions `OMT_*` (58), trigger signals `TRIG_SIG_*` (56),
animation motions `MT_*` (50), trigger actions `TRIG_ACT_*` (48), virtual buttons `VB_*` (64), damage types `DTYPE_*`,
ammo `AMMO_*`, weapons `WTYPE_*`, world materials `WMAT_*`, messages `MSG_*`/`EVT_*`.

In `.xmb` files an enum field is a schema type derived from `enum-value`, stored as varint (value + 1). The game picks
the table by the schema type's hash in a single switch (`EnumTypeDispatch`, 0x15C9B3), which gives an exact
**type → table** mapping.

`tools/enums.json` maps **52 enum types**, and the converted XML now shows names instead of numbers
(`<motion-type>MT_JUMP_FALL</motion-type>`, `<object-type>OT_CHARACTER</object-type>`, `<type>DTYPE_EXPLOSION</type>`):
- **25 confirmed**: read directly from `EnumTypeDispatch`.
- **27 inferred**: from field-name loaders or the type's own name (e.g. `overlay-motion-type` → `OMT_*`), kept only
  because every value used in the sampled data exists in that table. Each entry's `source` field says which.
  An inferred mapping whose data uses only a couple of values (0, 1) is a weaker match than one using many.

29 enum types seen in the data are still unmapped and show as numbers.

## Milestone 4 (done): the gameplay update

### Game object
`App_Init` allocates the **game object** (0x6F4 bytes, `Game_ctor` 0x79E70) and stores it at `app+8`; its vtable
`g_vt_Game` (0x3A2228) has 157 methods. The main ones:

| Slot | Function | Role |
| --- | --- | --- |
| [0] | `Game_Init` 0x7C2F0 | called once by `App_Init` |
| +0x08 | `Game_UpdatePlayerSlots` 0x7A070 | notifies the session about player slots that changed (8 slots) |
| +0x0C | **`Game_Tick` 0x7A1D0** | one simulation tick |
| +0x10 | `Game_TickPaused` 0x7A140 | used when no simulation ticks run: UI and a few world subsystems, no `World_Tick` (inferred: paused) |
| +0x14 | `Game_Draw` 0x7C580 | rendering |
| +0x254 | `Game_ReturnToFrontEnd` 0x79CA0 | idle timeout / network cable pulled (name inferred from those callers) |

Other methods load and save `t:\global-data`, `t:\global-gameflags`, profiles, campaign saves (`SaveMeta.xbx`) and
system-link co-op saves (`z:\syslinkcoop\`).

`Game_Tick` updates the front end/UI object (`game+0x5C4`), then, if a level is loaded (`game+0x5C0`) and the session
state is not 3, calls **`World_Tick`**, then advances the game clock (`game+0x5B8`).

### World (the loaded level)
Levels load asynchronously (loader at `game+0x6B4`). `Game_SwapInLoadedWorld` (0x7F5B0) installs the finished world at
`game+0x5C0`, `g_pWorld` (0x46999C) and `g_pWorldAlt` (0x469B5C), then calls `World_Activate` (0xD6520).
`World_CreateSubsystems` (0xD56C0) builds the subsystems and `World_DestroySubsystems` (0xD6B30) frees them.

**`World_Tick` (0xD9A20)** runs every subsystem in a fixed order each tick:

1. world tick counter `+0xB60`++, then `+0xBF0` and `+0xBEC` (vtbl+0x28)
2. `+0xBE0`, `+0xBF4`, `+0xBD4`, `+0xA40`, `+0xBE8`, static objects `+0xA44`, a global service (`0x443310`, vtbl+0x6C)
3. `+0x1F4`, `+0xA48`, `+0x1FC`, `+0x200`, cameras `+0xA58` (`CameraSystem_Tick`, creates `follow_cam_%X`), `+0x1F8`
4. a **request queue** (`+0x4`): entries are handled in order and removed; handling can stop early for this tick.
   Each entry points at an object (0x127290 handles objects flagged `+0x7D8` bit 1, 0x127C60 the rest)
5. a second queue (`+0x10`), drained at a limited rate per tick, then a list at `+0x1C` whose update reaches the follow-camera code
6. **the entity update**: `World_UpdateObjects` (0xD9840) calls every spawned object's Update (`vtbl+0x6C`): characters,
   weapons, items, effects, lights... (see Milestone 5)
7. **scene scripts** (`+0x20C`, `SceneScriptManager_Tick`: the `.gscr` Lua scenes), then a world-point list (`+0x14C`):
   `object->vtbl+0x1C(dt)` for the world points of two subtypes
8. **effects** (`+0xB80`, vtbl+0x04), **squads** (`+0xBDC`, `Squad_Tick` for each)
9. `+0xBE4`, a game-mode–specific subsystem `+0xC04` (only for two game types), `+0x204`, `+0x208`

Members marked only by offset are not identified yet: the gameplay code has almost no strings left, so a name needs
evidence from the data it reads.

### Data types (the archetype system)
Every `.xmb` element type is handled by a small reflected class. The executable holds 113 **type handlers**
(26-slot vtables, 0x393CC4–0x399AEC). In each handler, slot 4 is `GetTypeId` (returns the element's name hash),
slot 5 `Create` (allocates the object, often via a shared constructor), slot 6 `Destroy`, and slot 18 registers a
**property reader** that switches on child-name hashes. The script names these for the 68 types whose names are
recovered, e.g. `WeaponArchetype_ReadProperty` (fields `ammo-count`, `ammo-type`, `bullet-data`, `fire-sound-name`…),
`CharacterArchetype_ReadProperty`, `AiData_ReadProperty`, `EnergyShield_ReadProperty`, `Level_ReadProperty`.

Shared constructors show the inheritance in the data: `GameArchetype_ctor` (game, platform, projectile-mesh and head
archetypes), `WeaponArchetype_ctor` (weapon and turret), `InventoryArchetype_ctor` (inventory and grenade).

Code layout (source files are contiguous):

| Range | Contents |
| --- | --- |
| 0x08F000–0x0A4000 | material shaders (`BF_OceanMaterial`, `BF_LavaMaterial`, `BF_HoloMaterial`, `BF_InvisMaterial`…) |
| 0x0EC000–0x0EF000 | world points (`cover_point`, `hide_point`, `jump_down_point`, `elevator_point`…) used by the AI |
| 0x183000–0x188000 | enum parsers (Milestone 3) |
| 0x187F00–0x1A6800 | data-type readers (archetypes, level, campaign, menus, sounds, profiles) |
| 0x1FE000–0x20B000 | trigger/world-object components (counters, timers, elevators, spawners, sound objects, dialog) |

## Milestone 5 (done): objects, characters and AI

### From data to objects in the world
Spawning works in three layers:

1. **Archetype**: the data from an `.xmb` element such as `<character-archetype>` (read by the type handlers of Milestone 4).
2. **Type object**: built once per archetype by `ArchetypeLibrary_CreateTypes` (0x160570) through
   **`ObjectFactory_CreateType` (0x15FDE0)**, a switch on the archetype type (`AT_*`):

   | Archetype type | Creator | Type object |
   | --- | --- | --- |
   | `AT_CHARACTER` | `CharacterType_Create` | 0x710 bytes: skeleton bones, hardpoints, hero names, reticule |
   | `AT_WEAPON` / `AT_TURRET` | `WeaponType_Create` / `TurretType_Create` | shared `WeaponType_ctor` |
   | `AT_GAME` / `AT_PHYSICAL`, `AT_PLATFORM`, `AT_PROJECTILEMESH`, `AT_WORLDBUTTON` | `GameObjectType_Create` and variants | shared `GameObjectType_ctor` |
   | `AT_INVENTORY` / `AT_INVENTORYGRENADE` | `InventoryType_Create` / `InventoryGrenadeType_Create` | shared `InventoryType_ctor` |
   | `AT_EFFECT`, `AT_LIGHT`, `AT_HEAD`, `AT_DECAL`, `AT_PARTICLEFOG`, `AT_WATERLAVAFLOW`, `AT_BILLBOARD`, cameras | one creator each | |
   | `AT_PATH`, `AT_TRIGGER`, `AT_WPOINT` | `NavObjectType_Create` | added with `World_AddNavObject` |

3. **Instance**: the object in the level, made by the type's vtable slot 2 (`CreateInstance`), then set up with the
   type's `vtbl+0x1C` (`SetupInstance`). A character instance is 0x860 bytes (`Character_ctor`), a weapon 0x384, an
   inventory item 0x1DC, a generic game object 0x1C4.

### The entity update
`World_Tick` calls **`World_UpdateObjects` (0xD9840)** once per tick. It walks the world's object list and calls each
object's **Update (`vtbl+0x6C`)**, skipping objects already updated this tick or inactive. An Update that returns false
removes the object (`World_RemoveObject`). The Updates per class:

| Class | Update | Size of what it reaches |
| --- | --- | --- |
| Character | `Character_Update` 0x11CAA0 | largest by far: calls `GameObject_Update`, then movement, animation/IK (left/right sides), weapons, effects |
| Weapon | `Weapon_Update` 0x22EE30 | large |
| Inventory item, head, game object, water/lava flow | `InventoryItem_Update`, `Head_Update`, `GameObject_Update`, `WaterLavaFlow_Update` | medium |
| Effect, light, particle fog | `Effect_Update`, `Light_Update`, `ParticleFog_Update` | small |

### Characters and AI
Each character has a **controller** at `+0x830` (`Character_SetController` / `Character_GetController`).
`Character_Init` creates the default **AI controller** (`AIController_ctor`, 0x9E0 bytes) when the character has none;
a player-controlled character gets its controller from elsewhere.

- `AIController_Think` (vtable+0x04, inferred): the per-tick AI decision step.
- `AIController_OnEvent` (vtable+0x1C): reacts to game events (`EVT_*`). A 95-case switch picks the **chatter line** to say;
  the tags it uses (`ENEMY_SEEN`, `ENEMY_NOISE`, `SHOT_AT`, `GRENADE_NEAR`, `LEADER_KILLED`, `SOUND_ALARM`) are the same
  `line_tag` values found in the converted `*_chatter.xml` files.
- `AI_LoadData` (0x4F2E0, from `Game_LoadStartupData`): loads `common\ai\personality.xmb`, which maps each event to the
  goal a character takes (`EVT_ALARM_CLOSE_RANGE` -> `GOAL_INVESTIGATE`...).

Most AI code lives at **0x12000–0x60000** (navigation, goals, squads, chatter).

### Correction to Milestone 4
The list at `world+0x14C/0x150` holds **world points of two subtypes**, not entities; entities are updated by
`World_UpdateObjects`. The queue at `world+0x4` is still unidentified.

## Milestone 6 (done): AI goals

### Events in, goals out
The AI is **event-driven**. Game code creates an `AIEvent` (`AIEvent_ctor`, type = an `EVT_*` value such as
`EVT_ENEMY_SEEN`, `EVT_SHOT_AT`, `EVT_HEALTH_LOW`, `EVT_GRENADE_NEAR` or a squad order) and posts it to a character's
AI with `AI_PostEvent`. `AIController_OnEvent` handles it: it may say a chatter line, passes the event to the current goal
(`AI_GoalOnEvent`), and can switch goals with **`AI_SetGoal`**.

Which goal an event leads to comes from the **personality data** (`common\ai\personality.xmb`). With the names recovered
in this milestone it now converts as:

```xml
<personalities>
  <personality>
    <name>squad_guard</name>
    <inherit>#e1cd5297</inherit>          <!-- parent personality (name not recovered) -->
    <table>
      <_143df498>                          <!-- one entry per event (tag name still unknown) -->
        <event>EVT_GRENADE_NEAR</event>
        <priority>28.0</priority>
        <decay>86.860001</decay>
        <goal-entries>
          <goal>GOAL_DIVE</goal>
          <goal-priority>1.0</goal-priority>
        </goal-entries>
```

19 of the 64 personality names were recovered: the squad behaviours `squad_follow`, `squad_guard`, `squad_wingman`,
per-character ones such as `ms_brutus`, `ms_militia`, `ms_priest`, `ms_hound`, `ms_supermutant`, and `test_idle`.
**Run `convert_all.bat` again** to get these names in your converted files.

### The goal stack
Each AI keeps a **stack of goals** (`std::deque` of `AIGoal` objects, 0xA0 bytes, goal id at `+0x5C`):
`GoalStack_Push`, `GoalStack_Top` (the current goal; 157 callers), `GoalStack_Clear`. A new goal goes on top
(`AI_SetGoal` / `AI_StartGoal`); when it finishes, the one below resumes.

### The goal table: four handlers per goal
`AIGoalTable_ctor` builds a table with one entry per `GOAL_*` value (135), each holding four handlers:

| Slot | Name | Called by |
| --- | --- | --- |
| 0 | **Begin** | `AI_StartGoal`, when the goal becomes active |
| 1 | **End** | `AI_RunGoal` when the goal is finished, `AI_SetGoal` when it is replaced |
| 2 | **Update** | `AI_RunGoal`, every tick (from `AIController_Think`) |
| 3 | **OnEvent** | `AI_GoalOnEvent`, for each event while the goal is active |

The handlers are virtual methods of the AI's goal executor (`g_vt_AIGoalExecutor`). The script names all 209 that have
their own code as `AIGoal_<Goal>_<Begin|End|Update|OnEvent>`, for example `AIGoal_AttackCover_Update`,
`AIGoal_ThrowGrenade_Begin`, `AIGoal_AttackSnipe_Update`, `AIGoal_Flee_OnEvent`. Goals that share behaviour share handlers
(the comment lists every goal using it): the dodge variants, the `GOAL_GOTO_*` moves, `GOAL_FV_IDLE` = `GOAL_IDLE`.
The `ATTACK_DATA_*` and `IDLE_VARY_DATA_*` values are sub-steps run inside `GOAL_ATTACK_VARY` and `GOAL_IDLE_VARY`
(their switches are in `AIGoal_IdleVary_*` / attack code), so their own table entries are the do-nothing default.

Goal families (from the enum): idle/patrol/wander, investigate/alert, attack variants (cover, snipe, strafe, guard,
grenade, molotov, melee-style boss attacks for the Shadoon, supermutant and priest), evade/flee/hide/dodge/dive,
squad (follow, wingman, formation, reform), movement (goto cover/vantage/snipe/stealth, lifts and elevators),
and scripted motions.

## Milestone 7 (done): weapons, projectiles and health

### Firing
`Weapon_Update` -> `Weapon_UpdateTrigger` (fires while the trigger is held, `weapon+0x224` bit 2) -> `Weapon_Fire` ->
**`Weapon_FireBullet`**, a switch on the bullet type from the weapon's `bullet-data` (`BTYPE_*`):

| Bullet type | What happens |
| --- | --- |
| `BTYPE_PROJECTILE`, `BTYPE_PHYSICAL_PROJECTILE` | `Weapon_SpawnProjectile`: a record in the projectile manager |
| `BTYPE_MESH_PROJECTILE` | `Weapon_SpawnMeshProjectile`: spawns the bullet data's projectile archetype as a real, visible object |
| `BTYPE_S_BEAM_EFFECT` | beam case |
| `BTYPE_TRIGGER_ONLY` | `Weapon_FireTriggerOnly` |
| `BTYPE_RAY_DO_NOT_USE` | nothing, as its name says |

### Projectiles
World subsystem `+0x1FC` is the **projectile manager** (`ProjectileManager_ctor`). Each tick (`ProjectileManager_Tick`,
called from `World_Tick`) every projectile moves (`Projectile_Step`), is tested for collisions through the physics library
(`Projectile_Collide`), and hits are handled by `Projectile_ProcessHit`, which maps the physics body back to the game
object it belongs to.

### Health, stamina and combat messages
- Every object's archetype has a **`combat-target`** block (archetype `+0x50`): `hitpoints`, `target-type`, a
  `shield` (`EST_ENERGY_SHIELD` / `EST_STAMINA_SHIELD`, max charge, recharge with `delay`) and **`vulnerabilities`**: a list
  of `vulnerability` entries `{type: DTYPE_*, modifier}`, e.g. an item with `DTYPE_EXPLOSION` 10.0 and `DTYPE_BLADED` 0.25.
  These names were recovered in this milestone; run `convert_all.bat` again to see them.
- Characters also have **stamina** (`current-stamina` `+0x490`, `max-stamina` `+0x494`): `Character_SetStamina`,
  `Character_SetMaxStamina`, a per-tick drain and regeneration, and inventory items that restore it.
- Combat events reach the character as **`CBT_*` messages** (`CBT_DEAD`, `CBT_STUNNED`, `CBT_ARMOR_DEPLETED`,
  `CBT_DAMAGED`, `CBT_OUT_OF_AMMO`, `CBT_OUT_OF_CHARGE`) in `Character_OnCombatMessage`. On `CBT_DAMAGED` it computes the
  health percentage (current x 100 / max) and tells the AI (`EVT_HEALTH_PCT_75/50/25`), which is how the personalities
  react to being hurt.

Damage itself is applied by `CombatTarget_TakeDamage` (Milestone 8).

## Milestone 8 (done): damage

### From trigger to hit points
1. **Roll** (`Weapon_RollDamage`): a random amount between the weapon's min and max damage (the game's random generator,
   `g_randomSeed`), times the shooter's factors when the shooter is a character, then through any active modifiers.
   The value travels with the projectile (`projectile+0x20`).
2. **Travel**: `Projectile_GetDamage` lowers it with distance for weapons that have falloff.
3. **Hit**: `Projectile_RecordHit` notes which body part was struck and its multiplier
   (`Character_GetHitPart`, `Character_GetHitMultiplier`: location-based damage such as headshots).
   `Weapon_BuildDamageRecord` fills a damage record (`+0x04` damage type, `+0x08` amount) with splash falloff.
4. **Apply**: every game object has a **combat target** at `+0x48`; slot 0 of its vtable takes the damage.
   Characters override it (`Character_TakeDamage`: ignored while disabled, plus hit reactions) and then call the shared
   **`CombatTarget_TakeDamage`**:

```
damage = shield.Absorb(damage, type)                 // shield at +0x18, absorbs first
damage = damage * vulnerability[type]               // the <vulnerabilities> data, per DTYPE_*
if (gasImmune && type == DTYPE_GAS) damage = 0
if (damage != 0) send CBT_DAMAGED
if (damage >= health) { health = 0; send CBT_DEAD }
else health = health - damage                         // CombatTarget_SetHealth, capped at max health
```

5. **React**: `Character_OnCombatMessage` turns `CBT_DAMAGED` into the AI's `EVT_HEALTH_PCT_*` events (Milestone 7),
   and `CBT_DEAD` is broadcast to the world.

A character's health is at `character+0x54` and its maximum at `+0x50` (the combat target's `+0x0C` / `+0x08`); stamina
(`+0x490`) is a separate meter.

Still open: the energy/stamina shield classes. The only shield class found so far is the base one (`Shield_Absorb`
returns the damage unchanged); the real shields are probably implemented by the equipped shield item.

## Milestone 9 (done): shields

### Where a shield comes from
- An object's `<target-data>` can hold a `<shield>` with an `<energy-shield>` inside: `shield-type` (`EST_NONE`, `EST_ENERGY_SHIELD`, `EST_STAMINA_SHIELD`), `max-charge`, `active-effect`, `hit-effect`, a `<recharge>` block (`enabled`, `delay`, `sound-effect` and three fields whose names are still unknown) and its own `<vulnerabilities>`.
- `CombatProfile_InitFromTargetData` copies that into the object's combat profile (the object pointed to by combat target `+0x1C`). It also builds a `ShieldType` at profile `+0x5C`, which holds the shield data plus 12 absorb factors, one per `DTYPE_*` (default 1.0).
- `GameObject_CreateShield` (called from `GameObjectType_SetupInstance`) calls `Shield_Create` and stores the shield at combat target `+0x18` (object `+0x60`). The shield then starts fully charged.
- Items can give a shield too: inventory effect kind 24 (`InvEffectShield_Apply` / `_Remove`).

### The two shield classes
Each class has 7 virtual methods: destructor, Absorb, GetHitEffect, Update, GetActiveEffect, GetEffectStrength and one more effect getter. Slot 1 is what `CombatTarget_TakeDamage` calls first.

| | Energy shield (`EnergyShield_*`) | Stamina shield (`StaminaShield_*`) |
|---|---|---|
| Stores | its own charge at `+0x10` | nothing: it uses the owner character's stamina (`+0x490`) |
| Absorb | If the charge is greater than the damage, the charge drops by damage × the absorb factor for that damage type, and no damage gets through. Otherwise the charge empties, a depletion delay is added, and the rest of the damage gets through. Every hit also restarts the recharge delay. | Takes min(damage, stamina) out of stamina. The rest of the damage gets through. |
| Update | Counts the delay down, then recharges per second up to `max-charge`. It keeps the looping effect alive while there is charge. | Binds to its owner (characters only) and sets the effect strength. |
| Effect strength | 0.25 + 0.75 × charge / max | stamina / max stamina |

The stamina shield class derives from the energy shield class, but it replaces Absorb and Update. Shield updates run from `GameObject_Update`.

### Character_Update so far
In order, `Character_Update` does the following:
1. Removes and deletes entries in the list at `+0x50C` whose `+0x10` is 0.
2. Calls `GameObject_Update`. If that returns false, the character update stops there.
3. Calls `Character_UpdateTimedEvents`.
4. Runs a check (`0x11BD00`). If it returns false, the character update stops there.
5. If flags `+0x7D8 & 3` are set, it only updates the list at `+0x1F4` and stops. What these flags mean is not known yet.
6. Calls `Character_UpdateStaminaRegen`, `Character_UpdateFrameCountdown` and several unnamed helpers.
7. Runs a block on a 0.133 s period (`0x11C540`).
8. Runs aiming/look-at work on fields `+0x64C`–`+0x698`.
9. Runs the large movement block. This block is skipped when `+0x7D8 & 0x10` is set.
10. Calls `Character_UpdateMotion` (the animation state machine; see Milestone 12).
11. Does late per-tick work, including lookups through world `+0xA40`.
12. Updates the model pose (`[+0x6C]+0x10`).

Only the helpers with clear evidence are named. The rest of the split is still open.

## Milestone 10 (done): how the AI chooses a goal

### The personality data
`common\ai\personality.xmb` holds 65 personalities. Each has a `<table>` of entries, and each entry has:
- `event`: the `EVT_*` it reacts to.
- `priority`: how urgent the event is. Values run from -96 to 100; "involuntary" reactions like knockdowns are 100.
- `decay`: how fast that urgency fades, per second.
- `goal-entries`: up to 10 `{goal, goal-priority}` pairs.
- a `se-table` of spawn-events.
- an attack-data list (`ATTACK_DATA_*` goals with a weight). Its element names are still hashes.

At runtime an entry stores priority at `+0x04`, decay at `+0x08` and the goal pairs at `+0x0C`.

### From event to goal
1. **Queue.** `AIController_OnEvent` calls `AIController_QueueEvent`, which calls `AI_PrioritizeEvent`.
   - If the current personality has an entry for the event, the event gets that entry's priority and decay. It is then added to the AI's event queue (AI `+0x404`), a heap sorted by priority.
   - If the personality has no entry for the event, the AI ignores it.
   - If the same event is already queued, the two are merged.
2. **Decay.** Every tick, `EventQueue_Update` reduces each event's priority by decay × dt.
   - When the priority drops below 0, the event expires: it gets priority FLT_MAX so it rises to the top, and then it is deleted.
   - **Decay 86.86 is a magic value meaning "never decays".** 156 of the 320 entries use it.
3. **Arbitrate.** `AI_ArbitrateGoals` checks whether the current goal can be interrupted and whether the top event is a different one from the event that goal is serving. If both are true, it calls `AI_ChooseGoalForTopEvent`.
4. **Pick.** `Personality_PickGoal` makes a weighted random choice among the entry's goals, using `goal-priority` as the weight. So `{GOAL_ATTACK 3, GOAL_HIDE 1}` means attack 75% of the time. If all weights are 0, it takes the first listed goal.
5. **Special goals.**
   - `GOAL_NOOP` or `GOAL_INVALID` drop the event.
   - `GOAL_BASE` sends the AI back to its base goal.
   - Any other goal is built and started, which calls the goal's Begin handler from Milestone 6.

## Milestone 11 (done): what happens after a goal is chosen

Each personality entry carries two more lists besides its goals. Both run right after `AI_ChooseGoalForTopEvent` picks a goal.

### Spawn-events (`se-table`)
`AI_SpawnEvents` reads the entry's `{event, goal}` pairs (entry `+0x64`):
- If a pair's goal is `GOAL_DELETE_EVENT`, every queued event of that type is forgotten (`EventQueue_RemoveEventType`).
- Otherwise, if the pair's goal is empty or matches the goal just chosen, the triggering event is copied, retyped to the pair's event, and posted through the world message system.

Designers use this to chain behaviour. For example, a goal can raise `EVT_USER0`, which another entry reacts to, or clear the `EVT_USER*` flags when it is done.

### Parameter list (`_f48f8744` in the converted XML)
These are 12-byte records at entry `+0x70`: `{key hash, goal, float_value}`. The element names `_f48f8744`, `_edf09800` and `_fef92c8e` are still hashes.
- **Settings.** `AI_ApplyEntryParams` applies every record whose goal is empty or matches the chosen goal (`AI_ApplyParam`). The 21 keys set AI fields and flags, start timers, switch the AI mode (`+0x460`), or broadcast the event's source to others. The Ghidra comment on `AI_ApplyParam` lists what the known keys write. Their text names are unknown: a dictionary attack (pairs from about 2,100 game words, triples from 178 AI words) found nothing.
- **Attack and idle variation.** Records keyed `0xED46A645` pair an `ATTACK_DATA_*` (or `IDLE_VARY_DATA_*`) goal with a number. `AIGoal_AttackVary_PickMove` makes a weighted choice of the move (crouch, dodge, run sideways, stand still...) and then runs it for that many seconds (`EntryParams_GetValue`, `AITimer_Start`). This lookup matches by goal only; it ignores the key. This is why enemies in a fight keep switching between crouching, strafing and dodging.

### Correction to Milestone 10
`GOAL_BASE` really does forget pending sound, alarm and dead-body events (`EventQueue_RemoveEventTypes`). This is now confirmed.

## Milestone 12 (done): turning, aiming and motions

### Motions (animation states)
A character's animation is driven by **motion records**. Each record starts with its `MT_*` id: `MT_IDLE`, `MT_RUN`, `MT_WALK`, `MT_CROUCH`, `MT_DODGE_LEFT`, `MT_JUMP_LAUNCH`, `MT_KNOCKDOWN`, `MT_DIE0`...
- **Fields.** The current motion is at character `+0x1FC` and the requested one at `+0x200`.
- **State machine.** `Character_UpdateMotion` runs every tick. It applies requests, runs transitions and overlay animations, and picks standing motions when the stand timer runs out.
- **Stamina.** It also drains stamina by the current motion's cost (record `+0x25C`). Running costs stamina; standing doesn't.
- **Tired fallback.** If a requested motion costs stamina and the character has less than 5% left, `Character_ApplyMotionRequest` swaps in the character type's "tired" motion (type `+0x2B8`). That's why exhausted characters can't sprint.
- **Name correction.** Milestone 7 called `0x11A440` `Character_UpdateStaminaDrain`. It is really this whole motion state machine, so it is now named `Character_UpdateMotion`.

### Turning and aiming
- The character's facing and aim are kept as a 3×3 matrix at `+0x5BC`, plus a pitch angle at `+0x5F4`.
- `Character_UpdateTurning` turns the matrix about the up axis by turn input (`+0x5F8`) × turn speed (`+0x7EC`) × dt. It keeps the current pitch while doing so.
- `Character_SetAimPitch` clamps looking up and down to ±60°.

### Math library
Named the vector, matrix and angle helpers used throughout: `Vec3_Normalized`, `Vec3_NormalizeInPlace`, `Vec3_AngleBetween`, `Mat3_MulVec`, `Mat3_MulVecTransposed`, `Mat3_GetColumn2`, `Mat3_RotateX` (pitch), `Mat3_RotateY` (yaw), `Angle_Wrap2Pi` and `Angle_WrapPi`. Matrices are 3×3 row-major floats.

### Movement data
Each character type has a list of 4 speeds. For example, Brutus has 3.5, 4.55, 7.8 and 8.7; they likely go from slowest to fastest gait. The list is stored at archetype `+0x3A8`, and its element name is still a hash (`_14708eb2`). The collision capsule (radius, height, crouch-height, offset) is in `_0f7a74c1`. Where the speeds are read at runtime is not found yet.

## Milestone 13 (done): player input and character actions

### From the gamepad to the character
- **Reading the stick.** `CharacterType_HandlePlayerInput` runs per tick for a player-controlled character. It finds that player's input slot (`g_pInputManager +0x250 + player*4`) and reads four analog axes with `PlayerInput_GetAxis`: `VB_MOVE_AXIS`, `VB_STRAFE_AXIS`, `VB_TURN_AXIS` and `VB_LOOK_AXIS`. The look axis is scaled by the player's look sensitivity. The `<movement>` block in `game-options-*.xmb` maps these virtual buttons to physical sticks, per control scheme.
- **Shaping the stick.**
  - `Input_SquareToCircle` maps the square stick range onto a circle.
  - `Input_ResponseCurve` applies f(x) = x²(2 − x). Full tilt still gives 1, but half tilt gives only 0.375. That's why small stick movements give fine aim.
- **Buttons become actions.** Button presses become calls to `Character_DoAction`. The AI goals call the same function (60 call sites in total), so the player and the AI drive characters through one interface.

### `Character_DoAction(action, value)` (character vtable +0x15C)
The value is an analog amount. Above 65 the full-speed motion is used; below 65 the walk or `_SLOW` variant is used.

| Action | Effect |
|---|---|
| 0 | `MT_IDLE` |
| 1 | `MT_STOP` |
| 3 | forward: `MT_RUN` (value > 65) or `MT_WALK` |
| 4 / 5 | turn 45° left / right (`Character_TurnBy`) |
| 6 / 7 | strafe left / right (`MT_MOVE_LEFT[_SLOW]`, `MT_MOVE_RIGHT[_SLOW]`) |
| 8 | special on/off overlay (`OMT_SPECIAL_ON`/`OFF`, `MT_TOGGLE_SPECIAL`) |
| 12 | back: `MT_BACKPEDAL` or `MT_BACKSTEP` |
| 13 / 14 | back-left / back-right (`MT_MOVE_LEFT_BACK[_SLOW]`, `MT_MOVE_RIGHT_BACK[_SLOW]`) |
| 15 | `MT_CROUCH` |
| 16 | zoom in one level (up to the weapon's zoom count) |
| 19 | zoom out (`Character_ZoomOut`), or a stance change when not zoomed |
| 23 / 24 | `MT_DODGE_LEFT` / `MT_DODGE_RIGHT` |
| 25 | `MT_DIZZY` |
| 26 | `MT_COWER` |
| 27 | overlay `OMT_ALERT` |
| 28 | `MT_KNOCKDOWN` |
| 29 | `MT_DIVE` |
| 30 | `MT_WORK` |
| 31 / 32 | overlays `OMT_LOOK_LEFT` / `OMT_LOOK_RIGHT` |
| 33 / 34 | `MT_DODGE_SHORT_LEFT` / `MT_DODGE_SHORT_RIGHT` |
| 35 | `MT_SURPRISE` |

Actions 2, 9, 10, 11, 17, 18, 20, 21 and 22 do other things and are not decoded yet. Motion-requesting actions go through `CharacterType_RequestMotion`, which sets the requested motion that `Character_UpdateMotion` (Milestone 12) picks up.

## Milestone 14 (done): world subsystems

`World_Tick` updates the world's subsystems in a fixed order every simulation tick. Fields marked "?" are ticked, but what they hold isn't known yet.

| Order | World field | Subsystem | Tick function |
|---|---|---|---|
| 1 | `+0xBF0` | ? | `0x73330` |
| 2 | `+0xBEC` | player manager (maps a character to its player record) | vtbl+0x28 |
| 3 | `+0xBE0` | ? | `0x149E60` |
| 4 | `+0xBF4` | **message system**: AI events, combat messages, spawn-events | `MessageSystem_Tick` |
| 5 | `+0xA40` | ? | `0x1F48F0` |
| 6 | `+0xBE8` | ? | `0x140D70` |
| 7 | `+0xA44` | static objects | `0x169E20` |
| 8 | `+0x1F4` | ? | `0xCDF90` |
| 9 | `+0xA48` | ? | `0xD2040` |
| 10 | `+0x1FC` | projectiles | `ProjectileManager_Tick` |
| 11 | `+0x200` | ? | `0x2173A0` |
| 12 | `+0xA58` | **camera system** | `CameraSystem_Tick` |
| 13 | `+0x1F8` | ? (AI-side code) | `0x57A30` |
| 14 | — | all game objects | `World_UpdateObjects` |
| 15 | `+0x20C` | scene scripts | `SceneScriptManager_Tick` |
| 16 | `+0xB80` | effects | vtbl+0x04 |
| 17 | `+0xBDC` | squads | `Squad_Tick` |
| 18 | `+0xBE4` | **view manager**: one view and camera per player character | `ViewManager_Tick` |
| 19 | `+0xC04` | world sound (drives `g_pSoundManager`) | `WorldSound_Tick` |

The camera system, the view manager (`ViewManager_FindForCharacter`) and the message system (`MessageSystem_Post`) are now named.

## Milestone 15 (done): textures (`.tex`) and PNG export

### Format
- **Two files per level.** Each level or archive has a texture library: `textures-NAME.xmb` (the index) and `textures-NAME.tex` (raw pixel data, no header).
- **The index.** It lists every `<optimized-texture>` with:
  - its name hash
  - its `format` (an Xbox `D3DFORMAT` number)
  - width, height
  - mip count (attribute `_120336ef`)
  - offset and size in the `.tex` file
- **Mip chains.** Each texture is its full mip chain, top level first.

| format | Xbox format | Count | Notes |
|---|---|---|---|
| 14 | DXT2/3 | 8,229 | most textures |
| 11 | P8 | 2,486 | greyscale bump maps (no palette stored) |
| 25 | A8 | 1,490 | alpha only, swizzled |
| 12 | DXT1 | 152 | |
| 15 | DXT4/5 | 137 | |
| 7 / 6 | X8R8G8B8 / A8R8G8B8 | 30 | swizzled (Morton order, x in the lowest bit) |
| 0 | L8 | 19 | swizzled grey |
| 18 | LIN_A8R8G8B8 | 3 | linear |
| −n | cube map of format n | 99 | six faces, each mip chain padded to 128 bytes |
| 46 | — | 1 | empty render-target placeholder |
| −1000 | ? | 1 | unknown, skipped |

`<animated-texture>` entries only list frames of other textures (`<frame-texture>`).

### Exporting
`export_textures.bat` (in the recomp folder) runs `tools\tex2png.py` over the converted data folder made by `convert_all.bat`. Every `textures-NAME.xml` gets a `textures-NAME_png` folder next to it with one PNG per texture (top mip level).
- Cube maps are written as six faces side by side (`_cube.png`).
- P8 bump maps are written as greyscale (`_p8.png`).
- Textures without a recovered name are named by their hash.

Tested on all 61 libraries: **12,645 textures written, 0 failures**, in about 3½ minutes. The script uses only the Python standard library.

## Milestone 16 (done): 3D models (`.ivd`) and OBJ export

### Format
- **Files.** Each `objects-NAME.xmb` holds a `<mesh-library>` (`data-file` = `objects-NAME.ivd`) and the archetypes that use its meshes.
- **Mesh structure.** A `<mesh>` has one or more `<geoset>`s. Each geoset has a `material-name` and a list of draw batches.
- **Batches.** Each batch gives, in order:
  - primitive (`#1d62e437` = triangle strip, `#0e7ab726` = triangle list)
  - index count
  - index byte offset
  - `vertex-type`
  - vertex count
  - vertex byte offset
- **The `.ivd` file.** It holds all the 16-bit index data first, then the vertex buffers.
- **The `.ipn` file.** Not needed to rebuild the geometry; it is probably precompiled Xbox push-buffer data.

| vertex-type | Size | Layout | Batches |
|---|---|---|---|
| `#e90647a9` | 32 | position float3, packed normal (11:11:10), uv float2, 8 more bytes | 20,289 |
| `#ef44f398` | 32 | terrain: packed grid cell + height (decoded in Milestone 22) | 14,275 |
| `#1fe75072` | 24 | position float3, packed normal, uv float2 | 10,227 |
| `#13d18cfc` | 32 | as `#e90647a9`; the extra bytes look like skin/colour data | 2,054 |
| `#e7fd942a` | 32 | as above | 8 |
| `#0ade1ce7` | 16 | position, packed normal | 1 |

### Exporting
`export_models.bat` runs `tools\mesh2obj.py`. It writes `objects-NAME.obj` and `.mtl` next to each library, with one object per mesh, normals and UVs. The game's left-handed coordinates are mirrored on Z so models open the right way round in Blender and similar tools.

Tested on all 60 libraries: **12,390 meshes written** in about 13 seconds. Vehicles, tanks, pipes, creatures and plants all come out correctly.

Two known gaps:
- **Compressed meshes.** Solved in Milestone 22: the terrain vertex type `#ef44f398` is now exported.
- **Textures.** Solved in Milestone 17: the MTL files now link each material's colour texture.

## Milestone 17 (done): model textures, and the shared-buffer vertex type

### Where meshes get their textures
- **Paired materials.** Every mesh material (type `#eb58ab52`) is immediately followed in its `materials-NAME.xmb` by the shader material that holds its textures (`BF_CS_rtMaterial`, `BF_CSB_rtMaterial`, `BF_DetailTextureBlend`, ...). The pattern holds for every material in every library checked; for example, e01 has 454 materials = 227 pairs.
- **Exporter.** `mesh2obj.py` now follows that pairing. The MTL files link the `color` texture PNG made by `export_textures.bat` for **7,644 of 7,902 materials (97%)**. Test renders show the right textures on vehicles, tanks, pipes and plants.
- **Order matters.** Run `export_textures.bat` before `export_models.bat`, so the PNGs exist when the models are written.

### Vertex type `#ef44f398` (solved in Milestone 22; notes kept for history)
- **Shared buffer.** All batches of a geoset share one vertex buffer. The batch's "vertex count" is really the highest index + 1, and the stride is 32 bytes. That explains the odd sizes seen in Milestone 16.
- **Known fields.**
  - bytes 0–3: probably a colour
  - bytes 4–7: packed normal (11:11:10, unit length)
  - bytes 8–15: uv set 0
  - bytes 16–23: uv set 1 (about 3.6× set 0)
  - bytes 24–31: unknown
- **Terrain.** These meshes are terrain: uv set 0 is a planar projection of the ground.
- **Position (partly decoded).** Bytes 0–3 hold the position as two 16-bit fields:
  - the first (unsigned) follows the ground's x axis
  - the second (signed, about ±1,100) gives believable hills, valleys and a canyon when used as height
  - a third axis and the real scale are applied by the vertex shader: `0x91070` loads vectors from the geoset's runtime object (`+0x34`, `+0x40`) into shader constants c24/c25
  - one seam remains when the second field is read alone, so the exact packing still needs those constants
  - both 16-bit fields leave a gap around zero: the second field only takes values in [-1124, -433] and [374, 1124], and the first only in [0, ~16400] and [~45000, 65535] read as unsigned. The sign seems to select a half of the terrain, so this is a packed encoding rather than plain coordinates. Used as a ground plane, the pair gives badly shaped triangles; uv set 0 gives good ones.
- **Unknown.** Bytes 24–31 are unknown, possibly tangents.
- **Formats built at run time.** The game builds its vertex input formats while running (table `0x3E2300` → formats at `0x3C9060`+), so they can't be read from the file.
- **Status.** Superseded by Milestone 22, which decodes the position exactly.

## Milestone 19 (done): animations (`.chnl`) and JSON export

### Format
Each `animations-NAME.xmb` lists:
- **`<channel>`s**: each has a data type, a sampling kind, `offset`/`size` in `animations-NAME.chnl`, `num-frames`, and the frame time (about 1/15 s) for evenly sampled channels.
- **Clips (`<archetype>`)**: each has a `duration`, a `root-height`, an optional `event-channel`, and `<target>`s that bind a bone (or `root`) to a channel.

| Sampling kind | Meaning |
|---|---|
| `#f8b95953` | evenly sampled: frame *i* is at *i* × frame time |
| `#00ad833c` | keyed: every key starts with its time (float32) |

| Data type | Per frame | Count |
|---|---|---|
| `#06677bcb` rotation | compressed quaternion: x, y, z as int16 / 32767; w = √(1 − x² − y² − z²) | 169,932 |
| `#06901441` transform | position float3, then compressed quaternion (root motion) | 4,159 |
| `#f36d8810` vector | float3 | 2,700 |
| `#007162cc` events | time + event name hash | 1,403 |
| `#fed99c09`, `#e0b5650b` | single float | 89 |

The skeletons are 3ds Max Bipeds. 61 bone names were recovered from their hashes (`Bip01`, `Bip01 Pelvis`, `Bip01 L Thigh`, `Bip01 R Hand`, `Bip01 Head`, ...). They cover 81% of all animation tracks and are now in `names.json`.

### Exporting
`export_animations.bat` runs `tools\anim2json.py`. Every `animations-NAME.xml` gets an `animations-NAME_json` folder with a sub-folder per animation set (see Milestone 21) and one JSON file per clip, containing:
- name and duration
- events
- per bone: track type, key times, and values (quaternions as x, y, z, w; positions as x, y, z)

Tested on all 53 libraries:
- 6,107 clips and every track decoded.
- All 1.9 million rotations are unit quaternions.
- Every track ends exactly at its clip's duration.

Re-running `convert_all.bat` will also show the bone names in the converted XML.

## Milestone 20 (done): skeletons, and animations you can play in Blender

### Skeletons
- **Where they are.** A character's skeleton is an archetype in the mesh libraries (`objects-*.xmb`) built from `<PART>`s joined by `<joint>`s. Each joint names a `parent-part` and `child-part` (Biped bones such as `Bip01 Pelvis` → `Bip01 Spine`) and gives the bone's offset in its parent's space (`parent-point`). The joints' `ORIENTATION` is identity, so the animation quaternions are the bones' full local rotations.
- **How many.** 719 jointed archetypes were found, including vehicles and machines.
- **Linking clips to characters.** A character type in `objecttypes-*.xmb` names its skeleton archetype (`mesh-name`) and its motion sequence, whose name is its animation set (see Milestone 21). That links 41 animation sets to their character's skeleton.

### Export to BVH
`export_animations.bat` now also runs `tools\anim2bvh.py`. Every `animations-NAME.xml` gets an `animations-NAME_bvh` folder with a sub-folder per animation set and one BVH file per clip, at 30 fps in metres. The character stands on the ground (y = 0).
- **Opening one.** In Blender: *File → Import → Motion Capture (.bvh)*. It creates the armature and plays the motion.
- **Which skeleton.** Each clip uses the skeleton of the character type that owns its animation set. If there is none, it uses the smallest skeleton that has every bone the clip animates.
- **Results.** On all 53 libraries: **5,628 clips written**. 397 clips have no skeleton with all their bones, mostly non-Biped creatures with unknown bone names.
- **Verified.** The BVH files were read back, posed and rendered; they give the same motion as posing the decoded data directly (for example a creature's stepping cycle).

## Milestone 21 (done): rigged characters (glTF) with all their animations

### Skinned meshes
- **Vertex layout.** Character meshes use vertex type `#13d18cfc` (32 bytes):

  | Bytes | Field |
  |---|---|
  | 0–11 | position (float3) |
  | 12–15 | packed normal (11:11:10) |
  | 16–23 | uv (float2) |
  | 24–27 | 4 bone weights (bytes, sum 255) |
  | 28–31 | 4 bone slots (byte = 5 × slot + 1) |

- **Bone palettes.** Each mesh has one or more palettes (`_f1330520`, first child) listing part indices of the skeleton. Each batch names its palette in the field after its vertex offset. Meshes with many bones (Tex, Brutus, Shadoon) need two.
- **Push-buffer indices.** Their index data is a raw NV2A push-buffer rather than a plain index list. When a batch's index count is above 0xFFFF, it packs *(push-buffer bytes << 16) \| index count*. The commands are:
  - `0x17FC` SET_BEGIN_END: 5 = triangle list, 6 = strip, 0 = end
  - `0x1800` ARRAY_ELEMENT16: two indices per dword
  - `0x1808` ARRAY_ELEMENT32

  `mesh2obj.py` now reads these too. That brings back the 216 character meshes it used to skip, so **only terrain batches are still skipped**.
- **Inverse bind matrices.** Each bone part's own archetype has 12 floats (`_e4b822af`): rows R0, R1, R2 and t, with *bone-local = R·v + t*. That is the bone's inverse bind matrix. Its inverse gives the bind pose, and its translations match the joints' `parent-point`s exactly.
- **Face and attachment bones.** Bones joined by a fixed joint (type `#e3be168e`) have `parent-point` 0: the eyes, brows, lips and jaw of Tex, Hawk, Flint and Shadoon, and Tex's backpack mounts. Only their bind matrix places them, so the exporter takes every bone's rest position from the bind matrices. Clips that move the backpack mounts use these same positions. A posed vertex is Σ *weight* · *BoneWorld* · (*R·v + t*).

### Which animations belong to which character
- A character type names a **motion sequence** (`motion-archetype`, e.g. `ms_hf` → `characters\ms_hf.xmb`).
- The motion sequence's `<name>` is the name of the **animation set** holding the character's clips (`ms_hf` → set `#1cb6373d`).
- It also lists every motion (`MT_WALK`, `MT_DODGE_LEFT`, …) with its **script**, the clip name. For example, the script `f_side_walk_l` with the weapon prefix becomes `Sc_w1_f_side_walk_l` (`w1`/`w2` = weapon stance, `rp` = ready pose, `cr` = crouched, `sn` = sniper; `bz` is not yet known).

From these scripts, **244 of the 315 clip names** were recovered, along with 129 character, set and weapon names, such as:
- `Sc_w1_walk`, `Sc_w2_reload`, `Sc_w1_rp_idle`
- `hound`, `militia`, `priest`, `e34_hf`, `rail_cannon`

They are in `names.json` (now 4,245 names).

Clip names repeat across sets (every set has its own `Sc_w1_walk`). Before this milestone, BVH and JSON files of different sets in the same library overwrote each other. **They now go into one sub-folder per set**, named after its motion sequence (`hf`, `lf`, `hound`, `tex`, ...).

### Root height
A clip's `root-height` is the pelvis height above the ground. With it, the feet land exactly on y = 0 in every clip tested (walk 0.90, idle 1.12, crouch 0.54). The exports add it to the root, so characters stand on the ground.

### Export
`export_characters.bat` runs `tools\char2gltf.py`. It writes one `.glb` per character into `characters_glb\` in the data folder.
- **What's in each file:**
  - the highest-detail skinned mesh, with its textures (linked to the PNGs from `export_textures.bat`)
  - the skeleton in its bind pose (T-pose)
  - the skin weights
  - every clip of the character's animation set, by name
- **Opening one.** In Blender: *File → Import → glTF 2.0*. Pick a clip in the Action editor or NLA.
- **Results.** 31 characters, about 60 MB:
  - Tex, Brutus, Hawk, Flint
  - hf / lf (Shrike heavy and light soldiers), hound, hunter_lord
  - militia, priest, follower, shadoon, kingman, mutant, feral_colonist, ...

  Characters used by several levels are written once.
- **No clips for two kinds.** The `feral_colonist` and `mutant2` characters export without clips: the game's data has no animation set with their motion sequences' names.
- **Verified.** The files were read back by an independent reader, which checked:
  - every accessor in range, every vertex's weights summing to 1, and no NaNs
  - strictly increasing key times and unit quaternions

  The models were then posed with glTF's own skinning rules. The skinned bind pose matches the raw mesh to within 1 mm for all 31 characters (a T-pose), and walk and run clips render correctly (Tex running, the heavy Shrike walking).

## Milestone 22 (done): terrain

Every level's terrain now exports with the rest of its models, so `export_models.bat` no longer skips anything.

### How it was solved
- **The vertex format.** The game's vertex format table (`g_VertexFormatTable`, 0x3E2300) lists, for the terrain type (index 8, 0x3C9860), what each part of the 32-byte vertex is:

  | Bytes | Shader input | Type | Meaning |
  |---|---|---|---|
  | 0–3 | v0 | SHORT2 | packed position: (a, b) |
  | 4–7 | v2 | NORMPACKED3 | normal |
  | 8–15 | v9 | FLOAT2 | uv set 0 (colour textures) |
  | 16–23 | v10 | FLOAT2 | uv set 1, an exact planar projection of the ground |
  | 24–29 | v11 | NORMSHORT3 | a direction used for lighting (not position) |
  | 30–31 | v12 | PBYTE2 | two blend values |

- **The vertex shader.** The game's 114 vertex shader programs (NV2A microcode) are stored in the XBE. A new disassembler, `tools\vshdis.py`, decodes them: `python tools\vshdis.py default.xbe` lists all programs, and `python tools\vshdis.py default.xbe 0x3D8BF8` prints one. The terrain shader (`vsh_Terrain`, 0x3D8BF8) unpacks v0 with the GPU's EXP instruction, which splits a number into its whole and fractional parts:
  - **x and z:** |a| = z_cell × radix + x_cell. The sign of a is the sign of x, and the sign of b is the sign of z. Both are multiplied by the cell spacing.
  - **height:** y = |b| / 16. The 1/16 is hard-coded in `ShaderMaterial_SetPassConstants` (0x90F50) as c23.w.

  That explains the old puzzles: the "gap around zero" in b was the height (always positive) carrying z's sign, and a held two numbers.
- **Where the scale comes from.** Each level's `levels-NAME.xmb` has a `<Terrain>` entry with:
  - `width` and `length` in blocks of 16 × 16 cells
  - the cell spacing (`_e023cdc9`: 1.0 to 5.0 m)
  - the terrain's `mesh-name`

  The radix is 8 × width + 1, so the grid runs from −8·width to +8·width cells around the level's centre.

### Checks
- **uv set 1.** With this decoding, uv set 1 is an exact linear function of (x, z) in every level, with error 0.0000. The radix 8 × width + 1 is the only one that fits, in all 139 terrain layers.
- **Block heights.** The level file stores each block's minimum and maximum height. For e34 the decoded heights match them to within 1/16 m (for example 48.69/26.19 against 48.700001/26.200001).
- **Triangle edges.** In all 1.14 million terrain triangles, the longest edge spans exactly one block (16 cells). A wrong decoding would wrap points across the map.
- **Renders.** e01, e34, m05_a, m07_a and m14_a render as continuous landscapes of hills, canyons and plateaus.

### Export
`export_models.bat` (`tools\mesh2obj.py`) now writes the terrain:
- **Layers.** Each terrain layer (geoset) becomes its own object, `<mesh>_layerN`, with its material. The layers are texture passes over the same ground, so they overlap; hide all but one in Blender if they flicker.
- **Fallback.** For a terrain with no level entry, the exporter finds the radix from uv set 1.
- **Results.** On all 60 libraries: 12,656 meshes, 139 of them terrain layers, and **0 batches skipped**.

## Milestone 23 (done): particle effects (`.ale`)

### What the file is
`common\effects-common.ale` holds every particle effect in the game: muzzle flashes, impacts, explosions, blood, smoke, grenade trails, beams, shields and more. It is Digital Anvil's **Alchemy** effect system, the same one Freelancer used.

- **Container.** A UTF tree: an `UTF ` header, 44-byte nodes, a string block and a data block. It has two leaves, `AlchemyNodeLibrary` and `ALEffectLib`.
- **AlchemyNodeLibrary (version 1.2): 1,981 nodes.** Each node is:
  - a class, stored as the name hash of the class name
  - a list of properties, ending with a 0 type word

  Each property is a type (u16), the hash of its name (u32) and a value:

  | Type | Value |
  |---|---|
  | 1 | bool (stored in bit 15 of the type) |
  | 2 / 4 | int / enum |
  | 3 | float |
  | 0x103 | string (u16 length, padded to even) |
  | 0x104 | blend pair (D3DBLEND source, destination) |
  | 0x105 | transform: flags, then 9 curves (translate x/y/z, rotate x/y/z, scale x/y/z) |
  | 0x200 / 0x201 | float / colour animation: keys over a particle's life, per point in the effect's time |
  | 0x202 | curve: keyframes with in/out tangents |

- **ALEffectLib (version 1.1): 603 effects.** Each effect has:
  - its name
  - a bounding sphere
  - node references: flags, the node name's hash, the parent index, the index
  - links from each emitter to the appearances and fields it feeds

### Names
- **Property names.** All 90 were found in `default.xbe`, which keeps one table per class of {name, id, type, default} (`g_FxNode_PropertyTable`, 0x40E438, and its neighbours): `Emitter_Frequency`, `ConeEmitter_MaxSpread`, `GeneralApp_Color`, `GravityField_Gravity`, ...
- **Node, effect, class and texture names.** These use the game's usual name hash. That identified the 12 classes:

  | Class | Nodes |
  |---|---|
  | FxGeneralAppearance | 784 |
  | FxConeEmitter | 284 |
  | FxCubeEmitter | 223 |
  | BFCubeEmitter | 169 |
  | FxSphereEmitter | 140 |
  | FxGravityField | 116 |
  | FxTurbulenceField | 98 |
  | FxAirField | 75 |
  | FxParticleAppearance | 31 |
  | BFBeamAppearance | 29 |
  | FxRadialField | 25 |
  | BFKillField | 7 |

  The `BF` classes are Brute Force's own (Freelancer had `FL` ones). The capitalisation of the `BF` prefix is a guess, since the hash ignores case.
- **New names.** 2,592 names were added to `names.json` (effects, nodes, textures). 452 of the 514 effect references in the object types now show their effect's name after re-converting.

### Export
`export_effects.bat` runs `tools\ale2json.py`. It writes `common\effects-common_json\` with one JSON file per effect plus `_index.json`.
- **What's in each file:** the effect's node tree (parents, emitter → appearance links), and each node's class and named properties.
- **Textures.** Each appearance is linked to its texture PNG. For animated textures it gives the frame sheet and the number of frames. All 832 textured appearances are linked.
- **Missing nodes.** 262 node references point to nodes that are not in the shipped file. They are marked `missing`; the game presumably skips them. 343 nodes in the file are never used. Group nodes (reference flag 1) have no definition of their own and are marked `group`.
- **Blend modes.** Most appearances are additive (SRCALPHA, ONE: 547) or alpha-blended (SRCALPHA, INVSRCALPHA: 207).

## Milestone 24 (done): triggers, the mission logic

Weapons, projectiles, health and damage were mapped in Milestones 7–8. This milestone covers the system that runs every mission: **triggers**.

### How a level's logic works
A level file (`levels-NAME.xmb`) places trigger objects of 48 types. Some of them:
- spawn, timer, calculator, router
- sound, dialog, AI, animation, script
- warp, letterbox, level-over, mover, menu
- detectors

Each trigger has:
- **Receivers** (`_e91cf6a8`, `receiver`): the objects it sends its signals to.
- **Senders** (`_0884288c`, `sender`): the objects it listens to. The editor stores both directions; in every level checked, each sender link is mirrored by a receiver link.
- **Responses** (`_031cd9f5`): rules of the form "on signal `TRIG_SIG_x` (with an optional match-id) do `TRIG_ACT_y`", each at most `max-count` times (2147483647 = unlimited). Type-specific responses add fields, such as a calculator's `value`.

### The code
- **`Trigger_CreateByType`** (0x20A620) builds each trigger from its object-type name. That name is the name hash of `trigger_spawn`, `trigger_timer`, `trigger_router`, and so on; 33 of the 48 type names are now known.
- **The base class** (`g_vt_Trigger`, 0x395578) has a 26-slot vtable; about 40 trigger classes derive from it.
- **Message flow.** When a signal arrives, `Trigger_HandleMessage` (vtbl+0x60) walks the matching responses with `Trigger_FindResponse`. Every trigger understands:
  - **ENABLE / DISABLE / TOGGLE_ENABLE**: they send `TRIG_SIG_ENABLED` / `TRIG_SIG_DISABLED` to the receivers.
  - **USER_SIG1..8**: they re-send `TRIG_SIG_USER_SIGNAL1..8`.

  Each class handles its own actions in its own `HandleMessage`:
  - a timer: `OPERATE`, `DISPLAY_TIMER`, `TIMER_FULL_RESET`, `RESET_COUNTDOWN_TIME`
  - a calculator: `INC/DEC/MODIFY_COUNTER`, `SET_COUNTER_FROM_SIG_DATA`
  - a sound trigger: `PLAY/STOP/PAUSE/RESUME_SOUND`
  - a mover: `MOVE_FORWARD/BACKWARD` and their `LOOP` / `OSCILLATE` variants
- **Sending.** `Trigger_SendToReceivers` delivers a message to every receiver while the trigger is enabled. Destroying or disabling a trigger sends `TRIG_SIG_DESTROYED` / `TRIG_SIG_DISABLED` (`Trigger_FlushStateSignals`).
- **Ghidra.** 86 names were added to the script: the base functions, plus the vtable, create, message and property functions of 20 trigger classes.

### Names of level objects
- **Object names.** Level objects are named by hash. The editor's default names follow a pattern, `<type><number>`, so they could be recovered by trying every pattern with every number: `trigger_spawn612`, `trigger_router523`, `trigger_timer198`, `cover_point106`, `wpoint1481`, `trigger_level_over182`, ... Cutscene scripts (`.gscr`) gave others. **18,090 object names** were recovered, about half of all objects; `names.json` now has 24,949 names.
- **Tag names.** The level file tags for trigger types were also recovered (`router-trigger`, `world-point-trigger`, `destroyer-trigger`, `warp-trigger`, `gamevar-trigger`, ...). Two old wrong names from hash collisions (`fleeima`, `spreaddirtpuff`) are replaced. Re-run `convert_all.bat` to get the corrected and new names in the XML.

### Export
`export_mission_logic.bat` runs `tools\level2logic.py`. For every level it writes:
- **`levels-NAME_logic.txt`**: one block per trigger, for example

  ```
  #114c7a5f  [calculator-trigger, trigger_calculator]
    listens to: trig_router121
    on DESTROYED -> INC_COUNTER  (max-count=8, value=1)
    signals: trigger_timer198, ..., trigger_timer319, #0529b9f1
    settings: counter=0, target=6, op=CMP_EQUAL
  ```

  This one counts objects destroyed (relayed by a router) and starts seven timers when the count reaches 6.
- **`levels-NAME_logic.json`**: the same as data.
- **`levels-NAME_logic.dot`**: the signal graph. Open it with Graphviz or any online `.dot` viewer.

Results on all 54 levels: 19,839 objects with signal links, 68% of them named. The most common are spawn (5,328), router (2,790), timer (1,713) and sound (1,417) triggers. The most common rules are `ENTER_DETECTOR`, `TIMER_PULSE` and `FIRST_UPDATE` → `OPERATE`.

## Milestone 25 (done): what the main triggers do

The behaviour of the trigger types that carry most of each mission, read from their code:

| Trigger | Behaviour |
|---|---|
| **router** (2,790 in the game) | Relays signals. With its flag `_f0d6857f` false (the usual case) it forwards every signal it receives to its receivers unchanged. With the flag true it relays only signals whose rule says `OPERATE`, re-sent as `out-signal` when that is set. That is how one event fans out to many triggers. |
| **spawn** (5,328) | `OPERATE` spawns `spawned-object-name` at `location-world-point`, with `home-point`, `home-range-radius/height` and `goal-id` for its AI, after `spawn-delay` seconds. It then sends `OPERATED`, carrying the new object, to its receivers. `SPAWN_RESET` re-arms it. |
| **timer** (1,713) | `OPERATE` starts a countdown of `current-value` seconds. At zero it sends `TIMER_PULSE` and, with `enable-looping`, starts again from `initial-value`. `DISPLAY_TIMER` / `UNDISPLAY_TIMER` show it on screen. |
| **calculator** (219) | Counter actions change `counter`, which is then tested against `target` with `op` (5 comparisons). When the test passes it sends `CALCULATOR` and `TRUE`. |
| **AI** (591) | `OPERATE` applies its settings to the receiving characters: personality, alignment, event, squad command and destination point. It clears their goal stack and sets the new goals through the AI functions from Milestones 6 and 10. |
| **all triggers** | While disabled, a trigger reacts only to the enable/disable and `USER_SIG` actions, and sends nothing. |

- **Field names.** Nine more trigger field names were recovered: `spawn-delay`, `spawn-fade-time`, `home-range-height`, `warp-fade-time`, `fade-in-timer`, `fade-out-timer`, `volume`, `amount`, `enable-show-first`.
- **Exporter.** Each level's `_logic.txt` now starts with an explanation of every trigger type that level uses.
- **Ghidra.** 8 names were added (spawn trigger, timer tick, AI trigger), and the router's description was updated.

## Milestone 26 (done): more trigger types, game variables, and a correction

### A third object list, and a correction
Besides its receivers and senders, every trigger has a third list (`_0b6b92ba`; its name is not recovered): **the objects it acts on**. Where that list holds a spawn trigger, it stands for the character that trigger spawned.
- Milestone 25 said an AI trigger commands its receivers. That was wrong: it commands the characters it **acts on**.
- Two class names from Milestone 24 were swapped and are fixed in the Ghidra script:
  - vtable 0x395370 is the **letterbox** trigger (not damage)
  - vtable 0x395510 is the **shield** trigger (not elevator)
- The factory is now parsed fully, so all 48 trigger types are matched to their classes.

### What the triggers do
| Trigger | Behaviour |
|---|---|
| **warp** | `OPERATE` teleports the characters it acts on to `world-point`, fading out over `warp-fade-time`. |
| **dialog** | `PLAY_DIALOG` / `OPERATE` makes the character it acts on speak `dialog`. If that character is dead, it sends `FINISHED` at once so the mission doesn't stall. |
| **script** | `OPERATE` starts the scripted sequence `script-name` (a `.gscr` cutscene). |
| **level-over** | `OPERATE` ends the level. Each level has two: one fades to black and finishes the level; the other, flagged `_18f66101`, hands over to a separate world object and is probably the mission-failed ending (inferred). |
| **game variable** (496) | `OPERATE` computes `out = A op B` on game flags, the variables that persist across levels. They form a tree: `missions.m03.kill-kingman.state`, `campaign...`, `save-game.character-1...` (`common\default-gameflags.xmb`). `ASSIGN / ADD / SUBTRACT / MULTIPLY / DIVIDE / RESET_TO_DEFAULT` write a flag. The comparisons (`EQUAL / LESS / GREATER ...`) send `TRUE` or `FALSE`. This is how mission objectives are recorded and checked. |
| **evaluate** (255) | `OPERATE` sends `TRUE` or `FALSE`. `random-test` is the chance of `TRUE` (0.4 to 0.8 typically), which is how levels pick random branches. |

### Exporter
`export_mission_logic.bat` now also shows:
- **acts on:** each trigger's third list (a dashed line in the `.dot` graph)
- **game variables:** in readable form, e.g. `op=GVOP_EQUAL, A=1.0, B=missions.m14.#08bbbbc1.state, out=signal`
- **notes:** the type explanations at the top of each listing now cover these triggers too

### Names
- **Added.** The mission names `m01`–`m14` and `missions` were added to `names.json`. Objective names are mostly still hashed; one is recovered: `kill-kingman`.
- **Removed.** 52 doubtful level-object names from the Milestone 24 guessing were removed: odd prefixes, spaces, and texture-format or animation words such as `L 6529` and `Sc_w1_dash 1564`.

### Ghidra
23 names were added: warp, dialog, script, level-over, game variable, evaluate, text box, movie, radar, event, decal, expression and zone triggers.

## Milestone 27 (done): mission objectives

### Where they are
- **The flags.** Every objective is a game flag, `missions.<mission>.<objective>`, with two values:
  - `enabled`: the objective is shown in the list
  - `state`: 0 open, 1 done, −1 failed. These meanings are inferred from the levels: 108 triggers reset a state to 0, 69 set it to 1 and 21 to −1.
- **The campaign file.** `common\campaign-bf.xmb` lists each level's primary and secondary objectives:
  - the path to the objective's flag
  - a display text id (resolved in the English string table)
  - a score (`_f71c3e99`: 5000 for a primary objective, 1000 for a secondary one in the tutorial)

  That gives all 70 objectives of the 32 campaign levels their in-game text, for example:
  - E01: "Find downed Chopper."
  - M03_a: "Assassination of Edward Kingman."
  - M14_a: "Kill the Shrike Hunter Lord."

### Names
- **Objective keys.** The game names objective flags with short verb-noun keys. They were recovered by trying word combinations taken from the objective texts: `find-chopper`, `get-docs`, `kill-associates`, `get-briefcase`, `save-shaman`, `disable-nodes`, `destroy-cannon`, `investigate-lb429`, `kill-hunter-lord`, `open-gate-1..4`, ... **54 of the 70 keys** are recovered.
- **Wrong names fixed.** Four objective keys previously had wrong names from hash collisions, for example `cskill_orbital_beam-float-transform` for `locate-mutant-stronghold` and `sloppyquadratic` for `disable-exits`. Two more wrong names (`goreset`, `identification-move`) were removed, since their real keys are not known yet. The exporter now corrects names like these even in XML converted earlier.

### Exporter
Every game-flag reference in a level's `_logic.txt` now names its objective, for example:

```
settings: op=GVOP_ASSIGN, A=1.0, B=0.0, out=missions.e01.find-chopper.state  [primary objective of E01: "Find downed Chopper."]
```

That makes the mission flow readable from start to finish: which trigger completes or fails which objective.

## Milestone 28 (done): the remaining trigger types

Every trigger kind in the levels now has a description; behaviour not fully traced is marked "inferred".

| Trigger | Behaviour |
|---|---|
| **letterbox** | `OPERATE` slides the cinematic black bars in over `show-duration`; `STOP` takes them away over `hide-duration`. |
| **radar** (467) | `OPERATE` puts a radar marker of type `radar-contact` on the objects it acts on: `RCONTACT_OBJECTIVE`, `_ALIGNMENT`, `_BOSS_ALIGNMENT`, `_IMPORTANT_PICKUP`, ... |
| **text box** (151) | Shows an on-screen message and can wait for a button press or hold (inferred). |
| **menu**, **zone**, **event**, **squad** | Open a menu; hand over to another zone of the level; react to a game event (`EVENT_MATCH`); change the squad (all inferred). |
| **score** (75) | Changes the squad score by `amount`. Its names were recovered: `trigger_score` / `score-trigger`. |
| **detectors** | Box detectors (with `size`) and proximity detectors (with `radius`) send `ENTER_DETECTOR` / `EXIT_DETECTOR` / `EMPTIED_DETECTOR` when objects that pass their alignment filter come and go. Damage detectors hurt what is inside (`damage-type`). |

### Unnamed kinds
Seven kinds whose names are still unknown are now labelled by what they do, with a `?`:

| Label | What it does | How many |
|---|---|---|
| `box-detector?` | box detectors | 1,394 |
| `pickup?` | pickup objects that send `PICKUP_OBJECT` | 838 |
| `use-trigger?` | objects the player can use; they send `USE_OBJECT` | 52 |
| `test-trigger?` | tests that send `TRUE` | 119 |
| `controller-trigger?` | — | 38 |
| `damage-box-detector?` | — | 19 |
| `box-detector-2?` | — | 10 |

The top of each level's `_logic.txt` now explains the detectors too.

**Ghidra:** 11 names were added or updated: the letterbox, radar, menu, zone, event, score, text box and squad handlers.

## Milestone 29 (done): the menus, rebuilt

### Where the menus are
- **Layout.** All 64 front-end and in-game menus are defined in `common\game-options-LANG.xmb` (`game-menus`). They cover the start screen, campaign / deathmatch / squad deathmatch, profiles and controller options, team and squad select, loading, and more. Each menu has:
  - a position and size on the Xbox's 640 × 480 screen
  - its controls: static texts, buttons with event scripts, button hints, images, option selectors, list boxes, and movie and music players
- **Shared definitions.** Controls use 58 shared styles: text colours (normal / selected / disabled), font and size, alignment and button background textures. Larger panels are assembled from shared multi-part frames (`_1544c137`), and `menu-sequence` groups the menus shown together.
- **Text and artwork.** Texts come from the string tables. Artwork comes from the texture libraries (common and splash_screen).

### Image orientation
- **Some artwork is stored upside down.** Examples are the BRUTE FORCE logo and the loading-ring quarters; it was probably baked from bottom-up TGA files. The menu renderer draws images with v running upwards, so an image comes out upright in its stored orientation only when its flag `_06712921` is set.
- **How that was confirmed.** The loading ring is built from one quarter texture drawn four times with different flags, and it only closes into a circle with this reading. `_e3e50283` mirrors an image sideways.
- **Models are not affected.** Their UVs already sample the texture data the right way.

### Converter fixes (`xmb2xml.py`)
- **Seven XML files were not valid XML.** Wide (UTF-16) strings in the string tables, the campaign file and the menus were written with their zero bytes. They are now decoded, so texts read normally ("Depart" instead of "D e p a r t").
- **Invalid tag names.** Recovered names that aren't valid XML tag names (a leading digit, a space) now fall back to `_xxxxxxxx`.
- **Result.** All 759 converted files now parse as XML. Re-run `convert_all.bat` to get the fixed files; the other exporters work as before.

### Export
`export_menus.bat` runs `tools\menu2html.py`. It writes `common\game-options-en_menus.html`, one page showing all 64 menus rebuilt from their definitions with real artwork and texts. Other languages: `python tools\menu2html.py <data folder> de` (or `fr`, `es`, `it`). Texts that share a place (for example the help line for each button) are shown once, with the alternatives in a tooltip. The background movies aren't played; they are named in the corner of each menu.

**Inferred:** the meaning of the text alignment values (0 left/top, 1 right/bottom, 2 centred). 289 images are placed; 31 refer to textures in libraries that only load with a level and show as placeholders.

## Milestone 30 (done): controls, HUD settings, combat skills, and name clean-up

### Controller layout
Read from `common\game-options-LANG.xmb`. The button names were recovered (`a-button`, `left-x-axis`, `legacy`, ...); each button is mapped to a virtual button (`vb`) from Milestone 13.

| Button | Action | | Button | Action |
|---|---|---|---|---|
| A | `VB_JUMP` | | left trigger | `VB_USE_INV` |
| B | `VB_NEXT_ITEM` | | right trigger | `VB_FIRE` |
| X | `VB_ACTION_BUTTON` | | left thumb (click) | `VB_DUCK` |
| Y | `VB_TOGGLE_WEAPON` | | right thumb (click) | `VB_AIM_SNIPE` |
| black | `VB_USE_HEALTHPACK` | | white | `VB_SPECIAL_ABILITY` |

There are two stick layouts:
- **default:** left stick moves and strafes, right stick turns and looks
- **legacy:** the left stick turns, the right stick strafes

### HUD settings and combat skills
- **HUD settings** (same file):
  - radar `max-range` 120
  - reticule visibility and fade values
  - the scoreboard used (`default-scoreboard`)
- **Combat skills.** The squad's and the bosses' special abilities are configured in the `combat-skills` section. The recognisable entries are:
  - `air-strike`, `cloak`, `breath-of-vengar`, `corpse-eater`, `kingman`
  - `boss-shadoon`, `boss-supermutant`, `molotov-mutant`
  - a tracking skill (with `tracking-weapon-type`)
  - a power-blade skill

  They use the `CSKILL_*` enum. Four entry names are still unknown.

### Name clean-up
- **51 wrong names removed** from `names.json`. They were hash collisions from early name guessing that showed up as field and tag names, for example `relativeenglish` for `left-x-axis`, `idle_vary_data_pause_with` and `ttype_human_iprofileparser`. Wrong names are worse than none: the fields now show as `_xxxxxxxx` until their real names are found.
- **Re-convert to apply.** Re-run `convert_all.bat`.

## Milestone 31 (done): sounds

### Where the sounds are
A level's sounds are split three ways. Each level's sound library `sounds-<level>.xmb` says which wave lives where:

| Kind (`resource-type`) | What | Where |
|---|---|---|
| `SRT_MEMORY` | short effects: weapons, hits, bullet whizzes, menus, power-ups | `sounds-<level>.mem` in the level archive |
| `SRT_SHORT_STREAMING` | dialogue and streamed effects (wind gusts, doors, terminals) | `ml-sounds\<lang>\<level>-<lang>.tgz` → `<level>-<lang>.xwb` |
| `SRT_LONG_STREAMING` | music and long ambient loops | `data\sounds\<level>.xwb` on the disc (not in an archive) |

- **Sound cues.** `<SOUND id type>` entries name a wave file and give `falloff`, `max-dist` and `volume`. Types are `sfx`, `speech` and `ambient`. A cue's id is the name hash of its wave's name (for example `dxen_bratton_e34001`), so the wave bank names recover almost every cue name.
- **Waves.** `<optimized-wave>` entries have five slots of offset, size, time, format and `channels`, one per language. The slots follow the `<languages>` list (1 English, 3 German, 5 Spanish, 4 French, 6 Italian). Offsets are file offsets into the `.mem` or `.xwb`.
- **Wave banks (`.xwb`).** These are XACT wave banks, version 2 (`WBND`): bank data, an entry table (24 bytes per wave), a name table (64 bytes per wave) and the wave data aligned to 0x800. A wave's format packs the codec (bits 0–1: 0 PCM, 1 Xbox ADPCM), channels (bits 2–4) and sample rate (bits 5–30); the sound libraries use the same packing.
- **Codec.** Nearly everything is Xbox ADPCM at 22,050 Hz: mono effects and dialogue, stereo music. A few are at 44,100 or 16,000 Hz. Xbox ADPCM is IMA ADPCM in 36-byte blocks per channel, so the waves can be saved as standard IMA ADPCM `.wav` files without re-encoding.
- **Placeholders.** `mp_common\sounds\*.xwb`, `sounds\common.xwb` and a few multiplayer banks are empty placeholders (76 bytes).

### Names
- **Field names.** The library's fields were recovered: `sfx`, `speech`, `channels`, `languages`, `language` and `max-dist`.
- **Cue names.** 3,969 sound names were added to `names.json`, so the cues now show by name in the converted XML (re-run `convert_all.bat`). That covers 3,963 of the 4,476 cues (89%).
- **Still unknown.** Most of the in-memory effects (373 of 412) are still hashes. Names recovered so far include `hit_dirt_01`, `hit_flesh_01`, `whiz_bullet_1`, `whiz_laser_1`, `turret_fire_01`, `menu_select`, `powerup_health`, `grenade_frag_exp` and `exp_seer_grenade`. The unnamed ones are exported as `wave_<hash>.wav`.

### Export
`export_sounds.bat` runs `tools\snd2wav.py`. It asks for a dialogue language (Enter = English) and writes to `sounds_wav\` in the data folder:
- `music\<level>\` music and long ambient loops
- `en\<level>\` dialogue and streamed effects (or the chosen language; `all` exports all five)
- `memory\<level>\` the in-memory effects
- `_sounds.csv` every cue of every level: name, type, falloff, max distance, volume, storage, and the `.wav` it plays

For English that is about 24,000 files and 0.5 GB, written in about a minute. The files are IMA ADPCM `.wav` files, which Windows, VLC and Audacity play. Answer `y` to the PCM question for plain 16-bit PCM files instead, for tools that only read PCM. That is slower (5–10 minutes) and four times bigger.

**Checked:** the decoder's PCM output matches ffmpeg's IMA ADPCM decoder to within 40/32768; the small difference is ffmpeg's rounding of the step size. Stereo music decodes with correlated channels, and every cue in the 60 libraries links to a wave (the music needs the disc's `sounds` folder).

**Inferred:** the language order of the five slots (only English could be checked).

### Ghidra
`SoundManager_LoadLevelSounds` (0x17ADB0) loads the `.mem` and opens both wave banks; it picks the language suffix from `-en`/`-de`/`-fr`/`-es`/`-it`.

## Milestone 32 (done): whole levels as 3D scenes

Each level can now be exported as one glTF scene (`.glb`): the terrain plus every placed building, rock, prop, door, crate, pickup, weapon and character, with textures. Open it in Blender (File → Import → glTF 2.0) or the Windows 3D Viewer.

### How a level is put together
- **Placed objects.** `levels-<level>.xmb` lists every placed object (`game-object`, `inventory-object`, `character-object`, `world-button-object` ...) with its `object-type-name` and a `transform` of 12 floats: rows R0, R1, R2, then the position. A point of the model goes to `x·R0 + y·R1 + z·R2 + position` (row vectors, the Direct3D convention).
- **From type to model.** The object type's `mesh-name` names an archetype in `objects-*.xmb`. An archetype either:
  - lists `meshes` as levels of detail (the one with the highest `lod-switch` is the most detailed), or
  - is built from `parts`, each part its own archetype, placed by its `joint` (the parent-point minus the child-point; the rest rotations are identity, as for the characters).
- **Resolved.** In all 53 levels, every placed object that has a body resolves to a model: 26,601 objects. What's left are invisible logic objects (triggers, cameras, paths, lights, effects, sound emitters).
- **Terrain.** The level's `<Terrain>` entry names the terrain mesh (Milestone 22). Its texture layers cover the same ground, and layer 0 always covers all of it, so the scene uses layer 0.

### Material shaders
- **Shader names cracked.** The shader materials' type names follow `BF_<letters>_rtMaterial`, and the 10 unknown ones are now named: `BF_C_`, `BF_CA_`, `BF_AE_`, `BF_CAS_`, `BF_CAN_`, `BF_CES_`, `BF_CASB_`, `BF_CAES_` and `BF_CESB_rtMaterial`, plus `BF_GameMaterial` (a mesh's own material).
- **What the letters mean.** **C** colour, **A** alpha, **S** specular, **E** emissive, **B** bump. Shown by the textures:
  - In the **A** shaders the colour texture's alpha is a cut-out: foliage, fences, grilles. Nearly all pixels are fully on or off.
  - In the others (`BF_CS_`, `BF_CSB_`, `BF_CES_` ...) the alpha is a specular mask: smooth values, mostly low. Using it as transparency would make most buildings invisible.
- **In the scenes.** Only the **A** materials are exported as alpha-masked (and double-sided).

### Checks
- **Placement convention.** With the row-vector reading, buildings made of several pieces line up exactly. With the transposed reading the same pieces scatter.
- **Heights.** Objects stand on the terrain.
- **File structure.** A structural check of all 53 files (accessor bounds, index ranges, min/max, normals, node graph, rotation matrices without scale or mirroring, embedded PNGs) finds no errors.
- **Renders.** A textured z-buffer render of e34 shows the Caspian base with its hangars, catwalks and cacti in place.
- **No regressions.** `mesh2obj.py` was split so the geometry code is shared (`mesh_geometry`, `load_library`, `texture_png`). The OBJ files it writes are byte-identical to before, and the character export is unchanged (all 31 `.glb` files identical).

### Export
`export_levels.bat` runs `tools\level2glb.py`. It writes `<level>\<level>_scene.glb` for the 53 levels, 2 to 10.3 MB each and about 370 MB in total, in under a minute. Run `export_textures.bat` first, or the models come out untextured.
- **Node names.** Each placed object is a node named `<object name> (<object type>)`, under `objects`; the ground is `terrain`. Each model is stored once and shared by all its copies.
- **One level.** `python tools\level2glb.py "<data folder>" e34`.
- **All terrain layers.** Add `--all-layers` to get every terrain layer; they overlap, so hide all but one.

## Milestone 33 (done): sky, lights and fog in the level scenes

The level scenes from Milestone 32 now carry each level's sky, lights and atmosphere. Re-run `export_levels.bat`.

### What a level file says about them
- **Sky.** `<sky><Object>` names the sky mesh and where it sits, for example 250 m up in e34; `rotation` is how fast the game turns it. Sky meshes are domes and cloud layers several kilometres wide. `background-color` is the colour behind them.
- **Fog.** `<object-fog>` (and the same in `<Terrain>`) sets `enable`, `color` (0–255) and `start-distance` / `end-distance`, for example 30 to 150 m in e34.
- **Ambient light.** `object-ambient`, `character-ambient` and the terrain's `ambient` are the base light for objects, characters and the ground.
- **Lights.** Each `light-object` has a transform, a `diffuse` colour and a `range`. Its type's `light-type` is `LT_DIRECTIONALLIGHT` (161 in all, 2 to 4 per level: a key light plus fills) or `LT_POINTLIGHT` (2,249: lamps, fires, screens).

### In the scenes
- **`sky` node.** Holds the sky meshes as unlit, blended, double-sided materials (`KHR_materials_unlit`). In e34 it shows the red, cloudy sky over the canyon. Hide it in Blender to look at the level from outside.
- **`lights` node.** Has every light as a `KHR_lights_punctual` light (Blender imports them as sun and point lamps). The game has no physical units, so the brightness is a choice: directional 2, point range² / 4.
- **Light direction.** A directional light shines along minus the third *column* of its transform, (R0.z, R1.z, R2.z). This was corrected in Milestone 54; it had been R2, the third row. The hand-edited light matrices are often not quite orthonormal, so the exporter rebuilds a clean rotation around that direction.
- **Scene extras.** The background colour, the fog and the three ambient colours are kept in the scene's extras, because glTF has no fog or ambient light. In Blender they show under Scene properties → Custom Properties.

The direction was inferred at first (+R2). Milestone 54 found it in the code and corrected it.

**Checked:** all 53 scenes pass the structural check, including the 2,410 lights.

## Milestone 34 (done): the libraries inside the executable

A recompilation has to replace the Xbox libraries rather than translate them, so they need to be told apart from the game's own code. This milestone maps them.

### Where the libraries are
| Range | What |
|---|---|
| `.text` 0x012000–0x2EA000 | game code (Digital Anvil), with zlib and Lua linked in |
| `.text` 0x2EA000–0x2F2000 | Microsoft C/C++ runtime |
| `.text` 0x2F9000–0x3118B0 | mixed (link-time code generation reorders code): more runtime (maths, exceptions, `_strnicmp`), audio DSP code near 0x2FB000, and game code such as the asset extension table near 0x30CC50 |
| `.text` 0x2F2000–0x2F9000 | XAPI, the Xbox's Win32-style runtime (files, threads, memory, save games) |
| sections `DSOUND`, `XONLINE`, `XNET`, `D3D`, `XPP` | DirectSound, Xbox Live, network stack, Direct3D 8, controller/USB input |
| sections `BINK*`, `BINKDATA` | Bink video (13 sections, one per pixel format) |
| `DOLBY` | Dolby Digital encoder microcode for the audio DSP |

### Names added (Ghidra script)
- **Kernel imports.** All 147, from the XBE's kernel thunk table at 0x386340, are labelled `__imp_<name>` (for example `__imp_NtCreateFile`). Ordinals map to names with the standard xboxkrnl export list. Only 124 functions in `.text` call the kernel directly, nearly all of them inside XAPI.
- **37 XAPI functions.** Each was identified by the kernel services it calls, its argument count (from `ret N`) and its call sites:
  - Files: `CreateFileA`, `ReadFile`, `WriteFile`, `CloseHandle`, `SetFilePointer`, `GetFileSize`, `DeleteFileA`, `RemoveDirectoryA`, `SetFileAttributesA`, `FindFirstFileA` ...
  - Saves: `XCreateSaveGame`, `XDeleteSaveGame`
  - Memory and sections: `VirtualAlloc`, `XPhysicalAlloc`, `XLoadSection` ...
  - Threads: `CreateThread`, `WaitForSingleObjectEx`, `SleepEx` ...

  Similar functions were told apart by their open flags. For example, `DeleteFileA` opens with `FILE_NON_DIRECTORY_FILE` and `RemoveDirectoryA` with `FILE_DIRECTORY_FILE`; both then set `FileDispositionInformation`.
- **35 C/C++ runtime functions:**
  - `operator_delete` (3,402 call sites), `operator_new` (2,051; the two were swapped until Milestone 46), `atexit` (854 call sites, the static destructors), `_CxxThrowException`
  - string functions: `memmove`, `strncpy`, `strstr`, `strchr`, `strrchr`, `_strnicmp`, `wcslen`, `wcscmp`, `wcsncpy`
  - formatting: `sprintf`, `_snprintf`, `_snwprintf`, `sscanf`, `printf`
  - maths and compiler helpers: `floor` and `ceil` (told apart by their x87 rounding modes), `_ftol2`, `_alldiv`, `_allmul`, `_alloca_probe`, `_SEH_prolog` / `_SEH_epilog`
- **zlib 1.1.3.** `deflateInit2_`, `deflate`, `deflateEnd`, `inflateInit2_` and the inflate tree builders, added to the inflate functions named in Milestone 1. Also three game helpers: `Zlib_CompressBuffer`, `Zlib_UncompressBuffer`, and `ZStream_InitRawInflate`, the raw stream behind the `.tgz` reader. The game calls zlib with a register calling convention (edx = stream, ecx = window bits, eax = version), a sign of link-time code generation.
- **Lua 3.2.** The 34 built-in functions from the `lua_builtin_funcs` table, named `luaB_<name>`.
- **Section labels.** `SECTION_D3D`, `SECTION_DSOUND`, ... mark where each library section starts.

### Coverage
The script now names 940 functions. Direct call sites that land on a named function went from **4.8% to 23.5%** of all 46,312, because the runtime functions are called from everywhere.

## Milestone 35 (done): the Direct3D calls

The game draws through the XDK's Direct3D 8, linked into the `D3D` section. A PC port has to turn these calls into a modern graphics API, so they are the most important library calls to know. That section had no names, so its functions were identified from the GPU commands they write.

### How
- **The device.** Every Xbox Direct3D function loads the device pointer (`D3D__pDevice`, 0x37DD88) and writes NV2A (Kelvin) push-buffer commands at its write pointer (+0). When the pointer passes the limit (+4), it calls `D3D_MakeSpace` (0x371E10, 102 call sites).
- **Command headers.** A command header is `count << 18 | method`. The methods each function stores identify it. For example:
  - `SET_TRANSFORM_CONSTANT` (0x0B80) with 4 or 16 values: the `SetVertexShaderConstant` variants
  - `CLEAR_SURFACE` and the clear values (0x1D8C / 0x1D90): `Clear`
  - the surface setup (0x200–0x214): `SetRenderTarget`
  - the register-combiner methods (0x260, 0x288, 0xA60, 0x1E40, 0x1E70): `SetPixelShader`
  - single render-state methods (cull face 0x308 / 0x39C, depth test 0x30C, fog colour 0x2A8 ...): the `SetRenderState_*` functions
- **Other evidence.** The draw and resource functions were also confirmed from their structure: reference counts in the resource header (`AddRef` / `Release`), the per-stage texture table (`SetTexture`), and `SET_BEGIN_END` = 0 (`End`).

### Named (36 functions, 2 globals)
- **Drawing:** `DrawIndexedVertices`, `DrawVerticesUP`, `End`, `SetVertexData4ub`, `RunPushBuffer`, plus `D3D_SetupVertexArrays` (stream offsets and formats).
- **Shaders:** `LoadVertexShaderProgram`, `SelectVertexShader`, `SetVertexShader`, `SetVertexShaderConstant` / `1` / `4`, `SetPixelShader`.
- **Frame:** `Clear`, `SetRenderTarget`, `SetScissors`, `Swap`, `InsertFence`.
- **Resources:** `SetTexture`, `D3DResource_AddRef`, `D3DResource_Release`, `D3D_DestroyResource`.
- **Render states (12):** `CullMode`, `FrontFace`, `ZEnable`, `StencilEnable`, `StencilFail`, `FogColor`, `TextureFactor`, `FillMode`, `LineWidth`, `EdgeAntiAlias`, `NormalizeNormals`, `VertexBlend`.

**Result:** 612 of the game's 746 calls into Direct3D (82%) now go to a named function. Overall, 24.8% of all direct call sites go to named functions.

**Inferred** (marked in the comments): `DrawVerticesUP`, `InsertFence`, `RunPushBuffer` and `SetVertexShader`. Each matches the commands it writes, but there is no string or symbol to confirm the exact API name.

## Milestone 36 (done): controller input and DirectSound

The other two libraries a PC port has to replace are input (`XPP`) and sound (`DSOUND`). Their entry points are now named, along with the game classes that call them. Nothing in these libraries carries a name, so each function was identified from:
- its argument count (`ret N`)
- the constants the game passes in
- what the game does with the result

### Input (XPP), all 7 functions the game calls
| Function | Evidence |
|---|---|
| `XInitDevices` | called once with {gamepad type, 4 ports} |
| `XGetDevices` / `XGetDeviceChanges` | the connected-port mask / plugged-in and removed masks |
| `XInputOpen` / `XInputClose` | (gamepad, port, 0, NULL); the handle sits at +0xC0 of the game's controller object |
| `XInputGetState` | (handle, &state) |
| `XInputSetState` | rumble; the game accepts `ERROR_IO_PENDING` (0x3E5) as success |

The gamepad device type is labelled `XDEVICE_TYPE_GAMEPAD` (0x37ECD0). The game side is `Input_InitDevices`, `Input_PollDeviceChanges`, `Controller_Open` / `Close` / `ReadState` / `SetRumble`.

### Sound (DSOUND), 18 of the 50 functions the game calls
- **Device:** `DirectSoundCreate`, `DirectSoundDoWork` (every frame), `IDirectSound_GetCaps`, the listener's `SetPosition` / `SetOrientation`, `DownloadEffectsImage` (the DSP effects, `dsstdfx.bin`). The listener starts at the origin, facing +z with +y up.
- **Voices:** `DirectSoundCreateBuffer`, `SetBufferData`, `Play` (flags `FROMSTART`, plus `LOOPING` when looping), `Stop`, `StopEx`, `GetStatus`, `GetCurrentPosition`, `SetPosition` / `SetVelocity` (3D, deferred), `Release`.
- **Game classes:**
  - `SoundManager_CreateDevice`, `_DoWork`, `_DownloadEffects`, `_dtor` (vtable 0x399C40)
  - `SoundVoice_CreateBuffer`, `_SetData`, `_Play`, `_Stop`, `_IsPlaying`, `_SetPosition`, `_SetVelocity`, `_dtor` (vtable 0x399BF0)

The other 32 DSOUND functions the game calls are named in Milestone 44.

**Inferred:** `DirectSoundUseFullHRTF` and `DownloadEffectsImage`. (`SoundStream_SetData` was renamed `SoundBank_SetVoiceData` in Milestone 44.)

The script now names 1,024 functions.

## Milestone 37 (done): the physics engine, Lua's insides, and scene scripts

### The physics engine is Ipion
The physics library from Milestone 7 (the projectile collisions) is **Ipion Virtual Physics (IVP)**, the German engine Havok bought in 2000. Valve's Source engine also used it. The evidence:
- **Ipion identifiers in the executable:** `IVP_FLOAT`, `IVP_MD_HULL`, `IVP_MDRR_INTRUSION` ...
- **Its debug messages**, including one left in German: "Mehr als 27 Kinder" ("more than 27 children").
- **The constraint interface.** A row of base-class stubs each warns "You are changing ... of a constraint which does not exist".

For a port this matters: the physics is a self-contained third-party library. It can be swapped for a modern engine behind the same calls instead of being translated function by function. Named so far (16):
- the 12 `IVP_Constraint_*` methods (change / fix / free / limit translation and rotation axes, maximum impulses, fixing point, repositioning)
- `IVP_U_Active_Value_Manager_insert_active_float` / `_int`
- two tree-size checks

### Lua 3.2 internals (46)
The Lua functions behind the 34 built-ins are named from Lua 3.2's own error messages:
- **API (`lapi.c`):** `lua_error`, `lua_rawgettable`, `lua_pushobject`, `lua_settag`, `lua_beginblock` ...
- **Calls and execution:** `luaD_call`, `luaD_checkstack`, `lua_dofile`; `luaV_gettable` / `settable` / `rawsettable` / `lessthan` / `tostring`.
- **Tags and tables:** `luaT_gettagmethod` / `settagmethod` / `checktag` / `realtag`, `luaH_get`.
- **Auxiliary library:** `luaL_argerror`, `luaL_chunkid`.
- **Lexer:** `luaX_error`, `luaX_token2str`, `luaX_lex`, `read_long_string`.
- **Parser:** `checklimit`, `store_localvar`, `indexupvalue`, the `error_*` helpers, `funcparams`, `parlist`, `assignment`, `constructor`, `recfield`.
- **Precompiled chunks:** `luaU_undump1`, `LoadHeader`, `LoadSignature`, `LoadCode`, `LoadConstants`, `LoadNumber`.

The undump code means the game can also load precompiled Lua, although the `.gscr` files on the disc are plain text.

### Scene scripts and other game code (31)
- **Scene files.** `SceneScript_Load` reads `.gscr` scenes (the Lua tables `duration`, `entities`, `events`). `SceneScript_Write` writes them back as Lua text: editor code that shipped in the game.
- **Event types.** The 13 scene event types each have a name getter (`SceneEventMotion_GetTypeName` ...): `MOTION`, `SOUND`, `USER_EVENT`, `SET MONITOR`, `PATH MOTION`, `LIGHT` / `CAMERA` / `SPATIAL PROP ANIM`, `ATTACH`, `CONNECT`, `START SUB-SCENE`, `START PSYS`, `PSYS PROP ANIM`.
- **Curve types.** The 11 animation curve types do the same: `Linear`, `CatmullRom` (and their non-negative versions), `Smooth`, `Step`, `Ramp Up` / `Down`, `Bump In` / `Out`, `Free Form`.
- **String tables:** `StringTable_LoadCommon` / `_LoadLanguage`.
- **Heap debug dumps:** `Heap_DumpBlocks`, `LargeHeap_DumpFree` / `_DumpAllocated`.

The script now names 1,117 functions, and 25.9% of all direct call sites go to named functions.

## Milestone 38 (done): the game data sheet

The game's design numbers for every weapon, character and item, in one page.

### Export
`export_game_data.bat` runs `tools\data2sheet.py`. It writes, in the data folder's `common` folder:
- `game_data.html`, one page with sortable tables
- `game_data_weapons.csv`, `game_data_characters.csv` and `game_data_items.csv`

Other languages: `python tools\data2sheet.py "<data folder>" de`.

### What's in it
- **59 weapon types.** Name from the string table, class, damage min/max and type, splash radius, bullet type, speed and range, reload time, maximum ammo and ammo type, accuracy, burst delay and spread, recoil. Some examples:
  - the player guns: Confed LZR-23, L-Shot-50, Bower 20, Saryl-45, Jax-iR, Rail RVR, Sweeper V
  - the creature attacks: Breath Attack, Psionic Blast
  - built-in damage sources without a display name (explosions, the power blade)
- **128 character types.** Hit points, target type (`HUMAN`, `FERAL`, `SEER`, `HOUND` ...), shield, stamina, the four movement speeds, radar range, the weapons each carries, animation set and combat skill.
- **88 items.** Ammo, health, grenades and the "Alien Technology" DNA canisters and power-ups.

### Notes
- **Values that differ by level.** Many types are copied into every level that uses them. When a level's copy differs, the table shows the most common value with an asterisk; its tooltip lists every value and level. Only two values differ: the copies are consistent.
- **New field names.** `accuracy`, `burst-delay`, `burst-spread`, `recoil-per-shot`, `shell-eject-rate` and `shell-eject-effect` were recovered and added to `names.json`.

**Inferred:** the order of the four character speeds (walk to dash) and the units of `accuracy` (it runs 0–90).

## Milestone 39 (done): the mission guide

`common\campaign-bf.xmb` is the game's mission list. It is now decoded into one page.

### What the mission list holds
- **Missions.** Each `<level>` entry is a mission or map. It gives a display name (string table), a loading picture and a type: campaign, deathmatch or squad deathmatch.
- **Sections.** A mission is made of one or more sections, the level archives that load in turn. For example, Seers on the Bayou is `m05_a`, `m05_x`, `m05_b`, `m05_y`, `m05_c`, `m05_d`.
- **Movies.** Each section lists the Bink movies played before and after it, with timed on-screen texts (`start-delay`, `display-time`, colour, typing speed). These are the mission briefings: for example the "Deputy Director Operations" memo in the intro, and "Tex's memory chip recovered from KB-42" before the first mission.
- **Squad.** Each section lists the squad members it starts with.
- **Objectives.** Each mission lists primary and secondary objectives. Each has its game flag (`missions.m09.destroy-base`), on-screen text and `reward` in points.
- **Credits.** Each mission has a credits section (title, group headings, names).

**Game order** (18 campaign missions): Welcome to the 23rd!, Rescue Brutus, Cemetery Gates, Hunting for Shadoon, Terrorist Ghetto, Lonesome Death of Edward Kingman, Ship Graveyard, Return to Osiris, Seers on the Bayou, Squirm, Supermutant, Kill Shadoon, Abaddon, SETI 2049, Hostile Reception, Ripe for Plunder, Shrike Rogues, The Anvil.

The 7 deathmatch maps are Arena, Chamber, Ammo Depot, The Bush, Bio Dump Bash, Fog and Abandoned Quarry. The 7 squad deathmatch maps reuse campaign areas.

### Export
`export_campaign.bat` runs `tools\campaign2html.py`. It writes `common\campaign.html`, which lists for each mission its sections, movies with their texts, objectives with points, and credits. Each section links to what the other exporters made for that level: 3D scene, mission logic, logic graph, models, textures, music, dialogue and effect sounds. Run it last to get all the links. Other languages: `python tools\campaign2html.py "<data folder>" de`.

**Fix:** string-table texts kept XML escapes such as `&lt;`. They are now decoded, so the game data sheet (Milestone 38) also shows them correctly.

**New names:** `reward`, `start-delay`.

## Milestone 40 (done): the dialogue browser

Every voice line in the game, in one page, with a player for each line.

### Chatter libraries
**What they are.** `characters\<name>_chatter.xmb` holds what a character says in combat. There are 59 libraries:
- 23 for the campaign (the squad, Confed marines, militia, terrorists, ferals, seers, Shrike, mutants, Edward Kingman, Gunther Ghent, Ty McTavish ...)
- deathmatch (`dm_`) and squad deathmatch (`sdm_`) versions

Each character type names its library in `chatter-library`.

**What an entry holds.** Each entry is one situation (`line_tag`) with:
- the voice lines the game picks from (`sound_id` = the hash of the sound cue name, so the M31 wave names give them all)
- a facial expression (`EFE_ANGRY`, `EFE_GREAT_PAIN`, `EFE_SAD` ...)
- a chance in percent, a priority, two timings
- an option to avoid repeating the last line (`leastnon`)

**Situation names.** Only 6 situations are stored by name (`ENEMY_SEEN`, `SHOT_AT` ...). 44 more were recovered by hashing words from the executable's event names, for example:
- `OUT_OF_AMMO`, `RELOAD_WEAPON`, `THROWING_GRENADE`, `SUPPRESSING_FIRE`, `FRIENDLY_FIRE`, `TAUNT_ENEMY`
- `KILLED_ENEMY`, `SEEN_DEAD_FRIENDLY`, `TEX_KILLED` / `BRUTUS_KILLED` / `HAWK_KILLED` / `FLINT_KILLED`
- the squad orders `ORDER_FOLLOW`, `ORDER_GUARD`, `ORDER_MOVE_OUT` ...

That names 720 of the 1,497 situation entries; the names were added to `names.json`.

### Mission dialogue
The scripted lines of each level are the `dxen_<speaker>_<level><number>` (and `dxmi_`) sound cues. They are listed per level by number, with the speaker.

### Export
`export_dialogue.bat` runs `tools\chatter2html.py`. It writes `common\dialogue.html` with 4,718 chatter lines and the dialogue of 38 levels. Each line links to its `.wav` from `export_sounds.bat`, through `sounds_wav\_sounds.csv`.

Browsers only play plain PCM `.wav` files. To listen in the page, export the sounds with the PCM option (answer `y`), then run `export_dialogue.bat`.

## Milestone 41 (done): an effect viewer

`export_effect_viewer.bat` runs `tools\fx2html.py`. It writes `common\effects_viewer.html`, one self-contained page (about 4 MB, with the effects and textures embedded) that plays the game's 603 particle effects in 3D with WebGL. Pick an effect from the searchable list; drag to orbit, use the mouse wheel to zoom; there are restart, loop and speed controls. It works offline, straight from the data folder. Run `export_textures.bat` and `export_effects.bat` first.

### How the preview works
It follows the Alchemy effect model behind the Milestone 23 JSON:
- **Emitters.** Cone, sphere and cube emitters create particles at `Emitter_Frequency` per second, `Emitter_EmitCount` at a time, plus `Emitter_InitialParticles` at the start.
  - Each particle gets `Emitter_Pressure` as its speed and `Emitter_InitLifeSpan` as its life.
  - Its direction comes from the cone spread (`MinSpread`–`MaxSpread`, in degrees) and its start point from the emitter radius or box.
  - An emitter stops after its `Node_LifeSpan`.
- **Appearances.** The appearance linked to an emitter draws its particles as camera-facing quads with its texture, blend mode (`BlendInfo`), and colour, alpha, size and rotation curves over the particle's life. Animated textures pick frames from their 4×4 sheet.
- **Fields.** The fields linked to the appearance move the particles: gravity, air (wind with an approach rate), turbulence and radial fields.
- **Two kinds of curve.** An emitter property's keys run over the effect's own time (for example the burst of a hit effect). An appearance property's keys run over the particle's age, from 0 to 1.

### Limits
- **Not the game's renderer.** Motion blur, streak shapes (`ShapeType`, `ScaleX` / `ScaleY`), beams and sub-effects (`ParticleApp`) are drawn as plain particles or skipped.
- **Missing emitters.** 83 effects show nothing: their emitter is a node the effect library doesn't contain (the "missing" nodes in the JSON).
- **Inferred:** the cone axis (local +Y), the sign of gravity, and how texture-frame values select frames.

## Milestone 42 (done): one start page, and one button for everything

- **`export_all.bat`** runs every exporter in the right order: textures, models, animations, characters, effects, sounds, level scenes, mission logic, menus, game data, mission guide, dialogue and effect viewer. It can unpack and convert the game data first, and it asks whether to write the sounds as plain PCM (needed to play the dialogue in the browser). When it finishes it opens the start page. It can take up to an hour.
- **`index.html`** (`tools\make_index.py`) is a start page at the top of the data folder:
  - links to the overview pages (mission guide, game data, dialogue, effect viewer, menus, characters, sounds)
  - all 54 levels in game order, each with its mission name (for example *Campaign 6: Lonesome Death of Edward Kingman (part 2)* for `m03_b`) and links to its scene, logic, logic graph, models, textures, animations, music, dialogue and effect sounds

  Only exported files are linked; anything missing names the batch file that makes it. `export_all.bat` rebuilds the page at the end; to rebuild it alone, run `python tools\make_index.py "<data folder>"`.

## Milestone 43 (done): the render-state table and more Direct3D

### The render-state table
- **Where it is.** Direct3D keeps one DWORD per render state in an array, now labelled `D3D__RenderState` (0x37DDC4).
- **How it was confirmed.** The 13 render-state functions named in Milestone 35 from their GPU commands each store their value at `0x37DDC4 + 4 × index`, with the index in the XDK's `D3DRENDERSTATETYPE` order. For example `VERTEXBLEND` is 126, `FOGCOLOR` 127, `CULLMODE` 136 and `LINEWIDTH` 146. All 13 agree on that base.
- **What it gave.** The table names every other setter from the slot it writes. That gives 15 more:
  - `ZBias`, `LogicOp`, `BackFillMode`, `TwoSidedLighting`
  - `MultiSampleAntiAlias` / `Mask` / `Mode` / `RenderTargetMode`, `SampleAlpha`
  - `YuvEnable`, `OcclusionCullEnable`, `StencilCullEnable`
  - `RopZCmpAlwaysRead`, `RopZRead`, `DoNotCullUncompressed`
- **For a port.** A port can read the game's render state straight from this array.

### Other Direct3D functions
- **Device and streams:**
  - `Direct3D_CreateDevice` (the device is a static object at 0x37BD40, `D3D__Device`, with a 512 KB push buffer)
  - `SetStreamSource`, `SetVertexData4f`
- **Texture stage states:** `SetTextureState_BorderColor` and `SetTextureState_ColorKeyColor`. Each writes a per-stage GPU method and the stage-state table at 0x37BBA0.
- **Push buffers:** `BeginPushBuffer` / `EndPushBuffer` (inferred), the recording of precompiled push buffers that `RunPushBuffer` plays back.
- **Internals:** `D3D_FlushLazyState` (applies dirty state before a draw) and `D3D_RecordStateMethod` (inferred).

**Result:** 60 of the 80 Direct3D functions the game calls are named, covering 661 of its 746 Direct3D call sites (89%). The script names 1,141 functions.

## Milestone 44 (done): the rest of DirectSound

All 50 DirectSound functions the game calls now have names, and so does every game function that calls them. A PC port can replace the whole sound layer from this list.

### How they were told apart
The 32 left over in Milestone 36 take the same argument counts as each other, so the call sites couldn't separate them. The library code below them could:
- **Hardware registers.** The voice code writes the sound chip (MCPX APU) at 0xFE820000. The register numbers are known: 0x360 to 0x368 are volume and 0x37C is pitch. So the function that ends in the volume registers is `SetVolume`, and the one that ends in the pitch register is `SetPitch`.
- **Structure layouts.** The 3D setters write into a copy of the public `DS3DBUFFER` structure: position at +0x04, velocity at +0x10, min distance at +0x34, max distance at +0x38, mode at +0x3C. The listener setters write into a `DS3DLISTENER`, where the distance factor sits at +0x68 next to the orientation at +0x50. The envelope setter unpacks the ten fields of a `DSENVELOPEDESC`. The reverb setter reads the int/float pattern of a `DSI3DL2LISTENER`.
- **Header order.** The library's functions are laid out in the same order as the methods in the XDK `dsound.h`, with unused ones dropped by the linker. Every identification above fits that order.
- **Values.** `Pause` takes 0, 1 or 2 (`DSBPAUSE_*`). `XAudioCalculatePitch` returns 4096 × log2(f / 48000): the constants 1/48000 and 4096 are in the code. Mix bin 10 is `DSMIXBIN_I3DL2` (the reverb send) and bin 3 is the subwoofer.

### Named (126 functions, 1 label)
| Group | Functions |
|---|---|
| Buffers (`IDirectSoundBuffer_`) | `SetVolume`, `SetPitch`, `SetFrequency`, `SetFormat`, `SetHeadroom`, `SetEG`, `SetMixBins`, `SetMixBinVolumes`, `Pause`, `SetPlayRegion`, `SetAllParameters`, `SetMinDistance`, `SetMaxDistance`, `SetMode`, `SetRolloffCurve` |
| Streams (`IDirectSoundStream_`) | `SetVolume`, `SetPitch`, `SetEG`, `SetMixBins`, `SetMixBinVolumes`, `Pause`, `FlushEx`, `SetAllParameters`, `SetMinDistance`, `SetMaxDistance`, `SetMode`, `SetPosition`, `SetVelocity`, `SetRolloffCurve` |
| Device and listener | `DirectSoundCreateStream`, `IDirectSound_SetDistanceFactor`, `IDirectSound_SetI3DL2Listener`, `IDirectSound_CommitDeferredSettings`, `XFileCreateMediaObjectAsync`, `XAudioCalculatePitch` |
| Library internals | the `CDirectSoundBuffer_` / `CDirectSoundStream_` / `CDirectSound_` implementations behind each of the above, the shared `CDirectSoundVoice_` code, and the `CMcpxVoiceClient_` functions that write the hardware |
| Game: `SoundVoice_` | `SelectWave` (format, play region and rate for one wave of an in-memory bank), `SetupMixBins`, `SetVolume`, `SetPitch`, `SetEnvelope`, `Set3DParameters`, `SetMinDistance`, `SetMaxDistance`, `SetMode`, `SetReverbSend`, `SetSubwooferVolume`, `Pause` |
| Game: `SoundStream_` | `Create`, `OpenFile`, `Flush`, `PollStatus`, `Pause`, `SetupMixBins`, and the same setters as `SoundVoice_` |
| Game: other | `SoundManager_SetReverbEnvironment`, `SoundBank_MakeWaveFormat`, `SoundBank_SetVoiceData`, and the label `g_SoundRolloffCurve` |

### What this says about the game's sound
- **Short sounds** live in banks loaded into memory. Each voice plays one wave of the bank with `SetPlayRegion`, so no data is copied.
- **Music and long sounds** are streamed from the disc: `XFileCreateMediaObjectAsync` reads with 2 packets in flight, and `DirectSoundCreateStream` plays them.
- **Wave formats.** `SoundBank_MakeWaveFormat` turns the bank's format bits into a `WAVEFORMATEX`. That's Xbox ADPCM (tag 0x69, 64 samples per 36-byte block) or PCM, which matches what `snd2wav.py` decodes.
- **3D sounds** all use one 5-point rolloff curve: 1.0, 0.375, 0.125, 0.0625, 0.0. Distances are in metres (distance factor 1.0).
- **Reverb.** Each level's reverb comes from an I3DL2 preset sent with `SetI3DL2Listener`. A sound's share of the reverb is set through mix bin 10.

**Result:** 50 of the 50 DirectSound functions the game calls are named, which covers all of its DirectSound call sites. The script names 1,267 functions.

## Milestone 45 (done): the last Direct3D functions

The last 20 Direct3D functions the game calls now have names. That's all 80, covering all 746 of the game's Direct3D call sites. Together with Milestone 44 (all 50 DirectSound functions) and Milestone 36 (all 7 input functions), every call the game makes into the three Xbox libraries a PC port has to replace is now named.

### How they were told apart
- **The game's own state switches.** The game routes render states and texture-stage states through two `switch` statements, and they list the states in XDK order. Its case 0x9F sits between `SampleAlpha` and `YuvEnable`, so it is `Dxt1NoiseEnable`, and that function stores `D3D__RenderState[148]`. Cases 22–27 of the texture switch are the bump-environment states.
- **NV2A methods.** `SET_TEXTURE_PALETTE` (0x1B20) gives `SetPalette`, and `GET_REPORT` with the z-pass count type gives `EndVisibilityTest`. `SET_TEXTURE_OFFSET` and `SET_TRANSFORM_CONSTANT` written into the fixup buffer give the `D3DPushBuffer_` functions. A jump command (address | 1) gives `D3DPushBuffer_Jump`.
- **Structure layouts.** `SetViewport` reads a `D3DVIEWPORT8` (the game passes Z 0.0–1.0). `D3DSurface_GetDesc` fills the seven fields of a `D3DSURFACE_DESC`.
- **Hardware.** `D3D_WriteGammaRamp` writes 3 × 256 bytes to the video DAC registers. `D3D_BlockOnTime` sleeps on the device event with `KeWaitForSingleObject`.

### Named (20 functions)
| Function | What it does |
|---|---|
| `SetRenderState_Dxt1NoiseEnable` | render state 148 |
| `SetTextureState_BumpEnv`, `SetTextureState_TexCoordIndex` | texture-stage states 22–27 and 28 (texture-coordinate generation) |
| `D3DDevice_SetPalette`, `SetViewport`, `GetBackBuffer2` | palettes, viewport, front/back buffer |
| `D3DDevice_EndVisibilityTest` | occlusion queries (the game writes BeginVisibilityTest itself) |
| `D3DDevice_BeginPush`, `GetPushBufferOffset` | the game writes raw NV2A methods between BeginPush and its own inlined EndPush |
| `D3DPushBuffer_SetTexture`, `_SetVertexShaderConstant`, `_Jump` | patching recorded push buffers (fixups) |
| `D3D_BlockOnTime`, `D3D_BlockOnResource`, `D3D_BlockOnNonSurfaceResource` | waiting for the GPU (fences, BlockUntilNotBusy) |
| `D3DSurface_GetDesc`, `D3D_CreateSurfaceFromParent`, `D3D_GetTextureLevelLayout` | surfaces and texture levels |
| `D3D_WriteGammaRamp`, `D3D_DeviceUnInit` | gamma and shutdown |

The game uses a lot of push buffers: it writes NV2A commands directly and replays recorded buffers with fixups. A PC port will need to translate these commands, not only the API calls. The `BeginPush` call sites are where to start.

**Inferred:** `D3D_BlockOnNonSurfaceResource`, `D3D_CreateSurfaceFromParent`, `D3D_GetTextureLevelLayout` and `D3D_DeviceUnInit` are named from what they do, because the code matches no public function exactly. Several of these functions take their arguments in registers (link-time code generation), so their signatures differ from the XDK headers.

**Result:** 80 of 80 Direct3D functions named, covering 746 of 746 call sites. The script names 1,287 functions.

## Milestone 46 (done): where the game meets the physics engine

The Ipion physics library (Milestone 37) has now been mapped as a whole. This is the boundary a port would cut along to swap in a modern physics engine.

### Where it is
The library is one contiguous block of code, **0x2B2B10–0x2DF60F** (183 KB, about 700 functions). Unlike zlib and Lua, the linker did not mix it with game code. It was found in four steps:
1. Start from the 16 IVP functions named in Milestone 37 and the physics calls in `Projectile_Collide`.
2. Follow every direct call from those functions. This reaches 285 functions, all inside the block.
3. Test every function in the area: does it call game code, directly or through another function? Inside the block, none do. The only exceptions are four tiny helpers the linker shares between identical functions (a 3×3 matrix product, a vector length, the array-constructor loop and a "return this").
4. Check both edges. Just before 0x2B2B10 is game code. From 0x2DF610 on are the game's animation-curve evaluators.

Every called function in the block that has no better name is now named `ivp_<address>` (505 of them), so library code stands out from game code at a glance.

### The boundary
The game calls into the library at **48 functions, from 167 call sites in 62 game functions**. The full list, with the game functions that call each one, is in `physics_boundary.csv`. The library never calls the game directly; it reaches the game only through virtual functions, such as the ray-hit collector.

Named this milestone (inferred from what the code does, since IVP carries no symbols):

| Function | Evidence |
|---|---|
| `IVP_Polygon_ctor`, `IVP_Ball_ctor` | set object types 2 and 3, the `IVP_OBJECT_TYPE` order (none, cluster, polygon, ball); the ball constructor creates the shared ball surface manager on first use |
| `IVP_Real_Object_ctor` | the base constructor both of those call |
| `IVP_Real_Object_get_m_world_f_object_AT` | the world matrix at a given time, interpolated from the last step |
| `IVP_Core_calc_at_position` | position + (t − t_last) × speed |
| `IVP_Real_Object_ensure_in_simulation` | wakes an object and adds it to the environment's moving list |
| `IVP_Real_Object_async_push_object` | wakes the object, then applies an impulse; called on projectile hits |
| `IVP_Real_Object_beam_object_to_new_position` | sets a new rotation and position directly |
| `IVP_Core_calc_calc` | 1/mass and 1/rotation inertia |
| `IVP_U_Vector_increment_mem` | grows a vector to capacity × 2 + 1 |
| `IVP_Ray_Solver_ctor`, `_check_ray_against_object`, `_check_ray_against_polygon` | the ray casts behind `World_RayCast` (balls go through their surface manager) |
| `IVP_U_Quat_set_from_small_angles` | a quaternion from three angles; projectile spin |

The vtables of `IVP_Ball`, `IVP_Polygon` and the ray solver are labelled. So is the game's own ray-hit collector (`WorldRaySolver_vtable`), which keeps the nearest hit.

### How the game uses it
- **Static world geometry** becomes IVP polygons, built directly (`StaticObjects_ctor`).
- **Bullets** are small IVP balls. `Projectile_Collide` creates them, and `Projectile_ProcessHit` pushes what they hit.
- **Visible objects** take their pose from the physics with `get_m_world_f_object_AT`, interpolated to the frame time.
- **Line-of-sight and aiming checks** go through `World_RayCast`, which uses the IVP ray solver with a game-side hit collector.

### Correction: operator new and delete were swapped
The physics code made this clear: `push 0x34; call 0x2EA01D` is followed by a constructor, so 0x2EA01D allocates. Its target `Mem_Alloc` calls `*g_pfnMalloc`, which is `HeapAlloc(GetProcessHeap(), 0, size)`. `operator_new` / `operator_delete` and the two array versions now carry the right names. The allocator hooks (`g_pfnMalloc`, `g_pfnFree`, `g_pfnMsize`), the heap wrappers and the XAPI heap functions behind them are named too.

**Result:** the script names 1,312 functions individually, plus the 505 `ivp_` tags.

## Milestone 47 (done): every call into the Xbox libraries, and the network library

### The platform boundary in one file
`platform_boundary.csv` lists every Xbox library function the game calls: one row each, with its library, name, number of call sites and the game functions that call it. Together with `physics_boundary.csv` (Milestone 46), this is the complete list of what a port has to provide.

| Library | Functions called | Named | Call sites | Sites on named functions |
|---|---|---|---|---|
| Direct3D | 80 | 80 | 746 | 746 |
| DirectSound | 50 | 50 | 58 | 58 |
| Input (XPP) | 7 | 7 | 8 | 8 |
| Network (XNET) | 29 | 29 | 76 | 76 |
| Xbox Live (XONLINE) | 6 | 2 | 8 | 4 |
| Bink video | 13 | 0 | 27 | 0 |
| XAPI (files, threads, heap ...) | 70 | 37 | 376 | 298 |
| C/C++ runtime | 59 | 28 | 6,312 | 6,236 |
| runtime, mixed area | 11 | 2 | 1,018 | 974 |
| kernel, called directly | 3 | 3 | 128 | 128 |
| **Total** | **328** | **238** | **8,757** | **8,528 (97%)** |

### The network library (Winsock and XNet), all 29 functions the game calls
System link runs on the Xbox version of Winsock. The library's exports are stubs that load the socket layer object (0x40F5B4) into ECX and jump to the implementation, so each stub's target gives its argument count. Three things identified each one:
- **The export order follows the header.** The stubs run `socket`, `closesocket`, `shutdown`, `ioctlsocket`, `setsockopt`, `bind`, `connect`, `listen`, `accept`, `select`, `WSAGetOverlappedResult`, `WSACancelOverlappedIO`, `recv`, `WSARecv`, `recvfrom`, `send`, `WSASend`, `sendto`, `WSASendTo`. Unused ones were dropped by the linker.
- **The error codes in each implementation.** For example, `socket` returns `WSAEAFNOSUPPORT` / `WSAESOCKTNOSUPPORT` / `WSAEPROTONOSUPPORT`, and `connect` and `listen` return `WSAEISCONN`. `shutdown` returns `WSAENOTCONN`, `accept` returns `WSAEWOULDBLOCK`, and `ioctlsocket` handles `FIONBIO`.
- **How the game calls them.** It loops on `XNetGetTitleXnAddr` while the result is 0 (pending). It calls `XNetQosListen` with 64,000 bps and flags 9 to answer game searches. After failed socket calls it calls `WSAGetLastError` (37 call sites), which jumps to `GetLastError`.

The XNet functions named are `XNetStartup`, `XNetRandom`, `XNetCreateKey`, `XNetRegisterKey`, `XNetUnregisterKey`, `XNetXnAddrToInAddr`, `XNetInAddrToXnAddr`, `XNetGetTitleXnAddr`, `XNetQosListen` and `XNetQosRelease`. Three more are inferred: `XNetDnsLookup`, `XNetDnsRelease` and `XNetUnregisterInAddr`. The Winsock helpers named are `WSAStartup`, `__WSAFDIsSet` and `htons`.

### Also named
- **Xbox Live:** `XOnlineTaskContinue` and `XOnlineTaskClose`. The game runs its Live tasks (the content downloads) with these.
- **XAPI:**
  - `QueryPerformanceCounter` (it reads `rdtsc`; 23 call sites)
  - `OutputDebugStringA` (`int 0x2D`; 22 call sites)
  - `GetLastError`
- **C/C++ runtime:**
  - `atoi` / `atol`
  - `exit` / `doexit`
  - `std::_Xlen` ("string too long", 31 call sites) and `std::_Xran` ("invalid string position")

**Result:** the script names 1,360 functions.

## Milestone 48 (done): Bink video

All 13 Bink functions the game calls now have names. They were identified from the game's two players and the shape of the `HBINK` structure: +0 width, +4 height, +8 frame count, +0xC current frame. The playback loop is the standard Bink one: `BinkDoFrame`, then `BinkWait`, then `BinkNextFrame` (the counterpart of a "last frame reached" check on +0x8 / +0xC).

| Function | How the game uses it |
|---|---|
| `BinkOpen` | movies with `BINKSNDTRACK` (0x4000); video textures with `BINKALPHA` (0x100000) |
| `BinkSetSoundSystem`, `BinkOpenDirectSound` | Bink plays its sound through the game's DirectSound device |
| `BinkSetSoundTrack`, `BinkSetMixBins` | 4 sound tracks, each routed to its speakers |
| `BinkDoFrame`, `BinkWait`, `BinkNextFrame`, `BinkCopyToBuffer` | the frame loop; full-screen movies are copied into the back buffer |
| `BinkPause`, `BinkSetSoundOnOff` (inferred), `BinkClose` | pausing and stopping |
| `BinkGoto` (inferred) | video textures restart with it |
| `BinkLoadUnloadConverter` (inferred) | loads the pixel-format converter sections (`BINK32`, `BINK16` ...) on demand |

On the game side: `MoviePlayer_InitBink`, `_Open`, `_Update`, `_CopyFrame`, `_Pause`, `_Close` (the full-screen movies), and `BinkTexture_Load`, `_Update`, `_CopyFrame`, `_Restart` (the movies used as textures).

**Correction.** The section loader names from Milestone 34 were one level off. 0x2F78A6 / 0x2F78C7 take a section *handle*, so they are now `XLoadSectionByHandle` / `XFreeSectionByHandle`. `XLoadSection` / `XFreeSection` are the by-name versions at 0x2F78E5 / 0x2F7900, which look the handle up with `XGetSectionHandle` first.

**Result:** 251 of the 328 Xbox library functions the game calls are named, covering 8,555 of 8,757 call sites (97.7%). The script names 1,386 functions. `platform_boundary.csv` is updated.

## Milestone 49 (done): more of XAPI

35 more XAPI functions are named. 55 of the 70 XAPI functions the game calls are now known, covering 355 of their 376 call sites.

- **Timing and waits:**
  - `QueryPerformanceFrequency` stores 733,333,333, the Xbox CPU clock that `QueryPerformanceCounter` (M47) counts in.
  - `WaitForSingleObject`, `WaitForMultipleObjects`, `Sleep`, `ResetEvent`.
- **Memory:** `LocalAlloc` (flag 0x40 zero-fills), `LocalFree`, `XPhysicalFree`.
- **Console settings.** These all read the EEPROM through `ExQueryNonVolatileSetting`, and the setting number identifies each one:

  | Function | Setting |
  |---|---|
  | `XGetLanguage` | 7 |
  | `XGetVideoFlags` | 8 |
  | `XGetAudioFlags` | 9 |
  | `XGetVideoStandard` | 0x103, second byte |
  | `XGetGameRegion` | 0x104 |

  `XGetAVPack` reads `HalBootSMCVideoMode`. `XGetLanguage` falls back to Japanese or English by region. `XGetVideoFlags` drops the HDTV modes on PAL consoles.
- **Save signatures.** `XCalculateSignatureBegin` / `BeginEx` / `Update` / `End` compute an HMAC with the console key, or a certificate's alternate title key. The game signs its campaign saves with them (`Game_WriteCampaignSave`), so a port has to either produce the same signature or skip the check when loading.
- **Network:** `XNetGetEthernetLinkStatus`.
- **Kernel stubs.** The 15 `jmp [import]` stubs at 0x2FE1C0–0x2FE219 now carry their kernel names: `DbgPrint`, `RtlUnwind`, `ExQueryNonVolatileSetting`, the `Xc*` crypto functions (SHA, HMAC, RC4, DES, `XcVerifyPKCS1Signature`), and `DbgBreakPoint`.

**Result:** 269 of the 328 Xbox library functions the game calls are named, covering 8,612 of 8,757 call sites (98.3%). The script names 1,421 functions. `platform_boundary.csv` is updated.

## Milestone 50 (done): 107 more sound names, cracked from where the sounds are used

Sound cue names only exist as hashes, so they have to be guessed and checked. The usual brute-force search has a trap with this hash. It is a CRC, which is linear: two different names that share an ending (`_01` / `_02` ...) pick up the same difference in their hashes. So whenever two target hashes happen to differ by that same amount, any wrong guess that hits one also "hits" the other. A whole numbered family of nonsense matches (`tech_ambient_fall_10` ... `_19`) can therefore appear, and a family of matches proves nothing.

The fix was to check each candidate against **where the game uses the sound**. Every unknown cue was looked up in the level, object-type and world-material files:
- **World materials.** These hold the bullet-hit sounds for each material and ammo type. For example, solid metal hit by the cutter uses the same four cues as crystal.
- **Weapons.** Each one has `fire-sound-name` / `reload-sound-name`.
- **Object types.** Explosion and `sound-id` cues of destructible objects.
- **Level sound objects.**

The search then used small word lists built for each context: weapon names, ammo types, materials, creatures and object sizes. With so few combinations, the expected number of chance matches in a run is below one. Every accepted name also fits its context.

| Family | Names |
|---|---|
| Weapon fire | `fire_<weapon>_N` for 22 weapons, from `fire_standard_pistol_1` to `fire_seer_shadoon_1..3` |
| Weapon reload | `reload_<weapon>` for 16 weapons |
| Bullet hits | `hit_ballistic_metal_01..05`, `hit_cutter_metal_01..04`, `hit_cutter_wood_01..03`, `hit_energy_metal_04..05`, `hit_energy_wood_01..05`, `hit_dirt_energy_01..04`, `hit_flesh_energy_01..02`, `hit_powerblade_flesh/metal/stone_01`, `hit_sonic_flesh_01` |
| Footsteps | `fs_feral_dirt/wood/metal_01..04`, `fs_shrike_metal_01..04` |
| Explosions | `exp_crystal_lrg_01`, `exp_door_lrg_01`, `exp_pod_sml_01` / `_lrg_01`, `exp_helicopter_01`, `exp_gas_grenade_1`, `exp_sonic_grenade_1..2`, `seer_priest_exp` |
| Other | `spawn_feral_01`, `spawn_firehound_01`, `spawn_farcast_01`, `metal_door_1`, `stone_gate_03`, `grenade_hiss_loop_01` |

One weapon type got its name the same way: the fire sounds point at their object types, and `missile_cannon` hashes to one of them. (`bio_cannon`, `bio_rifle` and `laser_pistol` matched too, but were already known; a key-format slip hid them, see Milestone 57.)

**Result:** 4,070 of the 4,460 sound cues are named (91.3%, up from 88.9%; recounted in Milestone 57). The names are in `tools/names.json`, so the next run of `export_sounds.bat`, `export_dialogue.bat` and `export_game_data.bat` uses them.

## Milestone 51 (done): a sound effect browser

**Double-click `export_sound_effects.bat`** (it is also step 13 of `export_all.bat`). It writes `common\sound_effects.html`, which lists every sound effect by where the game uses it, each with a player:
- **Weapons:** fire, alternate fire, projectile (the bullet whiz), and reload sounds for each of the 48 weapon types.
- **Materials:** for each of the 27 world materials:
  - the bullet-hit sounds for each ammo type
  - the footsteps for each foot type (2 is the ferals and 4 the Shrikes, going by their sounds' names; 0, 3 and 5 share one set)
  - landing, sliding, and collision with every other material
- **Objects:** explosion, pickup, skill and jump sounds of the other object types.
- **Levels:** the sound objects placed in each level, how often each is placed, and how it plays (global, positional, indoor or outdoor).
- **Not referenced:** cues defined in a level's sound list that none of the above use. Scripts and triggers can still play them. Dialogue is on the dialogue page.

Sound references stay readable even in data converted before a name was found, because the page looks up `#hash` values in `names.json` itself. Players need the sounds exported as plain PCM (`export_sounds.bat`, answer y). The start page links the new page.

## Milestone 52 (done): the rest of the C runtime

I named 36 more C runtime functions. 58 of the 59 runtime functions the game calls are now known (the one left, 0x2EAD02, is a number-to-text helper called once), plus 8 of the 11 in the mixed area.

- **`setjmp` / `longjmp`.** `_setjmp3` saves the registers, the exception registration and the `'VC20'` cookie. `longjmp` (17 call sites) unwinds back to it. This is how Lua 3.2 raises its errors, so a port has to keep `setjmp` / `longjmp` semantics around the Lua calls, or replace them with C++ exceptions.
- **x87 maths:** `_CIacos` (18 call sites), `_CIfmod` (14), `_CIasin`, `_CIpow`. Each was identified by its instructions (`fpatan` of the right square roots, `fyl2x`) and its entry in the maths error table ("acos", "asin", "fmod", "pow").
- **Strings:** `strcmp`, `strncmp`, `_stricmp`, `strspn`, `strpbrk`, `memchr`, `strtol`, `atof`, `toupper`, `towupper`, `wcscpy`, `_wcsicmp`, `_wcsupr`.
- **Formatting:** `vsprintf`, `_vsnprintf`, `vswprintf`, `_vsnwprintf`. The narrow and wide versions were told apart by which output routine they call, and by the wide versions writing a two-byte terminator.
- **File I/O:** `fopen` (`_fsopen` with `_SH_DENYNO`), `freopen`, `fclose`, `fread`, `fseek`, `ftell`, `fgetc`, `ungetc`, `_access`, `_makepath`.
- **Other:** `_allshl`, plus the `std::exception` copy constructor and destructor.

**Result:** 305 of the 328 Xbox library functions the game calls are named, covering 8,728 of 8,757 call sites (99.7%). The script names 1,457 functions. `platform_boundary.csv` is updated.

## Milestone 53 (done): the game side of the physics boundary

I named 22 of the game functions that call into the Ipion library. With the five named earlier, 27 of the 62 are now known. All of these names are inferred from who calls each function and which IVP functions it uses, so each comment says which.

| Area | Functions |
|---|---|
| Per frame (`World_Tick`) | `PhysicsWorld_Update` (the largest user), `PhysicsWorld_ApplyImpulses`, `PhysicsWorld_SyncObject`, `StaticObjects_Tick` |
| Characters | `Character_CreatePhysics`, `Character_PlaceAt`, `Character_ProbeSurroundings`, `Character_PushObjects` |
| Bodies | `Physics_CreatePolygonBody`, `PhysicsObject_MoveTo`, `StaticObjects_AddBody`, `Platform_Update` |
| Projectiles | `Projectile_CreatePhysicsBall`, `Projectile_SyncFromPhysics`, `Projectile_PushTarget`, `Physics_QueryContacts` |
| Lava and water flows | `WaterLavaFlow_UpdatePhysics`, `WaterLavaFlow_CreateBody`, `WaterLavaFlow_RayCast` |
| Ray casts | `World_RayCastObjects`, `World_LineOfSight`, `AI_RayCast` |

`physics_boundary.csv` now shows these names in its caller column. A port that replaces IVP can start from these functions: per frame, the game creates bodies, moves them, pushes them, reads back their poses and casts rays.

**Result:** the script names 1,479 functions.

## Milestone 54 (done): the directional light direction, from the code

Milestone 33 guessed that a directional light shines along R2, the third row of its matrix. The code says otherwise.

`Light_SetTransform` (Light vtable+0x44, 0x14EC80) stores the light's 3×3 matrix. For `LT_DIRECTIONALLIGHT` (enum value 3) it then places the renderer's light 5000 units from the light's position along **(m02, m12, m22), the third column**. `Light_SetPosition` does the same when the light moves. A directional light is therefore a source far away in that direction, shining back along **minus the third column**.

The level data agrees. In all 15 levels with directional lights, the brightest one's source is above the scene: the column's y is positive. The old guess pointed it downward in only 9 of them.

`level2glb.py` now builds directional lights from minus the third column; a check on e05 confirmed the brightest light shines downward. Run `export_levels.bat` again to update the scenes. The light functions are named in the Ghidra script: `Light_SetTransform`, `Light_SetPosition`, `Light_Copy`.

## Milestone 55 (done): 48 more field names

48 unnamed XML field names are now cracked, many of them in the level and object-type files. Each one was checked against the values it holds in the data, and it fits:

| Area | Names (value seen) |
|---|---|
| Cameras | `near-clip` (0.005), `far-clip` (500), `fov` (50), `lod-distance-scale` |
| Sky | `texture-tiling` (4.0, sky dome), `angular-speed` (0 0.01 0, the rotating sky mesh), `planes` |
| Fog | `fog-type`, `density` (0.2) |
| Terrain | `resolution` (2.0: the grid spacing that `mesh2obj.py` reads) |
| Lights | `static-lights` (light names, such as `Point_light_no_corona_0`), `shadow-diffuse-scale`, `number-sides` (40) and `number-subdivisions` (the corona cone) |
| Weapons and items | `number-projectiles` (10 for the shotgun), `pickup-amount`, `ammo-regen`, `spin-speed`, `stamina-recovery` (1.5, combat skills) |
| Triggers | `in-a` / `in-b` (the calculator's two inputs), `target-level-name` (`m05_x`: the zone trigger's next level section, confirming an earlier guess), `range-object`, `flash-contact` (next to `radar-contact`), `squad-targets`, `goals`, `inventory-drops` |
| World materials | `weapon-hits` (the hit sounds for each ammo type), `collisions`, `sound-priority`, and `visual-effect`, the effect that goes with each sound effect (also the shield's recharge effect) |
| Containers | `items`: every object type in an object-type library, and every control of a menu, is an `items` element; `object-instances` |

The candidates came from a word list built from the names already known. With 501 unknown hashes and about 4 million two-word combinations, chance matches should average below one, and every match also fits its data.

**The tools read both spellings.** Data converted before this milestone still holds `_hash` tags, and data converted after holds the names. So `level2logic.py`, `menu2html.py`, `sfx2html.py` and `mesh2obj.py` accept either. To get the new names into the XML, run `convert_all.bat` (or answer y to the first question of `export_all.bat`).

Three shield fields are still unnamed: the shield's second effect (0xE76E8170), the extra delay after depletion (0x1CF65A16) and the recharge per second (0xFE14A5B7).

## Milestone 56 (done): 41 more field names, across every file type

This milestone ran Milestone 55's search over every converted XML file: 866 unknown names in 759 files, with a word list of 1,156 words built from the names already known. That gave 68 candidates. Each was checked against the values it holds, and the 41 that fit were kept. The other 27 were nonsense or ambiguous (`save-point0..3` on AI orientation points, `wpoint...` strings) and were left out.

| Area | Names |
|---|---|
| Campaign | `zone` (each map section of a mission; the element the mission guide calls a section), `planet-id` (each mission's planet), `current-zone-id`, `planet` |
| Mission flags | `kill-hounds`, `save-marines`, `destroy-cache`, `squad-deaths`, `mp_mutant2` |
| Player stats | `kills`, `deaths` |
| Weapons and objects | `fire-rumble`, `reload-rumble`, `rumble`, `rumble-effects` (controller vibration), `radar-position-type` (`RPOS_NONE` ...), `spin-axis-type` (`ROLL_AXIS`), `interface-icon-name` |
| Effects | `dest-blend-type` (`FXB_INVSRCALPHA`) |
| Textures | `number-lods`: the mip count that `tex2png.py` reads |
| Animations | `archetype-sets`, `num-archetypes`, `num-channels`, `num-targets`, `motions`, `overlays`, `bones`, `play-backwards`, `limits` (joint limits) |
| Text box trigger | `first-string-id` / `second-string-id` (the texts it shows) |
| Menus and options | `template-menu-id`, `combat-skills` (a controller binding), `sdm-archetype` (`sdm_tex`), `shadows`, `default-library` (`commentary-nnet`) |
| Other | `valid_targets` (chatter), `material-library`, `constants` (material constants), `x-resolution` / `y-resolution` (debug config) |

As in Milestone 55, the tools accept both spellings. `campaign2html.py` and `make_index.py` read `zone`, `tex2png.py` reads `number-lods`, and `level2logic.py` describes the text box trigger with the new names. The mission guide built from a freshly converted `campaign-bf.xml` finds the same 53 map sections as before.

## Milestone 57 (done): creature bone names, and a fix for names that were being missed

### 81 more bone names
Searching the unknown part names of the character skeletons with naming patterns, rather than free word lists, recovered 81 more:
- **`Joint1` … `Joint61`** (the Maya-built models: `root` → `JointN`), `Bone01` … `Bone04`, `Sphere01` and `Bip01 Footsteps`.
- **`Bip01 L/R Finger32` and `Finger42`**: the third joints of two fingers.
- **Cape chains `Cape_L_Bone_01` … `_05` and `Cape_R_Bone_01` … `_05`**, hanging from `Bip01 Spine2`.
- **Armour and attachment bones:**
  - `L_Arm_Bone01` / `03` on the upper arm and forearm, `05` / `07` on the spine
  - `Shoulder_Bone_L1` / `R1` on the clavicles
  - `Left_Hand` / `Right_Hand` on the hands

Each one sits where its name says: the cape bones form two chains of five, the shoulder bones hang from the clavicles, and the finger joints continue their fingers. A candidate that matched only once, with nothing to confirm it, was dropped.

The other unknown parts are mostly not bones. They are the pieces of destructible and multi-part objects, named after their meshes, and those mesh names are still unknown. One open question: three unknown parts hang from `Bip01 Head` in human models and from `Bone02` in another. They are probably attachment points.

### Fix: names stored as `0x0abc…` versus `0xabc…`
`names.json` held hash keys in two spellings. 6,227 were written without leading zeros (`0x57841d8`) and 1,036 with them (`0x057841d8`). The tools looked names up in only one spelling each, so:
- `anim2json.py`, `tex2png.py`, `level2logic.py` and `menu2html.py` missed the 1,036 padded names. These include many found in Milestones 50–56.
- `chatter2html.py` missed all 6,227 unpadded ones, so the dialogue page showed some lines as hashes.

`names.json` is now written in one spelling (`0x%08x`), and every tool reads either spelling. Run `export_all.bat` to get the missing names into the animation JSON, texture file names, mission logic, menus and dialogue page.

The same slip affected Milestone 50's counts, which are corrected above. Three of the "new" weapon names were already known, and the true count is 4,070 of 4,460 sound cues named.

## Milestone 58 (done): four more clips and three animation sets, and why object mesh names are out of reach

- **Clips.** I searched the 71 unknown clip names with the clip-name grammar (`Sc_` + stance such as `w1` / `w2` / `rp` / `bz` / `cr` + one or two action words from the known clips). Four came out, each in a set where it makes sense:
  - `Sc_w1_summon` in the Shadoon's set
  - `Sc_w1_death1` in the priest's set
  - `Sc_w1_scan_l` / `Sc_w1_scan_r` together in one set

  One more match, `Sc_w1_cower_heal_wake_r`, was dropped as nonsense. 67 clips remain unnamed.
- **Animation sets.** Three of the 42 unknown set names are `hf`, `lf` and `mutant`. The heavy and light feral motion sequences (`ms_hf`, `ms_lf`) already pointed at the first two, which earlier exports could only show as `#1cb6373d`. `anim2json.py` and `anim2bvh.py` now give those sets readable folder names.
- **Object mesh names: not feasible this way.** The multi-part objects hold 21,736 unknown archetype, mesh and part names. I tried every one of the 26,765 known names, each with 35 typical suffixes (`_lod0`, `_mesh`, `_col`, `_dest` ...) and 7 prefixes, and got 88 matches. Nearly all were nonsense, such as `a_wpoint5298_arch`. With that many targets, about one random guess in 200,000 hits something. These names need a real source, such as strings or texture names that share their stems, so they stay unknown for now.

## Milestone 59 (done): a map of every function, and names for the most-used helpers

- **`function_map.csv`** lists all 10,871 function starts the tools can find (every call target plus every named or tagged function). For each one it gives the address, which part of the program it is in (game, C runtime, XAPI, physics, Direct3D, sound, network, Bink, input), its name if it has one, whether it is named / tagged / unnamed, how many places call it, and its rough size. Sort it by `call_sites` to see which unnamed functions are worth looking at next.
- **21 helper names.** The most-called unnamed game functions turned out to be small container and string helpers that the rest of the code leans on. They are now named (all marked as inferred in their Ghidra comments):

  | Address | Name | What it does |
  |---|---|---|
  | `0x8F6A0` / `0x8F6E0` | `WeakRef_Link` / `WeakRef_Unlink` | add or remove a weak reference in the global bucket table at `0x414DA0` (2,048 buckets) |
  | `0x120F0` | `WeakRef_Get` | the pointer behind a weak reference, or NULL if the object is gone |
  | `0x1F4F0` / `0x1F5A0` | `std_string_assign_substr` / `std_string_erase` | std::string members |
  | `0x38940` | `std_vector_Xlen` | throws "vector<T> too long" |
  | `0x389D0` / `0xB2A50` | `std_list_Incsize8` / `std_list_Incsize` | grow a list's count, throw "list<T> too long" past the limit |
  | `0x64490` / `0x144B10` | `Vector8_PushBack` / `Vector8_Insert` | vector of 8-byte items |
  | `0x1611D0` / `0x5DC80` | `Vector4_PushBack` / `Vector_Grow` | vector of pointers; storage growth |
  | `0x1298B0` | `std_list_erase_range` | list erase |
  | `0x61500` / `0xFF730` | `List_NewNode` / `List_NewNode2` | 12-byte list nodes |
  | `0x1E97C0` | `StringTable_LookupW` | looks up a wide (localised) string in the map at `0x469970` |
  | `0x3D730` / `0x3D6C0` | `Matrix34_Copy` / `Matrix34_Set` | 3x4 matrices (rotation + position) |
  | `0x15ED00` | `Name_HashOrCopy` | turns a name into its hash with `BF_Crc32NoCase`, or copies it when the data is already hashed |
  | `0x2383B0` | `BF_Crc32NoCase_Str` | the same name hash for a NUL-terminated string |
  | `0xE35E0` | `TrivialObject_ScalarDeletingDtor` | delete for a class with an empty destructor |

- **Coverage now** (from `function_map.csv`):

  | Part | Functions | Named | Call sites covered |
  |---|---|---|---|
  | Game code | 9,596 | 1,060 (11%) | 20.0% (was 8.3%) |
  | Physics (Ipion) | 536 | 31 named + 505 tagged | 10.3% by name |
  | C runtime | 206 | 66 | 95.5% |
  | Direct3D, DirectSound, network, Bink, input | 251 | all | 100% |

  The helpers are few but called from everywhere, so naming 21 functions lifted the share of game call sites that go to a named function from about one in twelve to one in five. Decompiled code reads much better as a result, since most functions call several of these.

## Milestone 60 (done): the render-state cache

The most-called unnamed function (638 call sites) turned out to be the heart of the game's **render-state cache**, a layer between the game and Direct3D:

- The renderer object (`g_renderer`, static at `0x46B2E8`) keeps a copy of every Direct3D render state it uses, one 20-byte slot each starting at +0x64.
- **`RenderState_SetValue`** (`0x6A610`) changes a slot only if the value is different. It saves the old value as an undo record and puts the slot on a "changed" list. **`RenderState_SetResource`** (`0x622F0`) does the same for textures, keeping their reference counts right, and **`Render_SetTexture`** (`0x62280`) binds a texture to a stage (NULL means the built-in white texture).
- **`RenderState_PushScope`** / **`RenderState_PopScope`** (`0x2385A0` / `0x238630`) bracket the drawing of a material or effect, so any states it changes are put back afterwards.
- **`RenderState_Flush`** (`0x2386D0`, 117 call sites) sends only the changed states to Direct3D, just before something is drawn.
- **`Renderer_Init`** (`0x238BB0`) sets it all up at startup and makes two 1x1 default textures (`g_whiteTexture`, `g_blackTexture`). **`Render_SetStartupStates`** (`0x1EA890`) then sets the starting value of each state; the values match Direct3D constants such as `D3DFILL_SOLID` (`0x1B02`) and `D3DSHADE_GOURAUD` (`0x1D01`), which confirms what the slots are.

For a recompilation this layer matters: on PC it can map almost one to one onto a modern graphics API's state handling. Code that draws only needs these few calls, not raw Direct3D 8.

Other names this milestone:

| Address | Name | What it does |
|---|---|---|
| `0x238290` / `0x238300` | `Pool_Alloc` / `Pool_Free` | fixed-size block pool used by the state cache |
| `0x10BC40` | `Vector4_Assign` | copy one vector of pointers into another |
| `0x2701E0` | `SceneScript_WriteItem` | writes one formatted item into scene-script text (90 calls from `SceneScript_Write`) |
| `0x10A7D0` | `UIText_SetText` | sets the text of a menu text control |
| `0x23A310` | `Property_ReadEventReference` | reads a data property as an event reference (`IDAXProperty` / `IDatabaseEventReference`) |
| `0x238080` / `0x237FF0` | `TypeRegistry_QueryInterface` / `TypeRegistry_Register` | the registered-type list at `0x46CF80` |

25 names in all (21 functions, 4 globals). Game call sites that go to a named function: **24.9%** (20.0% after Milestone 59). `function_map.csv` is regenerated.

## Milestone 61 (done): the HUD classes, by name

`0xB9120` is now **`InterfaceObject_Create`**, the factory for everything drawn on the in-game HUD. It switches on the hash of a class name and builds the matching object. There are 82 classes, and the interface code (`0xA9000`–`0xCD000`) calls it 108 times to build each player's HUD.

Only one of the 82 hashes was known before (`radar`). I matched the rest against HUD vocabulary and the words in the English string table. The names are lowercase words run together, like `healthbar`. **53 of 82** came out. Each one is backed by the class tree in the code, not just by the hash:

- `healthbar` and `staminabar` are both built on `statusbar`.
- `actionreticule` and `moveoutreticule` are built on `complexreticule`.
- `playermarker`, `objectivecursor` and `lightcorona` are built on `interfacemark`.
- The text classes (`errorbox`, `pickupreadoutbox`, `activeitemcount`, ...) are built on `interfacetextbox`.
- Everything sits on `baseinterfaceobject`, then `complexinterfaceobject` or `simpleinterfaceobject`.

Several other matches were nonsense even though the hash fit, so I dropped them (`boxonbrutus`, `medalfollowerarmor`, `killsordersetting`, ...). Where two names fit one hash, the class tree picked the right one.

| Group | Classes |
|---|---|
| Bases | baseinterfaceobject, complexinterfaceobject, simpleinterfaceobject, circleinterfaceobject, interfacetextbox, interfacemark |
| Radar | radar, activeradar, radarinfohealth, radarinfoorder, radarinfodirection, radarinfoface |
| Reticules | complexreticule, actionreticule, moveoutreticule, reticulecrosshair, reticulecrosshairimage, turretreticule, turretreticuleimage, actioncrosshair, moveoutcrosshair |
| Bars | statusbar, healthbar, staminabar, healthstaminabar, titlebar, infobar |
| Items and pickups | activeitem, activeitemcount, activeitemimage, activeitemdescription, pickupreadout, pickupreadoutbox, weaponreadout |
| Awards and damage | awardreadout, awardreadoutline, damagedisplay, damagedisplayarrow, skillicon |
| Markers | playermarker, playermarkercontainer, objectivecursor, lightcorona |
| Characters and menus | selectcharacter, togglecharacter, togglecharactertext, ordersmenu, profiletargeted (probable) |
| Other | gamecredits, gamecreditsgroup, screenflash, errorbox, tutorialnotification |

- **Ghidra:** each constructor is named `<Class>_ctor` (for example `HealthBar_ctor`, `MoveOutReticule_ctor`). 54 names in all, counting the factory.
- **Data tools:** the class names are added to `tools/names.json`. Tools that show names will now show `healthbar` for the HUD item in `game-options` that was listed as `#0ec62d9d`.
- **Still unknown:** 29 classes, 5 of which have no constructor in the switch (they are created some other way). They include the base of the image classes (`0xBBCB0`, parent of `reticulecrosshairimage` and `activeitemimage`) and two classes built on another unnamed base (`0xCB580`).
- **Coverage:** 25.5% of call sites in game code now go to a named function.

## Milestone 62 (done): the component model behind the data objects

The constructor called from 180 places (`0x1E0EF0`) led to the engine's **component model**. It's a COM-style system from Digital Anvil, the studio that made Brute Force. Objects expose named interfaces with a `23.23_` prefix, such as `23.23_IDAComponent`, `23.23_IDatabaseEvent` and `23.23_IMaterial`. They hand them out through `QueryInterface`, with `AddRef` and `Release` reference counting. The binary names 76 interfaces.

- **How it was mapped:** each class keeps a small table of (interface name, offset) pairs, and its `QueryInterface` walks that table. I found all **60 tables**, their 60 `QueryInterface` functions and the vtables that use them. That gives a class-by-class list of which interfaces each object supports. Each class is named after its most specific interface.
- **Data objects:** everything the data loaders create (archetypes, menu objects, effects data and so on) is built on one base:
  - `DaxObject_ctor` (`0x1E0EF0`) builds an object that implements `IDatabaseEvent`, `IDAXProperty` and `IDAComponent`.
  - `DaxObject_PushPropertyReader` (`0x1E1090`, 95 calls) switches property reading to a nested block's handlers while a sub-block is read.
  - `DaxObject_AddRef` and `DaxObject_Release` keep the count at +0x38.
- **Shared AddRef functions:** many classes share the same tiny AddRef, differing only in where the count sits. These are named `Com_AddRef_04` … `Com_AddRef_30`.

| Group | Classes (QueryInterface address) |
|---|---|
| Effects (Alchemy) | `FxNodePropertySet` (`0x65ED0`), `FxAppearance` (`0x2AEB70`), `FxEffect` (`0x2ACE40`), `FxEffectArchetype` (`0x2ACF40`), `FxEffectLibrary` (`0x2AD6C0`), `FxField` (`0x2AE240`), `FxEmitter` (`0x65FB0`), `FxNodeLibrary` (`0x2A7CC0`), `FxAppearanceAlchemyEvent` (`0x2A3F60`), `Alchemy` (`0x29FAE0`), `FxRuntime` (`0x29FC60`), `FxNode` (`0x2A5A00`) |
| Animated values and ramps | `RampColorRamp` (`0x64050`), `AnimatedSingleRamp` (`0x630A0`), `ParamCurve` (`0x279FE0`), `Animation` (`0x261380`), `RampColor` (`0x646A0`), `RampSingle` (`0x638F0`), `RampSingleRamp` (`0x635A0`), `AnimatedSingle` (`0x63220`), `AnimatedTransform` (`0x62620`) |
| Textures | `NamedTextureLibrary` (`0x286590`), `AnimatedNamedTexture` (`0x263170`), `OptimizedNamedTexture` (`0x262020`), `NamedTextureLocator` (`0x260590`), `MovieNamedTexture` (`0x25B5C0`) |
| Scene controls and paths | `CRSpline` (`0x27C2B0`), `ScriptEngine` (`0x24FBC0`), `GeoTransformControl` (`0x177600`), `MotionPath` (`0x176E20`), `UserEventInfo` (`0x176600`), `LightControl` (`0x174050`), `CompoundControl` (`0x172B40`), `CameraControl` (`0x173350`), `CompoundControlB` (`0x172620`) |
| Engine, files and other | `File` (`0x264E20`), `MemCache` (`0x265710`), `ProfileParser` (`0x261A20`), `VertexBufferManager` (`0x25FDF0`), `Engine` (`0x25F680`), `Locator` (`0x25AB80`), `Location` (`0x25A680`), `FileManager` (`0x23BAD0`), `Camera` (`0x6EA90`) |
| Database and schemas | `DatabaseFileInstance` (`0x263A00`), `DatabaseEvent` (`0x258660`), `DatabaseEventReference` (`0x257670`), `SchemaLibrary` (`0x256E20`), `SchemaFileInstance` (`0x256220`), `Database` (`0x255BD0`), `Schema` (`0x2373E0`), `DaxObject` (`0x77260`), `DaxObjectB` (`0x15BD80`) |
| Materials | `MaterialLibrary` (`0x25D980`), `BatchedMaterial` (`0x25BDD0`), `Material` (`0xA1870`), `QuatMaterial` (`0x9E7C0`), `BF_Ocean_Material` (`0x93560`), `BF_Lava_Material` (`0x92390`), `BF_TerrainMaterial` (`0x914E0`) |

**134 names** in the Ghidra script: 60 `<Class>_QueryInterface` functions, 60 `g_qiTable_<Class>` labels, 8 shared AddRefs and 6 base or helper functions. For a PC port, this means the engine's internal APIs are cleanly separated by interface. Effects ("Alchemy"), materials, textures, the database and file system each sit behind one, which makes them good places to swap in new code.

## Milestone 63 (done): more shared helpers, and four more HUD classes

**14 helper names**, the next most-called unnamed functions (all marked as inferred in Ghidra):

| Address | Name | What it does |
|---|---|---|
| `0xB25A0` | `InterfaceObject_AddChild` | adds a HUD object to its parent (108 calls, right after `InterfaceObject_Create`) |
| `0xA4D00` | `Render_SetPixelShader` | switches pixel shader only when it changes, and counts the switches |
| `0x6A5F0` | `RenderState_SetByIndex` | sets render-state cache slot *n* (Milestone 60) |
| `0x6EC90` | `Matrix34_ToRows` | turns a rotation-plus-position matrix into the row layout shader constants use |
| `0x76FE0` | `ComPtr_Release` | releases a component interface pointer and clears it (Milestone 62) |
| `0x1A7170` | `Property_ParseOrCopy` | reads one data property from text, or copies it when the data is already binary |
| `0x4B5F0` / `0x1D590` | `WeakRef_Construct` / `WeakRef_DestroyRange` | weak references (Milestone 59) |
| `0xE1150` | `std_list_erase_range_weak` | list erase for lists of weak references |
| `0x12ABA0` / `0x279C40` | `std_tree_iterator_inc` / `std_map_int_insert` | std::map internals |
| `0x87940` | `std_wstring_assign_substr` | wide-string assign |
| `0x295370` | `luaO_verror` | Lua 4.0 error raising ("exit(1). Unable to recover.") |
| `0x26B210` | `SceneScript_GetField` | reads a field of a Lua table (46 calls in `SceneScript_Load`) |

**4 more HUD class names** (Milestone 61), each confirmed by its base class:
- `weaponreadoutimageone` and `weaponreadoutimagetwo` share the same base class.
- `slideoninterfacetextbox` is built on `interfacetextbox`.
- `actioncrosshairdescription` is built on `complexinterfaceobject`.

57 of the 82 HUD classes are now named. The names are also in `tools/names.json`.

Game call sites that go to a named function: **29.3%** (26.6% after Milestone 62).

## Milestone 64 (done): the whole game as C source

`ghidra_scripts/BruteForce_ExportC.java` has Ghidra decompile every function in `default.xbe` into C files, using all the names and comments from `BruteForce_ApplyNames.java`. It has been run once. The result is in **`C:\Users\<you>\Brute Force data\decompiled`**, outside OneDrive:

- **14,409 functions in 113 files, 0 failures** (about 4 minutes in Ghidra).
- **Game subsystems by name:** `game_AIGoal.c`, `game_Character.c`, `game_World.c`, `game_RenderState.c`, `game_SoundManager.c`, `game_Trigger*.c`, `game_SceneScript.c` and so on. Named functions whose group is too small for its own file are in `game_misc_named.c`.
- **Unnamed game code:** grouped by address, one file per 64 KB (`game_unnamed_0A0000.c` …), so neighbouring functions, which usually belong together, stay together.
- **Libraries:** `lib_d3d8.c`, `lib_dsound.c`, `lib_xapi.c`, `lib_crt.c`, `lib_physics_ipion.c`, `lib_lua.c`, `lib_bink.c`, `lib_xnet.c`, `lib_xonline.c`, `lib_xpp_input.c`.
- **`index.csv`:** every function's address, name and file. **`ghidra_types.h`:** the `undefined4` / `uint` style types Ghidra uses.

Each function starts with its address and name, then the comment from the naming script. This is Ghidra's reading of the machine code, not the original source. It is readable and searchable, but it won't compile as-is.

**To refresh it after new names:**
1. Start Ghidra with `Start Ghidra.bat` and open `default.xbe`.
2. Run `BruteForce_ApplyNames.java`, then `BruteForce_ExportC.java` (Window → Script Manager, filter "BruteForce").

**Starting Ghidra:** `Start Ghidra.bat` in this folder starts Ghidra 12.1.4 from your Downloads folder and logs startup messages to `ghidra_start.log`. Keep its black window open while Ghidra runs. The Brute Force project is `C:\Users\<you>\BruteForce.gpr`. It already has `default.xbe` imported, with all names up to Milestone 63 applied and saved (2,195 names, 0 failed).

## Milestone 65 (done): a tidier source export, plus globals and types headers

`BruteForce_ExportC.java` now sorts the leftover named functions by kind instead of dumping them all into one file. The 512-function `game_misc_named.c` is split up:

| File | Functions | What's in it |
|---|---|---|
| `game_data_types.c` | 205 | the many small archetype / data / type classes: `*_Create`, `*_ReadProperty`, `*_GetTypeId` |
| `game_hud.c` | 61 | the HUD classes (Milestones 61 and 63): `HealthBar_ctor`, `Radar_ctor`, `InterfaceObject_Create` … |
| `game_components.c` | 59 | the component model (Milestone 62): every `*_QueryInterface`, `Com_AddRef_*`, `ComPtr_Release` |
| `game_util.c` | 18 | small containers, matrices and strings |
| `game_misc_named.c` | 169 | everything else |

Two new headers:
- **`bruteforce_globals.h`:** an `extern` line for every named global (**1,312**), each with its address and the note from the naming script. For example, `g_renderer` explains the renderer object layout.
- **`bruteforce_types.h`:** every data type defined in the Ghidra program (structures, enums, typedefs, including the ones the Xbox loader adds). It's for reference: some typedefs overlap with `ghidra_types.h`.

Every `.c` file now includes both headers. The export was run again: **14,409 functions in 117 files, 0 failed**.

## Milestone 66 (done): structures, so the code shows field names

`ghidra_scripts/BruteForce_ApplyTypes.java` creates structures in Ghidra (category `/BruteForce`) from the field offsets documented in earlier milestones, then applies them:

| Structure | Size | Fields |
|---|---|---|
| `Character` | 0x860 | stamina / maxStamina, yaw / targetYaw, aimMatrix / aimPitch, motionCurrent / motionRequested / overlays, controller, zoomLevel, viewOffset, timers … (30 fields) |
| `World` | 0xC84 | the subsystem pointers from Milestone 14 (projectiles, sceneScripts, cameraSystem, viewManager, messageSystem, worldSound …), object list, tick count |
| `Renderer`, `RenderStateCache`, `RenderStateSlot` | | the render-state cache from Milestone 60 (198 slots) |
| `DaxObject` | 0x3C | the data-object base from Milestone 62 |
| `MotionRecord`, `CharacterType`, `Vec3`, `Mat3` | | supporting types |

- **Globals:** `g_renderer` is typed as `Renderer *` and `g_pWorld` as `World *`.
- **Functions:** the object parameter of **29** `Character_*` and `World_*` functions is typed and named `character` / `world`. Ghidra only knew most of these parameters inside the decompiler, so the script stores them in the program first. The 8 functions skipped have no object parameter (their object arrives in another register).
- **Result in the export:** for example, `Character_SetMaxStamina` now reads `character->maxStamina = param_2; if (character->stamina <= character->maxStamina) …` instead of `*(float *)(param_1 + 0x494)`. `game_Character.c` has 361 such field accesses.

Run order in Ghidra: `BruteForce_ApplyNames.java`, then `BruteForce_ApplyTypes.java`, then `BruteForce_ExportC.java`, then File → Save. This has been done: the project is saved and `Brute Force data\decompiled` is up to date.

## Milestone 67 (done): weapons, projectiles and the AI controller

`BruteForce_ApplyTypes.java` now also defines:

| Structure | Size | Fields |
|---|---|---|
| `Weapon` | 0x384 | minDamage / maxDamage, bulletData, triggerFlags (bit 2 = trigger held), projectileArchetype, plus the game-object fields |
| `Projectile` | 0x64 (size not known) | baseDamage, shooter, hitMap |
| `AIController` | 0x9E0 | vtable, thinkTimer |

It also adds five fields to `Character`: lastUpdateTick, shield, damageModifiers, shooterComponent and damageFactor, all game-object or shooting fields used by `Weapon_RollDamage`.

The object parameter is now typed on **47** functions (`character`, `world`, `weapon`, `projectile`, `ai`), up from 29. For example, `Weapon_RollDamage` reads `(weapon->maxDamage - weapon->minDamage) * random + weapon->minDamage`, and `Projectile_GetDamage` starts from `projectile->baseDamage`. The 14 functions skipped take no object parameter, and the script's console output lists them.

All three scripts have been run again in order, the project is saved, and `Brute Force data\decompiled` is up to date.

## Milestone 68 (done): structures for the game's data classes, named from the data files

Each data class the game loads from its data files (archetypes, AI data, menu objects, campaign, sky and so on) has property handlers. A handler switches on the hash of a field name and stores the value at a fixed offset in the class's object. I read every handler straight from the disassembly and collected the field-name hash, offset and parser for each case. The hashes were then matched against the field names already known in `tools/names.json`.

- **Result:** **44 data classes**, **168 fields** (118 with their real data-file names, the rest shown by hash). Every class's fields fall inside the object size allocated by its `_Create` function, which checks the offsets.
- **Dropped:** offsets that two different field names claim (sub-objects that reuse the same handler pattern) are left out.
- **`ghidra_scripts/BruteForce_ApplyDataTypes.java`:** creates these structures in Ghidra (category `/BruteForce/Data`) and makes 31 `<Class>_Create` functions return a pointer to theirs. Run it after `BruteForce_ApplyNames.java`.
- **`data_fields.csv`:** the full table (class, offset, field name, name hash, parser).

Examples:
- `CharacterAiData`: `personality_freewill`, `personality_formation`, `personality_follow`, `personality_wingman`, `sparam_trigger_string`, `alignment_type`
- `WeaponArchetype`: `weapon_type`, `fire_sound_name`, `projectile_sound_name`, `clip_archetype_name`, `reticule_prefix`
- `BulletData`: `spin_axis_type`, `trigger_effect_name`, `hit_effect_object_name`, `mesh_name`, `projectile_archetype_name`
- `CharacterArchetype`: `motion_archetype`, `chatter_library`, `radar_position_type`, `inventory`, `limit_weapon`, `speeds`

**Not yet covered:** 43 of the 87 classes. Their handlers use jump tables or shared code that this first extractor doesn't follow.

The script has been run, the project saved and `Brute Force data\decompiled` re-exported (14,409 functions, 0 failed). The structures appear in `bruteforce_types.h`.

## Milestone 69 (done): the field extractor covers 62 data classes, including the triggers

The property-handler extractor from Milestone 68 now also understands:
- **Direct stores:** handlers that write the value straight into the object (`mov [obj+0x8],eax`) instead of calling a parser. The store's width gives the field size (byte, word or dword).
- **Trigger serializers:** each `Trigger*_ReadProperty` compares the field hash, then writes back to the trigger object (`[esi+offset]`). These give structures for the trigger classes themselves. For example, `TriggerTimer` has `initial_value`, `current_value`, `enable_timer` and `enable_looping`, which matches the fields already documented for it.

Both passes are merged. A field is kept only where they agree, and an offset claimed by two different names is dropped.

| | Milestone 68 | Milestone 69 |
|---|---|---|
| classes with structures | 44 | **62** |
| fields | 168 | **430** |
| fields with real names | 118 | **224** |
| `_Create` functions returning their structure | 31 | **42** |

Every field still falls inside its object's allocated size. Field types:
- name hashes are `uint`
- one-byte fields (mostly on/off flags) are `byte`
- the rest stay `undefined4` until their parser is identified

`data_fields.csv` now has a size and kind column. The script has been run, the project saved and the C export redone (14,409 functions, 0 failed).

**Still not covered:** 25 classes. Their handlers use jump tables or hand off to shared code. They include `MotionSequence`, `StaticTextControl`, `WorldMaterialLibrary` and ten trigger types such as `TriggerSpawn` and `TriggerAi`.

## Milestone 70 (done): field types from the game's own data

The 4-byte data fields from Milestones 68 and 69 were untyped. Each named field also appears as a tag in the decoded data files (`full2`, 759 files), so I typed it from its values there:
- any value with a decimal point means `float`
- only whole numbers means `int`
- words mean the field holds text in the data. In memory that's an enum value, a name hash or a pointer, so it stays 4 raw bytes with a note saying so.

| Type | Fields |
|---|---|
| name hash (`uint`, from the parser) | 75 |
| `float` (from the data) | 41, e.g. `max_stamina`, `range`, `radius`, `fade_out_time`, `max_charge` |
| `int` (from the data) | 14, e.g. `hitpoints`, `ammo_count`, `goal_id`, `number_particles` |
| text in the data (enum / hash / pointer) | 38 |
| `byte` | 1-byte fields (mostly on/off flags) |
| still untyped | the rest: fields with no known name, or never set in the data |

Each typed field's Ghidra comment says where its type came from. `data_fields.csv` has a `type` column. `BruteForce_ApplyDataTypes.java` was run again, the project saved and the C export redone (14,409 functions, 0 failed). `bruteforce_types.h` now reads, for example, `float max_stamina;` and `int hitpoints;`.

**Caveat:** a few tag names are used by more than one class. The type comes from all the values of that tag, so it could be wrong for a class that uses the same tag differently.

## Milestone 71 (done): 71 of 87 data classes covered

Three more patterns in the property handlers:
- **Shared handlers:** a handler shared by several classes (for example the menu controls) writes the same offsets in each of them. Those fields belong to the common base, so they now go into every class that uses the handler. They were excluded before, as a side effect of a bug that has since been fixed.
- **Trigger object register:** some trigger readers keep the trigger object in `ebp` rather than `esi`. The extractor now follows whichever register the object was copied into, and also takes fields that are handed to a helper by address. `TriggerSpawn` now has `spawned_object_name`, `spawn_fade_time`, `spawn_delay`, `home_range_height` and `goal_id`.
- **Single-field handlers:** these compare the field hash against the stack (`cmp [esp+0xc],HASH`) instead of a register.

| | M68 | M69 | M70 | **M71** |
|---|---|---|---|---|
| classes | 44 | 62 | 62 | **71** |
| fields | 168 | 430 | 430 | **471** |
| named fields | 118 | 224 | 224 | **255** |
| typed from the data (float / int) | — | — | 41 / 14 | **52 / 16** |
| `_Create` functions returning their structure | 31 | 42 | 42 | **48** |

Every field is still inside its class's allocated size. The script has been run, the project saved and the C export redone (14,409 functions, 0 failed).

**Still not covered (16 classes):**
- CompoundArchetype, ContentPackageDatabase, FollowCameraArchetype, HeadArchetype, InventoryDropData, LevelFile, MotionSequence, PlatformArchetype, PlayerstatsFile, ProjectileMeshArchetype, ShieldRecharge, StaticTextControl, Vulnerability
- TriggerRespawn, TriggerResponse, TriggerSquad

Most of these only create child objects, or pass every field to a generic helper.

## Milestone 72 (done): a PC loader that maps the game at its Xbox addresses

Stage 3 has started: running the original game code on Windows, without an Xbox emulator, by giving it the Xbox system it expects. The loader is in `loader\`; double-click `run_loader.bat`. Each run writes `bfloader.log` next to it.

- **`bflaunch.exe`** is linked at the Xbox base address (`0x10000`) with a 4.75 MB empty section. When Windows starts it, the whole Xbox address range `0x10000`–`0x4D4000` already belongs to the program, so nothing else can be loaded there. It then loads `bfcore.dll`.
- **`bfcore.dll`** reads `default.xbe` from your game folder and copies its 30 sections to their Xbox addresses.
  - It fills the game's 147 kernel imports with its own functions.
  - It patches the 25 places where the game reads the Xbox processor block through `fs:`. Each one now calls a small helper that returns a per-thread fake block (thread pointer, TLS block, IRQL).
- **Windows quirks handled:**
  - Windows reads the program's PE header whenever it creates a thread, and the game's header covers it. The loader keeps a copy and puts it back for the moment a thread is created.
  - The process is large-address-aware so it can use the Xbox's high addresses.
- **Source:** `bfcore.c` (loader), `kernel.c` and `kernel2.c` (kernel functions), `bflaunch.c`. Builds with clang + lld-link; no Windows SDK is needed.

## Milestone 73 (done): every Xbox kernel function the game imports

All 147 imports are implemented (129 functions, 18 data):
- **Memory:**
  - a 64 MB contiguous pool at `0x80000000`, where address = `0x80000000` + physical address, as on the Xbox, allocated top-down
  - the same memory seen again at `0xF0000000` (the write-combined view)
  - virtual memory and pool allocations
- **Threads and timing:** threads with the Xbox TLS layout, critical sections, events, mutants, polling waits on the game's own kernel objects, timers and DPCs (run on a 1 ms ticker thread), `KeTickCount`, performance counters.
- **Files:**
  - `D:\` and `\Device\CdRom0` are the game folder.
  - The hard-disk partitions are folders in `loader\hdd\` (`p1` holds the save data `T:`/`U:`, `p5` is the cache drive `Z:`).
  - Symbolic links, directory listing, file information and volume information are supported. Raw volume reads give zeros and raw writes are dropped, so the Xbox "formats" the cache partition without touching anything.
- **Other:** disk model and serial, 3 cache partitions, settings (region: North America), SHA-1 / HMAC / RC4 (real), RSA and DES (accepted), time conversion.

With these, XAPI's start-up runs to the end. It mounts `T:`, `U:` and `Z:`, then hands over to the game.

## Milestone 74 (in progress): the game's own code runs, without graphics

Behind the game's Direct3D, DirectSound and USB code is real hardware, so the loader adds a stand-in:
- **Hardware range:** `0xFD000000`–`0xFEFFFFFF` (GPU, audio, USB) is plain memory.
- **GPU command buffer:** everything Direct3D puts in the GPU's command buffer counts as consumed at once (GET = PUT), and its fences are reported as reached.
- **GPU registers that are not plain memory** sit on pages that are kept inaccessible:
  - the interrupt status registers clear the bits written as 1
  - the master interrupt register summarises the units
  - the write-combine flush finishes at once

  The few instructions that touch these pages are decoded and carried out by the loader (`mmio_fault` in `kernel2.c`).
- **Privileged instructions:** `in`/`out` port access, `cli`/`sti`, `wbinvd` and control-register moves are skipped; port reads give 0.
- **Vertical blank:** 60 times a second the loader raises the GPU's vertical-blank interrupt and runs the game's own interrupt routine. Direct3D's DPC then wakes everything waiting for the next frame.
- **Watchdog:** every 3 s the log gets the number of kernel calls so far and the last 16 calls. If the game stops making calls, it also gets where each thread is.

**Where it stands:** the game starts its worker threads and loads its data:
- the cache files under `Z:\common\` (schemas, string tables in 5 languages, textures, sound banks, profiles)
- `d:\version.bin`
- the language sound archive

After that it runs steadily, about 1,000 kernel calls a second, and sends GPU commands every frame. No picture yet: the commands are only consumed, not drawn.

## Milestone 75 (done): reading the GPU command stream, and a window

`gpu.c` reads the push buffer the way the NV2A would:
- it follows jumps, calls and returns
- it keeps the last value of every 3D-object method
- it carries out clears

A 640×480 window shows the framebuffer that the CRTC start register points at. Semaphore releases are recorded but not written to memory yet. Draw calls are counted.

## Milestone 76 (done): the game reaches its frame loop

Four fixes take the game from a stall after `d:\version.bin` to rendering about 35 frames a second:
- **The I/O thread exited at once.** The game's file I/O thread (`0x264620`) waits on `count + 3` handles. The game can raise the count before the array slot is filled. On the single-core Xbox the higher-priority thread is already waiting by then; on the PC it saw a NULL handle, Windows failed the wait, and the thread quit. Every later read then waited forever for its completion. `NtWaitForMultipleObjectsEx` now leaves NULL handles out of the wait and maps the index back.
- **Reads cost a timer tick each.** Each synchronous read spins on `SleepEx(0)` until the I/O thread completes it, and the schema parser reads 1–4 bytes at a time (bfns.xsb alone is about 15,000 reads). `KeDelayExecutionThread(0)` used to become `Sleep(1)`, about 16 ms. It is now a yield, and the loader asks Windows for 1 ms timer resolution. This was the "bfns.xsb read loop".
- **DirectSound waited for the audio processor.** It polls the voice processor's PIO free-space register `0xFE820010` until it reads at least `0x20`. The ticker keeps it at `0x80` (`apu_tick`).
- **Direct3D waited for a GPU interrupt.** When the push buffer is too full, D3D adds `WAIT_FOR_IDLE` + `NO_OPERATION(5)` and waits on an event (device `+0x196C`). On the NV2A a NOP with a non-zero parameter raises PGRAPH `INTR_ERROR`, with the method and parameter in `TRAPPED_ADDR`/`TRAPPED_DATA_LOW`. The push-buffer reader now does the same. It runs the game's GPU interrupt routine and pauses until the routine clears the bit.

Also:
- `loader\build.bat` builds everything with LLVM (`C:\Program Files\LLVM`) and writes `bfcore.map` for looking up loader addresses.
- The watchdog's stack walk stays on the thread's own stack. It had crashed on game code without frame pointers.
- A Ke wait longer than 2 s is logged with its object and caller.

**Where it stands:** on a fresh `xbox_hdd` the game unpacks `data\common.tgz` into the Z: cache. It then loads:
- the schemas, string tables and AI personalities
- sounds, animations, textures, materials, objects and effects
- the four squad members
- the save-data globals on T:
- the `splash_screen` level

It then stays in its frame loop: about 35 flips/s, about 145 draw calls per frame, about 45 NOP interrupts a second. Only clears reach the screen.

## Milestone 77 (done): a picture, a controller, and the first mission

The loader now draws the game. Screenshots: `loader_title_menu.png`, `loader_menu_campaign.png`, `loader_intro_cutscene.png`, `loader_ingame_tutorial.png`.

**Software renderer (`loader\render.c`).** It draws into the colour and depth surfaces the game set up in Xbox memory, so the picture is whatever the game rendered:
- **Vertex input:** immediate data (`SET_VERTEX_DATA2F/2S/4UB/4S/4F`, `SET_VERTEX3F/4F`), inline arrays, and vertex arrays through `DRAW_ARRAYS` / `ARRAY_ELEMENT16/32`. Supported types: UB (D3D and OGL order), S1, S32K, F, CMP.
- **Vertex programs:** an interpreter for the NV2A microcode, uploaded through `0x0B00` with constants through `0x0B80`. It covers all MAC and ILU ops and `a0` indexing. D3D's programs output window coordinates, with `w` kept for perspective. A minimal fixed-function path (composite matrix only) also exists; the game hasn't used it so far.
- **Primitives:** triangles, strips, fans, quads, quad strips and polygons. Points and lines are not drawn yet.
- **Textures:** swizzled and linear formats, DXT1/3/5, palette, nearest and bilinear filtering, wrap/mirror/clamp. Level 0 only; no mipmaps yet.
- **Register combiners:** the general stages (input mappings, dot products, mux, output scale and bias, blue-to-alpha) and the final combiner.
- **Pixel operations:** alpha test, depth test and write (Z16 and Z24S8), blending (all factors and equations), colour mask, cull mode, clears of colour and depth.
- **Speed:** compiled with SSE2. Big draws are split into interleaved rows across 6 threads; small ones are drawn on the ticker thread. A built-in benchmark runs when `loader\bench.txt` exists; `rbench.bat` builds a standalone version.

**Fixes needed for the picture and the game:**
- **Pitch:** the window used `AvSetDisplayMode`'s pitch, which is half the surface's.
- **Semaphores:** semaphore releases (`0x1D70`) are now written, in command order, to D3D's fence word (the device's `+0x30` pointer). They replace the old "GET == PUT means every fence is reached" shortcut. That shortcut reported fences early. D3D then re-patched a recorded push buffer (`RunPushBuffer`, raw 0x35F8DC) from the CPU while the GPU still had to run its NOP fix-up (software method 3), and the GPU ended up looping through the recorded buffer forever.
- **The window moved to a separate program.** `bfview.exe` shows the frames the loader puts in the shared memory `BruteForcePCLoader_View`, and hands back the keyboard and the close button. With the window inside the loader process, the process (whose main image is the XBE) kept being frozen for about a second at a time, about once a second; the renderer had seemed 16× slower than it was. The present thread also waits for the contiguous memory to be mapped, and the exception handlers are installed first, so no early fault reaches Windows Error Reporting.
- **Save folders:** directories opened with "create" are created and then opened (they used to be opened with `CREATE_NEW`, which failed). Campaigns can now be created and saved on U:.

**Input (`loader\input.c`).** The game's XInput functions (`XInitDevices`, `XInputOpen/GetState/SetState/Close`) jump to the loader's own versions. These read the PC's controller through `xinput1_4.dll` on a polling thread of its own (PC `XInputGetState` can take milliseconds), with rumble passed through. `XGetDevices` and `XGetDeviceChanges` stay the game's own: they only read the gamepad type's bitmasks at `0x37ECD0`.

Keyboard, while the game window is in front:

| Keys | Xbox controller |
|---|---|
| arrows | d-pad |
| Enter | START |
| Backspace | BACK |
| Space | A |
| Esc | B |
| F | X |
| R | Y |
| W A S D | left stick |
| I J K L | right stick |
| Q / E | left / right trigger |
| Z / C | white / black |
| 1 / 3 | left / right stick click |

For testing, `loader\auto_press.txt` taps START+A every 4 s.

**Where it stands:** the game goes through its menus to the tutorial mission:
- title → Campaign → profile → campaign name → mission
- it plays the intro cutscene and reaches gameplay with the full HUD

The menus run at about 30 frames/s. In the mission it is under 1 frame/s: about 150,000 triangles and 2 million pixels per frame, all drawn on the CPU. Sound is not played yet.

## Milestone 78 (done): missions about 5× faster in software

Measured in the tutorial mission (per 15 s of play, with the `[gpu]`, `[render]` and `[profile]` log lines):

| Change | Effect |
|---|---|
| Vertex programs decoded once per upload; only the operands an instruction uses are fetched | vertex cost ÷4 |
| Indexed and array draws transformed per distinct index (hash map), in parallel on the workers for batches of 48+ | vertex work halved |
| Triangles of many draw calls queued with a per-draw state snapshot (`RS`), drawn together | flushes only at fences, NOP interrupts, render-target changes, clears and the end of each pass |
| Culled and zero-area triangles rejected before queuing | fewer copies |
| 8-row bands per thread, with a bounding-box reject; 12 threads | balanced work |
| Register combiners decoded per draw and run on 4-lane SSE vectors | the old per-channel switches were the hottest spot |
| Linear planes for depth, 1/w and the varyings; vector blend factors | |
| Swizzle lookup tables, power-of-two wrap masks, DXT block reuse in bilinear sampling | |
| NOP interrupts handled in the same pass when the game's routine clears them at once | less idle time |
| `memcpy`/`memset` with `rep movsb`/`stosb` | the byte loops had shown up in the profile |

Per-thread pixel rate in the benchmark went from 3.7 to 6.7 million pixels/s. The mission went from under 1 to about 4–5 frames/s.

What limits it now:
- **The GPU thread:** about 12 of every 15 s are spent drawing.
- **Flips:** the rest is waits for flip NOPs, which the game's routine completes at vertical blank.
- **The game itself:** its main thread spends most of its time in D3D's "push buffer full" wait (`0x371DFB`), waiting for the GPU.

New diagnostics:
- `[render] busy ms per thread`
- `vertex batches`
- `idle ms` (with work / nothing to do / NOP wait)
- `Ke waits` by caller
- the game-main-thread profile

## Milestone 79 (done): drawing with the PC's GPU; missions at about 27 frames/s

The viewer (`bfview.exe` + `bfgl.c`) now draws with OpenGL on the PC's graphics card (tested on a GeForce RTX 2080 SUPER). The software renderer stays as the fallback: it is used when the viewer reports no usable OpenGL, or when `loader\software.txt` exists.

**Protocol (`bfgl.h`):**
- The Xbox memory section is named `BruteForcePCLoader_Mem`, so the viewer maps it and reads textures straight from it.
- A 32 MB ring, `BruteForcePCLoader_Ring`, carries:
  - `CMD_STATE`: the 3D object's 0x800 registers, sent when they change
  - `CMD_DRAW`: an NV2A primitive and its vertices after the vertex program, 29 floats each
  - `CMD_CLEAR`
  - `CMD_FLIP`: the CRTC address, at every vertical blank
  - `CMD_FENCE`
- In GPU mode a semaphore release is sent as a fence. D3D's fence word is written only when the viewer has drawn up to it.

**Viewer drawing:**
- **Surfaces:** each Xbox surface (colour address and size) is a frame-buffer object with a D24S8 depth buffer.
- **Textures:** decoded from Xbox memory into RGBA8 and cached. Each one is re-checked once a frame against a 64-word fingerprint. A texture whose address is a surface already drawn on the GPU uses that surface's frame buffer.
- **Vertices:** window coordinates go back to clip space (`gl_Position = (ndc·w, w)`), so OpenGL clips triangles at the eye plane. The software path drops those.
- **Register combiners:** one uber fragment shader runs all 8 stages and the final combiner from uniforms. It also does the texture-stage modes 2D/projective/pass-through and the alpha test.
- **Render state:** NV2A uses OpenGL's enum values for blend, depth, stencil, cull and primitive numbers (+1), so most state passes straight through. Front-face winding is inverted, because y is mirrored.
- **Present:** the surface the CRTC shows is blitted upside down to the window. Every 5 s the viewer writes `frame.bmp`.

**The NOP software interrupts, done properly.** Direct3D's interrupt routine only masks the interrupt and queues a DPC, and the DPC services PGRAPH only while the PFIFO reports work in progress: CACHE1 status `0x3214` bit 4 clear and `0x3220` bit 12 set. It then loops until CACHE1 reads empty. The loader now:
- traps the `0x3000` register page, so those two registers follow the pending NOP
- runs the DPCs at once after raising the interrupt

The NOP wait was half the GPU thread's time; it is now 0.

**Where it stands:** the tutorial mission runs at about 27 frames/s. The GPU thread is busy about 14 of every 15 s, mostly running vertex programs on the CPU (in parallel, about 10 s).

## Milestone 80 (done): sound and movies

**Sound (`loader\dsound.c`): high-level emulation of DirectSound.**
- **Patched functions:** the game's 49 DirectSound wrapper functions (`IDirectSoundBuffer_Play` and so on), plus five more:
  - Bink's `IDirectSound_CreateSoundBuffer`, `IDirectSoundBuffer_Lock`, `SetCurrentPosition` and `SetHeadroom`
  - `IDirectSound_CommitDeferredSettings`

  All of them jump to the loader. The original library talked to the APU hardware.
- **Buffers and streams** become voices in a software mixer:
  - PCM 8/16-bit and Xbox ADPCM
  - volume (hundredths of a dB) and pitch/frequency
  - play regions, looping and pause
  - simple 3D: distance falloff and left/right pan from the listener's position and orientation
- **Streams:** the game gets an XMediaObject whose `Process` queues packets. When a packet has been played, its status and size are written and its event is set. Completion callbacks run on the ticker thread, which has the Xbox thread block game code needs.
- **Speech** (wave banks): `XFileCreateMediaObjectAsync` is replaced by a synchronous file media object with `Seek`, `GetLength` and `DoWork`.
- **Playback:** the mixer keeps about 100 ms of 48 kHz stereo in the shared ring `BruteForcePCLoader_Audio`. `bfview.exe` plays it with `waveOut`. Inside the loader process, Windows' audio stack failed: it does not accept the XBE in place of the main image.
- **Two bugs found on the way:**
  - The Xbox `DSCAPS` is 16 bytes; writing 24 overwrote the game's stack.
  - Holding the voice lock while mixing slowed every DirectSound call of the game; mixing is now lock-free.
- **Testing:** `nosound.txt` turns sound off. The `[sound]` status line counts buffers, plays, streams and packets, and shows the mixed output's peak level.

**Movies.** With sound present the game now plays its Bink movies:
- the MGS and Digital Anvil logos
- the intro montage
- the menu background
- the campaign and character intros
- the mission briefing

Bink writes its frames into the frame buffer with the CPU. When the shown surface has not been drawn on the GPU for 200 ms, the viewer shows it from Xbox memory instead. Bink's YUV 4:2:2 texture formats (`0x24`/`0x25`) are decoded too.

**Where it stands:** from start to the tutorial mission the game now plays its movies, menus with sound, and the mission (about 20 frames/s with sound running). The WMA soundtrack (`music\default.wma`) is not decoded: there is no music yet.

## Milestone 81 (done): vertex programs on the GPU; the mission runs at full speed

**Vertex programs on the GPU.** The viewer translates each NV2A vertex program into GLSL and caches it (`gen_vsh`/`vsh_program` in `bfgl.c`):
- all MAC and ILU ops, swizzles, negation, write masks and `a0`-relative constants
- the window-to-clip-space epilogue

The loader no longer transforms vertices. It sends:
- the program (`CMD_VSH`) and only the constants that changed (`CMD_CONST`)
- per draw, the index list and the values of attributes without arrays (`CMD_DRAW_IDX`), or raw attribute values for immediate-mode draws (`CMD_DRAW_IMM`)

The viewer reads the vertex arrays straight from the shared Xbox memory:
- **Formats:** float, short (normalized or not), unsigned byte and D3D colours (`GL_BGRA`) are passed to OpenGL as they are. Only the packed 11:11:10 normals are converted.
- **Uploads:** attributes that share a memory range are uploaded once. Each range is cached in a GL buffer and re-uploaded only when a 128-word fingerprint changes.

Fixed-function mode and programs that write constants keep the CPU path. `cpuvertex.txt` forces the CPU path for all draws.

**Fewer stalls:**
- **State caching:** the viewer re-applies each group of state (surface, raster state, textures, the program's uniforms) only when the registers it depends on change (hashes). Constants are uploaded only when changed.
- **Vertical blank on time:** it is now also run from inside long push-buffer passes, so it is not delayed while the loader is busy.
- **Presenting:** the viewer presents only when the game has flipped, with vsync off (`wglSwapIntervalEXT(0)`). A blocking `SwapBuffers` was holding up the command stream.

**Measured in the tutorial mission:** the game flips about 50–90 times a second (it was 27 with vertex programs on the CPU). The viewer presents up to the 60 Hz vertical-blank rate.

## Milestone 82 (done): fog; a 9.5-minute run without faults

**Fog** is drawn in the viewer's fragment shader (`bfgl.c`):
- the enable flag `0x2A4`, the mode `0x29C` (linear, exp, exp2 and their absolute-value variants) and the three parameters at `0x9C0`
- the fog coordinate comes from the vertex program's `oFog` output (`o5.x`), or from word 12 of each CPU-transformed vertex
- the result goes into the combiners' fog register alpha (`R3.a`), which the final combiner mixes in

The distant hills in the tutorial mission now fade into the pink haze (`loader_ingame_fog.png`).

**Stability:** the tutorial mission ran 9.5 minutes (with `auto_press.txt`) until it was stopped: 15,861 frames presented, 39 million draws, no exceptions other than the expected trapped register accesses.

## Milestone 83 (done): music

The game's soundtrack is one WMA file, `music\default.wma`:
- The loader passes its path on the viewer's command line.
- The viewer plays it in a loop through MCI (`mciSendStringA`, the `mpegvideo` device, so Windows decodes the WMA), at volume 350 of 1000.
- The viewer writes the MCI status to `loader\music_status.txt` ("playing" when it works).
- `nomusic.txt` (or `nosound.txt`) turns the music off.

**Correction (Milestone 84):** `default.wma` is not the game's music:
- Nothing in the XBE or the game data refers to it (it sits beside `cdxmedia\ambient\ambient.wma`, which looks like disc-menu material).
- The real soundtrack is in the level sound banks (`data\sounds\<level>.xwb`, `SRT_LONG_STREAMING`, see the sound section above). The game streams it with `XFileCreateMediaObjectAsync` + `DirectSoundCreateStream`, which `dsound.c` has handled since Milestone 80. So it already starts, stops and sets its volume as the game decides.
- Measured: 1 stream in the menus (`common.xwb`), 13 in the tutorial mission (`tutorial.xwb`: music and ambience), with steady output levels.
- The MCI loop played on top of that, so it is now opt-in: create `loader\wmamusic.txt` to hear `default.wma`.

## Milestone 85 (done): the game's clock; movies at the right speed

The intro movies and the splash screen ran several times too fast, picture and sound alike. The cause was the clock:
- XAPI's `QueryPerformanceCounter` (`0x2F2986`) is a bare `rdtsc`.
- `QueryPerformanceFrequency` (`0x2F2997`) returns 733,333,333 Hz, the Xbox CPU's speed.
- Bink has its own `rdtsc; ret` timer (`0x377780`).
- On the PC the time-stamp counter runs at several GHz, so game time ran about 5x fast.

Both `rdtsc` entry points now jump to the loader (`input.c`), which returns Windows' performance counter scaled to 733 MHz. The Bink timer keeps `ecx` as `rdtsc` does.

**Sound in the movies:**
- Bink plays four speaker tracks (front stereo 44.1 kHz, plus mono and stereo tracks at 11 or 22 kHz), all at full volume. Summed into stereo they clipped hard.
- The mixer now has a soft limiter above 0.75 instead of a hard clip.
- The `[sound]` log line also shows game flips per 3 s and, for each locked buffer, its format, position and Lock/GetCurrentPosition/Play counts.

Measured: the buffers' play positions advance at exactly their sample rate. The viewer presents the movies at about 40 frames/s. The user confirmed that the intro now plays at the right speed.

## Milestone 86 (done): smooth sound and picture (no more stutter)

After Milestone 85 the movies, splash and menus stuttered, picture and sound alike.

**The cause: PC controller checks froze the loader.**
- With no controller connected, the loader's poll thread asked XInput every 2 s whether one had appeared. Inside the loader, `xinput9_1_0.dll` was the one that loaded, and Windows' compatibility-shim engine (`AcLayers`) was present.
- During each check, every other thread of the loader stayed suspended for about 1.5 s. A probe in the viewer found every loader thread suspended except the poll thread.
- The game, the GPU emulation and the sound mixer all stopped: 1.5 s of silence and frozen frames every 3–4 s.
- **Fix:** the controller is now polled by `bfview.exe`, a normal Windows program. The state goes to the loader in the ring (`pad[30]` connected, `pad[31..33]` the XINPUT_GAMEPAD), and rumble comes back in `pad[34]`. The loader no longer loads XInput. `noxinput.txt` (now read by the viewer) still turns the controller off.

**What the window shows (`gpu_display_addr`, `gpu.c`):**
- The game's display-address function (`0x3782D0`) writes the CRTC start only when device field +0x1B8 is clear. That field comes from the game's presentation parameters (offset 0x48). Otherwise the GPU flips, and the CRTC register stays stale (0 at times: a black window). Patching the check out made the menus flicker, because the window then showed the buffer being drawn.
- **GPU-drawn screens:** at each flip (method `0x130`), the window shows the colour surface just drawn.
- **CPU-written screens (Bink movies, no draws between flips):** of the candidate buffers (CRTC, the last two flip surfaces, the AvSetDisplayMode frame buffer), the window shows the one whose memory changed since the last vblank.
- `AvSetDisplayMode` now also sets the CRTC start, as the hardware does. The viewer shows black while there is no address yet, instead of the striped memory at address 0.

**Timing:**
- The vblank is timed with the performance counter on a steady 16,667 µs schedule. `GetTickCount` steps 15.6 ms, so "16 ms passed" came after 15.6 or 31.2 ms.
- The mixer waits on an event that the viewer signals when it takes sound. The mixer and the viewer's audio thread run at time-critical priority, and the loader at high priority class (`normalpriority.txt` turns that off).
- The sampling profiler, which suspends two threads 1000 times a second, is opt-in (`profile.txt`).

**Optional switches added along the way (all off by default):**

| File | Effect |
| --- | --- |
| `flipstall.txt` | FLIP_STALL waits for the next vblank, as on the Xbox. The game loop then ran at ~42 Hz, because the flip handshake is not fully emulated. |
| `vsync.txt` | The vblank follows the monitor's refreshes (`DwmFlush` in the viewer). |
| `canary.txt` | Two clock-watching threads in the loader, plus a viewer probe that writes `freeze_log.txt`: freezes of the loader, with every thread's suspend count, eip and start address. |

**Diagnostics in the log:**
- `[sound]`: mixed blocks per 3 s (150 = 48 kHz) and the longest mixer wait.
- `[present]`: vblanks, flips sent, memory/FBO presents and sound underrun frames (as seen by the viewer).
- `[cadence]`: every 20 s, 240 vblanks: the shown buffer, and whether it changed.

**Measured:** zero sound underruns, mixer waits of at most 6 ms, no freezes. The user confirmed smooth intro, splash, menus and mission.

## Milestone 87 (done): mouse controls, pixel-count reports (no false lens glare), screenshots, reboots

**Mouse:**
- A click in the window captures the mouse (hidden, kept inside the window); F10 or leaving the window releases it. The capturing click doesn't fire.
- `bfview.exe` sends the total movement in the shared `VIEW` block (`mdx`, `mdy`, `mcap`).
- The loader turns mouse speed (measured over ≥ 15 ms) into right-stick aim. Any movement starts past the game's dead zone (7000).
- Left button = right trigger (fire), right button = left trigger (grenade). The game's tutorial: "Pull Left trigger to throw a grenade".
- Sensitivity: `loader\mouse_sens.txt` holds a number (default 50; higher = faster).

**Pixel-count reports (the lens glare):**
- The game calls `CLEAR_REPORT_VALUE` (0x17C8), `SET_ZPASS_PIXEL_COUNT_ENABLE` (0x17CC) and `GET_REPORT` (0x17D0, type 1) about 14,000 times a second, to measure how much of the sun, lights and glowing plants is visible. The results scale its glare and glow.
- These methods were ignored, so the game read stale memory: glare where it didn't belong, and brightness changing with the view direction.
- The report DMA object (handle 0xC, found through RAMHT) starts at physical 0, so the report offset is a physical address.
- The loader passes the methods to the viewer in order with the draws (`CMD_ZPASS`). The viewer counts with a `GL_SAMPLES_PASSED` query and writes the 16-byte NV2A report (timestamp, count, 0) into the shared Xbox memory, which it now maps writable.
- **Cost:** each report waits for the graphics card (`QUERY_RESULT`), so the mission runs at 45–90 frames/s instead of ~85. Making this asynchronous is a next step.

**Other changes:**
- **Gamma ramp:** `D3D_WriteGammaRamp` (0x37834E, ramp in `edx`) is hooked and the ramp kept (`g_gamma`). The game's default ramp is the identity, so it isn't applied yet.
- **Screenshots:** F12 in the window saves the next presented picture as `loader\shotN.bmp`.
- **Reboots:** `HalReturnToFirmware(1 or 2)` (the game's reset button combination, for example) writes `restart_loader.txt`, and `run_loader.bat` starts the game again. The log shows the pad state at that moment.

**Aim assist (not removed yet):**
- The game turns a character toward a target direction (`Character_Update` 0x11D6FC: the angle to the target, capped by the turn rate, into turn input +0x5F8). `Character_UpdateTurning` applies it as turn speed (+0x7EC) × input × dt.
- The stick moves that direction, and lock-on (the "automatic tracking ring") evidently pins it to the enemy. That's why mouse aiming feels slow after a lock.
- `CharacterType_HandlePlayerInput` (0x1332A0) also has a turbo ramp: at full stick, turning speeds up to 2.5× over a second (table 1 + k × 1.5, k = 0, 0.05, 0.1, 0.25, 0.58, 1).
- The player character is the `ecx` of the call to `0x11E570` from inside that function. `0x11E840` adds to the aim pitch.
- Lock-on names in the XBE: `CT_TARGET_LOCK_TOGGLE` (command 0x12), `EVT_LOCKED_ON` (0x19), `EVT_LOCKING_ON` (0x1A). Their handling hasn't been found yet.

## Milestone 88 (done): PC controls: direct mouse look, Division-style camera, sprint, no aim assist

**Direct mouse look (`input.c`, `mouse_apply`):**
- Through the right stick, the game caps turning at the character's turn rate (type +0x2D0, 1.57 rad/s), so fast mouse movements were cut short.
- Every frame, from a hook on `0x11E570` (called for the player by `CharacterType_HandlePlayerInput`, which also identifies the player character), the pixels moved become the turn input (+0x5F8 = angle / (turn speed +0x7EC × dt); `Character_UpdateTurning` applies speed × input × dt) and the pitch (through `Character_SetAimPitch` 0x11E7C0).
- 0.003 rad per pixel at the default sensitivity (`mouse_sens.txt`, 50). At most 15 rad/s; faster swipes carry over to the next frames.
- Directions were calibrated with `mousetest.txt` (stick vs simulated mouse; the yaw sign is −1).

**Aim assist off while the mouse aims (`aimassist.txt` keeps it):**
- **Lock-on:** `0xC4E70` / `0xC4EC0` link the lock target (weak reference +0x634/+0x638; while set, stick turning is ignored at 0x11E5F4 and `Character_Update` turns toward the target at 0x11D572). For the player they run with target 0.
- **Friction and magnetism:** in the input handler at 0x13389D, the stick is scaled by 0.75 while +0x7DC > 0, and the target under the reticle (+0x5E4) pulls the aim. Both are skipped.

**Buttons:**

| Input | Action |
| --- | --- |
| Left click | fire |
| Middle click / Q | grenade (the game's "Pull Left trigger to throw a grenade") |
| Right click (hold) | aim |
| Left Shift | sprint |
| F10 | release the mouse |
| F12 | screenshot (`shotN.bmp`) |

**Division-style camera (default; `gamecamera.txt` keeps the game's own):**
- **Rotation:** after `FollowCamera_Update` (vtable 0x3A2640 + 0x6C, wrapped), the camera is built from the player. Its rotation is the character's aim matrix (+0x5BC) transposed: one shared yaw and pitch, no lag, at any frame rate.
- **Position:** pivot = character position (vfunc +0x40, +0x24) + height + shoulder offset; eye = pivot + back × distance. Only the position eases between states:

  | State | Distance | Height | Shoulder |
  | --- | --- | --- | --- |
  | Normal | 3.6 m | 1.0 m | 0.5 m |
  | Aiming | 1.6 m | 1.15 m | 0.65 m |
  | Sprinting | 4.2 m | 1.15 m | 0.45 m |

- **Where it is written:**
  - The camera node (camera +0x48, vfunc +0x14), a 3×4 view with rows right, up, back. The view, the effects, the muzzle flash and the bullets all read it.
  - The game camera's world transform at [character +0x6B8] + 0x214 (the view rotation transposed, eye at +0x238). The game prepares what is visible from that; while it still trailed, fast turns showed areas it hadn't prepared, as textures "vanishing".
  - The game's own follow update is skipped for the player.
- **Wrong turns, for the record:**
  - Shifting the final view matrix (0x4A5858) moved only the drawing, so the muzzle flash came out to the side.
  - Shifting the multiply's output (0x2338C0 with the axis-flip matrix 0x46D034) moved a second camera too.
  - The view global stores matrices transposed (translation in element 3 of rows 0–2).
- **Eye-position constant:** shader constant c31 (and sometimes c24) carries the eye. Constants equal to the follow camera's eye are swapped for ours (`render.c`).
- **Not yet:** camera collision with walls.

**Sprint (Left Shift while moving, not aiming):**
- The velocity update `0x12C6C0` (velocity at +0x470/+0x474/+0x478) eases toward a desired horizontal velocity (`[ebp+0x14]`). For the sprinting player that velocity is scaled by 1.45 (run ≈ 8.5 m/s → ≈ 12).
- Found with the `speedtest.txt` probe: measured ground speed against the character's fields.
- The run animation is unchanged.

**Rendering fixes found on the way:**
- **Pixel counts were made never 0 (minimum 1).** The game also occlusion-culls objects with them, using the previous frame's results, so objects coming into view popped in late. This was reverted in Milestone 89: the sun's glare showed through mountains. Exact counts are the default again; `minreports.txt` brings the minimum back.
- **Vertex cache checks whole ranges.** It hashes each whole range once per game frame instead of a 128-word sample.
- **Stale render surfaces aren't used as textures.** A surface last drawn more than 100 ms ago no longer stands in for a texture at the same address.
- **Vsync on when presenting.** `novsyncpresent.txt` turns it off.

**Diagnosis tools (all switched by files in `loader\`):**

| File | What it does |
| --- | --- |
| `mousetest.txt` | turn and pitch calibration |
| `camlag.txt` | follow-camera lag |
| `camxflog.txt` | camera transform log |
| `eyeprobe.txt` | which shader constants hold a camera position |
| `statefind.txt` | memory search for the camera's state |
| `blockdiff.txt` | changing fields in the camera state block |
| `cullfind.txt` | per-frame order of camera readers |
| `camyaw.txt` | camera turned 30°, to test culling |
| `camposgame.txt` / `camrotgame.txt` | A/B tests: game position or game rotation |
| `speedtest.txt` | speed probe |

## Milestone 89 (done): lighting: shadow map, glow, texture fixes
The character's shadow, his lighting and the haze toward the sun now look right (checked by the user).

**Texture and surface fixes (after Milestone 88):**
- **Border colour:** NV2A `SET_TEXTURE_BORDER_COLOR` (0x1B24 + i×0x40) is applied. OpenGL's default black border made the shadow projector darken a large wedge around the character.
- **Shared depth buffers:** depth buffers are kept by address (`DEP`, `dep_get`), so surfaces that share Xbox depth memory share one GL depth-stencil texture.
- **Cube maps:** texture mode 3 samples a `samplerCube` (units 4–7). Faces are each mip chain padded to 128 bytes.
- **Pixel counts exact again by default:** the minimum-1 counts let the sun's glare show through mountains. `minreports.txt` brings the minimum back.
- **Watchdog:** its stack reads are guarded with `VirtualQuery` (it used to crash).

**Shadow map (texture mode 2 on a depth format):**
- Stage 1 uses PROJECT3D with a 512×512 `LIN_DEPTH_X8_Y24` texture (format 0x2E): the character's shadow map. It used to be sampled as a colour texture, which caused the black shadow under the character and his dark lighting.
- It's now a `sampler2DShadow` on the GL depth texture of the depth buffer at that address, using r/q scaled by 2^24−1 (or 65535 for 16-bit formats).
- The compare function comes from `SET_SHADOW_COMPARE_FUNC` (0x1E6C; the game uses GEQUAL).
- **NV2A compares stored depth OP r, the reverse of OpenGL,** so the direction is swapped (LESS↔GREATER, LEQUAL↔GEQUAL). With OpenGL's order, the whole projected area went dark. A result of 1 means lit.
- The game sets no polygon offset, so the compare is biased toward lit by 0.003. Without it, the character showed curved self-shadow lines. `shadowbias.txt` sets the bias (value ×1e-5).
- Polygon offset (0x320 enable, 0x384 factor, 0x388 units) is applied anyway.
- Switches:
  - `noshadowmap.txt` turns the shadow map off.
  - `shadowmode.txt` is read every second, so it can be changed while the game runs:

    | Value | Mode |
    | --- | --- |
    | 0 | normal |
    | 1 | bias sign flipped |
    | 2 | result 1 |
    | 3 | result 0 |
    | 4 | normal; beyond far or behind the projector counts as 0 |
    | 5 | same as 4 with the bias sign flipped |

**Glow (the haze toward the sun):**
- **The chain:**
  1. The 640×480 scene is copied to 320×240 through a 5-stage threshold combiner.
  2. That is added twice into 160×120 (blending ONE, ONE).
  3. The 160×120 image is added back onto the scene.
- **The threshold:**
  - Texture stage 1 of the copy is the scene's **depth-stencil buffer read as `LIN_R8G8B8A8`** (format 0x41, at the address just below the two scene colour buffers). Its alpha is the stencil value, the per-object glow threshold.
  - Stage 0 computes luminance (factor 0x4C961D) into R1.
  - Stage 1 computes R0.a = T1.a − lum + 0.5.
  - Stage 2 muxes on R0.a: a pixel glows only if its luminance is above its stencil threshold.
- **The bug:** OpenGL keeps depth and stencil on the GPU, so that memory read 0 and nearly everything glowed.
- **The fix:** `dep_color` copies the stencil (`DEPTH_STENCIL_TEXTURE_MODE` = `STENCIL_INDEX`, `usampler2D`) into the alpha of a colour texture, once per frame per buffer, before the draw's state is set. That texture is used for colour reads of a depth buffer. `nodepthcolor.txt` turns it off.
- The 160×120 and 320×240 glow surfaces are never cleared by the game. Clearing them each frame (`glowmode.txt` = 1) made no difference.

**Diagnosis switches added:**

| File | Writes or does |
| --- | --- |
| `vol3log.txt` | `vol3_log.txt` (mode-2 textures) |
| `cubelog.txt` | `cube_log.txt` |
| `modelog.txt` | `mode_log.txt` |
| `rttlog.txt` | `rtt_log.txt` |
| `clearlog.txt` | `clear_log.txt` |
| `glowlog.txt` | `glow_log.txt`: full combiner registers and textures of draws that read rendered surfaces; checked every second |
| `nortt.txt` | skips draws that read rendered surfaces |
| `lumaprobe.txt` | `luma_log.txt` |

`shadow_log.txt` is always written: the compare function, the offset registers and the format.

## Milestone 90 (done): shots go where the reticle points; hip fire and aiming
Checked by the user: shots land on the reticle, and the reticle is large from the hip and shrinks when aiming.

**Why shots went low:**
- The player's gun fires along the barrel of the animated gun: hardpoint position +0x22C and direction +0x25C, computed in 0x22E780.
- The reticle is a fixed HUD quad (64×64, centred at 320, 192 of 640×480).
- With the over-the-shoulder camera, the two don't meet.

**Fire paths:**
- `Weapon_FireBullet` (0x22F1D0) switches on the bullet type (archetype +0x40; jump table 0x22F288).
- The player's gun is type 4. It goes to the hitscan path 0x2317E0, which calls the ray function 0x230860(weapon, hardpoint, &pos, &dir, &range, 1) at 0x231868.
- Projectile weapons go to `Weapon_SpawnProjectile` 0x22BAC0 (muzzle and direction from 0x232200, hooked at 0x22BC64).
- The ray function uses the world raycast 0xDDD10.
- **Ownership:** the shooter is [[[weapon +0x1B0] +8] +0x1D4], the way `Weapon_SpawnProjectile` finds it.
  - An earlier test (weapon +8 == character +8) was wrong: +8 is shared by every character.
  - With that test, the AI's shots were re-aimed at the player's reticle and often hit the ground. Fixed in Milestone 92.

**The fix (`aim_fix`, input.c):**
- **Depth:** the viewer reads the depth under the reticle each frame (pixel 320, 190) through two pixel-pack buffers, one frame late with no stall. It sends the value in ring pad[50], with an update counter in pad[51].
- **Unproject:** the loader inverts the view×projection matrix in vertex constants slots 10–13. It finds them by projecting the camera's forward axis to the screen centre, keeps the last good matrix, and accepts the known rows while the camera turns. That gives the world point under the reticle.
- **Aim:** the shot is aimed from the muzzle at that point, so it hits the reticle at any distance.
- **Spread:** a random offset in a disk on screen, 18 px from the hip and 1.2 px when aiming, eased with `g_aim_t`.
- `noaimfix.txt` turns the fix off (checked every second).

**Reticle size:** the loader sends a scale in pad[54]: 1.25 from the hip, easing to 0.8 when aiming. The viewer scales any square quad centred on the reticle by it in `do_draw_imm`; the game changes the reticle's size and texture after a kill.

**Moving while aiming:** the desired velocity is scaled by 0.6, and aiming cancels sprint.

**Probes:**
- `hudlog.txt` (`hud_log.txt`): small draws near the screen centre, from the immediate and array paths. Checked every second.
- `[fire]`, `[aimdiag]` and `[aim]` lines in the log: the first shots.
- **Disassembly:** in `bf.dis` (llvm-objdump of the flat image), address = VA − 0x10000.

**Seen but not fixed:** once, after a respawn, the world and the character were missing for a moment. Only the sky, the HUD and a white box at the top left were drawn, and then it recovered.

## Milestone 91 (done): taking over a squad member after a death
Checked by the user: no crash, the world stays visible, and the new squad member gets the over-the-shoulder camera and aiming.

**The cause:**
- Since Milestone 88 the camera wrote the game camera's world transform at "[character +0x6B8] +0x214" (eye at +0x238).
- +0x6B8 is not a camera pointer. It's one of four 12-byte containers (+0x6AC, +0x6B8, +0x6C4, +0x6D0) copied from the character type at 0x12F185. Its buffer only happened to lie just before the camera block for the first character.
- When the player dies, the game hands control to another squad member. For that character the address held other data:
  - writing floats there crashed the game (0x2C2720 read a float as a pointer)
  - or hid the world (the blue screen and the white untextured box seen after respawns)

**The fix:**
- The block is reached through the camera instead: `cam_block`, [[camera +0x50] +0x5C], rotation at +0x14, eye at +0x38. This was found by a pointer search from the camera object toward the address that worked.
- It is written only when it already holds a rotation (`blk_has_rot`: unit rows, perpendicular).
- All of the loader's own probing reads are checked with `VirtualQuery` (`readable`). One unchecked read crashed the game once, in `cam_skip`.
- For 1.5 s after the camera's target changes, the game's own follow update runs, so it can pull back from the character. My camera is still drawn.
- If no valid block is found, that character keeps the game's camera and the game's aiming (`g_ourcam`).

**Not caused by the loader:** the new squad member is invisible for a few seconds after the takeover. It's the same with the game's own camera (`gamecamera.txt`, checked by the user).

**Probes:**
- `[respawn] player` lines.
- `[cam] no camera block ...`, which lists the character and camera fields that point to blocks holding a rotation.
- `[camblk]` (disabled; `done = 2`), the pointer search.

## Milestone 92 (done): health bar and the AI's aim with the PC camera
Checked by the user.

**Health bar:**
- With my camera the health bar didn't drop. My camera skipped the game's follow-camera update (0x702A0), which also drives other per-frame work.
- The update now always runs. `cam_after` overwrites the camera node and the game camera's world transform right after it.
- Textures still don't vanish on fast turns.
- `camskip2.txt` restores the skip (checked every second).

**AI shots into the ground:**
- The player-shot test matched every character, so `aim_fix` re-aimed enemy and squad shots at the point under the player's reticle.
- The shooter is now [[[weapon +0x1B0] +8] +0x1D4].

**Ruled out on the way:** writing the game camera's world transform. `noblockwrite.txt` (checked every second) turns that write off.

## Milestone 93 (in progress): weapon poses, findings so far
**Motion ids:** the enum table is at 0x3BFDAC, 12-byte entries {length, string, ...}. MT_DASH = 8, MT_RUN = 34, MT_IDLE = 19, MT_WALK = 47. MT_BRING_TO_FACE and MT_REMOVE_FROM_FACE are not in this enum.

**Requesting a motion:** `CharacterType_RequestMotion` (0x12EAB0) takes:
- eax: the id
- ecx: the type, [character +0xC]
- esi: the character
- one stack argument: 0

It looks the id up in the type's records (+0x2B0 count, +0x2B4 array, 0x584 bytes each).

**Sprint pose, two tries:**
- **Switching the motion.** The player's run already is MT_DASH (RUN is refused), so there is nothing to switch to.
  - Requesting a motion every frame restarts the animation: the character freezes in its first frame and slides.
  - The switch (`pose_update`, `nodash.txt`) is disabled.
- **Raising the aim pitch.** Tilting +0x5F4 to 55° while sprinting does nothing, because the run animation ignores the aim pitch. This is opt-in with `gunup.txt`.
  - The camera now uses its own view pitch (`g_view_pitch`, from the mouse) and the character's yaw. That keeps the view steady if the aim pitch is changed.

**Bone posing (done; the sprint pose was checked by the user):**
- **Skin palette builder** 0x95460(bones, index table, count): found by hooking `D3DDevice_SetVertexShaderConstant` (0x36F6C0; fastcall: ecx = register, edx = data, stack = dwords) and looking at who uploads 22–47 matrices at register 48.
- **Bone records** are 72 bytes:
  - +4: the bone's part object (joint world position at part +0x38)
  - +8: the skin matrix, 3×4 world-space rows R | t
  - bone index = part index
- **The player's skeleton** is archetype `#f18f3616` (90 parts):
  - 26/27 clavicles, 28/29 upper arms, 51/52 forearms, 56/57 hands, 54/55 and 58/59 arm twist helpers, 60–89 fingers
  - L/R as named in the data; the gun hand is "R"
  - also: 8/9 calves, 13/14 feet, 17 neck, 25 head
- **Character fields** point to some parts:
  - +0x3E8 head
  - +0x3F8 / +0x3FC hands (56/57)
  - +0x400 / +0x404 clavicles
- **Bone names:** `tools\char2gltf.py`'s part order (see the scratch script `skel.py` approach: `<PART>` order in `objects-common.xml`).
- **Changing parts doesn't move the mesh:** editing the part objects' matrices has no effect; only the bone records do (`pose_apply`, called from the palette hook and, earlier in the frame, from `cam_after`). Each bone is changed once per frame: `pose_last` remembers the last output.
- **The rifle** is a separate object.
  - Its main part is weapon +0x6C (rotation +0x14, position +0x38). Writing that before the frame is drawn moves it.
  - The player's weapons are recorded in `Weapon_Update` (0x22EE30).
  - **The one in his hand** is chosen while the pose is off: the weapon whose part is much nearer the right wrist (bone 57) than the neck (17) (`held_choose`). The gun on his back is the other way round. The choice is locked during the pose.
    - "Last fired" or "nearest to the hand" were both unreliable: the two guns are about equally far, so the back gun floated off.
  - **The barrel** is the rifle part's local Z axis. Its sign points from the right palm toward the part's origin, which lies on the barrel side of the grip.
    - Signing it "forward" flipped during the run cycle, and the gun popped above and below.
    - weapon +0x25C is computed from the already-posed part, which feeds back; don't use it.
  - **After the pose:** when it ends, the part is restored from its pre-pose matrix if it still holds our output. Otherwise an upright copy stayed behind.
- **Sprint pose** (`pose_gun_arm`, `g_pose_t` eased with the sprint, off in cutscenes):
  - **Gun arm:** the elbow is placed so the forearm is vertical, with the hand 15 cm above the shoulder and in front of it (the sprint reference). The rifle moves rigidly with the hand, then turns about the palm so the barrel points straight up and its underside faces forward.
  - **Free arm:**
    - bent 60° at the elbow; it swings from the shoulder with the right leg's phase (knee − hip along forward); sway 100°, rest −70°, hand twist 60°
    - on the backswing the forearm straightens by up to 60° and the arm swings 15° out from the side, so it clears the torso
    - the swing is clamped to −100°..−20° and smoothed over time (60 ms; the pose runs twice per frame, so per-call smoothing barely damped)
  - **Body axis:** the root bone's (Bip01) X column, lightly smoothed, so the arms stay with the torso while turning.
    - The aim's axes led the model, so the arms lagged.
    - The shoulder line twisted with every stride.
    - If the root axis is more than 45° from the aim's right axis, the aim's is used. Otherwise the arm flung out sideways during quick turns.
- **Live tuning files:**
  - `sprintpose.txt`: hand height in mm, elbow inward offset in mm, wrist twist in degrees
  - `leftpose.txt`: bend, sway, hand twist, rest
  - `nosprintpose.txt` turns the sprint pose off.
- **Gun-arm elbow:** 10 cm out from the shoulder line (user choice).
- **Mouse turning while sprinting:** 50% (`sprintsens.txt`, a percentage, checked every second). The 15 rad/s turn-rate cap is scaled too; otherwise fast flicks hit the same cap and felt unchanged.
- **Weapon switch while sprinting:** a switch press (Y / R) or the held gun leaving the right hand (more than 55 cm) fades only the gun arm out (`g_gunarm_t`) for 1.5 s.
  - The game's reach animation plays while the left arm keeps swinging.
  - With the gun arm unposed, `held_choose` picks up the new weapon; otherwise the pose kept tracking the old gun and never came back.
- **Shooting stops the sprint**, which stays off for 0.5 s after the last shot. The trigger is held back until the pose has eased out (both hands on the gun).
- **Test aids:**
  - `forcepose.txt` holds the sprint pose while standing.
  - `camorbit.txt` (degrees) turns the camera around the character.
  - `posetest.txt` sets fixed arm angles.
  - Probes: `bonedump.txt`, `vsclog.txt`, `gunprobe.txt`, `partprobe.txt`.

**Aim pose: built, then switched off at the user's request** (more trouble than it was worth). `aimpose_on.txt` enables it again. What it does (`pose_aim`):
- **Rifle:** turned so its barrel follows the view direction, with the butt at the right shoulder pocket.
- **Arms:** both arms solved with two-bone IK (`ik_arm`: elbow bending down and outward, hand rotation matched) to the two-handed grip.
- **Grip:** wrists and hand rotations in the rifle's frame, recorded automatically whenever both wrists are near the barrel line (`grip_try`) and saved to `aimgrip.bin`.
- **Torso:** the upper body tilts with the view pitch and is yaw-locked to the aim.

**Open problems when it was switched off:** hands not staying on the gun while moving, and the body lagging. The part objects' joint positions seem to lag a frame behind the skin.
- Computing joints from the skin and the bind positions (`bind_pos_ext`, from `#f18f3616`'s inverse bind matrices) fixed the lag for the aim pose.
- But it broke the sprint pose, which was tuned with the part positions. `part_pos` uses the part objects again.

## Milestone 94 (unfinished): camera collision
**The world raycast is `0xDDD10`.** Both the hitscan and AI use it.
- Call: eax = 0 or a result buffer; nine stack arguments; it pops its own; returns the hit object, or 0.

  | Argument | Meaning |
  | --- | --- |
  | a1 | the world (character +8) |
  | a2 | out: distance from the start |
  | a3 | out: hit point |
  | a4 | out: normal |
  | a5 | start |
  | a6 | unit direction |
  | a7 | ray filter |
  | a8 | range |
  | a9 | 0 |

- **Ray filters:**
  - Line of sight (`0x13DEC0`): vtable 0x39CC20; +0x104 = 0, +0x108 = 1 (byte), +0x10C = −8, +0x110 = −14, +0x114 = 0, +0x118 = the object to ignore.
  - Bullets (`0x230040`): vtable 0x394848, +0x110 = 0, +0x11C = −1, the rest the same.
- **What the tests showed:**
  - A ray down into the ground hits (distance in metres).
  - A level ray starting 0.6 m outside the character hit something 1.5 m behind him.
  - A ray from inside his collision finds nothing.
  - The camera ray (pivot +0.5 m toward the eye, either filter) never hit the walls the camera clipped through.
- **Next ideas:**
  - The walls may be one-sided or in another collision layer.
  - Find how the game's own follow camera avoids walls (0x702A0 and its callees 0x6F780 / 0x6FB00 / 0x70110).
  - Or cast several rays.
- The code is in `cam_build`; `camcollide.txt` turns it on. It's off by default.

**The 30-minute safety stop is now opt-in** (`timelimit.txt`).

## Milestone 95: the Razorback (the user's gun, replacing the MK-ASLT)
The user's files are in `Desktop\brute force files\` (an STL model, a gunsmith picture, two m4a clips).
- **Stats** (`razor_patch` in `input.c`, from `Weapon_Update`): the MK-ASLT's type (weapon +0x228) is found by its 48-round magazine (+0xE4) and
  8 shots/s (+0xCC). It becomes 30 rounds and 625 rpm (+0xCC = 10.42, the second rate +0xD0 scaled alike); clips above 30 (weapon +0x1F8) are cut.
  `norazorback.txt` turns the stats and the model off.
- **Model** (`razor_load` / `razor_draw` in `bfgl.c`): `loader\razorback.stl` (33,460 triangles) is drawn in place of the MK-ASLT mesh
  (954 indices / 399 vertices; its 28 / 12 part is hidden). Longest side → Z (1 m), next → Y, Z flipped by default
  (`razorflip.txt` / `razorrot.txt` override live). Muzzle at −Z, +Y up.
- **Texture:** `loader\razorback_tex.raw` ('RZTX', width, height, RGBA rows top first) is the gunsmith picture
  (`razorback_gunsmith.jpg`) cropped to the gun (x 440–1740, y 328–820 of 2000×1125). UVs are a side projection: u along Z, v down Y.
  It's bound to texture unit 0 for the gun's draw only. `norazortex.txt` = the old flat colour.
- **Name:** "MK-ASLT" → "Razorback" in `xbox_hdd\p5\common\stringtable\string-table-{en,de,es,it}.xmb` (backups `.orig`; French uses another name).
  These tables are plain records: [id u32][LEB128 byte length][UTF-16 text], to the end of the file, with no offset table, so a string can change length.
- **Sounds** (`dsound.c`): the MK-ASLT's shots are three ADPCM regions (22,050 Hz mono) of the weapon bank: 0xBC8B0+0x1A28, 0x82848+0x1680,
  0xFDE60+0x15F0, chosen at random. When one starts, a single shot from `razor_shots.wav` (0.370–0.670 s, faded, peak 0.9) plays on one of 8 extra
  voices with the game voice's volume and 3D position, and the game voice is not played. Reload: when the clip drops by more than 1 or reaches 0,
  `razor_reload.wav` plays and the game's reload sounds (0x12FC0+0xF0C, 0x83F10+0x1680) are muted for 3.5 s. `norazorsound.txt` = the original sounds.
  The WAVs were made from the m4a clips with the WinRT MediaTranscoder (48 kHz stereo 16-bit).
- **Fit in the hands:** drawn at 95%, 11.5 cm toward the stock, 1 cm up (`razorfit.txt` "<scale %> <back mm> <up mm>" overrides live).
  The left hand is held at a fixed spot on the gun by arm IK: captured once in the rifle part's frame (20.5 cm back along the gun from the
  animation's grip; `razorhand.txt` "<back mm> <down mm>"), with joints taken from the skin matrices (the part objects lag a frame while
  turning). The gun used is the MK-ASLT-type weapon nearest the right wrist; `g_held_weapon` flipped between the held gun and the one on
  his back, which made the hand jump ~30 cm between frames (small circles).
- **The body turns with the aim** (all weapons): `Character_Update` (0x11D7F7) turns the body heading +0x434 toward the target +0x4A0
  (set by 0x119FA0 from the aim while moving, left alone standing) by min(|gap| × 0.64, 1) × +0x5FC × dt, then rebuilds the character
  transform from it (and reads +0x434 back from the transform, so writing +0x434 elsewhere is lost). Hooked there: for the player the target
  is π − aim yaw and +0x5FC = 1000. Before, standing turns lagged up to ~45° and the turn-in-place animations swung the arms.
  Writing the transform or +0x434/+0x4A0 from the input hook made him spin or freeze. `bodylag.txt` = the game's own turning.
  The skeleton and guns are also turned on screen by any remaining gap (now about 0).
- **IK bug (fixed):** `ik_arm` normalised the reach direction with the length recomputed inside the loop (after d[0] changed),
  so d was not a unit vector: the hand landed 1–10 cm off and circled while turning with the gun readied. (This also explains the
  Milestone 93 aim pose's hands "not sticking".) `atan2_` is now plain C (the x87 `fpatan` inline asm is unreliable under clang).
  Fit now: gun 95%, 14 cm back, 1.6 cm down; left hand 35.7 cm back, 5.1 cm down.
- **Radar dots (fixed):** A8 textures (formats 0x19 / 0x1F) read as white with alpha on the NV2A; they were decoded as black.
- **Mouse turning** is capped at 60 rad/s (was 15: fast flicks queued up to 3 rad behind the mouse).
- **Sprint left arm:** bend 85°, sway 60° (was 60 / 100).
- **Sprint only forward:** Left Shift sprints only while the move stick points forward (W, W+A, W+D); back and sideways walk.
- **Weapon swaps:** the left-hand IK lets go for 1.5 s after the switch press, or while the Razorback is more than 45 cm from the
  right wrist, and eases back on (in ~0.3 s, out ~0.1 s).
- **Finding a weapon's sounds:** `B_Play` keeps a ring of the last 64 plays (region, format, caller); `input.c` dumps it (`[sndid]` in the log)
  whenever a player weapon's clip changes.

## Milestone 96: key bindings in the pause menu
- **Menu file editing (`tools\xmbedit.py`).** A .xmb (BXML) is a header (names table), then blocks: word count, the number of
  tokens the block holds (two words in version 0x101), then groups of a flags word (bit set = LZ back-reference into a 512-word
  window) and 16 words; the raw value bytes of the block's tokens follow each block. No checksum (0). `load` replays the
  token stream with each token's exact value bytes; `save` writes literal tokens only, **at most ~3,900 words per block** (the
  game crashed at 0x258B98 on a 6,000-word block; its own files never exceed 4,115). Round trips decode identically.
  Repeated elements carry a count token (0x9000 | n) in front of the run: adding an element means raising it.
- **Menus:** event scripts are (event, action, argument), stored as enum + 1: event 2 = select; **action 0 opens a menu by name**,
  action 1 opens a *menu-sequence* (PROFILE OPTIONS etc.). String entries in the string tables are
  `[entry][id][text][end]` after one count token.
- **`tools\kbmenu_patch.py`** (English only so far; originals kept as `*.kb_orig`): a KEY BINDINGS button in the pause menu
  (#fdcd29b4, above QUIT) that opens a new menu #4b42a002, a copy of the Controls help panel with strings #4b42a003 (title)
  and #4b42a004 (body, 1,223 characters).
- **Open menus at runtime:** the menu system is `[[0x469b58]+8]`->vfunc 0x78; its vfunc 0 (0xFEDB0) handles message 0x15 =
  open a menu, appending it to a list (sentinel node at system +8; nodes +0 next, +4 prev, +8 menu object). The top menu is
  the sentinel's prev; a menu object's name hash is at +0x24.
- **The screen (`input.c` kb_screen):** while the top menu is #4b42a002 the game gets no input. Up/Down (W/S) choose, Enter/Space
  waits for a key (Esc cancels), Esc/Backspace closes it (an A press for 150 ms), Left Ctrl + R restores defaults.
  The panel's text control holds no copy of its text (the static text is looked up when drawn), so the list is written
  straight into the loaded string-table entry: found once by scanning every committed region for the UTF-16 starting text,
  padded to the same length, re-found if it no longer holds what was last written.
- **Bindings:** 21 actions (moves, sprint, the face buttons, white/black, triggers, START, BACK, D-pad, stick clicks) in
  `loader\keybinds.txt`. Defaults: Esc = START (Enter is always START too), V = B, the rest as before. The mouse wheel binds
  as keys 0x0E / 0x0F (bfview.exe turns each notch into a 120 ms press).
- **Shotguns** keep the hip-fire spread when aiming: a weapon type that fires several rays in the same millisecond is marked
  multi-pellet.
- `UIText_SetText` (0x10A7D0) takes the control in esi and UTF-16 text in edi (not needed in the end).
- **HUD layout (`bfgl.c` hud_move):** screen-space draws on both 2D paths (`do_draw_imm`, `do_draw`) that lie wholly in the radar's
  area (x 40-205, y 280-452) move by (-45, +38) into the bottom-left corner; the health/energy panel (x 40-310, y 25-99) by
  (-50, -28) into the top-left (its end cap's quad reaches past x 300). Also applied, at half scale, on the 320x240 glow buffer;
  never to full-screen passes. `nohudmove.txt` = the game's layout (read every second).
- **Cutscene depth of field (Rescue Brutus, "and Brutus" went dark), fixed in `bfgl.c`:**
  - The effect draws a full-screen quad that reads the depth buffer as A8R8G8B8 (format 0x12) and blends by its alpha, mixing three
    blur levels (t1-t3) by depth. The depth-as-colour copy (Milestone 89) now holds the real depth too (R = bits 23-16,
    G = 15-8, B = 7-0, A = stencil) and is swizzled per read format: 0x12/0x06/0x07/0x1E see the Z24S8 word as A = depth high,
    R = mid, G = low, B = stencil; 0x41/0x3C (the glow pass) as A = stencil, R = low, G = mid, B = high.
  - The first blur level starts **inside** the 320x240 glow surface (0x021DE380 = its row 34): a linear 32-bit texture whose address
    lies inside a drawn surface now samples that surface with an offset (u_tscale zw). Before, it read Xbox memory = black.
  - Found by A/B runs (software.txt fine, nortt.txt fine, nodepthcolor.txt fine) and the F12 recorder: `dcollog.txt` makes each
    F12 write the last 2048 draws that read a depth buffer as colour (state, all stages, combiners) to `dcol_log.txt`.
  - Live test switches in `abtest.txt` (read every second): 2 no fog, 3 old black A8, 4 keep old surfaces, 5 depth RGB 0,
    6 copy at every read, 7 old channel layout, 8 no sub-surface textures.

- **Widescreen (`input.c` cam_widen):** a camera's projection object holds the view's half-extents at unit distance: +0x24
  horizontal (1.0), +0x28 vertical (0.75), aspect +0x2c (4/3, enforced at 0x6BA87 / 0x172CC1). 0x6AB30 builds the projection
  from the viewport and +0x24, 0x6AD00 the left/right culling planes from +0x24, 0x6AD90 the top/bottom ones from +0x28.
  Both +0x24 users are hooked: a 4:3 object gets +0x24 = +0x28 x aspect (16:9 by default; `aspect.txt`; `nowide.txt` = off), so
  the game draws and culls a wider view and the 640x480 frame is shown stretched to the window. The gameplay camera recomputes
  +0x28 = +0x24 x 0.75 every frame: that feedback is undone per object (otherwise the view grew until the world vanished).
  Squeezing in the vertex shaders instead (`shaderwide.txt`) left the game's 4:3 view in the middle with empty strips.
  Not done: the 2D HUD is stretched sideways too (oval reticle).
- **Present pass (`bfgl.c` present_tex):** the frame goes to the window through a shader: FXAA (`noaa.txt` = off) and
  contrast-adaptive sharpening (`sharpen.txt` = percent, default 60). `pillarbox.txt` = 4:3 with bars.
- **Internal resolution (`bfgl.c` res_scale):** every surface (colour, depth-stencil, the depth-as-colour copy) is allocated at
  g_res x its Xbox size on the GPU (`resscale.txt` 1-4, default 2, read at start; surfaces that would pass 4096 stay at 1). The
  game's own numbers are untouched; the GPU side is scaled at: viewports and scissors (draws, clears, the glow-mode clear, the
  depth copy), the reticle depth read, the brightness probe, screenshots / frame.bmp (scaled down first), the present pass
  (FXAA steps one GPU pixel) and the occlusion pixel counts (divided by g_res^2). Render-target sampling needs nothing (normalised).

## Next steps (stage 3)
- **Picture (done):** the HUD is unstretched in widescreen. Screen-space draws (w = 1) on the 640 screen and the 320x240 glow buffer
  are narrowed by 4:3 / window aspect in both vertex shaders (`u_hud` = k, anchor). The anchor is the screen centre, so a line of menu
  text drawn in several pieces stays together. The radar and health bar (moved by `hud_move`) keep the left edge. `hud_move` acts only in
  gameplay: input.c's camera code writes a heartbeat to ring pad[55] (menu text lay in the radar and health areas and was torn apart).
  `nohudfix.txt` turns the squeeze off. Movies (`present_memory`) skip FXAA and get half sharpening (FXAA softened them).
- **Pixel-count reports without stalls:** in-mission cutscenes ran at about 35 frames/s because the viewer was 100% busy. It read each
  occlusion query at once, up to 270 reports a frame, and every read waited for the whole GPU.
  - Now each counting stretch is its own query (a ring of 4096 segments), and a report records which segments it sums.
  - Counts are written when the GPU has them: checked each pump, and forced at each flip.
  - The game's D3D marks a pending report with word 3 = 0xFFFFFFFF and count 0. That word stays until the count is written, and it is written last.
  - Writing "done" early with a placeholder count made the glow haze flicker.
  - `zsync.txt` (live) = the old waiting reads; `zlag.txt` = no forced wait at the flip.
- **Milestone 97: online System Link, full screen, download with installer.**
  - **Online (`net.c`)**
    - The game links XNet, which drives the NVNET chip itself. The chip is emulated as follows:
      - Its register page 0xFEF00000 is trapped (`kernel2.c` NIC_PAGE -> `nic_rd`/`nic_wr`).
      - Transmit and receive rings use 8-byte descriptors: buffer, length-1, flags. TX_VALID and RX_AVAIL are 0x8000.
      - Interrupt vector 0x34 is raised by the ticker (`raise_vector`; `KeSynchronizeExecution` takes the same lock).
      - Reading 0x144 returns the IDLE bit 0x8, which the driver waits for after writing BIT2.
    - Frames travel over UDP port 34567 in a hub layout: the host relays everything, clients send keep-alives.
    - `netplay.txt` holds `host [port]` or `join addr[:port]`; without it the cable stays unplugged.
    - MAC is 00:50:F2 plus a hash of the computer name. It is answered for setting 0x101, and placed at 0x40 of setting 0xFFFF only before 0x101 is asked.
    - Setting 0xFFFF also feeds the key of the signed option files. Changing it caused "Unable to load option settings".
    - `XcModExp` is real (bignum) for the Diffie-Hellman exchange. DES stays a copy on both ends.
    - Hosting works: the WAITING FOR PEERS screen appears and frames are sent. A second PC has not been tested yet.
  - **Full screen (`bfview.c`)**
    - Borderless on the window's monitor by default. F11 or Alt+Enter toggles; `windowed.txt` starts in a window.
  - **Download (`dist\`)**
    - `make_dist.ps1` builds `BruteForceRecomp.zip`: Install.bat, install.ps1, and files\ (programs, patches, Razorback assets). No game data.
    - The installer goes to %LOCALAPPDATA%\BruteForceRecomp. It finds the game (default.xbe plus data\common.tgz), asks about online play, and makes desktop and Start-menu shortcuts.
    - Edited game files ship as patches: `tools\make_patches.py` writes `loader\patches\*.bfp` (copy/insert ops, FNV-checked).
    - `patch.c` applies a patch when the game opens the original for reading. It was tested on a fresh install: both English patches applied.
- **Milestone 97 fixes**
  - **Key-binding screen**
    - **What the game does with the text:** it splits the text into one buffer per line when it opens. It can also make shorter copies.
    - **Why it broke:** writing the full 1223 characters into a short copy crashed the game and showed the text twice. With the multi-line list left in the string table, the next opening only had line 1 to find, so the screen froze.
    - **Now:** each copy is written only up to its own 0, and our own buffer is excluded. Up to 16 copies are kept.
    - **On close:** the one-line "Loading key bindings..." text is put back.
    - Pause (START) is fixed to Esc and is left out of the list.
  - **Shots after a death**
    - A stale view-projection (the "last found" rows) aimed them wrongly. It is now used only if less than 0.3 s old; otherwise the game aims.
    - `[shooter]` is logged on every change, and an `[aim]` summary every 2 s.
- **Fixed: new missions crashed while loading (E01, the mission after Rescue Brutus).**
  - **Cause:** the game sizes its big-block heap from the free physical memory that MmQueryStatistics reports.
    - It happens in GlobalMemoryStatus at 0x2F2937, used by 0x68020: free minus the small pools minus 1.5 MB.
    - The heap is then allocated from contiguous memory at 0x443080 by 0x2376B0. Allocations bigger than the small-pool limit go to it through 0x2379A0 -> 0x237810 -> 0x258F70.
    - The loader reported a fixed 12000 pages, which gave a 29 MB heap.
    - E01's object data file (objects-e01.ipn, 0x395A84 bytes) then did not fit. 0x25E5D0 failed, so 0x25EA79 skipped loading it, and the offset->pointer fixup 0x25E610 never ran.
    - Characters then used archetype data blocks whose pointers were still file offsets (0x200). This led to unset sound positions (infinity) and crashes at 0x2B97D3, 0x2D5DCB and 0x2C8B4A.
  - **Fix (kernel.c MmQueryStatistics):**
    - Available pages = the pool's real free space minus the game image (0x4C4000, which on the Xbox lives in the same 64 MB), and never below 12000.
    - At the heap-sizing call this gives 13623 pages and a heap of about 35 MB.
  - **Verified:** E01 loads and runs.
  - **How it was found:**
    - A/B runs: auto_press goes straight into E01.
    - Probes of the six builders of class 0x39DF40, the file loader 0x23B420 and the data-file buffer at 0x25EA81. All have been removed.
  - **Shot aiming, any weapon and fire rate:**
    - The view-projection is caught when the game uploads it: render.c calls input.c vp_watch for every 4 constant rows written.
    - A match must be tight: our camera's point 10 ahead lands at the centre within 0.004, with w between 9.9 and 10.1. Objects' world x view x projection only nearly matches.
    - Proj = VP x inverse(our view) is kept double-buffered for 30 s. Each shot uses Proj x our view at that moment.
    - Searching the constants at the moment of a shot failed for the RVG-50 minigun in E01: the constants then belonged to another pass.
    - The age uses a signed difference: the capture can land just after the shot read the clock, and an unsigned difference called every fresh one "too old".
  - **Sprint pose and heavy guns:**
    - The held gun's resting distance from the right wrist (`g_held_base`) is recorded whenever held_choose picks a gun.
    - A gun more than 0.68 m away gets no sprint pose; it keeps the game's own run animation (user). Measured: the RVG-50 minigun is at 0.78–0.81 m, the Regen at 0.39–0.59 m.
    - The weapon-switch check is relative to that distance (base + 0.3 m, at least 0.55 m).
    - Ruled out as a way to tell which gun is held: no character field holds the current weapon pointer, and the weapon parts hang off bone 0. The "moving part" test does not work either, because the body turn rotates every weapon part.
  - The HUD gameplay signal (pad[55]) is now sent from cam_after whenever the follow camera tracks the player, so it works with a controller too, not only with mouse aiming.- **Milestone 98: online play through a server, with public and private rooms.**
  - **`bfserver.exe` (`loader\bfserver.c`)** is a UDP relay with rooms.
    - Packets are `"BFN2"` + room (u32) + Ethernet frame; an empty frame is a keep-alive.
    - Players are dropped after 15 s of silence.
    - Each frame goes to everyone else in the same room. Room 0 is public; a private room is the FNV-1a hash of the password, |1.
  - **netplay.txt:** `online <server>[:port]`, and `password <text>` for a private room. The old `host` / `join` modes still work.
  - **Server browser:** the game's own System Link session list shows every match hosted in your room.
  - **Tested locally:** the game joined the server's public room, and a stand-in second player's broadcast frames reached it (30 of 30).
  - **Text:** `tools\online_strings.py` rewrites 19 English strings (the menu entry "SYSTEM LINK" -> "PLAY ONLINE", "over System Link" -> "online", and so on). The original is kept as `.ol_orig`. Run `tools\make_patches.py` afterwards.
  - **Installer:** Enter = public, P = private with a password, N = offline. The server address comes from `dist\server.txt`; it asks if that file is missing. "Start online server.bat" is in the download.
- **Milestone 98b: match list instead of a relay server.** No PC has to stay on.
  - **The list:** a Cloudflare Worker with a D1 database (`dist\matchlist`, deployed with `npx wrangler@4 deploy`), at https://brute-force-list.zacharyhill1234.workers.dev.
    - GET `/host?room&port[&ip]` registers a match, `/stop` removes it, and `/list?room` returns "ip port" lines. A match drops off 90 s after its last report.
  - **netplay.txt:** `list <url>` (+ `password ...`).
    - The loader (net.c) detects hosting: it sends a unicast frame to a console that broadcast within 3 s, which means it answered a search. It then counts as hosting for 120 s.
    - Every player connects (`"BFN3"` + room + frame, port 34567) to every listed match in its room, so the game's own session list is the server browser. The host relays frames among its players.
  - **bflist.exe** (`bflist.c`) does the HTTPS (WinHTTP) work.
    - WinHTTP inside the loader failed with error 14, because it reads the program header where the XBE header sits. Swapping the header in for the call crashed a game thread.
    - The helper talks to the loader over localhost UDP: `"BFNH"` + hosting goes to port 34566, and `"BFNL"` + our IP + n x (ip, port) comes back to 34567.
    - It is started after the viewer reports its renderer. Started next to the viewer at boot, the viewer's OpenGL failed (software fallback, flicker).
    - Our IPv4 comes from https://1.1.1.1/cdn-cgi/trace: this PC reaches Cloudflare over IPv6, which the list can't hand out.
  - **Tested:** a stand-in host found via the list exchanged frames with the game. Our hosting got listed as the right public IPv4 and was removed when the game closed.
  - **UPnP** (`online_port.ps1`, run by play.bat when netplay.txt has `upnp`, which the installer asks about) got no answer here: either UPnP is off on the router, or the network profile is Public. Hosting from this PC needs UDP 34567 forwarded to it.
- **Milestone 99: button prompts show the bound keys.**
  - **Where the icons live:** the game draws every button prompt as a glyph of one font sheet. It is 128x256 A4R4G4B4, holds two copies of the icons, and has content hash 0x8AB7F635.
    - Found with `texdump.txt`, a switch-gated dump in bfgl.c tex_get of every texture up to 512 px.
    - The menu hints (game-options `button-string` plus a button ID) only name the string and the button; the icon comes from that sheet.
  - **The swap:** when the viewer decodes the sheet, it uploads a 4x copy (512x1024) instead. Texture coordinates are normalised, so this is a drop-in.
    - Each button cell gets a key cap with the bound key's short name (`key_cap`), drawn with GDI text.
    - The cells, as `g_cells` in bfgl.c: A, B, X, Y, the L/R triggers, START, BACK, the white and black buttons, and the D-pad arrows, in both copies.
    - Bindings are read from keybinds.txt every second; a change invalidates the cached sheet. START is always ESC.
  - `nokeyicons.txt` = the Xbox icons.
- Seen once: about a 10 s game-side stall on the profile screen, with the viewer idle. Possibly OneDrive locking `xbox_hdd` files; it didn't recur.
- **Key bindings:** patch the other languages (`python tools\kbmenu_patch.py de` etc.); the co-op join prompt is still shown.
- **Camera collision** (Milestone 94, unfinished).
- **Weapon poses:** one-handed and pointed up while sprinting, shoulder-mounted while aiming (animation/IK work).
- **Camera collision** with walls for the Division-style camera.
- **The START button icon** in the "Press START" HUD text draws as a black blob.
- **Depth read as colour:** only the stencil (alpha) is copied; RGB (the depth bytes) is 0. Enough for the glow, which uses alpha only.
- **Pixel-count reports:** read the query results without waiting.
- **Sound:** compare the mix (volumes, 3D positioning, effects sends) with the real game.
- **Graphics:**
  - checking the image against the real game (brightness, haze)
  - stencil, point sprites, points and lines
  - clipping of triangles that cross the eye plane (CPU path only; the GPU path clips in hardware)
  - fixed-function lighting, if the game turns out to use it
  - anti-aliased surfaces
- **Play-testing:** later missions, saving and loading, the multiplayer menus.

## Next steps (earlier stages)
- **Data classes:** cover the last 16 classes (jump-table switches, shared handlers), and type the remaining unnamed fields from the parser each one uses.
- **Structures:** add more fields (the AI controller and projectile are still thin) and more classes (CharacterType, Squad, the HUD base classes) to `BruteForce_ApplyTypes.java`; give the register-argument functions (RenderState_*, the 8 skipped Character ones) their object parameter.
- **Helpers:** the next most-called unnamed game functions are listed at the top of `function_map.csv` when sorted by call sites (see `function_map.csv`). **Components:** name each class's other interface methods from the vtables listed in the `QueryInterface` comments (Milestone 62). **HUD:** the other 25 interface class names (Milestones 61 and 63), and what each HUD class draws..
- **AI details:** the per-goal flag table (`0x4A5908`) that marks goals as uninterruptible, and the names of the 21 parameter keys.
- **Character systems:** where the 4 movement speeds are read; the position/velocity update in `Character_Update`'s movement block (`0x11D0E6`–`0x11D913`); the periodic block (`0x11C540`).
- **Shield field names:** crack the hashes `0xE76E8170`, `0x1CF65A16` and `0xFE14A5B7` (meanings known: second effect, extra delay after depletion, recharge per second).
- Identify the world subsystems still marked "?" in the Milestone 14 table.
- **Physics:** identify the remaining 36 unnamed IVP entry points in `physics_boundary.csv` (`ivp_2c00d0` is the most used, 12 call sites) and name the other 35 game functions that call them.
- **Libraries:** the other 4 Xbox Live functions (the content download tasks), and the remaining XAPI and runtime functions. `platform_boundary.csv` lists every unnamed one with its call sites.
- **Terrain extras:** the per-block shading values (`_e2d79bad`, 256 per block) and what v11/v12 drive in the terrain shader (lighting direction, texture blending).
- **Animations:** the mesh names of multi-part objects (the remaining unknown parts) and 67 clip names; the meaning of the motion-sequence fields (blend times, `aim`, `loop`, overlays).
- **Effects:** the missing emitter nodes (262 references), streak and beam shapes in the viewer, and the effect-placement fields in the object types.
- Confirm the meaning of session states 0–5.
- **Levels:** the sky dome block (`_fb7bae68`) and its slice count (`_f4cecd12`) and the six cube-map textures (`_0ec6a210`, indoor and outdoor), probably environment reflections.
- **Sounds:** names for the remaining 390 cues (footsteps of the other creatures, metal and glass hits, level ambiences; check candidates against where each cue is used, as in Milestone 50); how cues are triggered from object types (weapons, footsteps by material) and the meaning of `_fc07a71a` (0.01–0.15, maybe a pitch or volume variation).
- **Triggers:** the remaining 15 trigger type names, the inferred trigger behaviours (text box, menu, zone, event, squad, score), the decal / expression / interface / turret / shield / sparam / respawn triggers, the remaining 16 objective keys, and the rest of the level object names.
