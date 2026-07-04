---
applyTo: 'src/player/sample_player.{cpp,h}'
---
# SamplePlayer — Core Agent Decision Loop

## TL;DR
`SamplePlayer` is helios-base's concrete `rcsc::PlayerAgent` subclass that drives every on-field decision each cycle by delegating to `Strategy`/`SoccerRole` for normal play and to dedicated `Bhv_*` behaviors for set plays and emergencies.
- [sample_player.cpp](../../src/player/sample_player.cpp) is 847 lines, [sample_player.h](../../src/player/sample_player.h) is 102 lines — matches the size noted in project context.
- `actionImpl()` ([sample_player.cpp:218-316](../../src/player/sample_player.cpp:218)) is the per-cycle entry point; it updates `Strategy`/`FieldAnalyzer`, runs `doPreprocess()`, then dispatches to `SoccerRole::execute()`, `Bhv_PenaltyKick`, or `Bhv_SetPlay`.
- **Open the full file when:** you need to change preprocessing order (tackle/localization/shoot/intention checks), add a new communication planner, or wire in a new `ActionGenerator`/`FieldEvaluator`.

## Overview
`SamplePlayer` (declared in [sample_player.h](../../src/player/sample_player.h:37)) is the helios-base reference implementation of librcsc's agent extension points. It owns three strategy objects constructed in the constructor and refreshed every cycle:
- `M_communication` (`Communication::Ptr`) — say-message planning, defaults to `SampleCommunication`, swapped for `KeepawayCommunication` in `handleServerParam()` when `ServerParam::i().keepawayMode()` is set ([sample_player.cpp:465-473](../../src/player/sample_player.cpp:465)).
- `M_field_evaluator` (`FieldEvaluator::ConstPtr`) — created by `createFieldEvaluator()`, currently returns `SampleFieldEvaluator` ([sample_player.cpp:777-781](../../src/player/sample_player.cpp:777)).
- `M_action_generator` (`ActionGenerator::ConstPtr`) — built by `createActionGenerator()` as a `CompositeActionGenerator` combining shoot/pass/cross/dribble generators, each wrapped in an `ActGen_*ActionChainLengthFilter` ([sample_player.cpp:796-847](../../src/player/sample_player.cpp:796)).

The class is instantiated by `main_player.cpp` (documented separately in `entry-points-and-launch.instructions.md`) — this file only notes the entry point exists and is not duplicated here.

## Architecture
Per-cycle call chain inside `actionImpl()` (override of `rcsc::PlayerAgent::actionImpl()`, the protected hook invoked from `PlayerAgent::action()` at `player_agent.cpp:2361` in librcsc):

1. Log trainer message if present.
2. `Strategy::instance().update(world())` + `FieldAnalyzer::instance().update(world())` — refresh team-wide tactical state before any decision is made.
3. Rebuild `M_field_evaluator`/`M_action_generator` and push them into `ActionChainHolder::instance()` (per-cycle re-registration, not one-time).
4. `doPreprocess()` — short-circuit path for special situations; returns `true` if it fully handled the cycle (see below).
5. `ActionChainHolder::instance().update(world())` — recompute the best action chain for this cycle.
6. `Strategy::i().createRole(unum, world())` — obtain the `SoccerRole::Ptr` for this player's current position/situation.
7. If `role_ptr->acceptExecution(world())` returns true, or `gameMode().type() == GameMode::PlayOn`, delegate fully to `role_ptr->execute(this)`.
8. Else if in a penalty-kick mode, run `Bhv_PenaltyKick().execute(this)`.
9. Otherwise (any other stopped mode), run `Bhv_SetPlay().execute(this)`.

