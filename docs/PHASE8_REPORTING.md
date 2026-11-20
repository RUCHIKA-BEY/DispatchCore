# Phase 8 — History & Reporting

Owner: **Member 5** (with M1 integrating). Status: compiled with 0 warnings, statically
analysed clean, hand-reviewed. `test_report` has **not yet been executed**: Smart App
Control blocked it in the only run (see `docs/TEST_REPORT.md`, 2026-11-19 20:48).

New files: `src/report.h`, `src/report.c`, `tests/test_report.c`.
Changed files: `Makefile` only (new `REPORT_SRC`, `TEST_REPORT`). **No earlier `.c`/`.h`
file was modified in this phase.**

### Decisions for you to review

1. **A new module `report.c`**, instead of putting statistics into `dispatch.c` as the
   blueprint's file list suggested. Reports read *everything* (stores, heap, history,
   graph); the dispatch engine should not grow a second job. `report.c` sits on top of
   `dispatch.c` and nothing depends on it.
2. **Reports are strictly read-only**: every function takes `const AppState *`.
3. **Times are simulated clock ticks**, not seconds (blueprint decision D5). The report
   says so in its header line, so nobody reads "6.83" as minutes.
4. **"Wait" is measured to dispatch, not to arrival.** We have no travel speed, so we
   cannot say when a unit arrives; distance is reported separately.
5. **Unit ranking uses insertion sort**, written by hand. Fleets are tens of units, and
   insertion sort is the easiest sort to trace by hand in a viva.
6. **`report_write_file` creates or replaces one text file** whose path the caller passes.
   It never touches the CSV data files. Phase 9 will decide the default name (suggestion:
   `data/report.txt`, added to `.gitignore`).
7. **Not done in this phase:** incident "hotspots" per location and per-type response
   times. They are easy to add later on top of `SystemStats` if the team wants them.

---

## A. Purpose

Phases 1–7 built a system that *does* things: it queues incidents, routes units and saves
records. Phase 8 lets the operator — and the examiner — **see what happened**:

| Report | Question it answers |
|---|---|
| System statistics | How many incidents and units are in each state? How far did units travel? How long did incidents wait, and did critical ones wait less? |
| Allocation history (with filters) | What did unit 2 do all day? Who was sent to incident 1001, and by which route? |
| Completed incidents | Which emergencies are finished, and how long did each take? |
| Unit utilisation | Which units are overworked, and which were never used? |
| Cross-check | Do the counters kept in different structures still agree? |
| Full report file | One text file with all of the above, for submission or comparison. |

Phase 10 (baseline vs optimised strategy) will compare two runs using exactly these numbers:
total distance, average wait, and average wait per severity.

## B. Concepts

**B1. Aggregation in one pass.** Each statistic is a *fold* over a collection: start at 0,
look at each element once, update counters. `report_compute_stats` does four passes, one
per structure: the incident store (N), the heap array (P), the resource store (R) and the
history list (H). Total O(N + P + R + H). Nothing is sorted and nothing is searched
linearly. The one lookup per history node (incident by ID) uses the Phase 5 hash index:
O(1) on average.

**B2. Store sums, compute averages late.** `SystemStats` keeps `wait_sum` and `wait_count`,
not `average_wait`. Two reasons:
- tests compare integers exactly (`41` and `6`), with no floating-point tolerance;
- sums from two runs can be added together, but averages cannot.

`report_average` divides only when printing, and returns 0 when the count is 0 (no division
by zero). The printed report shows `-` instead of `0.00`, because 0.00 would wrongly look
like a measurement.

**B3. Filtering a linked list.** The history is a singly linked list in dispatch order
(Phase 6). A filtered view is a single walk that skips non-matching nodes, O(H). The
`HistoryFilter` struct uses `-1` to mean "any", so one function serves "all", "active
only", "unit 2" and "incident 1001".

**B4. Redundancy as a self-test (cross-check).** Some facts are recorded twice, by
different code:

| Fact | Copy 1 | Copy 2 |
|---|---|---|
| number of dispatches | `Resource.total_dispatches` (in `resource_assign`) | number of history nodes |
| total distance | `Resource.total_distance` | sum of `Allocation.distance` |
| active jobs | DISPATCHED incidents, DISPATCHED units | ACTIVE allocations |
| live queue entries | heap entries − stale entries | PENDING incidents |

If the copies ever disagree, some code path updated one copy and not the other. That is a
bug, or a hand-edited CSV file. `report_cross_check` compares the copies and names every
mismatch. This complements Phase 5's `app_check_consistency`, which checks pointers and
indexes rather than totals.

**B5. Insertion sort.** To rank units, take them one at a time from the store and insert
each into its place in the already-sorted prefix. Existing entries are shifted right until
the gap is found.

```
store order:  R1(1) R2(2) R3(1) R4(1) R5(1) R6(0)     (number = dispatches)
take R1:      [R1]
take R2:      R2 beats R1 -> shift R1 right      [R2 R1]
take R3:      R3 vs R1: same jobs, same dist, id 3 > 1 -> stop   [R2 R1 R3]
take R4:      [R2 R1 R3 R4]
take R5:      [R2 R1 R3 R4 R5]
take R6:      0 jobs, beats nobody            [R2 R1 R3 R4 R5 R6]
```

Its properties:
- **Best case O(R):** the input is already in order, so each new unit is compared once.
- **Worst case O(R²):** the input is in reverse order, so each new unit shifts past
  everything before it.
- **In place:** it works inside the `ranking` array, with O(1) extra memory.
- **Stable.** Our comparison ends with the ID, so there are no ties anyway, and the
  ranking is fully deterministic.

Why not merge sort or heap sort, which take O(R log R)? R is a few dozen units, so the
difference is microseconds. Insertion sort is also the clearest one to trace on paper.
(We already have a heap in Phase 3; reusing it here would teach nothing new.)

**B6. Borrowed pointers in the ranking.** The ranking array holds `const Resource *`
pointers *into* the store. They are borrowed, exactly like the heap's pointers in Phase 3.
`report_print_utilisation` frees the **array**, never the units.

**B7. Checked file output.** `report_write_file` follows Phase 7's rule that output can fail
in three places:
- `fopen` fails, e.g. the folder does not exist;
- a write fails, which shows up in `ferror`;
- `fclose` fails while flushing the buffer.

All three return `DC_ERR_IO`.

## C. Code tour

### `report.h`

- `SystemStats`: every number, as integer sums and counts. Index 0 of the per-severity
  arrays is unused, so `slot[s]` holds severity `s`.
- `HistoryFilter {status, incident_id, resource_id}`: `-1` means "any".
- Statistics: `report_compute_stats`, `report_average`, `report_cross_check` and
  `report_print_stats`.
- History: `report_filter_all`, `report_allocation_matches`, `report_count_history` and
  `report_print_history`.
- `report_print_completed`.
- Ranking: `report_busier` (the comparison), `report_rank_resources` (the insertion sort)
  and `report_print_utilisation`.
- Full report: `report_print_full` and `report_write_file`.

### `report.c`, function by function

