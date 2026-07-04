---
applyTo: 'src/player/sample_communication.{cpp,h}'
---
# SampleCommunication — Teammate Say/Hear Planning

## TL;DR
`SampleCommunication` is helios-base's `Communication` strategy that decides **what to say** each cycle (ball/player/goalie/stamina/intercept info) by picking the highest-priority stale object and constructing one of librcsc's pre-defined `SayMessage` subclasses; it does **not** implement its own bit-packing — encoding/decoding is entirely delegated to librcsc.
- [sample_communication.cpp](../../src/player/sample_communication.cpp) is **2290 lines** (confirmed via `read_file`, not the ~1942 a naive `Get-Content` line count reports due to CRLF handling), [sample_communication.h](../../src/player/sample_communication.h) is **98 lines** — both larger/smaller respectively than the ~2290-combined estimate in project context, but the `.cpp` figure matches.
- Entry point is `SampleCommunication::execute(PlayerAgent*)` ([sample_communication.cpp:184-240](../../src/player/sample_communication.cpp:184)), invoked **every cycle, unconditionally** from `SamplePlayer::communicationImpl()` — but it opportunistically decides *whether* to actually append a say message based on staleness/priority scoring.
- **Open the full file when:** you need to change *what* gets said (priority scoring in `sayBallAndPlayers()`), add a new message type, or tune per-object "staleness" thresholds (`shouldSayBall`, `updatePlayerSendTime`).

## Overview
`SampleCommunication` (declared [sample_communication.h:44](../../src/player/sample_communication.h:44)) implements librcsc's abstract `Communication` interface. It is a **say-side planner only**: it builds outgoing audio messages and hands them to `PlayerAgent::addSayMessage()`. The complementary **hear-side** decode path (turning a received `(hear ...)` string back into structured data) is registered separately in `SamplePlayer`'s constructor via `addSayMessageParser(new XxxMessageParser(audio_memory))` calls ([sample_player.cpp:107-126](../../src/player/sample_player.cpp:107)) — one parser per message type, all writing into a shared `rcsc::AudioMemory`. These two sides are **symmetric but independent call paths**: `SampleCommunication` never calls the parsers, and the parsers never call back into `SampleCommunication`.

Key state tracked across cycles:
- `M_current_sender_unum` / `M_next_sender_unum` — round-robin turn-taking so only one teammate "speaks" about a given topic per opportunity (`updateCurrentSender()`, [sample_communication.cpp:247](../../src/player/sample_communication.cpp:247)).
- `M_ball_send_time`, `M_teammate_send_time[12]`, `M_opponent_send_time[12]` — last-sent timestamps per object, used to avoid redundant re-sends (`updatePlayerSendTime()`, [sample_communication.cpp:329](../../src/player/sample_communication.cpp:329)).

## Architecture
`execute()` ([sample_communication.cpp:184-240](../../src/player/sample_communication.cpp:184)) call sequence, run every cycle:
1. Bail out immediately if `agent->config().useCommunication()` is false.
2. `updateCurrentSender(agent)` — advance/compute whose turn it is.
3. If not `PlayOn` and stamina recovery has degraded, call `sayRecovery(agent)` (recovery info always takes priority in stopped modes).
4. If `BeforeKickOff`/`AfterGoal_`/penalty-shootout → return early (no further messages).
5. `sayBallAndPlayers(agent)` — the main logic (see below) + `sayStamina(agent)`.
6. `attentiontoSomeone(agent)` — set the `(attentionto ...)` target for the next hear.
(An `#if 0` legacy branch with `sayBall/sayGoalie/saySelf/sayPlayers` called individually is compiled out.)