`doPreprocess()` ([sample_player.cpp:526-637](../../src/player/sample_player.cpp:526)) is itself an ordered decision tree, checked in this order:
1. Tackle-frozen → face ball, return true.
2. `BeforeKickOff`/`AfterGoal_` → move to strategy position via `Bhv_CustomBeforeKickOff`.
3. Invalid self position → `Bhv_Emergency` (handles its own view).
4. Ball position too stale → `Bhv_NeckBodyToBall` + `View_Tactical`.
5. Set default `View_Tactical` view action.
6. `doShoot()` — strict shoot check when kickable and not in `IndFreeKick_`.
7. `doIntention()` (inherited from `PlayerAgent`) — resume any queued multi-cycle intention.
8. `doForceKick()` — simultaneous-kick collision avoidance.
9. `doHeardPassReceive()` — react to a teammate's heard pass message.

## Patterns & Conventions
- **Behavior objects are constructed and executed inline**, never stored: `Bhv_SetPlay().execute(this)`, `Body_Intercept().execute(this)`, following librcsc's `SoccerBehavior`/`BodyAction` pattern (see librcsc's `action-behavior-library.instructions.md`).
- **View/Neck/Focus/Arm actions are set explicitly at each branch** via `setViewAction()`, `setNeckAction()` — e.g. `View_Tactical` is the default view, `Neck_TurnToBallOrScan`/`Neck_ScanField`/`Neck_TurnToBall` are chosen per situation. There is no single top-level "view manager" — each decision branch is responsible for setting its own view/neck action before returning.
- **`dlog.addText(Logger::TEAM, ...)`** calls trace every branch taken — always add a matching `dlog` line when introducing a new preprocess branch.
- **Chain-of-responsibility booleans**: `doPreprocess/doShoot/doForceKick/doHeardPassReceive` all return `bool` (handled vs. not) so `actionImpl()` and `doPreprocess()` can short-circuit with early `return`.
- Long-cycle actions register a `SoccerIntention` (e.g. `IntentionReceive` in `doHeardPassReceive()`) consumed next cycle by the inherited `doIntention()`.

## Key Abstractions
- **`Strategy`** ([strategy.h](../../src/player/strategy.h:68)) — team-wide singleton (`Strategy::instance()` / `Strategy::i()`) providing `update()`, `createRole(unum, world)` → `SoccerRole::Ptr`, `getPosition(unum)`, `getPositionType(unum)`. `SamplePlayer` calls `Strategy::instance().init()`/`.read()` during `initImpl()` and `.update()`/`.createRole()` every cycle in `actionImpl()`. Deep-dive on formation/role assignment belongs in a separate Strategy/SoccerRole-focused instruction file, not here.
- **`SoccerRole`** ([soccer_role.h](../../src/player/soccer_role.h:48)) — abstract role interface with `execute(PlayerAgent*)` and `acceptExecution(WorldModel&)`; concrete roles are looked up by name via the `rcss::Factory<Creator,std::string>` registry (`SoccerRole::creators()`/`create()`). `SamplePlayer` only consumes `SoccerRole::Ptr` returned by `Strategy::createRole()` — it does not know concrete role classes directly.
- **`FieldEvaluator` / `ActionGenerator`** — pluggable strategy objects for the action-chain search (used by `ActionChainHolder`); `SamplePlayer` supplies the default (`SampleFieldEvaluator`, composite generator) but exposes `createFieldEvaluator()`/`createActionGenerator()` as `virtual` overridable hooks.
- **`Communication` / `Communication::Ptr`** — say-message planning strategy (`SampleCommunication` default, `KeepawayCommunication` alternate), invoked from `communicationImpl()` ([sample_player.cpp:514-521](../../src/player/sample_player.cpp:514)), the librcsc-invoked communication-phase counterpart to `actionImpl()`.
- **`KickTable::instance()`** — singleton kick-solution cache; read from `config().configDir() + "/kick-table"` in `initImpl()` ([sample_player.cpp:203-208](../../src/player/sample_player.cpp:203)) and (re)built in `handlePlayerParam()` via `KickTable::instance().createTables()` ([sample_player.cpp:479-497](../../src/player/sample_player.cpp:479)) once server/player params are known.

