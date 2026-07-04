---
applyTo: "src/player/planner/**"
---

# Action-Chain Planner (`src/player/planner/`)

## TL;DR

The `planner/` directory implements helios-base's **cooperative action-chain search**: for each cycle it generates candidate multi-step sequences (dribble → pass → shoot chains among teammates), evaluates each resulting field state with a heuristic, and keeps the best chain so behaviors can execute its first action. Entry point: `ActionChainHolder::update()`, called every cycle from `SamplePlayer::actionImpl()` (confirmed at [sample_player.cpp:259](../../src/player/sample_player.cpp:259)). The directory has **72 files total**, of which **15** use the `actgen_*` prefix (not ~60 — see Important Notes).

## Overview

- Directory: `D:\workspace\robo\ss2d\helios-base\src\player\planner\`
- Core search engine: [action_chain_graph.cpp](../../src/player/planner/action_chain_graph.cpp) — **827 lines** (verified).
- Field/geometry helpers: [field_analyzer.cpp](../../src/player/planner/field_analyzer.cpp) — **1320 lines** (verified), header **268 lines**.
- Concrete pass-course search: [strict_check_pass_generator.cpp](../../src/player/planner/strict_check_pass_generator.cpp) — **1809 lines** (verified).
- Other generator engines (candidate-course producers, not `actgen_*`): `self_pass_generator.cpp` (645), `short_dribble_generator.cpp` (707), `shoot_generator.cpp` (686), `cross_generator.cpp` (650), `clear_generator.cpp`, `tackle_generator.cpp`.
- `actgen_*` adapter files (15 total: 7 `.cpp` + 8 `.h`, one header-only): `actgen_shoot`, `actgen_strict_check_pass`, `actgen_cross`, `actgen_direct_pass`, `actgen_self_pass`, `actgen_short_dribble`, `actgen_simple_dribble`, plus the header-only filter `actgen_action_chain_length_filter.h`.

## Architecture

Two-layer generator design:

1. **`*_generator.*` classes** (singletons, e.g. `StrictCheckPassGenerator::instance()`) do the heavy geometric/tactical search: they scan teammates, ball trajectories, opponent interception risk, etc., and produce a `std::vector<CooperativeAction::Ptr>` of concrete candidate courses (a pass, a dribble, a shoot) via a `courses(wm)` method (see [strict_check_pass_generator.h:116](../../src/player/planner/strict_check_pass_generator.h:116)).
2. **`ActGen_*` classes** ([action_generator.h](../../src/player/planner/action_generator.h)) implement the abstract `ActionGenerator::generate()` interface. Each wraps one `*_generator` singleton, converts its `CooperativeAction` results into `ActionStatePair` (action + resulting `PredictState`), and appends them to the search frontier — see [actgen_strict_check_pass.cpp:48-101](../../src/player/planner/actgen_strict_check_pass.cpp:48).

`CompositeActionGenerator` combines multiple `ActGen_*` instances (decorated with `ActGen_MaxActionChainLengthFilter` / `ActGen_RangeActionChainLengthFilter` from `actgen_action_chain_length_filter.h` to restrict at which chain depth each generator fires). This composite tree is built once in `SamplePlayer::createActionGenerator()` ([sample_player.cpp:796-846](../../src/player/sample_player.cpp:796)), registering: shoot (chain length ≥2), strict-check pass (length 1 only), cross (length 1), short dribble (length 1), self-pass/long-dribble (length 1). Direct-pass and simple-dribble generators exist but are commented out in the default build.

**Search loop** — `ActionChainGraph` ([action_chain_graph.h](../../src/player/planner/action_chain_graph.h), [.cpp:827 lines](../../src/player/planner/action_chain_graph.cpp)):
- `calculate(wm)` → `calculateResult(wm)` picks one of two strategies: `calculateResultBestFirstSearch()` (default, priority-queue based, expands the highest-evaluated partial chain first via `std::priority_queue`) or the recursive `calculateResultChain()` → `doSearch()` (depth-first, mainly kept for comparison/debug builds).
- Each search step calls the `ActionGenerator` composite to expand the frontier from the current `PredictState`, applies the `FieldEvaluator` to score the resulting state, and bounds the search with `DEFAULT_MAX_CHAIN_LENGTH = 4` and `DEFAULT_MAX_EVALUATE_LIMIT = 500` evaluated nodes ([action_chain_graph.cpp:59-60](../../src/player/planner/action_chain_graph.cpp:59)).
- The highest-evaluation complete chain found is kept in `M_result` (`std::vector<ActionStatePair>`); `M_best_evaluation` tracks its score.

**Evaluation** — `FieldEvaluator` ([field_evaluator.h](../../src/player/planner/field_evaluator.h)) is an abstract functor: `operator()(const PredictState&, const std::vector<ActionStatePair>& path) -> double`. `SamplePlayer::createFieldEvaluator()` supplies `SampleFieldEvaluator` (defined in `sample_player.cpp`), which scores predicted ball position, ball-holder safety, and the goal-scoring potential of the terminal state.

**Holding/caching** — `ActionChainHolder` (singleton, [action_chain_holder.h](../../src/player/planner/action_chain_holder.h)) stores the current `FieldEvaluator`/`ActionGenerator` and the resulting `ActionChainGraph`. `update()` is idempotent per cycle: it compares `wm.time()` plus the evaluator/generator pointers against cached static values and skips recomputation if unchanged ([action_chain_holder.cpp:119-138](../../src/player/planner/action_chain_holder.cpp:119)).

**Consumption** — `Bhv_PlannedAction` ([bhv_planned_action.cpp](../../src/player/planner/bhv_planned_action.cpp), 638 lines) reads `ActionChainHolder::i().graph().getFirstAction()`/`getFirstState()` and dispatches to concrete behaviors: `Bhv_PassKickFindReceiver`, `Bhv_NormalDribble`, `Body_ForceShoot`, plus `basic_actions/kick_table.h` (helios-base's own `KickTable`, not librcsc's — see `basic-actions-behaviors.instructions.md`) and `basic_actions/body_*` primitives.

**FieldAnalyzer** ([field_analyzer.h](../../src/player/planner/field_analyzer.h)/`.cpp`, 268/1320 lines) is a stateless-ish helper singleton providing shared geometry: Voronoi diagrams (`M_all_players_voronoi_diagram`, `M_teammates_voronoi_diagram`, `M_pass_voronoi_diagram`), interception/reach-time estimates, and other spatial queries used by `*_generator` classes to decide whether a pass/dribble/shoot course is viable.

## Patterns & Conventions

- **Singleton generators**: `*_generator` classes expose `static X& instance()` and cache per-cycle results keyed by `WorldModel::time()`, mirroring `ActionChainHolder`'s own caching.
- **Decorator generators**: `ActGen_MaxActionChainLengthFilter` / `ActGen_RangeActionChainLengthFilter` wrap another `ActionGenerator*` to gate it by current chain depth (`path.size()`), rather than each `ActGen_*` self-checking depth (though `ActGen_StrictCheckPass::generate` still early-returns on `!path.empty()` for a first-action-only generator).
- **Action representation**: `CooperativeAction` ([cooperative_action.h](../../src/player/planner/cooperative_action.h), 230 lines) is the common value type for all generated actions — has `ActionCategory` enum (`Hold, Dribble, Pass, Shoot, Clear, Move`), target unum/point, kick/turn/dash counts, and a `M_final_action` flag marking chain-terminal actions (typically shoot).
- **State prediction**: `PredictState` advances ball/player positions by `duration_step` without touching the real `WorldModel` — the search explores a simulated future, never mutates real state.
- GNU GPL/LGPL headers and Doxygen-style `/*! \brief */` comments are used consistently across the directory (same convention as librcsc).

## Key Abstractions

| Class | File | Role |
|---|---|---|
| `ActionChainHolder` | action_chain_holder.h/cpp | Per-cycle singleton cache; owns the current `ActionChainGraph` |
| `ActionChainGraph` | action_chain_graph.h/cpp (827 ln) | Search engine: best-first / DFS chain search |
| `ActionGenerator` / `CompositeActionGenerator` | action_generator.h | Abstract + composite generator interface |
| `ActGen_*` (15 files: 7 .cpp + 8 .h) | actgen_*.cpp/h | Adapters: `*_generator` singleton → `ActionStatePair` |
| `FieldEvaluator` | field_evaluator.h | Abstract scoring functor for a `PredictState` |
| `CooperativeAction` | cooperative_action.h | Value type for one candidate action (pass/dribble/shoot/etc.) |
| `PredictState` | predict_state.h/cpp | Simulated future world state |
| `FieldAnalyzer` | field_analyzer.h/cpp (1320 ln) | Shared spatial analysis (Voronoi, interception) |
| `StrictCheckPassGenerator` | strict_check_pass_generator.h/cpp (1809 ln) | Exhaustive geometric pass-course search |
| `Bhv_PlannedAction` | bhv_planned_action.h/cpp (638 ln) | Executes the first action of the chosen chain |

## Integration Points

- **Entry point**: `SamplePlayer::actionImpl()` ([sample_player.cpp:259](../../src/player/sample_player.cpp:259)) calls `ActionChainHolder::instance().update(world())` every cycle — confirmed exactly as flagged by `sample-player-core.instructions.md`.
- **Wiring**: `SamplePlayer::createFieldEvaluator()` / `createActionGenerator()` ([sample_player.cpp:778-846](../../src/player/sample_player.cpp:778)) build the `SampleFieldEvaluator` and the `CompositeActionGenerator` tree once per `init()`/`handleInit()`, then re-register them on `ActionChainHolder` before each `update()`.
- **librcsc**: `PredictState`, `CooperativeAction` and the generators build on `rcsc::WorldModel`, `rcsc::PlayerObject`, `rcsc::VoronoiDiagram` and geometry types from librcsc. Final low-level execution (kick/dash/turn) happens via helios-base's own `basic_actions/` primitives, not directly via librcsc's `Body_*` classes — see `./basic-actions-behaviors.instructions.md` for the fork details (local `KickTable`, `Body_Dribble2008`, `Body_SmartKick`).
- **Action primitives**: The action-behavior vocabulary these chains ultimately compose down to originates in librcsc's `rcsc/action/soccer_action.h` — see `../../../librcsc/.github/instructions/action-behavior-library.instructions.md`.

## Build & Test

- Built as part of the `helios-base` player binary via the top-level `Makefile.am`/`configure.ac`; `planner/Makefile` (Automake) lists all sources including every `actgen_*.cpp`.
- No dedicated unit tests found in this directory; validation is via full-game simulation (`rcssserver`) and `dlog` (`Logger::ACTION_CHAIN`) debug output.

## Logging

- Uses librcsc's `dlog` with `Logger::ACTION_CHAIN` category throughout `action_chain_graph.cpp`, `actgen_*.cpp`, and generator files — logs evaluated points, best-chain updates, and per-course rejection reasons (compiled out by default; guarded by `ACTION_CHAIN_DEBUG`/`DEBUG_PRINT` macros).
- `ActionChainGraph::write_chain_log()` writes chain summaries; `debug_send_chain()` sends the chain to the monitor for visualization.

## Important Notes

- **Flag — factual correction**: the task context stated "~60 `actgen_*` files"; the actual count is **15** (7 `.cpp` + 8 `.h`). The directory has 72 files total in all categories (behaviors, generators, checkers, etc.), which may be the source of the "~60" estimate.
- Line counts for `action_chain_graph.cpp` (827), `field_analyzer.cpp` (1320), and `strict_check_pass_generator.cpp` (1809) were verified via direct line read and match the context's estimates exactly.
- Two search modes exist (`calculateResultBestFirstSearch` vs `calculateResultChain`/`doSearch`); best-first is the one actually invoked by `calculateResult()`.
- `ActGen_DirectPass` and `ActGen_SimpleDribble` are wired in `createActionGenerator()` but currently commented out — dead-but-present code paths.

## See Also

- [./sample-player-core.instructions.md](./sample-player-core.instructions.md) — `SamplePlayer::actionImpl()` dispatch and entry point.
- [./basic-actions-behaviors.instructions.md](./basic-actions-behaviors.instructions.md) — local `KickTable`/`Body_Dribble2008`/`Body_SmartKick` forks used by `Bhv_PlannedAction`.
- [../../../librcsc/.github/instructions/action-behavior-library.instructions.md](../../../librcsc/.github/instructions/action-behavior-library.instructions.md) — underlying Body/Neck/View action primitives.

---
Part of: [../copilot-instructions.md](../copilot-instructions.md)