`sayBallAndPlayers()` ([sample_communication.cpp:594-1117](../../src/player/sample_communication.cpp:594), the largest function in the file) is a **priority-scoring scheduler**, not a fixed template:
1. Compute `available_len = ServerParam::i().playerSayMsgSize() - agent->effector().getSayMessageLength()` — respects the server-enforced say-message length cap (see rcssserver's [player-command-protocol.instructions.md](../../../rcssserver/.github/instructions/player-command-protocol.instructions.md) for the raw `(say ...)`/`(hear ...)` s-expression limits this budgets against).
2. Build a `std::vector<ObjectScore>` of 23 slots (0=ball, 1-11=teammates, 12-22=opponents), seeded with a base score = cycles-since-last-heard-or-seen for that object (from `wm.audioMemory()` / `wm.time()`), then adjusted by kickability, goalie status, and `shouldSayBall()`/`shouldSayOpponentGoalie()` triggers.
3. Sort by score (`ObjectScore::Compare`), filter out low scores (`IllegalChecker`, threshold ≤0.1).
4. Greedily pick objects (ball first, then players/goalie) that fit in `available_len`, choosing the concrete message class based on the combination selected (single ball, ball+player, ball+goalie, goalie+player, 1/2/3 plain players) — e.g. [sample_communication.cpp:870-1097](../../src/player/sample_communication.cpp:870).
5. Each `agent->addSayMessage(new XxxMessage(...))` call queues one encoder object; `PlayerAgent` concatenates all queued messages (respecting the length cap) into the single `(say "...")` command sent this cycle.

## Message Format (delegated to librcsc)
**Correction to initial framing:** the bit-packed encoding is **not implemented in `sample_communication.cpp`**. `SampleCommunication` only *selects and constructs* message objects; the actual 6-bit-per-character custom encoding lives in librcsc:
- `rcsc/player/say_message_builder.h/.cpp` — one class per message type (e.g. `BallMessage`, `PassMessage`, `InterceptMessage`, `GoalieMessage`, `GoalieAndPlayerMessage`, `OffsideLineMessage`, `DefenseLineMessage`, `OnePlayerMessage`, `TwoPlayerMessage`, `ThreePlayerMessage`, `SelfMessage`, `TeammateMessage`, `OpponentMessage`, `BallPlayerMessage`, `BallGoalieMessage`, `StaminaMessage`, `RecoveryMessage`, `WaitRequestMessage`, `PassRequestMessage`, `DribbleMessage`, `SetplayMessage`, `StaminaCapacityMessage`). Each documents its own wire format and fixed length in a header comment, e.g. `BallMessage`: `"b<pos_vel:5>"` → 6 chars total; `PassMessage`: `"p<unum_pos:4><pos_vel:5>"` → 10 chars.
- `rcsc/common/audio_codec.h/.cpp` — the actual bit-packing/quantization primitives (position/velocity → fixed-width base64-like character encoding) used by every `appendTo()` implementation.
- `rcsc/common/say_message_parser.h/.cpp` — one `SayMessageParser` subclass per message type, each keyed by a single-character `header()` tag, decoding the payload back into `AudioMemory` records.
- Used symmetrically from **both** ends: `SampleCommunication` (helios-base, encode) picks which message classes to instantiate; `SamplePlayer`'s constructor registers the matching parser list (decode) so every message type `SampleCommunication` can produce has a corresponding listener.

See librcsc's [client-framework.instructions.md](../../../librcsc/.github/instructions/client-framework.instructions.md) (`SayMessageParser` summary) for the framework-level view, and rcssserver's [player-command-protocol.instructions.md](../../../rcssserver/.github/instructions/player-command-protocol.instructions.md) for the raw `say`/`hear` command and length-limit rules this encoding is budgeted against.

## Patterns & Conventions
- **Priority-score-then-greedy-pack**, not a fixed per-cycle template — every cycle, `SampleCommunication` re-evaluates which 0-3 objects are "most stale/important" and packs as many as fit in the remaining say-message budget.
- **One `SayMessage` subclass instantiated with `new`, passed directly to `addSayMessage()`** — ownership transfers to the agent's outgoing-message queue (no manual `delete`).
- **`dlog.addText(Logger::COMMUNICATION, ...)`** traces scoring decisions (only active under `#ifdef DEBUG_PRINT`/`DEBUG_PRINT_PLAYER_RECORD`, both commented out by default).
- **Round-robin sender turn-taking** (`M_current_sender_unum`) avoids every teammate trying to say the same thing in the same cycle.
- Message classes are always chosen in **fixed logical groups** (ball, ball+player, ball+goalie, goalie+player, N-player) rather than composed ad hoc — matches the fixed-format nature of the underlying wire encoding.

## Key Abstractions
- **`Communication`** (base interface, `virtual bool execute(PlayerAgent*)`) — `SampleCommunication` is the default implementation; `KeepawayCommunication` is the alternate swapped in for keepaway-mode training (see [sample-player-core.instructions.md](./sample-player-core.instructions.md)).
- **`rcsc::AudioMemory`** (`wm.audioMemory()`) — per-agent heard-message history read by `SampleCommunication` (staleness scoring) and by `SamplePlayer::doHeardPassReceive()` — see Integration Points below for how these relate.
- **`ObjectScore`** (anonymous-namespace struct, [sample_communication.cpp:60-85](../../src/player/sample_communication.cpp:60)) — local priority-queue element (`player_`, `number_`, `score_`) with `Compare`/`IllegalChecker` functors driving the greedy selection.
- **`rcsc::SayMessage`** (librcsc abstract base) — `header()`, `length()`, `appendTo(std::string&)`; every concrete message class in `say_message_builder.h` implements this.

## Integration Points
- **`SamplePlayer::communicationImpl()`** ([sample_player.cpp:514-521](../../src/player/sample_player.cpp:514)) — the *only* call site of `SampleCommunication::execute()`. This is a **separate protected virtual hook** from `actionImpl()`, invoked once per cycle by the base `rcsc::PlayerAgent` framework's action-cycle dispatch (say-phase, distinct from the think/act phase) — confirming it runs every cycle unconditionally, independent of whatever `doPreprocess()`/`actionImpl()` decided.
- **`SamplePlayer::doHeardPassReceive()`** ([sample_player.cpp:704-751](../../src/player/sample_player.cpp:704), called from `doPreprocess()` at [sample_player.cpp:631](../../src/player/sample_player.cpp:631)) — **correction to initial framing**: this does *not* call into `SampleCommunication` at all. It reads `wm.audioMemory().pass()`, a record populated by librcsc's `PassMessageParser` (registered in `SamplePlayer`'s constructor, [sample_player.cpp:108](../../src/player/sample_player.cpp:108)) when a teammate's `PassMessage`/pass-related say arrives. `SampleCommunication` and `doHeardPassReceive()` are the **say** and **hear** ends of the same channel, connected only indirectly through the shared `AudioMemory` object and the wire format — not through any direct function call between the two files.
- **`SamplePlayer` constructor** ([sample_player.cpp:100-126](../../src/player/sample_player.cpp:100)) — registers one `SayMessageParser` per message type against a shared `AudioMemory`, and constructs the default `M_communication = new SampleCommunication()` ([sample_player.cpp:146](../../src/player/sample_player.cpp:146)).
- **`rcsc::ServerParam::i().playerSayMsgSize()`** — the say-message length budget `sayBallAndPlayers()` packs against; ultimately enforced server-side per rcssserver's [player-command-protocol.instructions.md](../../../rcssserver/.github/instructions/player-command-protocol.instructions.md).

