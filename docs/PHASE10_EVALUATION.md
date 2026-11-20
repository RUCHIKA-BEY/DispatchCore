# Phase 10 — Testing & Performance Evaluation

Owner: **Member 5**, with M2 and M3. Status: compiled with 0 warnings and statically
analysed clean (35 files).

**Executed on 2026-11-19 21:32:** `test_evaluation` passed 180/180. That run confirms
the hand traces T1–T4 and printed the experiment results in §F, which are **real
output**, copied from `docs/evidence/2026-11-19_2132_phase10_make_test.log`.

**Not yet available:** timing numbers. `make bench` was blocked by Smart App Control.

**New files:** `src/evaluation.h`, `src/evaluation.c`, `tests/test_evaluation.c`.

**Changed files:**
- `src/dispatch.h` / `src/dispatch.c`:
  - adds `UnitRule`, `dispatch_select_resource_rule`, `dispatch_next_rule` and
    `dispatch_all_rule`;
  - the private `commit` is now the public `dispatch_assign`;
  - `dispatch_select_resource`, `dispatch_next` and `dispatch_all` are now one-line
    wrappers using `UNIT_NEAREST`. Their behaviour is unchanged: `test_dispatch` passed
    165/165 on this version.
- `src/menu.c`: option 20 runs the comparison.
- `src/main.c`: adds `--benchmark`.
- `tests/test_menu.c`: new option-20 checks, and the line-ending fix described in §F.
- `Makefile`: `test_evaluation`, `make bench`.

### Decisions for you to review

1. **Four strategies, not two.**
   - Baseline = FIFO queue + first-fit unit.
   - Optimised = priority queue + nearest unit.
   - The two mixed versions are run too, so the results show *which* idea causes each
     difference.
2. **What "first fit" means.** The first unit in registration order that is compatible,
   available **and reachable**. Without the reachability check the baseline would
   "dispatch" units that can never arrive, which is not a fair comparison.
3. **Time model:**
   - a constant speed of `EVAL_SPEED` = 5 distance units per step;
   - travel time = ceil(distance / 5);
   - service time comes from the scenario;
   - the unit is released at the scene.
4. **Scenarios are generated in memory from fixed seeds**, not stored as CSV files in
   `data/scenarios/` as the blueprint suggested. The same seed gives the same scenario on
   every machine, and nothing on disk can be edited by accident.
5. **Option 20 uses the loaded map read-only.** It runs on *copies*, with generated
   fleets and incidents, and never touches the live records.
6. **Timing is a separate command** (`--benchmark` / `make bench`), never part of the
   tests: timings vary between runs and machines, so they cannot be pass/fail checks.

---

## A. Purpose

The blueprint promised to compare "a baseline dispatch strategy against the optimized one
on identical, reproducible simulated data". Phase 10 delivers:

1. **A simulator** that replays a timeline of incidents against a fleet, under any
   strategy, and measures response times and distances.
2. **A fair experiment:** every strategy sees exactly the same city, fleet and incidents,
   each in its own `AppState`.
3. **A factorial design** (2 queue orders × 2 unit rules), so each effect can be
   attributed to its cause.
4. **Timing tools** for the data-structure claims made in earlier phases: heap vs array
   Dijkstra (Phase 2), and hash vs linear lookup (Phase 5).
5. **Testing evidence**, with what ran and what did not recorded in `TEST_REPORT.md`.

## B. Concepts

**B1. Baseline vs optimised.** A *baseline* is the simplest reasonable method. We measure
the optimised method against it, because "fast" means nothing without a reference. Our
baseline is what a person might do with a paper list: serve calls in the order they
arrived, and send the first suitable unit on the roster.

**B2. Factorial experiment.** If we changed both ideas at once and saw an improvement, we
would not know which idea caused it. Running all four combinations separates:
- the **queue effect**: rows FIFO+X vs PRIORITY+X;
- the **unit effect**: rows X+FIRST_FIT vs X+NEAREST.

**B3. Fairness rules:**
- the same scenario object for every strategy;
- a fresh `AppState` for each run, built from a copy of the city;
- the same time model;
- the same validity check at the end (`app_check_consistency` plus the Phase 8
  cross-check). A run that leaves inconsistent data would be marked `NO`.

