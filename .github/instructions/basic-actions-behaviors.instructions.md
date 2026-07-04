---
applyTo: 'src/player/basic_actions/**'
---
# Basic Actions & Behaviors Library (helios-base)

## TL;DR
[`src/player/basic_actions/`](../../src/player/basic_actions) (verified path; **88 files** at top level + 2 more under `obsolete/` = 90 total, close to the ~85 estimate) is helios-base's own `body_*`/`bhv_*`/`neck_*`/`view_*`/`focus_*`/`arm_*` action library, following the same `AbstractAction` → `BodyAction`/`NeckAction`/`ViewAction`/`FocusAction`/`ArmAction`/`SoccerBehavior` effector-slot pattern documented for librcsc.
- **Critical, non-obvious finding**: this directory is **not** a thin wrapper around librcsc's `rcsc/action/` — it is a **parallel fork**. librcsc ships its own `rcsc::KickTable`, `rcsc::Body_Dribble2008`, `rcsc::Body_SmartKick`, etc. in namespace `rcsc`; helios-base compiles its **own separate, near-identical classes with the same names in the global namespace** from this directory, and helios-base's player code (`sample_player.cpp`, `bhv_penalty_kick.cpp`, etc.) exclusively uses the **local/global-namespace versions** via `#include "basic_actions/kick_table.h"` (quoted, relative include), never `<rcsc/action/kick_table.h>`.
- `kick_table.cpp` is **2239 lines** (header 409 lines) — confirmed by `read_file`; librcsc's equivalent is 2241/411 lines. Both define global class `KickTable` but under **the same include guard `RCSC_ACTION_KICK_TABLE_H`** — librcsc's is inside `namespace rcsc {}`, helios-base's is **not namespaced at all**. `grep` across the repo found **zero** references to `rcsc::KickTable` in helios-base — librcsc's copy is entirely unused here.
- `body_dribble2008.cpp` is **2367 lines** (header 300 lines) — confirmed; librcsc's `rcsc::Body_Dribble2008` equivalent is 2369/similar. Same relationship: helios-base's global-namespace `Body_Dribble2008` (aliased as `Body_Dribble` via `using Body_Dribble = Body_Dribble2008;` in [body_dribble.h:38](../../src/player/basic_actions/body_dribble.h:38)) is what `bhv_penalty_kick.cpp` and everything else actually calls.
- **Open the full file when:** tuning kick feasibility/search depth (`kick_table.cpp`), dribble/dodge heuristics (`body_dribble2008.cpp`), or tracing exactly how a helios `Body_*`/`Neck_*` class turns `WorldModel` state into `agent->doDash/doKick/doTurnNeck` calls.

## Overview
This directory is helios-base's **behavior primitive layer**, sitting between `Strategy`/`SoccerRole` (see [strategy-roles-formations.instructions.md](./strategy-roles-formations.instructions.md)) and librcsc's low-level effector commands. `SamplePlayer::actionImpl()` (see [sample-player-core.instructions.md](./sample-player-core.instructions.md)) and role/behavior classes construct these action objects on the stack and call `execute(agent)` immediately — there is no persistent behavior tree. The directory mirrors librcsc's `rcsc/action/` almost file-for-file (same filenames, same author comments, similar line counts) because it began as a fork of an earlier librcsc action library and has since diverged independently per-team.

