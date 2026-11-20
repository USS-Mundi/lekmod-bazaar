# Round-start planning: a seat plays its turn before its turn

Status: built, in trial (the switch is off). Written 2026-11-14 from game `civ5-game-72bf92c1` (4 Opus 5.5 seats, 220 seat-turns).

## Goal

A seat decides its whole turn while the human plays, on the state its turn will open with. When its turn comes, the
plugin carries the orders out with no model call unless something that matters changed.

Constraints, each checked in "Done when":

- No loss in play: every decision is still made after the seat opened this round's turn view, and anything that
  could change a decision brings the turn to the model.
- Token cost level or lower.
- The human's wait per round falls by more than half.

## What the data says

| Fact | Value | Why it matters |
|---|---|---|
| Seats play one after another, never together | 0 of 151 overlaps | The human waits for the sum of the seats' turns |
| Human's wait per round for the four seats | median 74s | The number to cut |
| Time from last seat ending to first seat starting (city-states, barbarians, then the human) | median 72s | Idle time the seats can plan in |
| Model calls per seat-turn | 6.5 (4.8 inside the turn, 1.7 after) | Each is about 3.1s and reads the whole context |
| Tool calls per model message | exactly 1, in 1,489 of 1,489 messages | Every order is its own round trip |
| Game time inside a seat's turn | median 1.5s of 16s | The rest is waiting on the model |
| Context read per call | median 204,000 tokens, so 1.31M cached tokens a seat-turn | Calls, not output (1,700 tokens a turn), are the cost |
| When the plan is made | about 6s after the seat ends its turn, before city-states and barbarians move | Stale by design |
| Plans with something changed by the time they were due | 96 of 196; a barbarian in 76 | |
| Plans run as written when nothing had changed | 17 of 103 | Staleness is not the main cause |
| Plans holding no orders at all | 52 of 206 | Plans are sketches; the turn is decided live |
| Changes caused by a player who moves between round start and the seat's turn (the human, earlier seats) | 18 of 168 lines, all peaceful units passing | The churn left after this upgrade, in this game |
| Tool errors | 0.8 a turn: end_turn handed back by a queued move (43), bad path steps (29), unread messages (20) | Each costs a round trip |

Engine facts this rests on (`CvPlayer::setTurnActive`): a new game turn starts only after barbarians have moved; a
player's units get their moves and healing back when that player's turn ends; its income, production, growth and
research are applied when its turn opens.

## The change

Today: `wait` → (turn opens) `turn_view` → orders one call at a time → `end_turn` → `plan_turn` → `wait`.

After: `wait` → (round opens, human starts playing) `turn_view` → `plan_turn` with every order → `wait`.
When the seat's turn opens inside that wait, the plugin runs the plan and ends the turn, and the same wait keeps
waiting for the next round. The model is called on the seat's own turn only when the plan is held.

## Rules

1. **Round open.** A seat's round is open when the game turn is later than the turn it last ended.
2. **View first, unchanged in spirit.** `turn_view` works once the round is open. `plan_turn` is refused until the seat
   has opened this round's view, and refused between the seat's end of turn and the next round. The gate on the seat's
   own turn (`McpViewGate`) stays as it is.
3. **A plan is the turn.** `plan_turn` answers with what would stop the turn from ending: an order that cannot run as
   things stand, a unit ready with no order, a city with nothing to build, no research. The seat fixes them in the
   same planning session.
4. **Run or hold**, decided once, when the seat's turn is open and a plan made this round exists:
   - Hold if a waking change is seen since the plan's view: war or peace, a unit or city lost, damage taken, an enemy
     unit new or moved in sight, another civilization's unit new or moved within 5 tiles, the treasury emptied,
     happiness crossing zero, a diplomacy request, a message from a player.
   - Otherwise run the orders, then `end_turn`. Hold if an order fails or the game does not end the turn.
   - Not waking: only the drift the seat's own turn brings (income, happiness that stays on its side of zero).
5. **Held turn.** `wait_for_my_turn` returns "DO NOW: call turn_view", with why it was held and what already ran.
   From there the turn is played as today (`run_plan` with `drop` and `add`, or single orders).
6. **No plan in time.** A turn that opens before the seat has planned is played as today. A plan sent after the turn
   opened runs when the seat next enters the wait.
7. **Off switch.** `Logs/MCPServer-auto-run.off` holds every turn (planning still happens at round start). It is
   on for the first game: the model decides every turn, and the journal records what rule 4 would have done
   (`held` events: `rule_would_run`) beside what the view showed against the round's view (`turn_view` events) and
   what the model did (`run_plan` entries). Plans run by themselves only after that count shows nothing lost.

## Churn from players who move in between

Only the human and earlier seats act between the round opening and a seat's turn, so the last seat sees the most.
Three things bound it:

- Rule 4 is strict: whatever another player did that the seat can see holds the turn, so a seat never acts on a view
  the human or an earlier seat has changed. In this game that is 18 change lines over about 14 of 196 seat-turns.
- A held seat is not worse off than one whose plan ran: it decides on the fresh state, as every seat does today. The
  difference between early and late seats is how often their turn is the slower kind, not what they know.
- A held turn costs 2 to 3 calls (view, `run_plan`, wait), fewer than today's 4.8, because the orders are already
  written and only the difference is decided.
- At war with a neighbour every turn may hold. That is today's flow and today's cost, not worse.

## What changes, and how

Plugin, `MCPServer/src`:

| File | Change |
|---|---|
| `McpTurnCycle.h/.cpp` (pure, native tests) | `McpRoundOpen(gameTurn, lastEnded)`. `McpViewChanges` marks each line waking or not (rule 4). `McpAutoRun(planMadeThisRound, wakingChanges, switchOff)` returns run or the hold reason. |
| `McpTurnPlan.cpp` `ToolTurnView` | Allowed when the round is open. Before the seat's turn it says the turn's income, production and growth are not applied yet. Records the round it was shown in. A view on a held turn is gathered again and marked against the round-start view, with the plan's results. |
| `McpTurnPlan.cpp` `ToolPlanTurn`, `CheckOrders` | Rule 2 refusals. `CheckOrders` also validates each path step with `move_unit`'s own check, and lists what rule 3 names. |
| `McpTurnPlan.cpp` plan take (`ToolRunPlan`) | An internal auto mode (not a tool argument): skips the on-turn view check, applies `McpAutoRun`, keeps the plan when held, journals `auto` or `held` with the reason. |
| `McpTurnPlan.cpp` `McpNoteTurnEnded`, `McpTakePlanReminder` | A plan is owed when the round opens, not when the turn ends. |
| `McpBridge.cpp` `Flags`, `SessionWakes` | A session also wakes when its seat's round opens and it has no plan for it. |
| `McpBridge.cpp` wait loop, `RunPlan` | On entry and on each pass: turn open and a plan due → `RunPlan` in auto mode, on the waiting request's own thread. Turn ended → keep waiting. Held → return rule 5's reply. Auto mode always finishes with `end_turn`. The result is kept for the next reply. |
| `McpBridge.cpp` `ViewGateRefusal` | Auto mode passes; every model call is gated as now. |
| `McpTools.cpp` `end_turn`; `McpAddPlanningView` | `next` says to wait for the round. `coming` and the game plan leave the `end_turn` reply; the round-start view already has them. |
| Server instructions and the four tools' descriptions | One description of the new flow. |
| `tests/turn_cycle_test.cpp` | Round open, waking and non-waking lines, each `McpAutoRun` outcome. |

Agents:

| File | Change |
|---|---|
| The agent rulebook's "Your turn" and planning sections | Rewritten for the new flow: plan when the round opens, a complete plan runs by itself, a held turn starts with `turn_view`. |

Measurement:

| File | Change |
|---|---|
| A measurement script | The round-wait, calls-per-turn and hold-reason counts used for the table above, read from the agents' transcripts and the plan journal. |

## Expected effect (estimates until measured)

| Per seat-turn | Today | Plan runs | Plan held |
|---|---|---|---|
| Model calls | 6.5 | about 3.5 | about 6.5 |
| Seconds the human waits | 16 | about 2 | about 10 |

Tokens are level with today even if every turn is held. If a third are held: about 4.5 calls (30% fewer cached
tokens) and a round wait near 20s instead of 74s.

## Done when

Measured over one game of 40 turns or more, same models and efforts:

- Round wait: median 30s or less.
- Model calls per seat-turn: 6.5 or fewer.
- No turn ran by itself with a waking change present (journal check, and a native test).
- Every held turn has its reason in the journal; the reasons are reviewed once to tune rule 4.
- Units lost and damage taken per turn are no higher than in `civ5-game-72bf92c1`.

## After the first trial game

Game `civ5-game-af72491e`, turns 0 to 21, 5 Opus 5.5 seats, switch off (the model decides every turn), beside
`civ5-game-72bf92c1` over the same turns (3 to 4 seats).

| Per seat-turn | Before | Trial |
|---|---|---|
| Seconds on the clock | 14 | 9 |
| Model calls (inside the turn) | 6.2 (4.6) | 7.7 (3.8) |
| Cached tokens read | 1.23M | 1.56M |
| Plans run as written | 3% | 67% |
| Plans with no orders | 42 of 80 | 0 of 108 |

- `run_plan` ended the turn in 37 of 89 runs. What stopped the rest: a game message one of the plan's own moves brought
  (a ruin, a city-state met) refusing the next order or the end (about 21), a unit with no order (17, mostly just
  built), the game handing the turn back after queued moves (9), bad path steps (5).
- Rule 4 would have run 82 of 89 turns, and in 23 of those the model changed the plan, nearly always for what its own
  turn opening produced (a unit built, a tech or policy finished). Rule 4 does not see those, so the switch stays off.

Changes made for it:

| File | Change |
|---|---|
| `McpTurnCycle` | `McpPlanEndHeld(orderFailed, messagesCame)`, `McpEndTurnAgain(blocked, unread, timesAgain)`, with tests. |
| `McpBridge.cpp` `RunPlan`; `McpToolSupport.cpp` `MeActing` | While a plan's orders run, a game message one of them brings does not refuse the next order. `run_plan` returns the messages in `messages_while_running`, marked read, and leaves the turn open for the seat instead of ending it. |
| `McpTurnPlan.cpp` turn view | The view on the open turn marks the game messages it shows as read. With a plan due it lists `your_plan_leaves_open`: units ready with no order, cities with nothing to build, no research. |
| `McpTools.cpp` `end_turn` | A turn the game hands back after queued moves, with nothing blocking and nothing unread, is ended again once by the same call. |

Aim: the view and `run_plan` end the turn, about 2 calls inside it. `round_report.py` prints how often `run_plan` ended
the turn and how often a message came during a run; if seats only ever end the turn after such a message, the end can
run too.

## Not in this upgrade

- Orders marked "only if possible" (skip instead of hold). Add only if failed optional orders turn out to be a common
  hold reason.
- A smaller context window. It is the other cost lever (204,000 tokens read per call), with its own risk to memory.
- Seats taking their turns at the same time.

## Open decisions

1. Does a plan made on this round's view satisfy "nothing before the turn view" when the plugin runs it?