**B4. Reproducibility.** Scenarios come from our own linear congruential generator (LCG),
`x ← x·1103515245 + 12345 (mod 2³²)`, with fixed seeds. `rand()` is not used, because its
sequence differs between C libraries. The tests check that the same seed gives an
identical scenario and that the same run repeated gives identical results.

**B5. Discrete-event simulation.** Nothing changes between events: an arrival or a
completion. So the simulator **jumps** straight to the next event step instead of ticking
through empty steps. The order inside a step is fixed: completions, then arrivals, then
dispatch. A unit freed at step t can therefore serve an incident at step t.

**B6. Metrics** (all in simulated steps):
- wait = dispatch − arrival;
- **response = wait + travel**, i.e. when help *arrives*;
- critical response = response for severity 5 only;
- **severity-weighted response** = Σ(severity × response) / Σ(severity), which rewards
  serving severe incidents quickly;
- total distance;
- makespan: when the last job finished;
- served / unserved. Unserved incidents can *never* be reached by a compatible unit,
  for example on the island.

**B7. Greedy is not optimal.** Both "nearest unit now" and "most severe first" are
**greedy**: each makes the best choice for the current incident only. Sending the nearest
unit can leave a later, more severe incident without a close unit. §F shows a real case
where FIFO + nearest travelled *more* than the baseline. Optimal assignment over time is a
much harder problem (online scheduling) and outside this project.

**B8. Measuring time honestly.**
- `clock()` measures processor time. Its resolution can be as coarse as 1–15 ms, so each
  measurement repeats the work (50 Dijkstra runs, 100,000 lookups).
- The numbers depend on the machine, its load and the compiler options. Only rows
  measured in the same run should be compared.
- Correctness is checked alongside speed: the heap and array versions must report equal
  distances.

## C. Code tour

### `dispatch.c` changes

- `dispatch_select_resource_rule`: the only change from Phase 6 is the choice condition:
  `best == NULL || (rule == UNIT_NEAREST && (d < best_dist || tie → lower ID))`. With
  `UNIT_FIRST_FIT`, the first reachable eligible unit in store order is kept.
- `dispatch_assign`: the former `commit`, now public and argument-checked. It does **not**
  touch the heap, so a FIFO caller leaves a stale heap entry, which lazy deletion
  tolerates.

### `evaluation.c`

| Function | Notes |
|---|---|
| `eval_strategy_at`, `eval_strategy_name` | Index 0..3: FIFO+first, FIFO+nearest, PRIORITY+first, PRIORITY+nearest. |
| `eval_travel_steps` | Integer ceiling: `(d + 4) / 5`. |
| `eval_random` | Our LCG (B4). |
| `eval_clone_graph` | Copies names, and every road once (`dest > u`). Adjacency order may differ, which can change which of two *equal-length* routes is reported; distances never change. |
| `eval_make_grid_city` | rows × cols blocks, roads right and down with lengths 1–9. Always connected. |
| `eval_generate` / `eval_scenario_free` | Unit i gets type i % 3, so every incident type has some unit. Arrivals advance by 0..max_gap steps. Severity is 1–5 and service 1–6 steps. |
| `scenario_is_valid` | Checks every field, and that arrivals are in time order. |
| `fifo_pass` | Scans incidents in registration order and serves each PENDING one if possible. One pass is enough: during a pass units are only used up, never freed. |
| `dispatch_pass` | FIFO → `fifo_pass`; PRIORITY → `dispatch_all_rule` (the Phase 6 bounded passes). |
| `eval_run` | The simulator (D). |
| `eval_compare`, `eval_print_*`, `eval_run_suite` | The tables, and the three standard scenarios with "baseline → optimised" summaries. |
| `time_dijkstra`, `time_lookup`, `eval_benchmark` | Timing (B8). |

## D. Algorithm: `eval_run`

```
validate scenario; copy city; app_init; register units in spec order
t ← first arrival step
loop:
    1. for each ACTIVE job with done_step ≤ t: dispatch_complete (unit released at scene)
    2. register every incident arriving at t   (IDs 1001, 1002, … in spec order)
    3. tail ← history.tail; dispatch_pass(strategy)
       for each new allocation after tail:
           travel ← ceil(distance / 5); wait ← t − arrival; response ← wait + travel
           done_step ← t + travel + service; record the metrics
    4. next_t ← min(next arrival, min done_step of ACTIVE jobs)
       if none: stop                           (nothing can ever change again)
       t ← next_t, or t + 1 if next_t ≤ t     (a 0-length job finishes next step)
end: unserved ← N − served; valid ← consistency check + cross-check + no active jobs
```

