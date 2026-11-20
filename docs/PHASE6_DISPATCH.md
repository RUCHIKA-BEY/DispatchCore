# Phase 6 — The Dispatch Engine and Allocation History

> **Verification status (2026-11-19 17:55)**
>
> | Program | Compiled, 0 warnings | `make analyze` clean | Hand-reviewed | **Executed** |
> |---|---|---|---|---|
> | `test_history` | ✅ | ✅ | ✅ | ❌ **not yet** (blocked by Smart App Control) |
> | `test_dispatch` | ✅ | ✅ | ✅ | ❌ **not yet** (blocked by Smart App Control) |
> | `test_app_state` (rebuilt with the new checks) | ✅ | ✅ | ✅ | ❌ **not yet** |
>
> No Phase 6 test has been run, so **none is marked as passed**. See `docs/TEST_REPORT.md`.

## Files in this phase

| File | Status | Contents |
|---|---|---|
| `src/history.h` / `src/history.c` | **new** | `Allocation` records in a singly linked list with head **and tail** pointers |
| `src/dispatch.h` / `src/dispatch.c` | **new** | read-only selection, commit, bounded `dispatch_next`, `dispatch_all`, completion, cancellation, route and active-dispatch display |
| `tests/test_history.c` | **new** | 5 test functions |
| `tests/test_dispatch.c` | **new** | 10 integration test functions |
| `src/app_state.h` / `src/app_state.c` | **modified** | added `history` and `next_allocation_id`; the history is freed in `app_free`; allocation rules added to `app_check_consistency` |
| `Makefile` | **modified** | two new test programs; `HISTORY_SRC`, `DISPATCH_SRC` |
| `docs/TEST_REPORT.md` | **modified** | Phase 6 rows and run log |

No other earlier file changed.

### Design details decided in this phase (please review)

1. **Selection and commit are separate.** `dispatch_select_resource` only *reads* the state and
   returns a decision; `commit` applies it. The decision can be tested on its own, and Phase
   10's strategy comparison can reuse it.
2. **The history refers to incidents and resources by ID, not by pointer.** It therefore
   never holds a pointer into the stores, and it can be saved to CSV directly (Phase 7).
3. **The clock advances on dispatch, completion and cancellation**, not on failed attempts.
   Failed attempts are counted in `dispatch_attempts` instead.