| Function | Notes |
|---|---|
| `format_place` | `"5 Old Town"` into a buffer of `12 + DC_NAME_MAX` bytes: enough for any int, a space, and the longest name with its `'\0'`. |
| `format_average` / `print_average` | `"%.2f"` or `"-"` when the count is 0. |
| `report_compute_stats` | Zeroes the struct, sets `distance_min/max = -1` ("none"), then makes the four passes from B1. Enum values are range-checked before being used as array indexes, so a corrupted value can never write outside an array. A history node whose incident is missing is counted in `missing_incident_refs` and skipped for waiting time. |
| `disagree` / `report_cross_check` | Seven comparisons (B4). Returns the number that failed. |
| `report_print_stats` | Fixed English text; widths chosen so the severity table lines up. Severities are printed from 5 down to 1, the order an operator cares about. |
| `report_allocation_matches` | `NULL` filter = match all; each field is checked only if it is not `-1`. |
| `report_print_history` | Three lines per allocation: what/where, who/how far/when, and the route with names (reusing `dispatch_print_route` from Phase 6). "done at" appears only for COMPLETED; "waited" only if the incident record exists. |
| `report_print_completed` | Table of COMPLETED allocations in dispatch order, with wait and service ticks. |
| `report_busier` | More dispatches → longer total distance → lower ID. Strict, so `busier(a, a)` is 0. |
| `report_rank_resources` | Checks `capacity` **before** writing anything, then runs the insertion sort of B5. |
| `report_print_utilisation` | `malloc`s the ranking array, ranks, prints, then frees the array (and never the units). Returns -1 if out of memory. |
| `report_print_full` | Header, statistics, cross-check result, utilisation, completed incidents, full history. |
| `report_write_file` | `fopen` → `report_print_full` → `ferror` → `fclose`, with every failure mapped to `DC_ERR_IO` (B7). |

## D. Algorithm walkthrough: `report_compute_stats`

```
st = all zeros; distance_min = distance_max = -1; st.clock = app.clock
for each incident:      total++, by_status[status]++, by_type[type]++,
                        failed_attempts += dispatch_attempts
for each heap slot i:   entries++, if data[i].status != PENDING: stale++
for each unit:          total++, by_status++, by_type++,
                        dispatches_by_type[type] += total_dispatches,
                        dispatch_sum += ..., distance_sum += ...
for each allocation a (oldest first):
    total++; COMPLETED ? (completed++, service += complete_seq - dispatch_seq)
                       : active++
    distance_sum += a.distance; update min / max
    inc = hash lookup(a.incident_id)
    if inc missing: missing_refs++; continue
    wait = a.dispatch_seq - inc.reported_seq
    wait_sum += wait; wait_count++
    wait_by_severity[inc.severity] += wait; count_by_severity[...]++
```

## E. Worked example (the values used in `test_report.c`)

### E1. Building the scenario

This continues the Phase 6 "seven incidents" example (PHASE6 doc §E3). Every
registration, dispatch, completion and cancellation advances the clock by 1; changing a
unit's service status does not.