**Why it terminates.** Time strictly increases. Every ACTIVE job has a finite
`done_step`, and arrivals are finite. Once all arrivals have happened and no job is
active, the loop stops. Incidents that can never be served stay PENDING and are counted as
unserved.

**Why the new allocations can be found.** The history is a linked list with a tail
pointer, and dispatching only appends. So the nodes after the old tail are exactly this
step's dispatches: O(new) work, with no search.

## E. Hand-traced scenarios (the expected values in `test_evaluation.c`)

Reference city. Speed 5. Units are listed in registration order.

### E1. T1 — two ambulances, two incidents at step 0

Units: U1 at 0, U2 at 6. Incidents (both at step 0, service 4):
- A: MEDICAL at 1, severity 2;
- B: MEDICAL at 5, severity 5.

The distances needed:
- to A (location 1): U1 is 4 away; U2 is 15 away (6-7-4-2-1).
- to B (location 5): U1 is 9 away (0-3-5); U2 is 11 away (6-3-5 or 6-7-5).

| Strategy | Assignment | Responses (travel) | Distance | Weighted | Critical |
|---|---|---|---|---|---|
| FIFO + either rule | A first: U1 (4, travel 1). Then B: only U2 is left (11, travel 3). | A 1, B 3 | 15 | (2·1 + 5·3)/7 = **17/7 = 2.43** | 3 |
| PRIORITY + either rule | B first: U1 (9, travel 2; it is also the nearest). Then A: U2 (15, travel 3). | B 2, A 3 | 24 | (5·2 + 2·3)/7 = **16/7 = 2.29** | 2 |

**Lesson:** priority helps the critical call (3 → 2) but costs distance (15 → 24). An
"optimisation" is always optimising *some* measure.

### E2. T2 — one ambulance, a minor and a critical call

U1 at 0. Incidents (both at step 0, service 2):
- P: MEDICAL at 1, severity 1;
- Q: MEDICAL at 3, severity 5.

**FIFO:**
1. Step 0: P gets U1 (distance 4, travel 1). P is done at 0+1+2 = **3**.
2. Step 3: U1 is released at 1. Q gets it: distance 1→5→3 = 8 (the route via 0 is 11),
   travel 2.
3. Q waited 3; its response is 5. Done at 3+2+2 = **7**.

**PRIORITY:**
1. Step 0: Q gets U1 (distance 7, travel 2). Done at 4.
2. Step 4: P gets U1 from 3 (distance 8, travel 2). P waited 4; its response is 6. Done at
   **8**.

| | Distance | Wait sum | Response sum | Weighted | Critical | Makespan |
|---|---|---|---|---|---|---|
| FIFO | 12 | 3 | 6 | 26/6 = **4.33** | 5 | 7 |
| PRIORITY | 15 | 4 | 8 | 16/6 = **2.67** | 2 | 8 |

**Lesson:** with one unit, the unit rule cannot matter; only the queue order does. The
average response over *all* incidents gets worse (3.00 → 4.00), while the weighted
average and the critical response improve.

### E3. T3 — unreachable

U1 at 0. One incident on the island (location 9). It is never served, by any strategy:
`served 0, unserved 1, makespan −1`. The run stops after step 0.

### E4. T4 — zero-length job

U1 at 5; an incident at 5 at step 2, with service 0. It is dispatched at step 2 with
distance 0 and travel 0, so `done_step = 2`. It completes when time moves on to step 3.
Results: `makespan 2`, `last_step 3`. This tests the "t + 1" rule in D.

## F. Testing and results

### Tests (`tests/test_evaluation.c`, 180 checks, **all passed on 2026-11-19 21:32**)

| Test | Checks |
|---|---|
| `test_basics` | The strategy table and names; ceiling travel time; LCG reproducibility and range. |
| `test_unit_rules` | Nearest picks U2 (4); first fit picks U1 (15, with a valid path); `dispatch_assign` applies it and the state stays consistent; invalid arguments are rejected. |
| `test_cities` | A cloned city has the same roads, names and distances; the 3 × 4 grid has 17 roads, is connected and is reproducible; invalid sizes give NULL. |
| T1–T4 | Every number in §E. |
| `test_invalid_scenarios` | Out-of-order arrivals, bad location, severity 0, negative service time and a bad unit location are all rejected. |
| `test_generated` | Same seed gives an identical scenario; a different seed gives a different one; all values are in range; all 4 runs are valid; served + unserved = 80; **the unserved count is the same for every strategy**; a repeated run gives identical results. |
| `test_suite_runs` | The option-20 suite runs with and without a map; NULL output is rejected. |