## Integration Points
- **librcsc base class**: subclasses `rcsc::PlayerAgent`; overrides the protected virtual hooks `initImpl()`, `actionImpl()`, `communicationImpl()`, `handleActionStart()`, `handleActionEnd()`, `handleInitMessage()`, `handleServerParam()`, `handlePlayerParam()`, `handlePlayerType()`. See librcsc's `world-model-and-agent-core.instructions.md` for the base `PlayerAgent::action()` dispatch at `player_agent.cpp:2361` that calls into these overrides, and for the `Body/Neck/View/Focus/Arm` action-slot pattern (`rcsc/action/soccer_action.h`).
- **`rcsc::PlayerParam`** — the librcsc singleton for heterogeneous player-type parameters is included (`<rcsc/common/player_param.h>`) but `SamplePlayer` itself does not query it directly in this file; it is consumed indirectly through `world().self().playerType()` and by `KickTable`/`ServerParam`-dependent behaviors. `handlePlayerParam()` is the hook where `PlayerParam` values become available and is where `KickTable` construction is triggered.
- **`ServerParam::i()`** — used throughout (e.g. `ballSpeedMax()`, `maxDashPower()`, `pitchHalfLength()`, `keepawayMode()`) for simulator-wide constants.
- **Entry point**: instantiated by `main_player.cpp` — see `entry-points-and-launch.instructions.md` for process startup/CLI wiring (not duplicated here).
- **Downstream behaviors**: `Bhv_PenaltyKick`, `Bhv_SetPlay`, `Bhv_CustomBeforeKickOff`, `Bhv_Emergency`, `Bhv_StrictCheckShoot`, `Body_Intercept`, `Body_KickOneStep`, `Body_GoToPoint`, `Neck_ScanField`, `Neck_TurnToBallOrScan`, `Neck_TurnToBall`, `View_Tactical`, `IntentionReceive` — all constructed and executed directly from `SamplePlayer`.

## Build & Test
Part of the standard helios-base autotools/CMake build producing the `sample_player` binary from `src/player/*.cpp`. No dedicated unit tests exist for `SamplePlayer` itself; validation is via running full matches (`start.sh`) or the `librcsc`-side agent core tests. See the top-level `copilot-instructions.md` for the general build command.

## Logging
Uses librcsc's `dlog` (`Logger`) facility exclusively: `dlog.addText(Logger::TEAM, ...)` for decision-branch tracing and `dlog.addText(Logger::WORLD, ...)` for ball/self kinematics dumps in `handleActionEnd()` ([sample_player.cpp:404-444](../../src/player/sample_player.cpp:404)). `debugClient()` calls (`addLine`, `addMessage`, `setTarget`) feed the soccer monitor's debug overlay, not the log file.

## Important Notes
- Context estimate of ~847 lines for `sample_player.cpp` was **confirmed exact** by `read_file`/line count; `sample_player.h` is 102 lines (small — mostly declarations of protected override hooks and 4 private `do*` helpers).
- `PlayerParam` is a librcsc singleton (`<rcsc/common/player_param.h>`), not a helios-specific extension — it is included but only referenced indirectly here; deeper `PlayerParam` usage (heterogeneous player types) lives in librcsc and in role/behavior files, not in `SamplePlayer` itself.
- The deep dive on `Strategy` (formation reading, role-name-to-position mapping) and `SoccerRole` (concrete role implementations, factory registration) is intentionally out of scope for this file — see the dedicated Strategy/SoccerRole instruction file when it exists.
- `main_player.cpp` (instantiation site) is documented in a sibling instruction file (`entry-points-and-launch.instructions.md`) being written concurrently — do not duplicate that content here.

## See Also
- [`sample_player.cpp`](../../src/player/sample_player.cpp), [`sample_player.h`](../../src/player/sample_player.h)
- [`strategy.h`](../../src/player/strategy.h), [`soccer_role.h`](../../src/player/soccer_role.h)
- `entry-points-and-launch.instructions.md` (main_player.cpp, concurrent doc)
- librcsc: `world-model-and-agent-core.instructions.md`, `action-behavior-library.instructions.md`

---
Part of: [`helios-base copilot-instructions.md`](../copilot-instructions.md)
