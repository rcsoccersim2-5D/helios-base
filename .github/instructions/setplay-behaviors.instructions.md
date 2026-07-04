---
applyTo: 'src/player/setplay/*.{cpp,h}'
---
# Set-Play (Dead-Ball) Behaviors

## TL;DR
`src/player/setplay/` holds the field-player dead-ball behaviors dispatched from `SamplePlayer::actionImpl()` whenever `GameMode::type() != PlayOn`. `Bhv_SetPlay` ([bhv_set_play.cpp](../../src/player/setplay/bhv_set_play.cpp), 693 lines) is the **top-level set-play dispatcher**: it inspects `wm.gameMode().type()`/`.side()` and routes to one situation-specific behavior class per dead-ball type. Penalty kicks are handled separately by `Bhv_PenaltyKick` (`src/player/bhv_penalty_kick.{cpp,h}` — **not** in this directory), called directly from `sample_player.cpp` before `Bhv_SetPlay` is ever reached.
- Directory contains **20 files** (10 `.cpp`/`.h` pairs) + a `Makefile` — not 19; see full list below.
- Goalie dead-ball handling (`Bhv_GoalieFreeKick`, `bhv_goalie_*`) and `Bhv_PenaltyKick` live in `src/player/` root, sibling to this directory, and are only *called from* `Bhv_SetPlay`.
- **Open the full file when:** adding a new dead-ball situation type, changing kicker-selection logic (`Bhv_SetPlay::is_kicker`), or tuning shared helpers (`get_avoid_circle_point`, `get_set_play_dash_power`).

## Overview
Files in `src/player/setplay/` (verified via `read_file`/directory listing):

| File (lines .cpp/.h) | Class | Situation |
|---|---|---|
| bhv_set_play.cpp/h (693/63) | `Bhv_SetPlay` | Top-level dispatcher for all non-PlayOn, non-penalty modes |
| bhv_set_play_kick_off.cpp/h (284/53) | `Bhv_SetPlayKickOff` | Our `KickOff_` |
| bhv_set_play_kick_in.cpp/h (447/50) | `Bhv_SetPlayKickIn` | Our `KickIn_` and our `CornerKick_` (shared handler) |
| bhv_set_play_goal_kick.cpp/h (533/57) | `Bhv_SetPlayGoalKick` | Our `GoalKick_` |
| bhv_their_goal_kick_move.cpp/h (245/47) | `Bhv_TheirGoalKickMove` | Opponent `GoalKick_` (we must retreat) |
| bhv_set_play_indirect_free_kick.cpp/h (658/52) | `Bhv_SetPlayIndirectFreeKick` | `BackPass_`, `IndFreeKick_`, and near-goal `FoulCharge_`/`FoulPush_`; also goalie's own indirect free kick |
| bhv_set_play_free_kick.cpp/h (403/52) | `Bhv_SetPlayFreeKick` | Fallback "our set play" (direct free kick / other set-play types reaching the `isOurSetPlay()` catch-all) |
| bhv_prepare_set_play_kick.cpp/h (102/54) | `Bhv_PrepareSetPlayKick` | Shared helper: walk to ball, face `M_ball_place_angle`, wait `M_wait_cycle` before kicking — used by the kick-off/kick-in/goal-kick/free-kick behaviors |
| bhv_go_to_placed_ball.cpp/h (119/46) | `Bhv_GoToPlacedBall` | Shared helper: move to the placed/restarted ball position at a given angle |
| intention_wait_after_set_play_kick.cpp/h (93/48) | `IntentionWaitAfterSetPlayKick` | Multi-cycle `SoccerIntention` queued after the set-play kick executes, consumed by `PlayerAgent::doIntention()` next cycle |

**No dedicated "corner kick" or "penalty kick" file exists in this directory**: corner kicks share `Bhv_SetPlayKickIn` (see `GameMode::KickIn_`/`GameMode::CornerKick_` case together in `Bhv_SetPlay::execute`), and penalty kicks are handled entirely outside this directory by `Bhv_PenaltyKick`. Goalie-specific free-kick/catch handling (`Bhv_GoalieFreeKick`) also lives outside this directory (`src/player/bhv_goalie_free_kick.{cpp,h}`) but is invoked from `Bhv_SetPlay::execute()`.

## Architecture
Dispatch chain, confirmed by reading [sample_player.cpp:300-315](../../src/player/sample_player.cpp:300) and [bhv_set_play.cpp:73-188](../../src/player/setplay/bhv_set_play.cpp:73):

1. `SamplePlayer::actionImpl()` — after `role_ptr->acceptExecution()`/`PlayOn` check fails:
   - `if ( world().gameMode().isPenaltyKickMode() )` → `Bhv_PenaltyKick().execute(this); return;` (sample_player.cpp:304-309).
   - else → `Bhv_SetPlay().execute(this);` (sample_player.cpp:315) — the catch-all for every other non-PlayOn mode.
