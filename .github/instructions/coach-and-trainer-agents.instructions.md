---
applyTo: 'src/coach/**,src/trainer/**'
---
# Coach & Trainer Agents

## TL;DR
`SampleCoach` (`src/coach/`) and `SampleTrainer` (`src/trainer/`) are helios-base's concrete subclasses of librcsc's `CoachAgent`/`TrainerAgent`. Neither sends real CLang advice.
- `sample_coach.cpp` (**759 lines**, verified) / `sample_coach.h` (**100 lines**) — the online coach: registers audio "say" message parsers, substitutes tired/first players, and sends opponent-player-type estimates as a `FreeformMessage` (`(say (freeform "..."))`), **not** a CLang `(define ...)/(rule ...)` advice message.
- `sample_trainer.cpp` (**320 lines**, verified) / `sample_trainer.h` (**74 lines**) — the offline trainer: uses `TrainerAgent` commands (`doMoveBall`, `doMovePlayer`, `doChangePlayerType`, `doRecover`, `doChangeMode`) to script training episodes; its `actionImpl()` currently only calls `doKeepaway()`, which just logs when `world().trainingTime()` is reached — it does **not** itself load formation files.
- **Open the full file when:** changing coach substitution logic, freeform-message content, or trainer episode scripting (ball/player placement, player-type randomization).

## Overview
Both agents follow the same skeleton as `SamplePlayer` (see [entry-points-and-launch.instructions.md](./entry-points-and-launch.instructions.md)): `main_coach.cpp`/`main_trainer.cpp` construct a static instance and drive it via `AbstractClient::run()`. All actual decision logic lives in `SampleCoach::actionImpl()` / `SampleTrainer::actionImpl()`.

**`SampleCoach`** (`src/coach/sample_coach.h:41-98`) subclasses `rcsc::CoachAgent`. Its constructor registers ~19 `SayMessageParser` subclasses (`BallMessageParser`, `PassMessageParser`, `InterceptMessageParser`, `GoalieMessageParser`, etc., all backed by a shared `AudioMemory`) so the coach can decode teammates' "say" hear-messages into its `CoachWorldModel` (the same `CoachWorldModel` class librcsc gives both coach and trainer — full-field `(see_global)` snapshot, no partial-observation fusion; see librcsc docs). `actionImpl()` (`sample_coach.cpp:196-210`) each cycle: updates the debug client, sends the team graphic once at cycle 0 if configured, then calls `doSubstitute()` and `sayPlayerTypes()`.

**`SampleTrainer`** (`src/trainer/sample_trainer.h:32-72`) subclasses `rcsc::TrainerAgent`. `actionImpl()` (`sample_trainer.cpp:108-124`) first calls `doTeamNames()` until team names are known, then — with `sampleAction()`/`recoverForever()`/`doSubstitute()` left commented out — calls only `doKeepaway()`. The other three methods remain as reference implementations for episodic training (ball-interception drills, periodic stamina recovery, random player-type substitution) but are dead code in the shipped build.

## Architecture
```
CoachAgent (librcsc)          TrainerAgent (librcsc)
      |                              |
 SampleCoach                   SampleTrainer
      |                              |
 doSubstitute()                doKeepaway() (active)
 sayPlayerTypes()              sampleAction() / recoverForever() / doSubstitute() (commented out)
 sendTeamGraphic()
      |
 OpponentPlayerTypeMessage : FreeformMessage
 (sample_freeform_message.{h,cpp}) -> addFreeformMessage() -> "(say (freeform ...))"
```
Both agents share librcsc's single `CoachWorldModel` (full global state via `(see_global)`), so there is no coach-vs-trainer world-model split — only the *commands* each is allowed to issue differ (`coach_command.h` substitution/team-graphic/say vs. `trainer_command.h` move-ball/move-player/change-mode/change-player-type/recover).