## Architecture
Files group cleanly by prefix (counts from the live directory listing):
- **`body_*` (35 files)** — body-effector behaviors (dash/turn/kick), subclass `rcsc::BodyAction`. Includes navigation (`body_go_to_point[.h]`, `body_go_to_point_dodge`), ball control (`body_hold_ball2008`, `body_stop_ball`, `body_stop_dash`, `body_advance_ball2009`, `body_clear_ball2009`), kicking (`body_kick_one_step`, `body_kick_to_relative`, `body_smart_kick`, `body_pass`), interception (`body_intercept2018`, plus `obsolete/body_intercept2009.cpp`), dribble (`body_dribble2008` + `body_dribble.h` alias), and trivial header-only turn primitives (`body_turn_to_angle.h`, `body_turn_to_ball.h`, `body_turn_to_point.h`, `body_tackle_to_point.h`).
- **`bhv_*` (12 files)** — composite multi-slot behaviors, subclass `rcsc::SoccerBehavior`: `bhv_before_kick_off`, `bhv_emergency`, `bhv_go_to_point_look_ball`, `bhv_scan_field`, plus header-only body+neck combinators (`bhv_body_neck_to_ball.h`, `bhv_neck_body_to_point.h`, etc.).
- **`neck_*` (17 files)** — `NeckAction` subclasses controlling `turn_neck`: scanning (`neck_scan_field`, `neck_scan_players`), reactive tracking (`neck_turn_to_ball_and_player`, `neck_turn_to_ball_or_scan`, `neck_turn_to_goalie_or_scan`, `neck_turn_to_low_conf_teammate`, `neck_turn_to_player_or_scan`), and header-only primitives (`neck_turn_to_ball.h`, `neck_turn_to_point.h`, `neck_turn_to_relative.h`).
- **`view_*` (5), `focus_*` (4), `arm_*` (2)** — `ViewAction`/`FocusAction`/`ArmAction` subclasses (`view_wide.h`, `view_change_width.h`, `view_synch`, `focus_move_to_point`, `focus_reset`, `arm_off.h`, `arm_point_to_point.h`), mostly small/header-only, cloneable per the librcsc slot pattern.
- **`intention_*` (4)** and **`intercept_*` (4)** — multi-cycle action queues (`intention_dribble2008`, `intention_time_limit_action`) and the pluggable interception-choice strategy (`intercept_evaluator[.h/.cpp]`, `intercept_evaluator_factory`).
- **`kick_table.{h,cpp}`** and **`basic_actions.{h,cpp}`** (aggregator header/impl for trivial turn/view/arm includes) sit at the top level, not under any of the above prefixes.
- **`obsolete/`** — `body_intercept2009.{cpp,h}`, kept for reference, excluded from the build (mirrors librcsc's own `obsolete/` convention for prior-year variants like `body_dribble2006/2007`).

## Patterns & Conventions
- **Same `<Slot>_<BehaviorName>` naming and construction-time-params/execute-time-effects pattern as librcsc** — see [action-behavior-library.instructions.md](../../../librcsc/.github/instructions/action-behavior-library.instructions.md) for the base pattern; not re-derived here.
- **Year-versioned evolution**: `body_dribble2008` (active; `body_dribble.h` aliases `Body_Dribble` to it), `body_advance_ball2009`, `body_clear_ball2009`, `body_hold_ball2008`, `body_intercept2018` (active; supersedes `obsolete/body_intercept2009`) — the non-obsolete, highest-year file wired into `Makefile`/`Makefile.am` is the one actually compiled.
- **Local includes shadow librcsc**: every file here uses quoted includes (`"kick_table.h"`, `"basic_actions/body_dribble2008.h"`), which the build's include path resolves to *this* directory before `<rcsc/action/...>`. This is what makes the fork effective — no explicit namespace qualification is needed anywhere in helios-base's own code because the local classes simply live in the global namespace while librcsc's live in `rcsc::`.
- **`KickTable` is the only singleton** in this directory (`KickTable::instance()`, mirroring librcsc's design exactly) — everything else is stateless, stack-constructed value objects.

## Key Abstractions
- **`KickTable`** ([kick_table.h](../../src/player/basic_actions/kick_table.h), 409 lines / [kick_table.cpp](../../src/player/basic_actions/kick_table.cpp), 2239 lines) — global-namespace singleton, precomputed 1-3 step kick/dash search (`MAX_DEPTH=2`, `DEST_DIR_DIVS=72`) with `Flag` bits (`SELF_COLLISION`, `KICKABLE`, `OUT_OF_PITCH`, `KICK_MISS_POSSIBILITY`, etc.) nearly identical in structure to librcsc's `rcsc::KickTable`. Read from `config().configDir() + "/kick-table"` and (re)built via `createTables()` in `SamplePlayer::initImpl()`/`handlePlayerParam()`; consumed by `Body_SmartKick::execute()` ([body_smart_kick.cpp:86](../../src/player/basic_actions/body_smart_kick.cpp:86)) and `Body_Dribble2008`.
- **`Body_Dribble2008`** ([body_dribble2008.h](../../src/player/basic_actions/body_dribble2008.h), 300 lines / [body_dribble2008.cpp](../../src/player/basic_actions/body_dribble2008.cpp), 2367 lines) — global-namespace class deriving from `rcsc::BodyAction` (so it **does** use librcsc's base action interfaces, just not librcsc's sibling `Body_Dribble2008` implementation). Nested `KeepDribbleInfo` struct tracks `first_ball_vel_`, `ball_forward_travel_`, `dash_count_`, `min_opp_dist_` per candidate plan; `doKickTurnsDash(es)`/`doKickDashes(WithBall)`/`simulateKickDashes`/`existKickableOpponent` enumerate and score candidate dash sequences with opponent-avoidance. This is the class actually invoked by `bhv_penalty_kick.cpp:818` and via the `Body_Dribble` alias — **not** `rcsc::Body_Dribble2008`.
- **`intercept_evaluator` / `intercept_evaluator_factory`** — pluggable scoring strategy selected by name, used by `Body_Intercept2018` to rank candidate interception cycles/positions.
- **`basic_actions.h`** — pure aggregator include (arm/bhv/body-turn/neck/view fundamentals), not a class of its own — same convenience-header pattern librcsc uses.

## Integration Points
- **librcsc base classes**: every class here still derives from librcsc's `rcsc::BodyAction`/`NeckAction`/`ViewAction`/`FocusAction`/`ArmAction`/`SoccerBehavior` (`rcsc/player/soccer_action.h`) — only `KickTable`, `Body_Dribble2008`, `Body_SmartKick` and a few dependents are **fully re-implemented** locally rather than reused; most other classes (`Body_GoToPoint`, `Neck_*`, `View_*`, etc.) are genuinely helios-specific and have no librcsc counterpart at all.
- **Callers**: `SamplePlayer::actionImpl()`/`doPreprocess()` and concrete `SoccerRole`s construct these directly (see [sample-player-core.instructions.md](./sample-player-core.instructions.md) and [strategy-roles-formations.instructions.md](./strategy-roles-formations.instructions.md)); `bhv_penalty_kick.cpp`, `bhv_set_play*.cpp` also include this directory's headers.
- **librcsc counterpart directory**: [`action-behavior-library.instructions.md`](../../../librcsc/.github/instructions/action-behavior-library.instructions.md) documents `rcsc/action/` (116 files) — read that first for the shared `AbstractAction` pattern; this file only covers what's helios-specific or forked.

## Build & Test
Compiled into the `sample_player` binary via `src/player/basic_actions/Makefile` (autotools; listed individually in the parent `Makefile.am`, e.g. `basic_actions/body_dribble2008.h` at `Makefile.am:136`). No dedicated unit tests; validated via full-match runs. See top-level `copilot-instructions.md` for the general build command.

## Logging
Uses librcsc's `dlog` (`Logger`) facility — e.g. `Logger::KICK`/`Logger::TEAM`/`Logger::DRIBBLE` categories traced from `kick_table.cpp` and `body_dribble2008.cpp` (guarded by commented-out `#define DEBUG`/`DEBUG_PROFILE` macros at the top of `kick_table.cpp` for extra verbose tracing during tuning).

## Important Notes
- **Context claim corrected**: the task context describes librcsc's `Body_Dribble2008` as "the" class and asks whether helios-base's file is "the same class living in librcsc." It is **not** — it is a separate, independently-compiled, global-namespace class with an almost identical implementation (2367 vs. 2369 lines). Same conclusion for `KickTable` (2239 vs. 2241 lines): separate compiled singletons, same include guard, different namespace, zero cross-references in either direction.
- File count: ~85 estimate is close but the actual count is **88 files directly in `basic_actions/`** (87 excluding `Makefile`) **+ 2 in `obsolete/`** = 90 total.
- Do not assume "uses librcsc's KickTable" when reading helios-base call sites that say `KickTable::instance()` — always check the `#include` line (quoted local path vs. `<rcsc/...>`) to know which singleton is meant.

## See Also
- [`kick_table.h`](../../src/player/basic_actions/kick_table.h), [`kick_table.cpp`](../../src/player/basic_actions/kick_table.cpp)
- [`body_dribble2008.h`](../../src/player/basic_actions/body_dribble2008.h), [`body_dribble2008.cpp`](../../src/player/basic_actions/body_dribble2008.cpp), [`body_dribble.h`](../../src/player/basic_actions/body_dribble.h)
- [`sample-player-core.instructions.md`](./sample-player-core.instructions.md) — per-cycle caller of these behaviors
- [`strategy-roles-formations.instructions.md`](./strategy-roles-formations.instructions.md) — role classes that also construct these behaviors
- librcsc: [`action-behavior-library.instructions.md`](../../../librcsc/.github/instructions/action-behavior-library.instructions.md) — shared `AbstractAction` base pattern and librcsc's own (unused-by-helios) `KickTable`/`Body_Dribble2008`

---
Part of: [`helios-base copilot-instructions.md`](../copilot-instructions.md)