2. `Bhv_SetPlay::execute()` (bhv_set_play.cpp:74-188) is a two-stage router:
   - **Goalie branch first**: `if ( wm.self().goalie() )` → `Bhv_GoalieFreeKick` unless mode is `BackPass_`/`IndFreeKick_`, in which case `Bhv_SetPlayIndirectFreeKick` (goalies also take indirect free kicks near their own goal). Returns immediately — goalies never fall through to the `switch` below.
   - **Field-player `switch ( wm.gameMode().type() )`**:
     - `KickOff_` → `Bhv_SetPlayKickOff` if `gameMode().side() == ourSide()`, else `doBasicTheirSetPlayMove()`.
     - `KickIn_` / `CornerKick_` (same case) → `Bhv_SetPlayKickIn` if ours, else `doBasicTheirSetPlayMove()`.
     - `GoalKick_` → `Bhv_SetPlayGoalKick` if ours, else `Bhv_TheirGoalKickMove`.
     - `BackPass_` / `IndFreeKick_` → always `Bhv_SetPlayIndirectFreeKick` (side-agnostic — the mode itself only occurs for the offending/awarded team's context).
     - `FoulCharge_` / `FoulPush_` → `Bhv_SetPlayIndirectFreeKick` **only** if the ball is inside either penalty area (else falls through to the default).
     - `default` → falls through to the final `if ( wm.gameMode().isOurSetPlay( wm.ourSide() ) )` check: ours → `Bhv_SetPlayFreeKick` (direct free kick / anything else); theirs → `doBasicTheirSetPlayMove()`.
3. Whichever concrete `Bhv_SetPlay*` behavior runs typically composes `Bhv_PrepareSetPlayKick`/`Bhv_GoToPlacedBall` for positioning and queues an `IntentionWaitAfterSetPlayKick` intention after the actual kick, so the kicker holds position for a few cycles post-kick before normal play resumes.

## Patterns & Conventions
- **Goalie-first branching**: `Bhv_SetPlay::execute()` always checks `wm.self().goalie()` before the mode `switch` — goalie dead-ball logic (`Bhv_GoalieFreeKick`) is entirely separate from field-player logic and is not itself in `setplay/`.
- **Side check via `gameMode().side() == wm.ourSide()`**: every mode case distinguishes "it's our restart" vs "opponent's restart" — our side gets an active kick/position behavior, opponent's side gets the generic `doBasicTheirSetPlayMove()` (private helper in `Bhv_SetPlay`) which just retreats teammates to legal/tactical positions.
- **Shared positioning helpers factored out**: `Bhv_PrepareSetPlayKick` and `Bhv_GoToPlacedBall` are reused across `Bhv_SetPlayKickOff`, `Bhv_SetPlayKickIn`, `Bhv_SetPlayGoalKick`, `Bhv_SetPlayFreeKick`, and `Bhv_SetPlayIndirectFreeKick` rather than duplicated — confirmed via `grep` showing all five `.cpp` files include/construct them.
- **Static helpers on `Bhv_SetPlay` itself** (`get_avoid_circle_point`, `get_set_play_dash_power`, `is_kicker`, `is_delaying_tactics_situation`) are called by the sibling situation behaviors, not just internally — `Bhv_SetPlay` doubles as a small utility library for the whole directory.
- **`dlog.addText(Logger::TEAM, ...)`** traces every branch, same convention as `SamplePlayer` (see [sample-player-core.instructions.md](./sample-player-core.instructions.md)).
- Dead code guarded by `#if 0` (bhv_set_play.cpp:163-170) lists modes intentionally *not* separately handled (`FreeKick_`, second `CornerKick_` duplicate, `GoalieCatch_`, `Offside_`, `FreeKickFault_`, `CatchFault_`) — these fall through to the `isOurSetPlay()` default.

## Key Abstractions
- **`Bhv_SetPlay`** ([bhv_set_play.h:37](../../src/player/setplay/bhv_set_play.h:37)) — the router; `execute(PlayerAgent*)` plus static utilities used across the directory.
- **`Bhv_SetPlay::is_kicker()`** (bhv_set_play.cpp:422) — determines whether `agent->world().self()` is the designated kicker (nearest teammate to ball, with hysteresis against a second-nearest candidate) vs. a support player; used by the concrete kick behaviors to decide "kick" vs. "get into position."
- **`Bhv_PrepareSetPlayKick`** (bhv_prepare_set_play_kick.h:33) — takes `(AngleDeg ball_place_angle, int wait_cycle)`; wraps `Bhv_GoToPlacedBall` plus a wait state before the kicker is allowed to kick.
- **`IntentionWaitAfterSetPlayKick`** (intention_wait_after_set_play_kick.h:35) — a `SoccerIntention` (librcsc pattern, see [infrastructure-utils.instructions.md](../../../librcsc/.github/instructions/infrastructure-utils.instructions.md) if applicable) queued post-kick, resumed via `PlayerAgent::doIntention()` from `SamplePlayer::doPreprocess()`.
- **`GameMode`** (librcsc) — supplies `.type()` (client-side enum: `PlayOn`, `KickOff_`, `KickIn_`, `CornerKick_`, `GoalKick_`, `BackPass_`, `IndFreeKick_`, `FoulCharge_`, `FoulPush_`, `GoalieCatch_`, `PenaltySetup_`, etc.), `.side()`, `.isOurSetPlay(SideID)`, `.isPenaltyKickMode()`, and `.getServerPlayMode()` which bridges to the server's canonical `PlayMode` — see librcsc's [infrastructure-utils.instructions.md](../../../librcsc/.github/instructions/infrastructure-utils.instructions.md) and rcssserver's [referee-rules.instructions.md](../../../rcssserver/.github/instructions/referee-rules.instructions.md) for the authoritative `PlayMode` state machine this all mirrors.

## Integration Points
- **Called from**: `SamplePlayer::actionImpl()` ([sample_player.cpp:315](../../src/player/sample_player.cpp:315)) — see [sample-player-core.instructions.md](./sample-player-core.instructions.md) for the full per-cycle dispatch chain.
- **Calls out to**: `Bhv_GoalieFreeKick`, `Strategy::i().getPosition()` (home positions for retreat/support), `ServerParam::i()` (penalty-area/pitch geometry), librcsc `Body_GoToPoint`/basic actions, `Neck_ScanField`/`Neck_TurnToBallOrScan`, `View_Tactical`.
- **Sibling behaviors outside this directory**: `Bhv_PenaltyKick` (`src/player/bhv_penalty_kick.{cpp,h}`) and goalie-specific `bhv_goalie_free_kick.{cpp,h}`, `bhv_goalie_basic_move.{cpp,h}`, `bhv_goalie_chase_ball.{cpp,h}` — invoked either directly from `sample_player.cpp` (penalty) or from `Bhv_SetPlay` (goalie free kick).

## Build & Test
Compiled as part of `src/player`'s standard autotools/CMake build (own `Makefile` present in the directory, folded into the `sample_player` binary). No dedicated unit tests; validated via full-match runs. See the top-level `copilot-instructions.md` for the general build command.

## Logging
Uses librcsc's `dlog` (`Logger::TEAM`) exclusively, consistent with `SamplePlayer` and the rest of the behavior library — every dispatch branch and kicker-selection decision is traced with `dlog.addText`/`addCircle`/`addLine`/`addMessage` for the soccer monitor's debug overlay.

## Important Notes
- **Context correction**: the task context's "~19 files" estimate is close but not exact — actual count is **20 files** (10 pairs) + `Makefile` = 21 filesystem entries. There is no separate `bhv_penalty_kick.*` or `bhv_corner_kick.*` inside `setplay/` — penalty kicks are fully external, and corner kicks share `Bhv_SetPlayKickIn`.
- **Confirmed**: `Bhv_SetPlay` is indeed the top-level set-play dispatcher, matching the sibling Worker's finding about `sample_player.cpp`'s non-PlayOn dispatch; `Bhv_PenaltyKick` is checked *first* via `GameMode::isPenaltyKickMode()`, before `Bhv_SetPlay` is even constructed — so `Bhv_SetPlay` never needs to special-case penalty kicks itself.
- **Correction to assumed routing mechanism**: routing is not literally through `GameMode::getServerPlayMode()` inside `Bhv_SetPlay` — `Bhv_SetPlay::execute()` switches on the client-side `GameMode::type()` enum directly (`KickOff_`, `KickIn_`, `CornerKick_`, `GoalKick_`, `BackPass_`, `IndFreeKick_`, `FoulCharge_`, `FoulPush_`) plus `isOurSetPlay()`/`isPenaltyKickMode()` helper methods. `getServerPlayMode()` exists on `GameMode` (librcsc) for converting back to the server's `PlayMode` wire value, but is not called anywhere in this dispatch path — the client-side `type()` enum alone is sufficient for behavior routing.
- Goalies bypass the mode `switch` entirely (checked first in `Bhv_SetPlay::execute()`), so any new dead-ball mode added to the `switch` only affects non-goalie field players unless also added to the goalie branch.

## See Also
- [`bhv_set_play.cpp`](../../src/player/setplay/bhv_set_play.cpp), [`bhv_set_play.h`](../../src/player/setplay/bhv_set_play.h)
- [`sample-player-core.instructions.md`](./sample-player-core.instructions.md) — `actionImpl()` dispatch into this directory
- librcsc: [`infrastructure-utils.instructions.md`](../../../librcsc/.github/instructions/infrastructure-utils.instructions.md) — `GameMode` client-side wrapper
- rcssserver: [`referee-rules.instructions.md`](../../../rcssserver/.github/instructions/referee-rules.instructions.md) — canonical `PlayMode` state machine
- `src/player/bhv_penalty_kick.{cpp,h}`, `src/player/bhv_goalie_free_kick.{cpp,h}` (penalty/goalie handlers, outside this directory)

---
Part of: [`helios-base copilot-instructions.md`](../copilot-instructions.md)