## Patterns & Conventions
- **Freeform messages, not CLang advice.** `sayPlayerTypes()` (`sample_coach.cpp:646-725`) builds an `OpponentPlayerTypeMessage` (`sample_freeform_message.h/.cpp`) — a `rcsc::FreeformMessage` subclass encoding `"(player_types (1 id)(2 id)...)"` — and hands it to `addFreeformMessage()`, gated by `config().useFreeform()` and `world().canSendFreeform()`. This is the coach `say` freeform channel (protocol v13+), a **different, simpler mechanism** than the CLang `(define)/(rule)/(del)` advice grammar documented for rcssserver/librcsc. `SampleCoach` never calls any CLang-builder API (no `doAdvice`, no `CoachWorldModel::M_clang_capacity` usage) — it is effectively a **passive CLang non-user**: it observes and logs, but its only outbound "message" content is this freeform opponent-type report plus team-graphic tiles and player substitutions.
- **Player-type analysis loop.** `sayPlayerTypes()` diffs `world().theirPlayerTypeId(unum)` against cached `M_opponent_player_types[11]` each cycle; only sends a freeform message when at least one opponent's estimated type changed (`analyzed_count > 0`), avoiding redundant messages.
- **Substitution heuristics.** `doFirstSubstitute()` (`sample_coach.cpp:285-...`) ranks candidate `PlayerType`s by `realSpeedMax()`/`cyclesToReachMaxSpeed()` (via `RealSpeedMaxCmp`) and assigns them to a fixed uniform-number priority order (wing players first); `doSubstituteTiredPlayers()` runs only during stopped play (not `PlayOn`, not penalty-kick mode).
- **Trainer episode scripting idiom** (see disabled `sampleAction()`, `sample_trainer.cpp:170-247`): a `static int s_state` finite-state machine driving `doRecover()` → `doMoveBall()` → `doChangeMode(PM_PlayOn)` → `doMovePlayer()` → `doChangePlayerType()` → wait a few cycles → re-launch ball with randomized velocity (`UniformReal`/`UniformInt` from `rcsc/random.h`). This is the template to copy for any new offline-training scenario.
- **`doKeepaway()` is a stub, not an integration.** `sample_trainer.cpp:310-320` only prints a message when `world().trainingTime() == world().time()`; it does **not** read `formations-keeper`/`formations-taker` or call any formation API. Formation loading for keepaway is entirely player-side.

## Key Abstractions
- **`SampleCoach`** (`src/coach/sample_coach.{h,cpp}`) — `M_opponent_player_types[11]` (cached type-id guesses), `M_team_graphic` (`rcsc::TeamGraphic`, loaded from `team_logo.xpm` or a configured file).
- **`SampleTrainer`** (`src/trainer/sample_trainer.{h,cpp}`) — stateless wrapper; all state for the (disabled) episode FSM lives in function-local `static` variables inside `sampleAction()`/`doSubstitute()`.
- **`OpponentPlayerTypeMessage`** (`src/coach/sample_freeform_message.{h,cpp}`) — `rcsc::FreeformMessage` subclass; `buildMessage()`/`append()` produce the `"(player_types ...)"` payload; `M_player_type_id[11]` mirrors the coach's cache.
- **`rcsc::CoachWorldModel`** (librcsc, shared by both agents) — see [../../../librcsc/.github/instructions/coach-trainer-clang.instructions.md](../../../librcsc/.github/instructions/coach-trainer-clang.instructions.md) for the full model, including `theirPlayerTypeId()`, `trainingTime()`, `canSendFreeform()`, and the real (unused-by-helios) CLang rate-limiter `M_clang_capacity`.

## Integration Points
- **librcsc `CoachAgent`/`TrainerAgent`** — base classes; both ultimately derive from `SoccerAgent`. `CoachAgent` exposes `doSubstitute`, `doTeamGraphic`, `addFreeformMessage`, `addSayMessageParser`; `TrainerAgent` exposes `doTeamNames`, `doRecover`, `doMoveBall`, `doMovePlayer`, `doChangeMode`, `doChangePlayerType`, `doSay`. See [../../../librcsc/.github/instructions/coach-trainer-clang.instructions.md](../../../librcsc/.github/instructions/coach-trainer-clang.instructions.md).
- **CLang grammar (rcssserver)** — `SampleCoach` never emits `(define)`/`(rule)`/`(del)` CLang advice, so the server-side CLang parser documented in [../../../rcssserver/.github/instructions/coach-language-clang.instructions.md](../../../rcssserver/.github/instructions/coach-language-clang.instructions.md) is effectively unexercised by this codebase's shipped coach — it only matters if a developer extends `SampleCoach` to call the CLang builder APIs directly.
- **Keepaway formations** — `formations-keeper`/`formations-taker` (see [entry-points-and-launch.instructions.md](./entry-points-and-launch.instructions.md)) are loaded by `sample_player` processes via `--config_dir`, spawned by `keepaway.sh.in` alongside `sample_trainer`; the trainer process itself never opens these directories (confirmed: `grep` for `formations-` in `src/trainer/` finds no matches beyond incidental doc references).
- **`sample_player`** — `role_keepaway_keeper.cpp`/`role_keepaway_taker.cpp` and `keepaway_communication.{h,cpp}` (in `src/player/`) implement the actual keepaway behavior/formation consumption that the trainer merely times via `doKeepaway()`.

