---
applyTo: "src/player/strategy.*,src/player/soccer_role.*,src/player/role_*.cpp,src/player/role_*.h,src/formations-dt/**,src/formations-keeper/**,src/formations-taker/**"
---

# Strategy / Role / Formation Layer

## TL;DR
`Strategy` (singleton, [strategy.cpp](../../src/player/strategy.cpp) — **1147 lines**) owns 13 `rcsc::Formation::Ptr` instances loaded from per-situation JSON `.conf` files, decides which formation applies to the current game situation, looks up a role name from that formation, and instantiates the matching `SoccerRole` subclass (one of 11 `role_*.cpp` files) via an internal factory map. `SamplePlayer::actionImpl()` calls `Strategy::instance().createRole(unum, world())` then `role->execute()` — see [sample-player-core.instructions.md](./sample-player-core.instructions.md).

## Overview
- **[strategy.h](../../src/player/strategy.h)** (213 lines) / **[strategy.cpp](../../src/player/strategy.cpp)** (1147 lines, confirmed via `read_file` — matches the ~1147 estimate in the task).
- **[soccer_role.h](../../src/player/soccer_role.h)** (94 lines) / **[soccer_role.cpp](../../src/player/soccer_role.cpp)** (105 lines): abstract base `SoccerRole` + a generic `rcss::Factory`-based registry (`SoccerRole::creators()` / `SoccerRole::create()`), gated behind `#ifdef USE_GENERIC_FACTORY` (currently **disabled** in the build — see Patterns below).
- **11 `role_*.cpp/h` files** in `src/player/`, all subclassing `SoccerRole`:
  `role_sample`, `role_goalie`, `role_center_back`, `role_side_back`, `role_defensive_half`, `role_offensive_half`, `role_side_half`, `role_side_forward`, `role_center_forward`, `role_keepaway_keeper`, `role_keepaway_taker`.
  (Corrects the task's assumed list: **no `role_savior` or `role_sweeper`** — those only appear as commented-out/dead code in [soccer_role.cpp:38-47](../../src/player/soccer_role.cpp) and are not built.)
- **Data directories** (each holds the same 14 `.conf` filenames, confirmed via `Get-ChildItem`):
  - `src/formations-dt/` — the **live in-game** formation set, loaded by the normal `SamplePlayer` (`--config_dir` defaults here via `start.sh`); despite the directory name ("dt"), most files use `"method":"DelaunayTriangulation"` but a few (`before-kick-off.conf`, `goal-kick-*.conf`, `goalie-catch-*.conf`) use `"method":"Static"` — verified per-file.
  - `src/formations-keeper/` / `src/formations-taker/` — used only by the **keepaway training** scenario, wired via [keepaway.sh.in](../../src/keepaway.sh.in) (`--config_dir ${keeper_config_dir}` / `${taker_config_dir}`), driving `RoleKeepawayKeeper` / `RoleKeepawayTaker`.

## Architecture
```
SamplePlayer::init()
  └─ Strategy::instance().read(config().configDir())      # strategy.cpp:173
       └─ createFormation(path + <CONF>) for all 13 situations
            └─ rcsc::FormationParser::parse(filepath)      # reads JSON, picks
                                                             # FormationStatic or
                                                             # FormationDT by "method"
SamplePlayer::actionImpl()
  └─ Strategy::instance().update(world())                  # picks M_current_situation
  └─ Strategy::instance().createRole(unum, world())         # strategy.cpp:442
       ├─ getFormation(wm)         -> Formation::Ptr (situation-based)
       ├─ f->roleName(number)      -> role name string (from formation's "role" array)
       └─ M_role_factory[role_name]()  -> SoccerRole::Ptr (concrete Role*)
  └─ role->execute(agent)
```
This confirms the hypothesis in the task: **`Strategy` wraps librcsc's `Formation` class** (see [formation-and-ann.instructions.md](../../../librcsc/.github/instructions/formation-and-ann.instructions.md)), reading `FormationDT`/`FormationStatic` instances from the `formations-*/*.conf` JSON files at runtime — one `Formation::Ptr` per situation, not a single formation.

## Patterns & Conventions
- **13 named `.conf` slots**, each a `static const std::string` constant on `Strategy` (`BEFORE_KICK_OFF_CONF`, `NORMAL_FORMATION_CONF`, `DEFENSE_FORMATION_CONF`, `OFFENSE_FORMATION_CONF`, `GOAL_KICK_OPP/OUR_FORMATION_CONF`, `GOALIE_CATCH_OPP/OUR_FORMATION_CONF`, `KICKIN_OUR_FORMATION_CONF`, `SETPLAY_OPP/OUR_FORMATION_CONF`, `INDIRECT_FREEKICK_OPP/OUR_FORMATION_CONF`) — [strategy.h:70-82](../../src/player/strategy.h).
- **`getFormation(wm)`** ([strategy.cpp:714](../../src/player/strategy.cpp)) is a big `switch`/`if` chain on `wm.gameMode().type()` and `M_current_situation` (`Normal_Situation`, `Offense_Situation`, `Defense_Situation`, `OurSetPlay_Situation`, `OppSetPlay_Situation`, `PenaltyKick_Situation`) that picks which of the 13 `Formation::Ptr` members to return for the current tick.
- **Dual factory pattern for roles**:
  - Production path (`USE_GENERIC_FACTORY` **undefined**, the default): `Strategy` builds its own `std::map<std::string, SoccerRole::Creator> M_role_factory` in its constructor ([strategy.cpp:97-117](../../src/player/strategy.cpp)), registering all 11 concrete roles by `RoleXxx::name()`.
  - Alternate path (`USE_GENERIC_FACTORY` defined, commented out at [strategy.h:45](../../src/player/strategy.h)): would use `SoccerRole::create(name)` which delegates to the generic `rcss::Factory`-based `SoccerRole::creators()` registry ([soccer_role.cpp:56-89](../../src/player/soccer_role.cpp)). Not active in the current build.
- **Each `role_*.h`** follows an identical shape (e.g. [role_center_back.h](../../src/player/role_center_back.h)): `static const std::string NAME`, `static const std::string& name()`, `static SoccerRole::Ptr create()` (self-registering factory function), plus `virtual bool execute(PlayerAgent*)` override and private `doKick()`/`doMove()` helpers.
- **Role name → formation position mapping** is entirely data-driven: `Formation::roleName(number)` reads the `"role"` array of the active `.conf` JSON (`{number, name, type, side, pair}`), so swapping a name in the JSON (e.g. `"role_side_back"` → `"role_center_back"`) changes which C++ class plays that uniform number, with zero code change.
- **`.conf` JSON schema** (already confirmed reading `before-kick-off.conf`): `{"version", "method": "Static"|"DelaunayTriangulation", "role": [{number, name, type, side, pair}, ...], "data": [{...x,y per role plus ball anchor...}, ...]}`.

## Key Abstractions
| Type | File | Role |
|---|---|---|
| `Strategy` (singleton) | [strategy.h/.cpp](../../src/player/strategy.h) | Owns all formations, situation detection, role factory, home-position cache |
| `SoccerRole` (abstract) | [soccer_role.h/.cpp](../../src/player/soccer_role.h) | Base interface: `execute()`, `acceptExecution()`, static `create()`/`creators()` |
| `RoleXxx` (11 concrete classes) | `role_*.h/.cpp` | One per tactical position; each defines `NAME`, `create()`, `execute()` |
| `rcsc::Formation` / `FormationDT` / `FormationStatic` | librcsc | See [formation-and-ann.instructions.md](../../../librcsc/.github/instructions/formation-and-ann.instructions.md) |
| `rcsc::FormationParser` | librcsc (`formation_parser.h`, included at [strategy.cpp:60](../../src/player/strategy.cpp)) | Parses `.conf` JSON, dispatches to the right `Formation` subclass by `"method"` |

## Integration Points
- `Strategy::init()` / `Strategy::read()` called once from `SamplePlayer::init()` — see [sample_player.cpp:197](../../src/player/sample_player.cpp): `Strategy::instance().read(config().configDir())`.
- `Strategy::update(wm)` and `Strategy::createRole(unum, wm)` called every cycle from `SamplePlayer::actionImpl()` — full call chain documented in [sample-player-core.instructions.md](./sample-player-core.instructions.md); this file focuses on what happens *inside* `Strategy`/`SoccerRole`, not the outer agent loop.
- `config_dir` (and therefore which `formations-*` directory is loaded) is a command-line option (`--config_dir`), defaulted to `formations-dt` for the normal `start.sh`/`start-offline.sh` launchers, and overridden to `formations-keeper` / `formations-taker` by [keepaway.sh.in:40-41,265,279](../../src/keepaway.sh.in) for the keepaway training binaries.
- `Strategy::exchangeRole(unum0, unum1)` swaps two players' `M_role_number` entries at runtime (used for dynamic role switching, e.g. mark exchange) without touching the underlying `Formation`.

## Build & Test
- All `role_*.cpp`, `strategy.cpp`, `soccer_role.cpp` are compiled into the `player` target listed in [src/CMakeLists.txt](../../src/CMakeLists.txt); the same file lists `formations-dt formations-keeper formations-taker player.conf coach.conf ...` as install/data assets (line 42).
- No dedicated unit tests for this layer; the practical smoke test is running `start.sh` / `start-offline.sh` and confirming `Strategy::read()` does not print `"Failed to read ... formation"` to stderr (each formation-load failure returns `false` from `Strategy::read()` and aborts player init).
- `keepaway.sh.in` is the template for the generated `keepaway.sh` test/training harness and is the only consumer of `formations-keeper`/`formations-taker`.

## Logging
- Failures during formation load go to `std::cerr` (not the `dlog` debug-log channel), e.g. `"Failed to read before_kick_off formation"`, `"(Strategy::createFormation) Could not create a formation from " << filepath` ([strategy.cpp:296](../../src/player/strategy.cpp)).
- Runtime role-exchange and role-selection errors use `dlog.addText(Logger::TEAM, ...)` (e.g. `exchangeRole`, [strategy.cpp:400](../../src/player/strategy.cpp)) plus `std::cerr` for unsupported/invalid role names ([strategy.cpp:480](../../src/player/strategy.cpp): `"***ERROR*** unsupported role name [...]"`).

## Important Notes
- **Corrected assumption**: the task's example role list included `role_goalie`/`role_sample` (correct) but implied a possible ~10-role set; the actual count is **11** role files, and `role_savior`/`role_sweeper` mentioned only in dead/commented code in `soccer_role.cpp` do **not** exist as files.
- **Corrected assumption**: `formations-dt` is not exclusively `FormationDT`/DelaunayTriangulation — several of its `.conf` files (`before-kick-off`, `goal-kick-*`, `goalie-catch-*`) use `"method":"Static"`. The directory name reflects the dominant/typical method, not a strict guarantee; `FormationParser::parse()` dispatches per-file based on the JSON `"method"` field, so `Strategy` transparently mixes `FormationStatic` and `FormationDT` instances across its 13 members.
- `USE_GENERIC_FACTORY` is defined but never `#define`'d anywhere in the active build (only referenced via `#ifndef`/`#ifdef` guards) — the generic `rcss::Factory` path in `soccer_role.cpp` is effectively dead code today; `Strategy`'s own `M_role_factory` map is what actually runs.
- `Strategy` is a classic Meyers singleton (`static Strategy & instance()`); `Strategy::i()` is a shorthand alias for `instance()`.

## See Also
- [sample-player-core.instructions.md](./sample-player-core.instructions.md) — `SamplePlayer::actionImpl()` call chain into `Strategy`/`SoccerRole`.
- [../../../librcsc/.github/instructions/formation-and-ann.instructions.md](../../../librcsc/.github/instructions/formation-and-ann.instructions.md) — `Formation` base class, `Formation::create()` factory, `FormationDT`/`FormationStatic` internals.

---
Part of: [../copilot-instructions.md](../copilot-instructions.md)
