# DispatchCore — Viva Notes

Revision notes for all five members. The phase documents (`PHASE1`–`PHASE10`) hold the
full explanations; this file is the condensed version. **Every member must be able to
explain §1 and §2**, not only their own module.

---

## 1. The whole flow in 60 seconds

1. **Start-up.** `main` → `fm_load("data")`.
   - It reads the map (`locations.csv`, `roads.csv`) into a **graph** (adjacency lists),
     then the units, incidents and allocations.
   - Each record is `malloc`'d and owned by its **store**. The **hash indexes** (ID → record)
     and the **max-heap** of pending incidents only borrow pointers.
   - The loader then checks consistency; if anything contradicts, it refuses to start
     **without saving**.
2. **Register an incident.** It gets the next ID and the current clock value as
   `reported_seq`, and goes into the store, the index and the heap: all three or none.
3. **Dispatch next.** Extract the top of the max-heap: highest severity first, then the
   earliest report.
   - Cancelled entries are skipped (**lazy deletion**).
   - For the incident, filter units: **compatible → available → reachable**.
     Reachability comes from **one Dijkstra from the incident**, because roads are
     undirected, so one run gives every unit's distance.
   - Choose the **nearest** unit; a tie goes to the lower ID.
   - Commit: append to the **history list** first (the only step that can fail), then
     mark the unit and the incident DISPATCHED.
   - Incidents that cannot be served are put back after the pass (a **bounded pass**: each
     is tried once), so one blocked incident never blocks the others and the loop always
     ends.
4. **Complete.** The incident becomes COMPLETED, the unit is released **at the incident
   location**, and the allocation is marked completed.
5. **Save.** All three record files are written to `.tmp`. Only if all three succeed are
   the old files renamed to `.bak` and the new ones moved into place.
6. **Reports and evaluation** read everything through `const` pointers. The strategy
   comparison runs on **copies**.

## 2. Ten ideas the examiner will probe

| Idea | One-line answer |
|---|---|
| **Ownership** | Stores own records; the heap and indexes borrow pointers. Borrowers are freed first, owners last. Each record is `malloc`'d individually, so `realloc` of a pointer array never moves the records themselves. |
| **One `AppState`** | All live data is in one struct passed by pointer. No mutable globals, so tests can build private states. |
| **Lazy deletion** | Cancelling only changes the status, O(1). The stale heap entry is discarded when it reaches the top. Removing it from the middle of a heap would need an O(n) search. |
| **Bounded pass** | Extract each pending incident at most once per command, then re-insert the skipped ones. This guarantees termination and avoids head-of-line blocking. |
| **One Dijkstra per incident** | Undirected roads make d(unit, incident) = d(incident, unit). One run from the incident covers all R units: O((V+E) log V) instead of R times that. |
| **Filter order** | Compatibility, then availability, then reachability, then distance. A close but unsuitable unit is never chosen. |
| **State machines** | Incident: PENDING → DISPATCHED → COMPLETED, or PENDING → CANCELLED. Unit: AVAILABLE ↔ DISPATCHED (only through assign/release), and AVAILABLE ↔ OUT_OF_SERVICE. |
| **All-or-nothing** | Registration rolls back on failure. A commit does the failure-prone step first. Saving writes every `.tmp` before touching any real file. |
| **Determinism** | Ties break by ID. Our own random generator (LCG) with fixed seeds, never `rand()`. A simulated clock instead of wall time. |
| **Honest testing** | Evidence levels C/A/R/E. Smart App Control blocks many runs; only real output is reported as passed. |

## 3. Data structures — what, why, cost

| Structure | Why this one | Main costs | Likely question |
|---|---|---|---|
| **Adjacency-list graph** | Road networks are sparse. Memory is O(V+E) against O(V²) for a matrix, and visiting all neighbours costs O(deg). | add road O(deg) (duplicate check) | *When is a matrix better?* For dense graphs, or many "is there an edge u-v?" queries. |
| **Min-heap (Dijkstra)** | Gives the closest unvisited vertex in O(log n). | push/pop O(log n) | *What are stale entries?* An older, longer distance for a vertex still in the heap. It is skipped when popped (lazy Dijkstra). |
| **Dijkstra** | Shortest paths with non-negative weights. | O((V+E) log V) heap, O(V²) array | *Why not negative weights?* A finalised vertex could later get shorter, which breaks the greedy proof. |
| **Max-heap (incidents)** | Insert and extract-max in O(log n). A sorted array has O(n) insert; an unsorted array has O(n) extract. | O(log n); peek O(1) | *Parent/children of i?* (i−1)/2, 2i+1, 2i+2. |
| **Hash table (chaining)** | Lookup by ID in O(1 + α) on average. | worst case O(n) | *Why 101 buckets?* A prime spreads key patterns (e.g. multiples of 10) evenly. α = n / 101. |
| **Linked-list history** | Append-only, ordered, never removed. The tail pointer makes appending O(1). | find O(H) | *Why not an array?* Its size is unknown, and there is no random access need. |
| **Pointer arrays (stores)** | Registration order for listings and saving; O(1) access by index. | registration O(N) (duplicate scan) | *Why pointers, not structs?* Records must never move, because others hold pointers to them. |
| **Insertion sort** | Ranks a few dozen units. Simple and stable. | O(R) best, O(R²) worst | *Trace it on 3, 1, 2.* Worst case n(n−1)/2 comparisons. |