**Why must "unserved" be equal across strategies?** A unit only ever moves to incidents
it can reach, so it never leaves its connected part of the city. The set of unit types in
each part is therefore fixed, and an incident is unservable under every strategy or under
none.

### Real results (from the 21:32 run; reproducible from the seeds)

Light load on the reference city (seed 101; 6 units; 60 incidents):

| Strategy | Distance | Avg response | Critical avg | Weighted avg |
|---|---|---|---|---|
| baseline (FIFO + first fit) | 377 | 3.26 | 4.57 | 3.58 |
| FIFO + nearest | 325 | 2.91 | 4.57 | 3.13 |
| priority + first fit | 372 | 3.67 | 3.57 | 3.23 |
| optimised (priority + nearest) | 326 | 3.44 | **2.86** | **2.96** |

Heavy load on the reference city (seed 202; 6 units; 150 incidents; 14 are on the island,
so they are unserved by every strategy):

| Strategy | Distance | Avg response | Critical avg | Critical max | Weighted avg |
|---|---|---|---|---|---|
| baseline | 1012 | 45.19 | 40.52 | 134 | 42.40 |
| FIFO + nearest | 984 | 43.76 | 38.76 | 129 | 40.98 |
| priority + first fit | 957 | 43.22 | 4.76 | 17 | 26.22 |
| optimised | **947** | **43.20** | **4.72** | **17** | **26.18** |

10 × 10 grid, heavy load (seed 404; 15 units; 400 incidents):

| Strategy | Distance | Avg response | Critical avg | Weighted avg | Makespan |
|---|---|---|---|---|---|
| baseline | 10034 | 35.84 | 36.23 | 36.77 | 275 |
| FIFO + nearest | 10102 | 37.20 | 37.45 | 38.19 | 276 |
| priority + first fit | 10574 | 36.75 | 6.63 | 20.71 | 279 |
| optimised | **9460** | **31.25** | **6.26** | **17.72** | **264** |

**What the results show** (simulation only):
1. **The priority queue is responsible for the big gain on critical calls.**
   - Under heavy load, the critical average response falls from about 40 to about 5 on the
     reference city, and from about 36 to about 6 on the grid. This happens whichever
     unit rule is used.
   - FIFO makes a critical call wait behind every earlier minor call.
2. **The nearest-unit rule mainly saves distance**, for example 377 → 325 under light load.
   Under heavy load its effect is small, because few units are free at any moment, so
   there is little to choose from.
3. **Greedy choices are not always better** (B7). On the grid, FIFO + nearest travelled
   *more* (10102) than the baseline (10034), and priority + first fit travelled most
   (10574). Only the combination of both ideas was best on every measure there.
4. **There is a trade-off under light load.** The optimised strategy's average response
   over *all* incidents (3.44) is worse than FIFO + nearest (2.91), because minor calls
   wait for severe ones. That is the intended policy; the weighted measure captures it.
5. **Every run had VALID = yes.** The engine's consistency check and the Phase 8
   cross-check held after thousands of simulated dispatches and completions.

### Timing (`make bench`)

**Update (2026-11-19 21:41, Phase 11): `make bench` has now run once.** The measured
numbers are in `docs/TEST_REPORT.md` (Phase 11 entry) and match the expected shape below:
- Dijkstra on 961 locations: heap 9 ms vs array 67 ms;
- lookup among 4000 units: hash 5 ms vs linear 208 ms.

At 21:32, `dispatchcore --benchmark` had been blocked. The measurements:
- Dijkstra, 50 sources each, on grids of 100, 400 and 961 locations: heap vs array.
- 100,000 lookups among 1000, 2000 and 4000 units: hash index vs linear scan.

**Expected shape** (theory, to be confirmed):
- heap Dijkstra pulls ahead of the array version as V grows, since a grid is sparse
  (E ≈ 2V);
- the hash lookup cost stays roughly flat, while the linear scan grows with n.

### Other evidence from the same run