## Build & Test
Part of the standard helios-base build (`src/player/*.cpp` → `sample_player` binary), no separate build target. No dedicated unit tests; validated via full-match runs. See the top-level `copilot-instructions.md` for the general build command.

## Logging
`dlog.addText(Logger::COMMUNICATION, ...)` is the dedicated log category for this file (mostly gated behind `#ifdef DEBUG_PRINT`/`DEBUG_PRINT_PLAYER_RECORD`, disabled by default — enable those macros locally to trace scoring decisions).

## Important Notes
- `sample_communication.cpp` line count is **2290** (per `read_file`), not the raw `Get-Content` count of 1942 lines seen from PowerShell (likely a CRLF/embedded-control-character artifact of that specific file) — trust `read_file`'s count.
- **Flagged correction**: the task context implied `sample_communication.cpp` contains "bit-packed custom encoding of ball/player positions" itself. In fact the bit-packing lives in librcsc (`audio_codec.h/.cpp`, `say_message_builder.cpp`); `sample_communication.cpp` only selects which pre-built message classes to instantiate and queues them via `addSayMessage()`.
- **Flagged correction**: `doHeardPassReceive()` in `sample_player.cpp` does connect to the say/hear *mechanism*, but not to the `SampleCommunication` *class* directly — it consumes `AudioMemory` populated by librcsc's `PassMessageParser`, a completely separate registration path set up in `SamplePlayer`'s constructor.
- `execute()` runs **every cycle** (called unconditionally from `communicationImpl()`), but whether any message is actually appended is opportunistic/conditional (staleness scoring, `available_len` budget, turn-taking).
- `KeepawayCommunication` (sibling file, not covered here) implements the same `Communication` interface for the simplified keepaway-training scenario — see `sample-player-core.instructions.md` for where it's swapped in.

## See Also
- [`sample_communication.cpp`](../../src/player/sample_communication.cpp), [`sample_communication.h`](../../src/player/sample_communication.h)
- [`sample-player-core.instructions.md`](./sample-player-core.instructions.md) — `SamplePlayer::actionImpl()`/`communicationImpl()`/`doPreprocess()`/`doHeardPassReceive()`
- librcsc: [`client-framework.instructions.md`](../../../librcsc/.github/instructions/client-framework.instructions.md) (`SayMessageParser`, `audio_codec`, `audio_memory` overview)
- rcssserver: [`player-command-protocol.instructions.md`](../../../rcssserver/.github/instructions/player-command-protocol.instructions.md) (raw `say`/`hear` s-expression, length limits)

---
Part of: [`helios-base copilot-instructions.md`](../copilot-instructions.md)
