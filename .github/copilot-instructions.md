# helios-base — Copilot Instructions

## TL;DR
helios-base is the reference RoboCup 2D client team (player/coach/trainer binaries) built on top of the `librcsc` library, which in turn talks to `rcssserver` over UDP.
- Three entry points: `src/player/main_player.cpp` → `SamplePlayer : rcsc::PlayerAgent`, `src/coach/main_coach.cpp` → `SampleCoach : rcsc::CoachAgent`, `src/trainer/main_trainer.cpp` → `SampleTrainer : rcsc::TrainerAgent`.
- Per-cycle decision-making flows through `SamplePlayer::actionImpl()` → `Strategy`/`SoccerRole` (normal play) or `Bhv_SetPlay`/`Bhv_PenaltyKick` (dead balls) or `ActionChainHolder` (multi-step planned actions).
- **Gotcha**: helios-base forks its own `KickTable`/`Body_Dribble2008`/`Body_SmartKick` in the global namespace under `src/player/basic_actions/` — these are NOT the same classes as librcsc's `rcsc::KickTable`/`rcsc::Body_Dribble2008`, despite identical names.
- **Where to go next:** see the Instruction Index below — read the scoped file for the area you're touching.

## Project Overview
helios-base is the open-source base team codebase used by the Helios RoboCup 2D Simulation League team (and many derivative teams). It provides fully working `player`, `coach`, and `trainer` agent binaries built on `librcsc`. It is the "application layer" of the ecosystem: `rcssserver` runs the match, `librcsc` provides the client SDK (network I/O, world model, action primitives, formation/CLang plumbing), and helios-base implements the actual soccer strategy — role assignment, dribble/pass/shoot decision trees, set-play behaviors, and inter-agent communication.

## Architecture
Request/decision flow through the codebase:
1. **Launch** — `main_player.cpp`/`main_coach.cpp`/`main_trainer.cpp` parse CLI args, connect via librcsc's `AbstractClient`, and hand control to the agent's `run()` loop. Launched via `start.sh.in` (normal match), `keepaway.sh.in` (keepaway training), or `train.sh.in`. See [entry-points-and-launch](.github/instructions/entry-points-and-launch.instructions.md).
2. **Per-cycle player dispatch** — `SamplePlayer::actionImpl()` is librcsc's `PlayerAgent::action()` override; it updates `Strategy`, rebuilds field-evaluator/action-generators, runs `doPreprocess()` (tackle/kickoff/recovery handling), then routes to normal play or set play. See [sample-player-core](.github/instructions/sample-player-core.instructions.md).
3. **Role & formation assignment** — `Strategy` wraps 13 `rcsc::Formation::Ptr` instances (loaded from JSON `.conf` files under `formations-dt/`) and factory-creates a `SoccerRole::Ptr` (one of 11 `role_*` classes) per cycle. See [strategy-roles-formations](.github/instructions/strategy-roles-formations.instructions.md).
4. **Basic action primitives** — `src/player/basic_actions/` (Body/Neck/View/Bhv_* classes) implement concrete skills; includes helios-base's own forked `KickTable` and `Body_Dribble2008`. See [basic-actions-behaviors](.github/instructions/basic-actions-behaviors.instructions.md).
5. **Multi-step action planning** — `ActionChainHolder`/`ActionChainGraph` in `src/player/planner/` search chains of dribble/pass/shoot candidates (best-first search, bounded depth/eval count), scored by `SampleFieldEvaluator`. See [action-chain-planner](.github/instructions/action-chain-planner.instructions.md).
6. **Set plays** — `Bhv_SetPlay` (in `src/player/setplay/`) routes dead-ball situations (free kick, goal kick, kick-in, kick-off) by `GameMode::type()`; `Bhv_PenaltyKick` (outside `setplay/`) handles penalties. See [setplay-behaviors](.github/instructions/setplay-behaviors.instructions.md).
7. **Communication** — `SampleCommunication::execute()` runs every cycle from `SamplePlayer::communicationImpl()`, building librcsc `SayMessage` subclasses; hear-side is handled independently via `AudioMemory`/`PassMessageParser`. See [communication](.github/instructions/communication.instructions.md).
8. **Coach & trainer** — `SampleCoach` reports opponent player types via freeform message (no CLang tactical advice sent in practice); `SampleTrainer` scripts offline training scenarios (keepaway uses player-side `role_keepaway_keeper/taker`, not the trainer). See [coach-and-trainer-agents](.github/instructions/coach-and-trainer-agents.instructions.md).