- `test_menu` ran for the first time: 51 of 52 checks passed.
- The one failure was **in the test itself**:
  - it expected a saved line ending in `\n`;
  - on Windows the file is written in text mode, so it ends in `\r\n` (confirmed with
    `od -c`);
  - the program's behaviour was correct, and the reload in the same test passed.
- The check was corrected to accept either ending. The corrected test was **blocked on
  re-run**, so the fix is not yet verified by execution.

## G. Complexity

| Part | Time |
|---|---|
| `eval_generate` | O(U + N) |
| `eval_clone_graph` | O(V + E · deg) (each `graph_add_road` checks for duplicates in O(deg)) |
| `fifo_pass` | O(N) scan, plus one Dijkstra, O((V+E) log V), for each incident that has an available compatible unit |
| PRIORITY pass | As Phase 6: per `dispatch_next`, O(P log P) heap work plus Dijkstra runs |
| One event step | Completions O(N) + arrivals + one pass + next-event scan O(N) |
| Whole run | O(S · (N + pass cost)), where S = number of event steps (≤ 2N + 1, because each step has an arrival or a completion, plus the zero-length case) |
| `app_check_consistency` at the end | O(N · P) (`heap_contains` is linear); FIFO leaves up to N stale heap entries |

## H. Viva questions (with short answers)

1. **What are the baseline and optimised strategies?** Baseline: FIFO order, and the first
   suitable reachable unit in the roster. Optimised: max-heap priority order, and the
   nearest suitable unit (Dijkstra).
2. **Why run four strategies?** A 2 × 2 factorial design separates the effect of the queue
   order from the effect of the unit rule.
3. **How do you make the comparison fair?** The same scenario, a fresh copy of the city and
   `AppState` per run, the same time model, and the same validity check.
4. **Why not `rand()`?** Its sequence differs between C libraries, so the results would not
   be reproducible. Our LCG with fixed seeds gives identical scenarios everywhere.
5. **What is response time here, and why that and not wait?** Wait plus travel steps, i.e.
   when help arrives. Distance matters to the person waiting, not only queue time.
6. **Why can the optimised strategy have a worse average response?** It deliberately makes
   minor calls wait for severe ones. The severity-weighted and critical measures show the
   benefit.
7. **Why is "nearest unit" not always best?** It is greedy. It ignores future incidents,
   and on the grid FIFO + nearest travelled further than the baseline.
8. **Why does the simulator jump between events?** Nothing changes between arrivals and
   completions, so ticking through empty steps would waste time. Termination is
   guaranteed because time strictly increases.
9. **Why are timings not part of `make test`?** They vary between runs and machines, so a
   fixed threshold would fail randomly. Correctness checks (equal distances) are
   deterministic and *are* tested.
10. **How does the baseline coexist with the heap?** FIFO ignores the heap. Dispatched
    incidents remain in it as stale entries, which the lazy-deletion design already
    allows, and the cross-check counts them as stale.

## I. Team tasks

| Member | Task |
|---|---|
| M5 (owner) | Present B2/B3 and §F. Run `make bench` on any machine where it runs and record the numbers here and in `TEST_REPORT.md`. Re-run `make test` to verify the `test_menu` fix. |
| M2 | Re-derive T2 (§E2) by hand. Explain the heap's role in the critical-response gain. |
| M3 | Re-derive the T1 distances (§E1). Interpret the Dijkstra timing results once available. |
| M4 | Interpret the hash-vs-linear timing. Explain why `clone_graph` keeps distances but may change tie routes. |
| M1 | Review the `dispatch.c` refactor (wrappers keep the old behaviour; `test_dispatch` 165/165). Decide on decisions 1–6. |

## J. Integration

- Menu option 20 calls `eval_run_suite(app->graph, out)`.
- `dispatchcore --benchmark` (or `make bench`) calls `eval_benchmark`.
- `evaluation.c` depends on `dispatch`, `app_state`, `report` and `dijkstra`. Nothing
  depends on it except the menu and `main`.
- Phase 11 (documentation and demo) will:
  - use §F as the "results" slide;
  - use T2 as the simplest explanation of why a priority queue matters.
- **Git (for you to do manually):**
  - new files: `src/evaluation.*`, `tests/test_evaluation.c`, this document, and the 4
    new logs in `docs/evidence/`;
  - modified: `src/dispatch.*`, `src/menu.c`, `src/main.c`, `tests/test_menu.c`,
    `Makefile` and `docs/TEST_REPORT.md`.
  - I ran no Git commands.