| Step | Action | Clock value used | Clock after |
|---|---|---|---|
| 1 | Register R1..R6 (amb@0, amb@6, fire@2, rescue@4, fire@7, amb@8) | 0..5 | 6 |
| 2 | Register incidents 1001..1007 (below) | reported_seq 6..12 | 13 |
| 3 | `dispatch_all` → 5 dispatches (Phase 6 trace) | dispatch_seq 13..17 | 18 |
| 4 | Complete 1002 (alloc #1); R3 released at 2 | complete_seq 18 | 19 |
| 5 | Complete 1007 (alloc #4); R2 released at 1 | complete_seq 19 | 20 |
| 6 | Cancel 1003 (stays in the heap: lazy deletion) | — | 21 |
| 7 | R6 out of service | — | 21 |
| 8 | `dispatch_next` → 1001 gets R2 (alloc #6) | dispatch_seq 21 | 22 |

The incidents:

| Incident | Type | Location | Severity | reported_seq |
|---|---|---|---|---|
| 1001 | MEDICAL | 5 | 3 | 6 |
| 1002 | FIRE | 2 | 5 | 7 |
| 1003 | ROAD | 6 | 3 | 8 |
| 1004 | STRUCT | 4 | 4 | 9 |
| 1005 | MEDICAL | 0 | 5 | 10 |
| 1006 | FIRE | 7 | 1 | 11 |
| 1007 | ROAD | 1 | 4 | 12 |

**How step 8 works.** The heap holds 1001 (severity 3, seq 6) and the cancelled 1003. 1001
is extracted first. Its compatible units are the ambulances R1, R2 and R6:
- R1 is busy with 1005;
- R6 is out of service;
- R2 is at 1 after step 5.

Dijkstra from 5 gives a distance of 6 to location 1, via road 1–5 (weight 6). The
alternative route 5-3-0-1 is 2+7+4 = 13. So R2 is sent, with route `1 > 5` and distance 6.
The pass stops after this success, so the stale 1003 entry stays in the heap.

### E2. The allocation history

| # | Incident (sev) | Unit | From → To | Dist | Sent | Done | Wait = sent − reported | Service |
|---|---|---|---|---|---|---|---|---|
| 1 | 1002 (5) | R3 | 2 → 2 | 0 | 13 | 18 | 13 − 7 = **6** | 5 |
| 2 | 1005 (5) | R1 | 0 → 0 | 0 | 14 | – | 14 − 10 = **4** | – |
| 3 | 1004 (4) | R4 | 4 → 4 | 0 | 15 | – | 15 − 9 = **6** | – |
| 4 | 1007 (4) | R2 | 6 → 1 | 15 | 16 | 19 | 16 − 12 = **4** | 3 |
| 5 | 1006 (1) | R5 | 7 → 7 | 0 | 17 | – | 17 − 11 = **6** | – |
| 6 | 1001 (3) | R2 | 1 → 5 | 6 | 21 | – | 21 − 6 = **15** | – |

### E3. Expected statistics

- **Incidents** (7):
  - by status: PENDING 0, DISPATCHED 4 (1004, 1005, 1006, 1001), COMPLETED 2 (1002, 1007),
    CANCELLED 1 (1003);
  - by type: MEDICAL 2, ROAD 2, FIRE 2, STRUCT 1;
  - failed attempts: 1001 had 2 and 1003 had 2 (Phase 6 passes 5 and 6), so **4** in total.
- **Queue:** 1 entry, 1 stale (the cancelled 1003), 0 live.
- **Units** (6):
  - by status: AVAILABLE 1 (R3), DISPATCHED 4 (R1, R2, R4, R5), OUT_OF_SERVICE 1 (R6);
  - by type: 3 ambulances, 2 fire units, 1 rescue unit;
  - dispatches by type: ambulance 1 + 2 + 0 = **3**, fire 2, rescue 1. Sum **6**;
  - distance by unit: R2 has 15 + 6 = 21, every other unit 0. Sum **21**.
- **Allocations:**
  - 6 in total: 4 active, 2 completed;
  - distance: total 21, average 3.50, min 0, max 15;
  - wait: 6+4+6+4+6+15 = **41** over 6, average **6.83**;
  - service: 5 + 3 = **8** over 2, average **4.00**.
- **Wait per severity:**
  - severity 5: {6, 4}, sum 10, average 5.00;
  - severity 4: {6, 4}, sum 10, average 5.00;
  - severity 3: {15}, average 15.00;
  - severity 2: none, shown as `-`;
  - severity 1: {6}, average 6.00.
- **Cross-check:** 6 = 6, 21 = 21, 4 = 4, 4 = 4, 2 = 2, 0 = 0, 0 = 0. So **0 problems**.

**What the wait-per-severity numbers show.** Severities 5 and 4 waited the least. 1001
(severity 3) waited 15 ticks, but not because of its priority: every ambulance was busy
or unreachable until step 5. The report makes that visible, and Phase 10 will use the same
numbers to compare strategies.

**Utilisation ranking:** R2 (2 jobs), then R1, R3, R4, R5 (1 job each, distance 0, so ID
order), then R6 (0 jobs). Expected order: **2, 1, 3, 4, 5, 6**.

### E4. Lines of the report file predicted exactly

`test_full_report_file` searches the written file for these lines. The spacing of the
severity lines follows from the format `"    %d %-9s %3d dispatched, ..."`:
- `Critical` is 8 characters, so it gets 1 padding space; then comes 1 literal space and
  `"  2"`. That gives 4 spaces before the 2.
- `Serious` is 7 characters, so it gets 2 padding spaces, then 1 literal space and `"  1"`.
  That gives 5 spaces before the 1.

```
SYSTEM STATISTICS (simulated clock = 22)
  by status: PENDING 0 DISPATCHED 4 COMPLETED 2 CANCELLED 1
  failed dispatch attempts: 4
Queue: 1 heap entries (0 live, 1 cancelled awaiting lazy removal)
  by status: AVAILABLE 1 DISPATCHED 4 OUT_OF_SERVICE 1
Allocations: 6 (active 4, completed 2)
  distance: total 21, average 3.50, min 0, max 15
  wait (reported -> dispatched): average 6.83 ticks over 6
  service (dispatched -> completed): average 4.00 ticks over 2
    5 Critical    2 dispatched, average wait 5.00
    3 Serious     1 dispatched, average wait 15.00
    2 Moderate    0 dispatched, average wait -
Cross-check: all counters agree
#1 COMPLETED: incident 1002 FIRE (severity 5) at 2 Fire Station North
    unit 3 FIRE_UNIT from 2 Fire Station North, distance 0, sent at 13, done at 18, waited 6
    route: 6 Airport Road > 7 University > 4 Industrial Zone > 2 Fire Station North > 1 City Square
#6 ACTIVE: incident 1001 MEDICAL (severity 3) at 5 Old Town
    unit 2 AMBULANCE from 1 City Square, distance 6, sent at 21, waited 15
```

(The `route:` line shown belongs to allocation #4; the test searches for each line
separately.)

## F. Testing (`tests/test_report.c`)

| Test | What it proves |
|---|---|
| `test_empty_state` | No records: all zero, min/max = -1, 0 rows printed, the ranking of 0 units fits in 0 slots. |
| `test_invalid_arguments` | Every `NULL` and empty path is rejected with the documented value. Nothing crashes. |
| `test_average` | Count 0 or negative gives 0.0. 21/6 = 3.5 exactly; 41/6 is within ±0.001 of 6.833. |
| `test_scenario_statistics` | Every number of E3. The cross-check finds 0 problems, and Phase 5's checker agrees. The cross-check does catch 1 and 2 planted mismatches. Computing and printing changes nothing (clock still 22). |
| `test_history_filters` | Counts per filter: all 6, active 4, completed 2, unit 2 → 2, unit 2 completed → 1, unit 6 → 0, incident 1001 → 1, cancelled 1003 → 0. |
| `test_completed_report` | Exactly 2 completed rows. |
| `test_busier_rule` | Each tie-break level, and strictness (`busier(a, a)` = 0). |
| `test_ranking` | The scenario ranks 2, 1, 3, 4, 5, 6, and too small a capacity gives -1. Also: reverse order (the insertion sort worst case), all-equal stats keeping ID order, and distance breaking a tie in dispatches. |
| `test_full_report_file` | The E4 lines appear in the written file, and no `CROSS-CHECK FAILED` text does. A missing folder gives `DC_ERR_IO`. |

The file is written to `build/test_data/report.txt`, which `make test` creates. Nothing in
`data/` is touched.

**Evidence status:**
- Compiled ✅
- Analysed ✅ (`-fanalyzer`, 28 files)
- Reviewed ✅
- **Executed ❌**: blocked by Smart App Control on 2026-11-19 20:48.

In that same run, three earlier programs did run and passed: `test_incident` 89/89,
`test_resource` 98/98 and `test_hash_table` 67/67. The full log is in `docs/evidence/`.

## G. Complexity

| Operation | Time | Extra space |
|---|---|---|
| `report_compute_stats` | O(N + P + R + H), with H hash lookups at O(1) average | O(1) |
| `report_cross_check` | O(1) | O(1) |
| `report_count_history` / `report_print_history` | O(H), plus route printing O(total route length) | O(1) |
| `report_print_completed` | O(H) | O(1) |
| `report_rank_resources` | O(R) best, O(R²) worst | O(1) (uses the caller's array) |
| `report_print_utilisation` | O(R²) worst | O(R) for the ranking array |
| `report_print_full` / `report_write_file` | O(N + P + R² + H + routes) | O(R) |

N = incidents, P = heap entries, R = units, H = allocations.

## H. Viva questions (with short answers)

1. **Why does every report function take `const AppState *`?** So the compiler guarantees
   that reports cannot change the simulation. A report that changed data would make its
   own output wrong.
2. **Why store `wait_sum` and `wait_count` instead of the average?** Integers compare
   exactly in tests, sums from several runs can be combined, and there is no division by
   zero until printing.
3. **What exactly is "wait"?** `dispatch_seq − reported_seq` in clock ticks: the number of
   events between the report and the dispatch. It is not real time, and not arrival time.
4. **Why can `heap_entries` be larger than the number of PENDING incidents?** Lazy deletion.
   A cancelled incident stays in the heap until it reaches the top and is discarded.
5. **What does the cross-check catch that `app_check_consistency` does not?** Totals that
   drift apart, e.g. `total_distance` updated without a history node, or a hand-edited
   CSV with wrong statistics.
6. **Walk through insertion sort on 3, 1, 2.**
   - Start: [3].
   - Insert 1: 1 < 3, so it goes after 3: [3, 1].
   - Insert 2: 2 beats 1, so 1 shifts right; 2 does not beat 3, so it stops: [3, 2, 1].
   - Comparisons: 1 + 2 = 3, the worst case for n = 3, which is n(n−1)/2 = 3.
7. **Why insertion sort and not quicksort?** R is small. Insertion sort is simple,
   in place and stable, and its worst case of O(R²) is negligible for tens of units.
   Quicksort's worst case is also O(R²), and it is not stable.
8. **Why does `report_busier` end with the ID?** To make it a strict total order. Any two
   different units have a defined order, so the ranking is identical on every machine.
9. **Who frees the ranking array, and who frees the units in it?** `report_print_utilisation`
   frees the array. The units belong to the `ResourceStore` and are freed by `app_free`.
10. **Why check `fclose`?** Output is buffered; the final write to disk can happen inside
    `fclose` and fail, e.g. if the disk is full.

## I. Team tasks

| Member | Task |
|---|---|
| M5 (owner) | Present B2/B4 and the E3 trace; recompute E3 by hand independently; run `make test` on any machine where it is allowed and log the result. |
| M4 | Confirm that the cross-check rules match the CSV loader's guarantees (Phase 7): a hand-edited `resources.csv` with a wrong `total_distance` should load but fail the cross-check. |
| M2 | Present insertion sort (B5, viva Q6/Q7) and compare it with the heap from Phase 3. |
| M3 | Check the route and distance of allocation #6 (1 → 5, 6) by running Dijkstra by hand. |
| M1 | Decide the default report file name for Phase 9 and add it to `.gitignore`. Review the decisions at the top of this document. |

## J. Integration

- **Uses:** `app_state.h` (stores, heap, indexes, history, clock), `dispatch.h`
  (`dispatch_print_route`), and the name functions of `incident`, `resource` and `history`.
- **Used by:**
  - Phase 9 menu options 18 "Allocation history" (`report_print_history` with a filter
    chosen by the user) and 19 "System statistics" (`report_compute_stats` +
    `report_print_stats` + `report_cross_check`), plus an option to write the report file;
  - Phase 10 `evaluation.c`, which will run the same scenario under two strategies and
    compare their `SystemStats` (total distance, average wait, wait per severity).
- **Makefile:** `REPORT_SRC` is part of `ALL_SRC`, so `make analyze` covers it, and
  `test_report` is part of `make test`.
- **Git (for you to do manually):** new files `src/report.h`, `src/report.c`,
  `tests/test_report.c`, `docs/PHASE8_REPORTING.md` and
  `docs/evidence/2026-11-19_2048_phase8_make_test.log`; modified `Makefile` and
  `docs/TEST_REPORT.md`. I ran no Git commands.