## Build & Test, Run
- Dual build systems: Autotools (`configure.ac` + `Makefile.am`, Autoconf ≥2.61, C++17, Boost, manual `librcsc` discovery) and CMake (`CMakeLists.txt`, CMake ≥3.5, C++17).
- Produces `sample_player`, `sample_coach`, `sample_trainer` binaries.
- Run via `src/start.sh.in` (normal match, requires a running `rcssserver`), `src/keepaway.sh.in` (keepaway training), or `src/train.sh.in` (training mode — note: some variables in this script reference stale/non-existent paths, see entry-points-and-launch notes).
- No `trainer.conf` exists; the trainer process reuses `player.conf`.

## Conventions
- Every skill/behavior class follows librcsc's effector-slot action pattern (`BodyAction`/`NeckAction`/`ViewAction`/etc. from `rcsc/action/soccer_action.h`), even though many concrete classes (`Body_GoToPoint`, `Neck_*`, `View_*`, `Bhv_*`) are helios-specific with no librcsc counterpart.
- Factory/singleton patterns are pervasive: `Strategy::instance()`, `ActionChainHolder::instance()`, `SoccerRole::creators()` registry, per-generator singletons (e.g. `StrictCheckPassGenerator::instance()`).
- Formation/config data is externalized as JSON `.conf` files (schema: `{"version","method","role":[...],"data":[...]}`), not hardcoded — read at runtime, not compiled in.
- Naming: `body_*`/`bhv_*`/`neck_*`/`view_*`/`role_*`/`actgen_*` prefixes signal the class's action-slot category.

## Instruction Index
Read the scoped instruction for the area you are working in (they are NOT auto-loaded — open them on demand):

| Instruction | Covers (applyTo) | Summary | Path |
|-------------|------------------|---------|------|
| Entry Points & Launch | `src/player/main_player.cpp`, `src/coach/main_coach.cpp`, `src/trainer/main_trainer.cpp`, launch scripts, `.conf` files | Binary entry points, launcher scripts, formation JSON schema, build system | [entry-points-and-launch.instructions.md](.github/instructions/entry-points-and-launch.instructions.md) |
| Sample Player Core | `src/player/sample_player.*` | `SamplePlayer::actionImpl()`/`doPreprocess()` per-cycle dispatch tree | [sample-player-core.instructions.md](.github/instructions/sample-player-core.instructions.md) |
| Strategy, Roles & Formations | `src/player/strategy.*`, `src/player/soccer_role.*`, `src/player/role_*.cpp`, `formations-*/**` | `Strategy` wraps `Formation`; 11 `SoccerRole` subclasses; JSON formation data | [strategy-roles-formations.instructions.md](.github/instructions/strategy-roles-formations.instructions.md) |
| Basic Actions & Behaviors | `src/player/basic_actions/**` | Body/Neck/Bhv skill classes; helios-base's own forked `KickTable`/`Body_Dribble2008` | [basic-actions-behaviors.instructions.md](.github/instructions/basic-actions-behaviors.instructions.md) |
| Action Chain Planner | `src/player/planner/**` | `ActionChainGraph` best-first search over dribble/pass/shoot generators | [action-chain-planner.instructions.md](.github/instructions/action-chain-planner.instructions.md) |
| Set-Play Behaviors | `src/player/setplay/**` | `Bhv_SetPlay` dead-ball routing by `GameMode::type()` | [setplay-behaviors.instructions.md](.github/instructions/setplay-behaviors.instructions.md) |
| Communication | `src/player/sample_communication.*` | Say/hear message scheduling via librcsc `SayMessage`/`AudioMemory` | [communication.instructions.md](.github/instructions/communication.instructions.md) |
| Coach & Trainer Agents | `src/coach/**`, `src/trainer/**` | `SampleCoach`/`SampleTrainer` — opponent-type reporting, offline training scripting | [coach-and-trainer-agents.instructions.md](.github/instructions/coach-and-trainer-agents.instructions.md) |

## Related Repositories

| Repo | Path | Role |
|------|------|------|
| librcsc | `../../librcsc` | Client SDK helios-base is built on: `SoccerAgent`/`PlayerAgent`/`CoachAgent`/`TrainerAgent` base classes, `WorldModel`, action primitives, `Formation`, CLang, RCG parsing. |
| rcssserver | `../../rcssserver` | The game server helios-base connects to (UDP 6000 players/trainer/monitor, 6002 online coach, 6032 debug); defines the protocol and referee rules. |
| rcssmonitor | `../../rcssmonitor` | Visualization client for watching matches; not used directly by helios-base but shares the RCG log format via librcsc. |
| rcsoccersim.github.io | `../../rcsoccersim.github.io` | Project documentation site. |

## Where to Look
- For anything not covered by the Instruction Index above, check `.github/instructions/` directly, or the sibling `librcsc`/`rcssserver` instruction sets for lower-level protocol/library behavior.
- No skills directory or automation metadata is maintained for this repo — instructions only.
