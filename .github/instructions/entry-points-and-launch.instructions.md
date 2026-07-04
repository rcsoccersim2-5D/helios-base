---
applyTo: 'src/{player,coach,trainer}/main_*.cpp,src/*.sh.in,src/*.conf,src/formations-*/**,CMakeLists.txt,configure.ac'
---
# Entry Points & Launch Infrastructure

## TL;DR
helios-base ships three thin `main()` entry points (player/coach/trainer) that wrap librcsc agents, launched via generated shell scripts with `.conf` text files and JSON formation files.
- `main_player.cpp` (113 lines), `main_coach.cpp` (114 lines), `main_trainer.cpp` (114 lines) — nearly identical boilerplate: signal handlers, `CmdLineParser`, `agent.init()`, `createConsoleClient()`, `client->run(&agent)`.
- Launch scripts (`start.sh.in`, `keepaway.sh.in`, `train.sh.in`) are Autoconf/CMake `configure_file` templates that spawn N player processes + 1 coach/trainer with computed ports.
- **Open the full file when:** modifying signal handling, adding new CLI flags to a launcher script, or changing which formation directory / `.conf` file a script points to.

## Overview
Each of the three team programs (`sample_player`, `sample_coach`, `sample_trainer` binaries) is a single-file `main()` in `src/player/main_player.cpp`, `src/coach/main_coach.cpp`, `src/trainer/main_trainer.cpp`. They construct a static `SamplePlayer`/`SampleCoach`/`SampleTrainer` instance (subclasses of librcsc's `PlayerAgent`/`CoachAgent`/`TrainerAgent`), install `SIGINT`/`SIGTERM`/`SIGHUP` handlers that call `agent.finalize()` before `exit(EXIT_FAILURE)`, parse `argv` with `rcsc::CmdLineParser`, call `agent.init(cmd_parser)`, obtain a console client via `agent.createConsoleClient()`, print the mandatory agent2d copyright banner, then block in `client->run(&agent)`.

Processes are not started directly by a user — they are spawned by shell scripts generated from `.in` templates at build time (`start.sh.in` → `start.sh`, `keepaway.sh.in` → `keepaway.sh`, `train.sh.in` → `train.sh`), which fill in `@LIBRCSC_LIBDIR@` and set `LD_LIBRARY_PATH`.

## Architecture
```
start.sh.in ──spawns──▶ sample_player ×N  (main_player.cpp → SamplePlayer : rcsc::PlayerAgent)
            ├─spawns──▶ sample_coach       (main_coach.cpp  → SampleCoach  : rcsc::CoachAgent)
keepaway.sh.in ─spawns─▶ sample_player ×N (Keeper team) + ×N (Taker team) + sample_trainer
train.sh.in    ─spawns─▶ sample_player ×N + sample_coach + trainer (name mismatch, see Important Notes)
```
Each spawned process reads a `.conf` file (`--player-config`/`--coach-config` CLI flag) for agent-level settings and a `--config_dir` pointing at a `formations-*` directory containing per-situation JSON formation files.

## Patterns & Conventions
- **main_*.cpp boilerplate is intentionally near-identical** across player/coach/trainer — a static global `agent` + `client` in an anonymous namespace, a `sig_exit_handle`/`sigExitHandle` function, then a linear `main()`. When adding a new entry point, copy this pattern.
- **Do NOT remove the agent2d copyright banner** printed to stdout before `client->run()` — it is explicitly commented as mandatory in all three files.
- Shell launcher scripts use POSIX `/bin/sh` (not bash-specific), parse long/short options manually via `case`/`shift`, and background (`&`) each spawned process with a small `sleep` between the goalie and the rest (`goaliesleep=1`).
- `coach_port` defaults to `port + 2` and `debug_server_port` to `port + 32` when not explicitly given — matches rcssserver's default port scheme (6000 players/trainer, 6002 online coach, 6032 debug — see rcssserver instructions).
- Formation JSON files share one schema across `formations-dt/`, `formations-keeper/`, `formations-taker/` (all 3 dirs contain the same 14 filenames, e.g. `before-kick-off.conf`, `normal-formation.conf`, `goalie-formation.conf`, `setplay-our-formation.conf`, etc.) — only the coordinate `data` differs per team role (regular team / keepaway keeper / keepaway taker).

## Key Abstractions
- **`SamplePlayer` / `SampleCoach` / `SampleTrainer`** (`src/player/sample_player.{h,cpp}`, `src/coach/sample_coach.{h,cpp}`, `src/trainer/sample_trainer.{h,cpp}`) — concrete subclasses of librcsc's `PlayerAgent`/`CoachAgent`/`TrainerAgent` (see librcsc client-framework instructions). This is where all decision-making logic lives; `main_*.cpp` itself has no soccer logic.
- **Formation JSON schema** (confirmed by reading `src/formations-dt/before-kick-off.conf`):
  ```json
  {
    "version": "",
    "method": "Static",
    "role": [
      { "number": 1, "name": "Goalie", "type": "Unknown", "side": "C", "pair": 0 },
      ...
    ],
    "data": [
      { "index": 0, "ball": {"x":0.0,"y":0.0}, "1": {"x":-49.0,"y":0.0}, ... }
    ]
  }
  ```
  The `role` array assigns a `name` (role class, e.g. `CenterBack`, `SideForward`), `type` (usually `Unknown`), `side` (`C`=center/symmetric), and `pair` (paired-role index for side/mirrored roles) per uniform `number` (1-11). The `data` array (present in per-situation files like `before-kick-off.conf`) holds `Static`-method sample positions keyed by uniform number plus a `ball` anchor point — this is beyond the schema quoted in the task context and worth noting for anyone editing formations.
- **14 formation files per directory**: `before-kick-off.conf`, `defense-formation.conf`, `goal-kick-{opp,our}.conf`, `goalie-catch-{opp,our}.conf`, `goalie-formation.conf`, `indirect-freekick-{opp,our}-formation.conf`, `kickin-our-formation.conf`, `normal-formation.conf`, `offense-formation.conf`, `setplay-{opp,our}-formation.conf`.

## Integration Points
- **librcsc** — `agent.init()`, `createConsoleClient()`, `AbstractClient::run()` are all librcsc APIs (`rcsc::AbstractClient`, `rcsc::CmdLineParser`); see [client-framework.instructions.md](../../../librcsc/.github/instructions/client-framework.instructions.md).
- **rcssserver** — launcher scripts connect to the ports rcssserver listens on: `-p 6000` (players/trainer), computed `coach_port` (default 6002, online coach), `debug_server_port` (default 6032). See rcssserver's `copilot-instructions.md`.
- **`--config_dir`** CLI flag selects which `formations-*` directory a player process loads; `start.sh.in` defaults to `formations-dt`, `keepaway.sh.in` uses `formations-keeper`/`formations-taker` per team, `train.sh.in` references `formations-train` (see Important Notes — this directory does not exist in the repo).

## Build & Test
Both build systems are present and confirmed:
- **Autotools**: `configure.ac` (Autoconf 2.61+, `AM_INIT_AUTOMAKE`, `AX_CXX_COMPILE_STDCXX_17`, Boost via `AX_BOOST_BASE`/`AX_BOOST_SYSTEM`, manual librcsc discovery via `--with-librcsc=PREFIX` or search paths `$HOME/.local`, `$HOME/local`, `/opt/robocup`, etc.) + per-directory `Makefile.am`. Standard flow: `./bootstrap && ./configure && make`.
- **CMake**: root `CMakeLists.txt` (`cmake_minimum_required(VERSION 3.5)`, `project(helios-base VERSION 2023.03)`, C++17, `find_library(LIBRCSC_LIB ...)`, `find_package(Boost 1.36.0 COMPONENTS system REQUIRED)`, optional `find_package(ZLIB)`) delegates to `src/CMakeLists.txt`, which adds `player`/`coach`/`trainer` subdirectories (each with its own `add_executable(sample_player|sample_coach|sample_trainer ...)`) and uses `configure_file(... @ONLY)` to generate `start.sh`, `keepaway.sh`, `train.sh` from the `.in` templates into `${PROJECT_BINARY_DIR}/bin`, then copies `.conf` files and `formations-*` directories alongside.
- No automated test suite was found for helios-base itself; validation is via running the generated scripts against a live rcssserver.

## Logging
- `.conf` files (`player.conf`, `coach.conf`) contain commented-out `debug_*` toggles (`debug_system`, `debug_sensor`, `debug_world`, `debug_action`, `debug_intercept`, `debug_kick`, ... `debug_action_chain`) that control librcsc's debug-log categories; `log_dir` defaults to `/tmp`, `debug_log_ext` to `.log`.
- Launcher scripts expose `--debug`, `--debug_DEBUG_CATEGORY`, `--debug-server-connect/-host/-port/-logging`, `--log-dir`, `--debug-log-ext`, `--offline-logging` flags that map directly onto librcsc CLI options (`--debug_server_connect`, `--log_dir`, etc.) consumed inside `agent.init()`.
- The mandatory startup banner (agent2d copyright) is printed via `std::cout` in every `main_*.cpp` — not a logging facility, just required attribution text.

## Important Notes
- **`train.sh.in` references a binary named `helios_trainer`** (line: `trainer="${DIR}/helios_trainer"`) but `src/trainer/CMakeLists.txt` builds `add_executable(sample_trainer ...)` — the script's trainer variable is unused later in the file for the reviewed portion and appears to be a stale/vestigial reference; `keepaway.sh.in` correctly uses `trainer="${DIR}/sample_trainer"`.
- **`train.sh.in` points `config_dir` at `formations-train`**, a directory that **does not exist** in this repository (only `formations-dt`, `formations-keeper`, `formations-taker`, `obsolete` exist under `src/`). Treat `train.sh.in` as needing a user-supplied formation directory before use.
- **There is no `trainer.conf` file** in the repo — `train.sh.in` reuses `player.conf` (`config="${DIR}/player.conf"`) for the trainer process; there is no separate trainer configuration file to load.
- Context assumption check: the task context's claim that main_player/coach/trainer are 113/114/114 lines was **verified correct** by direct `read_file` reads.
- The formation JSON schema in the task context (`{"version","method":"Static","role":[{number,name,type,side,pair}]}`) is **confirmed correct** but incomplete — situational files like `before-kick-off.conf` also carry a `"data"` array of static `{x,y}` coordinates per uniform number plus a `"ball"` anchor, keyed under `"index"`.

## See Also
- [../../../librcsc/.github/instructions/client-framework.instructions.md](../../../librcsc/.github/instructions/client-framework.instructions.md) — `SoccerAgent`, `PlayerAgent`/`CoachAgent`/`TrainerAgent`, `AbstractClient`, `ServerParam::instance()`.
- rcssserver `copilot-instructions.md` (`../../../rcssserver/.github/copilot-instructions.md`) — port scheme (6000/6002/6032) and protocol consumed by these launcher scripts.

---
Part of: [`helios-base copilot-instructions.md`](../copilot-instructions.md)