4. **"Busy" includes out of service** in the failure reason ("all compatible units are busy
   or out of service").
5. **A completed incident keeps `assigned_resource_id`** as a record of which unit served it.
6. **Completion refuses inconsistent data** (`DC_ERR_STATE`) rather than guessing how to
   repair it.
7. **Cancelled entries leave the heap permanently** when the dispatch pass reaches them. This
   is where lazy deletion finishes.

---

## A. Purpose

This phase answers the project's central question:

> *Which pending incident should be served next, and which unit should go?*

It also records every decision. Every earlier module is used here:

| Need | Module (phase) |
|---|---|
| most urgent incident | `incident_heap` (3) |
| which units may serve it | `compatibility` (4) |
| which units are free | `resource` (4) |
| road distances and routes | `graph` (1), `dijkstra` + `minheap` (2) |
| find records by ID | `hash_table` via `app_state` (5) |
| remember what happened | `history` (6) |

---

## B. Concepts you need first

### B1. Integration and "filter, then optimise"

The engine applies **three filters** before it compares anything:

```
all units
  └─ compatible with the incident type?      (compat table, O(1) each)
       └─ AVAILABLE?                          (status, O(1) each)
            └─ reachable by road?             (Dijkstra dist ≠ DC_INF)
                 └─ choose: shortest distance; tie → lower resource ID
```

The counts at each stage (`compatible_units`, `available_units`, `reachable_units`) tell us
**why** a dispatch failed, so the user gets a specific reason instead of a bare "failed".

### B2. One Dijkstra per incident (blueprint D4)

Roads are undirected, so dist(unit → incident) = dist(incident → unit). One run **from the
incident** produces every unit's distance in one `dist[]` array. Cost: one
O((V+E) log V) run instead of one per unit. The path comes out incident → unit and is
**reversed** to unit → incident, which is the way the unit actually drives.

### B3. Head-of-line blocking and the bounded pass

If the top incident cannot be served (say, a fire with no fire unit), a naive engine either
stops, so nothing else is ever served, or pops it, fails and pushes it back on top, forever.
Our `dispatch_next` makes **one bounded pass**:

```
skipped ← []
while heap not empty:
    inc ← extract_max
    if inc is not PENDING: drop it (cancelled; lazy deletion)    ; continue
    if a unit can be chosen: commit, STOP
    else: record the reason, skipped.append(inc)
put every skipped incident back into the heap
```

**Why it always terminates:** every loop iteration removes one element from the heap, and
nothing is put back until after the loop. So the loop runs at most *(heap size)* times.

**Why nothing is lost:** each extracted incident is dispatched, dropped because it was
cancelled, or put back.

### B4. Separating a decision from its effects

`dispatch_select_resource` takes a `const AppState *`, so the compiler **guarantees** it
changes nothing. That makes it safe to call for "what would happen?" questions, easy to test,
and reusable by the Phase 10 strategy comparison.

### B5. Ordering steps so that failure is harmless

A commit changes three things: append an allocation, mark the unit DISPATCHED, mark the
incident DISPATCHED. Only the append can fail (out of memory). So it is done **first**: if it
fails, nothing has changed yet. The other two steps cannot fail, because the selection was
made a moment earlier from this same state. The code still checks their return values and
reports `DC_ERR_STATE` rather than hiding a "can't happen".

### B6. Transferring ownership ("move")

The route array is `malloc`'d during selection. On commit, `history_append` **takes
ownership**: the selection's pointer is set to `NULL`, so `dispatch_selection_free` will not
free it a second time. If the commit fails, the selection still owns it and frees it. At every
moment, exactly one owner is responsible for freeing it.

### B7. Linked list with a tail pointer

The history only ever **appends** and is read **in order**. A singly linked list with a
`tail` pointer gives O(1) append (no walk to the end) and natural chronological order, with
no resizing. This is the "justified linked list" from the blueprint.

### B8. Cross-structure invariants

After Phase 6, the consistency checker also enforces:

> every ACTIVE allocation ⇔ one DISPATCHED incident and one DISPATCHED resource that point at
> each other; every DISPATCHED incident has an ACTIVE allocation; the number of active
> allocations = the number of dispatched units.

The random simulation test checks this after **each of 3,000 random steps**.

---

## C. Code (where things are)

- `src/history.h` / `.c`: names → init/free → append (validation, tail insertion) → find →
  completion → counting → printing.
- `src/dispatch.h`: `DispatchOutcome`, `DispatchSelection`, `DispatchResult`, the public
  functions.
- `src/dispatch.c`: outcome text → display helpers → **selection** → **commit** →
  **dispatch_next / dispatch_all** → completion and cancellation → active listing.
- `src/app_state.c`: the new consistency block is at the end of `app_check_consistency`.

---

## D. Code walkthrough

### D1. `history_append`

Validates everything, **including that the route really starts at `from_location` and ends
at `to_location`**, before allocating. On any failure it returns `NULL` and the caller keeps
the route. Otherwise it links the new node after `tail`, or makes it the `head` if the list is
empty, and moves `tail`.

### D2. `dispatch_select_resource`

1. `reset_selection` puts every output field in a safe state.
2. **Pass 1 over the units** counts compatible and available ones. If either count is 0 we
   return a precise reason **without running Dijkstra**, which saves work.
3. `dijkstra_run` from the incident's location.
4. **Pass 2** considers only eligible units, skips unreachable ones, and keeps the best:
   `d < best_dist`, or an equal distance with a lower ID.
5. `dijkstra_build_path` to the winner's location, `shortest_paths_free`, `reverse`.

The idiom `return sel->outcome = DISPATCH_X;` stores the outcome in the struct **and**
returns it in one statement.

### D3. `commit`

It rechecks the preconditions (unit AVAILABLE, incident PENDING, allocation IDs not
exhausted), appends to the history (B5), takes the route (B6), then assigns the unit, changes
the incident's status, records `assigned_resource_id`, and advances the allocation ID and the
clock.

### D4. `dispatch_next`

- The `skipped` array is allocated **before** the heap is touched, with size = heap size, the
  maximum possible. So an allocation failure leaves the queue exactly as it was.
- In the loop, a non-PENDING incident increments `discarded`. A successful commit fills
  `result` and breaks. A failed selection increments `dispatch_attempts` and `kept_pending`,
  logs the reason (with the needed unit types for "no compatible unit"), and goes into
  `skipped`.
- **Re-insertion cannot run out of memory:** the heap held at least this many entries a moment
  ago, so its array is already big enough and `grow` is never called. The return value is
  still checked.
- The final outcome is `DISPATCH_OK`, `DISPATCH_NO_PENDING` (nothing pending was found) or
  `DISPATCH_NOTHING_DISPATCHABLE` (incidents were tried but none could be served).

### D5. `dispatch_all`

It calls `dispatch_next` until a call dispatches nothing. **Termination:** each success
changes one incident from PENDING to DISPATCHED permanently, so there can be at most
*(number of pending incidents)* successes, plus one final unsuccessful pass.

### D6. `dispatch_complete`

It finds the incident (hash, O(1) average), requires DISPATCHED, finds the unit (hash) and the
active allocation (history, O(h)), and checks that they agree. Then it releases the unit **at
the incident location**, completes the incident, marks the allocation completed with the
current clock, and advances the clock.

### D7. `dispatch_cancel`

`incident_set_status(inc, INC_CANCELLED)` already enforces "only PENDING can be cancelled".
The heap is not touched (lazy deletion, Phase 3 B7).

### D8. Display

`dispatch_print_route` turns vertex IDs into `0 Central Hospital > 3 Riverside > 5 Old Town`.
`dispatch_print_active` lists every ACTIVE allocation with unit type, incident type, severity,
distance and route. That will be menu item "View Active Dispatches".

---

## E. Example execution (hand-calculated)

Reference city (Phase 1) and reference fleet: R1 AMB@0, R2 AMB@6, R3 FIRE@2, R4 RESCUE@4,
R5 FIRE@7, R6 AMB@8 (island).

### E1. The blueprint example

MEDICAL, severity 4, at Old Town (5).

| Stage | Units left |
|---|---|
| compatible (ambulances) | R1, R2, R6 |
| available | R1, R2, R6 |
| reachable (Dijkstra from 5: d0 = 9, d6 = 11, d8 = ∞) | R1 (9), R2 (11) |
| choice | **R1, distance 9** |

The path from Dijkstra is 5 → 3 → 0; reversed, the unit drives **0 → 3 → 5**. Predicted log
line:

```
Dispatched unit 1 (AMBULANCE) to incident 1001 (MEDICAL, severity 4 Severe) at 5 Old Town
  distance 9, route: 0 Central Hospital > 3 Riverside > 5 Old Town
```

### E2. Head-of-line blocking avoided

Only an ambulance at 0. Incident A = FIRE sev 5 at 2; incident B = MEDICAL sev 2 at 5.

| Pass | Extracted | Result | Queue afterwards |
|---|---|---|---|
| 1 | A | no compatible unit → skipped (attempts 1) | — |
| 1 | B | ambulance, distance 9 → **dispatched** | A (put back) |
| 2 | A | no compatible unit → skipped (attempts 2) | A (put back), pass ends |

### E3. `dispatch_all` on the Phase 3 example (7 incidents, reference fleet)

Priority order: 1002 FIRE@2 s5 · 1005 MED@0 s5 · 1004 STRUCT@4 s4 · 1007 ROAD@1 s4 ·
1001 MED@5 s3 · 1003 ROAD@6 s3 · 1006 FIRE@7 s1.

| Call | Tried (in order) | Outcome |
|---|---|---|
| 1 | 1002 | R3 (fire unit already at 2), distance **0** |
| 2 | 1005 | R1 (ambulance already at 0), distance **0** |
| 3 | 1004 | R4 (rescue unit already at 4), distance **0** |
| 4 | 1007 | ambulances/rescue: R1 and R4 busy; R6 unreachable; R2 at 6 → distance **15**, route 6 > 7 > 4 > 2 > 1 |
| 5 | 1001, 1003, 1006 | 1001: only R6 is free and unreachable → kept. 1003: same → kept. 1006: R5 at 7, distance **0** |
| 6 | 1001, 1003 | both kept again → **NOTHING_DISPATCHABLE**, loop ends |

Result: `dispatch_all` returns **5**. 1001 and 1003 remain PENDING with
`dispatch_attempts = 2` each, and 2 entries stay in the heap.

How the distance 15 for 1007 arises (Dijkstra from 1): 1→2 (3), 2→4 (5), 4→7 (4), 7→6 (3) =
15. The alternative 1→5→3→6 = 6+2+9 = 17 is longer.

### E4. Completion and re-dispatch

After E1, `dispatch_complete(1001)`: R1 becomes AVAILABLE **at 5**, and allocation 1 becomes
COMPLETED. A new MEDICAL call at 0 now goes to R1 again (from 5: distance 9, route
5 > 3 > 0), not to R2 (from 6: distance 16). R1's statistics: 2 trips, 18 distance units.

---

## F. Testing

### F1. Commands (MSYS2 UCRT64 shell, project folder)

```bash
make all
```
```bash
make analyze
```
```bash
make test
```

Without make:

```bash
mkdir -p build && gcc -std=c11 -Wall -Wextra -Wpedantic -g -Isrc -Itests -o build/test_dispatch tests/test_dispatch.c tests/reference_city.c src/*.c
```

### F2. What the tests validate

**`test_history.c`**: status names; append order, `head`/`tail`/`next` links and find; 9
kinds of invalid append, after which the caller still owns (and frees) the route; completion
only once and not before dispatch time; counting by status; 5,000 appends in order.

**`test_dispatch.c`**

| Test | Validates |
|---|---|
| `test_blueprint_example` | E1: the selection changes nothing; stage counts 3/3/2; R1, distance 9, route 0 > 3 > 5; every status updated; allocation route is a valid shortest path; consistent |
| `test_failure_reasons` | unreachable, no compatible unit, all busy, out of service: each keeps the incident PENDING, in the heap, with `dispatch_attempts` incremented |
| `test_head_of_line_blocking_avoided` | E2, including that the second pass stops |
| `test_equal_distance_tie` | two units both 9 away: the lower ID wins whichever location it is at |
| `test_unit_already_at_scene` | distance 0, one-vertex route |
| `test_cancel` | cancel rules; the heap entry stays until reached, then `discarded = 1` |
| `test_complete_and_redispatch` | E4 and every completion error case |
| `test_empty_queue` | empty queue; queue holding only a cancelled incident; `NULL` arguments |
| `test_dispatch_all_seven_incidents` | the whole of E3: exact incidents, units, distances, route, attempts |
| `test_random_simulation` | 3,000 random registrations, dispatches, completions and cancellations: **consistent after every step**; every allocation compatible, a valid route, and **exactly the shortest distance** (recomputed independently) |

**Not covered:** out-of-memory paths (code review only). Nothing in Phase 6 has been executed
yet.

---

## G. Complexity

V, E = graph size; R = units; n = heap size; h = history length; L = route length;
k = incidents tried in one pass (k ≤ n).

| Operation | Time | Extra space |
|---|---|---|
| `dispatch_select_resource` | O(R + (V+E) log V + L) | O(V + E) during Dijkstra; O(L) route |
| `commit` | O(1) | — (route ownership moves) |
| `dispatch_next` | O(k · (log n + R + (V+E) log V) + L) | O(n) `skipped` array |
| typical `dispatch_next` (top incident served) | O(log n + R + (V+E) log V) | O(n) |
| `dispatch_all` (D dispatches) | O(D · cost of `dispatch_next`) | — |
| `dispatch_complete` | O(1) average (hash) + O(h) (active allocation search) | — |
| `dispatch_cancel` | O(1) average | — |
| `dispatch_print_active` | O(h · L) | — |
| `history_append` | **O(1)** (tail pointer) | O(L) per record |
| `history_find*` | O(h) | — |

**Possible improvement (viva discussion):** storing the active allocation's ID in the
`Incident` would make completion O(1) instead of O(h). We kept the history independent of the
incident structure; for hundreds of records O(h) is negligible.

---

## H. Viva preparation

**Basic**

1. *Describe the dispatch rule.* Highest-priority pending incident; keep units that are
   compatible, then available, then reachable; choose the shortest road distance; ties go to
   the lower ID.
2. *How many times does Dijkstra run per dispatch?* Once per incident tried, from the
   incident's location.
3. *What happens when nothing can serve an incident?* It stays PENDING, its attempt count
   rises, a reason is reported, and it goes back into the queue.
4. *What does completion do?* Incident → COMPLETED; unit → AVAILABLE at the incident's
   location; allocation → COMPLETED with a timestamp.

**Tricky**

5. *Why can't the loop in `dispatch_next` run forever?* Each iteration removes one heap
   element and nothing is re-inserted until the loop ends, so there are at most n iterations.
6. *Why not just leave an unservable incident at the top and stop?* That is head-of-line
   blocking: a fire with no fire unit would stop every ambulance call behind it (E2).
7. *Why is the route reversed?* Dijkstra ran from the incident, so the path is incident →
   unit; the unit drives the other way. Roads are undirected, so the length is the same.
8. *What if the history append fails?* It is done first, so nothing else has changed; the
   selection still owns the route and frees it; the incident is put back into the queue.
9. *Why can the re-insertions never fail?* The heap's array already held at least that many
   entries, so it never needs to grow.
10. *Two units are exactly 9 away. Which is sent?* The lower resource ID (tested in both
    location orders).
11. *Why does the history store IDs instead of pointers?* It never dangles, it is independent
    of how the stores are organised, and it maps directly to a CSV row.
12. *Why separate selection from commit?* Selection is read-only (`const AppState *`), so it
    can be tested and reused; commit has a single, careful failure path.
13. *A unit is the closest but incompatible. Can it ever be chosen?* No. It is filtered out
    before distances are compared (R5 in E1).

**Complexity**

14. *Cost of a typical dispatch?* O(log n + R + (V+E) log V).
15. *Worst case of one `dispatch_next`?* O(n · (log n + R + (V+E) log V)), when every pending
    incident has to be tried.
16. *Why does history append take O(1)?* The tail pointer, so there is no walk to the end.

**Pointers and memory**

17. *Who owns the route array at each moment?* The selection, from `dijkstra_build_path`
    until `history_append` succeeds; then the `Allocation` node, until `history_free`.
18. *Why is `skipped` allocated before extracting anything?* So that a failure leaves the
    queue untouched.
19. *What does `app_free` now free in addition?* Every allocation node and its route.

**Integration**

20. *Which consistency rules were added?* See B8; checked after every step of the random
    simulation.
21. *How will Phase 7 save this?* Each allocation becomes one CSV row, with the route written
    as `0|3|5`.

---

## I. Team explanation

| Member | Must understand | Independently validate |
|---|---|---|
| **M1 (lead)** — primary owner | All of it | Approve design details 1–7; review `dispatch.c` line by line; redo E3 by hand without looking |
| **M2 (priority queue)** | B3 and the termination argument; lazy deletion finishing in `dispatch_next` | Prove in writing that `dispatch_next` terminates and loses no incident; check `attempted`/`kept_pending`/`discarded` in E2 and E3 |
| **M3 (graph)** | B2 and the reversal; E1, E3 and E4 distances | Recompute every distance and route in E1–E4 with your own Dijkstra tables |
| **M4 (records)** | History ownership (B6, B7); the new consistency rules (B8) | Design the CSV columns for `allocations.csv` from the `Allocation` struct (Phase 7) |
| **M5 (testing)** | The random simulation test | Write three end-to-end scenarios (normal day, shortage, disconnected city) as tables of inputs and expected outcomes, for the Phase 9 menu tests |

---

## J. Integration

- **Dependencies introduced:** `history → common`; `app_state → … , history`;
  `dispatch → app_state, compatibility, dijkstra`. `dispatch` is the first module that uses
  **every** data structure in the project.
- **Used next by:**
  - Phase 7: save and load `allocations.csv`; restore DISPATCHED/COMPLETED states.
  - Phase 8: history listings and statistics.
  - Phase 9: menu items Dispatch Next, View Active Dispatches, Complete, Cancel.
  - Phase 10: baseline vs optimised strategies, reusing selection.
- **Build:** `make all`, `make analyze`, `make test`.

### Suggested Git steps (for you to run; I have not run any Git command)

```bash
git switch -c feature/phase6-dispatch
```
```bash
git status
```
```bash
git add src/history.h src/history.c src/dispatch.h src/dispatch.c src/app_state.h src/app_state.c tests/test_history.c tests/test_dispatch.c Makefile docs/PHASE6_DISPATCH.md docs/TEST_REPORT.md
```
```bash
git commit -m "Phase 6: dispatch engine with bounded pass, allocation history and integration tests"
```

Then push and open a Pull Request. Ask **all four** other members to review: this phase
connects everyone's modules.