## Build & Test
- Both built via the same Autotools/CMake dual system as the rest of helios-base (`src/coach/CMakeLists.txt` / `Makefile.am` → `add_executable(sample_coach ...)`; `src/trainer/CMakeLists.txt` / `Makefile.am` → `add_executable(sample_trainer ...)`). See [entry-points-and-launch.instructions.md](./entry-points-and-launch.instructions.md) for the full build flow.
- No automated tests for either agent; validation is via `keepaway.sh` (spawns trainer + keeper/taker players) or `start.sh` (spawns coach + regular players) against a live rcssserver.

## Logging
- `SampleCoach` prints substitution diagnostics to `stderr` (player-type speed table in `doFirstSubstitute()`) and freeform-send/team-graphic-send confirmations to `stdout` (`sample_coach.cpp:720-724`, `753-757`), plus per-cycle debug-client annotations (`debugClient().addMessage("Cycle=%ld", ...)`).
- `SampleTrainer` prints episode-state transitions to `stdout`/`stderr` in the disabled `sampleAction()`/`doSubstitute()` reference code, and the active `doKeepaway()` logs `"trainer: ... keepaway training time."` to `stderr` when `world().trainingTime()` fires.
- Both share librcsc's `debug_*` log-category flags via `.conf` files (see entry-points doc); the coach also participates in the graphical debug protocol via `CoachDebugClient` (`coach_debug_client.h`).

## Important Notes
- **Line-count correction:** the task context's estimate of "~759 lines" for `sample_coach.cpp` was **verified correct** via `read_file` (759 lines exactly; header is 100, not the assumed-similar size). However a naive `Get-Content | Measure-Object -Line` shell count reported 631/53 for `sample_coach.cpp`/`sample_trainer.h` — **lower than the true line count** returned by `read_file` (759/74) for these two files, likely due to how the shell counts trailing/blank lines on this file set. Always trust `read_file`'s reported total, not shell line-count estimates, for these files.
- **`sample_trainer.cpp` is 320 lines / `sample_trainer.h` is 74 lines** (verified via `read_file`), not the ~257/53 a shell estimate suggested.
- **`SampleCoach` does not send CLang advice.** This directly narrows the sibling librcsc note that "coaches only BUILD outgoing CLang" (`coach_agent.cpp:1715`) — that capability exists in librcsc, but helios-base's shipped `SampleCoach` never invokes it. Its only outbound coach-channel content is the freeform `"(player_types ...)"` report, team-graphic tiles, and substitute commands.
- **`SampleTrainer::doKeepaway()` does not touch formation files.** The task context's suggestion that the trainer "interacts with" `formations-keeper`/`formations-taker` is **not supported by the code** — those directories are passed only to `sample_player` (`--config_dir`) by `keepaway.sh.in`; the trainer binary receives no formation directory argument and `doKeepaway()`'s body is a single time-check + log line.
- `sampleAction()`, `recoverForever()`, and `doSubstitute()` in `SampleTrainer` are fully-implemented but **dead code** (commented out in `actionImpl()`) — useful reference templates for anyone building a new offline-training scenario (e.g. a real `formations-train` drill, per the entry-points doc's note that `train.sh.in`'s `formations-train` directory doesn't exist).

## See Also
- [./entry-points-and-launch.instructions.md](./entry-points-and-launch.instructions.md) — `main_coach.cpp`/`main_trainer.cpp`, launch scripts, formation directory wiring.
- [../../../librcsc/.github/instructions/coach-trainer-clang.instructions.md](../../../librcsc/.github/instructions/coach-trainer-clang.instructions.md) — shared `CoachWorldModel`, CLang builder location, real rate-limiter (`M_clang_capacity`).
- [../../../rcssserver/.github/instructions/coach-language-clang.instructions.md](../../../rcssserver/.github/instructions/coach-language-clang.instructions.md) — server-side CLang grammar/parser (not exercised by helios-base's shipped coach).

---
Part of: [`helios-base copilot-instructions.md`](../copilot-instructions.md)