## 4. Numbers worth remembering

- **Reference city:**
  - 10 locations and 11 roads;
  - **8–9 is an island** (2 connected parts);
  - shortest distances: 0→5 = **9** (0-3-5), 0→7 = **16** (0-1-2-4-7), 6→1 = **15**
    (6-7-4-2-1).
- **IDs** start at 1001 for incidents, 1 for units, 1 for allocations. The **clock**
  advances by 1 on every registration, dispatch, completion and cancellation.
- **Hash index:** 101 buckets. **Line limits:** CSV 8,192 characters; menu input 126.
- **Evaluation:**
  - speed 5 distance units per step;
  - response = wait + travel;
  - heavy load: critical average **≈ 40 → ≈ 5** on the reference city, **≈ 36 → ≈ 6** on
    the grid.
- **Timing** (one run, coarse clock):
  - Dijkstra on 961 locations: heap **9 ms** vs array **67 ms**;
  - 4000 units: hash **5 ms** vs linear **208 ms** for 100,000 lookups.

## 5. Questions that need careful, honest answers

1. **"Did you test this?"**
   - Every program compiles with zero warnings, and GCC's static analyser is clean on all
     35 files.
   - Execution is partly blocked by Smart App Control. `docs/TEST_REPORT.md` lists exactly
     which programs ran. For example: `test_dispatch` 165/165, `test_evaluation` 180/180,
     the end-to-end tests 37/37, and `test_minheap` never.
   - Never claim more than that file says.
2. **"Did anything fail?"** Yes, and that is good to show.
   - The first real runs of `test_menu` and `test_report` failed. The tests expected `\n`,
     but Windows text mode writes `\r\n`. The program's output was right.
   - Both tests were fixed. The fixes are not yet re-run.
   - Lesson: real execution finds what review misses.
3. **"Is the optimised strategy always better?"** No.
   - The priority queue makes minor calls wait longer. Under light load, its average
     response over all incidents was worse than FIFO + nearest.
   - The nearest-unit rule is greedy. On the grid, FIFO + nearest travelled *more* than
     the baseline.
4. **"Is this a real dispatch system?"** No. It is an academic simulation with made-up
   distances, no traffic, and one unit per incident.
5. **"Why is completing an incident O(H)?"** It searches the history for the active
   allocation. Storing a pointer to the allocation in the incident would make it O(1);
   this is listed as a possible improvement.
6. **"What happens if saving fails halfway?"**
   - If writing a `.tmp` fails, the originals are untouched.
   - If one of the final renames fails, the files may be mixed; the `.bak` copies allow
     recovery by hand.
   - The data in memory is intact, and the user can retry.
7. **"How do you stop a menu from looping forever on bad input?"**
   - `fgets` reads whole lines and `strtol` checks them.
   - Over-long lines are discarded up to `'\n'`.
   - End of input returns `INPUT_EOF`, and the program exits cleanly.

## 6. What each member must explain

| Member | Own topics (in depth) | Phase documents |
|---|---|---|
| **M1** | Architecture; ownership; `AppState`; the bounded pass; single Dijkstra; state machines; integration and the `dispatch.c` refactor (Phase 10) | 0, 5, 6, 9 |
| **M2** | Both heaps; heapify up/down; tie-break rule; lazy deletion; why bottom-up heapify is O(n); insertion sort | 2, 3, 8 |
| **M3** | Graph representation; BFS components; Dijkstra and stale entries; route reversal; heap vs array timing | 1, 2, 10 |
| **M4** | Compatibility table; unit state machine; hashing (prime size, load factor, chaining vs open addressing); CSV validation and safe save | 4, 5, 7 |
| **M5** | Test strategy and evidence levels; the test-report history, including the two real test failures; e2e scripts; the evaluation design (fairness, factorial design, reproducibility, timing limits) | 8, 9, 10, TEST_REPORT |

## 7. Practice: trace these by hand

1. Insert severities 3, 5, 3, 4, 5, 1, 4 into the max-heap (Phase 3 doc). Show the array
   after each insert.
2. Dijkstra from 0 on the reference city: the `dist` and `prev` tables (Phase 2 doc).
3. The seven-incident `dispatch_all` (Phase 6 doc §E3): who goes where, and why 1001 and
   1003 stay pending.
4. T2 in Phase 10 §E2: one ambulance, FIFO vs priority, with response times 1/5 vs 2/6.
5. Hash keys 1, 6, 11, 16 into 5 buckets (Phase 5): the chain order, then delete the
   middle key.
