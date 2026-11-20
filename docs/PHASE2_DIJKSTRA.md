# Phase 2 — Min-Heap and Dijkstra's Algorithm

> **Verification status (2026-11-19)**
>
> | Program | Compiled, 0 warnings | `make analyze` clean | Hand-reviewed | **Executed** |
> |---|---|---|---|---|
> | `test_minheap` | ✅ | ✅ | ✅ | ❌ **not yet** (blocked by Smart App Control) |
> | `test_dijkstra` | ✅ | ✅ | ✅ | ❌ **not yet** (blocked by Smart App Control) |
>
> No Phase 2 test has been run, so **no Phase 2 test is marked as passed**. Every "expected"
> value below was calculated by hand. See `docs/TEST_REPORT.md`.

## Files in this phase

| File | Status | Contents |
|---|---|---|
| `src/minheap.h` / `src/minheap.c` | **new** | `MinHeapEntry`, `MinHeap`, init/free/push/pop/peek, invariant checker |
| `src/dijkstra.h` / `src/dijkstra.c` | **new** | `ShortestPaths`, `dijkstra_run` (heap), `dijkstra_run_array` (O(V²) reference), path building, path validation |
| `tests/test_minheap.c` | **new** | 7 test functions for the heap |
| `tests/test_dijkstra.c` | **new** | 10 test functions, including hand-computed tables and 60 random graphs |
| `docs/TEST_REPORT.md` | **new** | evidence levels, the Smart App Control limitation, execution log |
| `tests/test_util.h` | **modified** | added `#include <stdint.h>` and `test_random()`, a deterministic random generator |
| `Makefile` | **modified** | two new test programs; `make test` now runs all of them; new `make analyze` target |
| `docs/PHASE1_GRAPH.md`, `docs/PHASE0_BLUEPRINT.md` | **modified** | status updated; references to tools outside our MSYS2 setup removed |

No Phase 1 source file (`src/graph.*`, `src/common.*`) was changed.

---

## A. Purpose

- **`minheap`** is a priority queue that always hands back the item with the **smallest**
  distance. It exists only to make Dijkstra fast.
- **`dijkstra`** answers *"what is the shortest road distance from location S to every other
  location, and which route achieves it?"* The dispatch engine (Phase 6) calls it once per
  incident to rank the candidate resources by distance.

---

## B. Concepts you need first

### B1. Complete binary trees stored in an array

A **binary tree** is a tree in which each node has at most two children. A **complete**
binary tree fills each level fully, left to right, before starting the next. That shape lets
us store the tree in a plain array with **no pointers**:

```
index:       0   1   2   3   4   5   6
               ┌───────┴───────┐
level 0:       0
level 1:     1   2
level 2:    3 4 5 6

parent(i) = (i - 1) / 2        (integer division)
left(i)   = 2i + 1
right(i)  = 2i + 2
```

Check: the children of 1 are 3 and 4; the parent of 6 is (6−1)/2 = 2. ✓

### B2. The heap property

A **min-heap** is a complete binary tree in which **every parent ≤ its children**.
Consequences:

- The minimum is always at index 0, so `peek` is O(1).
- Siblings are *not* ordered, so a heap is **not** a sorted array. That is exactly why it is
  cheaper to maintain than a sorted array.
- The height of a tree with n items is ⌊log₂ n⌋. A tree of 1,000 items is only 9 levels tall.

### B3. The two repair operations

**Sift-up (after push).** Put the new item in the first free slot, which keeps the tree
complete. While it is smaller than its parent, swap them.

```
push (1,6) into [(4,12)]:     [(4,12),(1,6)]  → 6 < 12, swap → [(1,6),(4,12)]
```

**Sift-down (after pop).** Take out the root. Move the *last* item into the root, which keeps
the tree complete. While it is larger than its smaller child, swap it with that child.

Each operation moves along one root-to-leaf path, so each costs **O(log n)**.

### B4. Our ordering rule and why ties matter

`entry_less(a, b)` is true when `a.dist < b.dist`, or when the distances are equal and
`a.vertex < b.vertex`. The tie rule makes the pop order **fully deterministic**. The same
graph always produces the same routes, so test expectations can be exact.

### B5. Max-heap (Phase 3) vs min-heap (this phase)

The same array formulas and the same sift-up/sift-down code. Only the comparison flips.

| | Min-heap (Dijkstra) | Max-heap (incidents, Phase 3) |
|---|---|---|
| Root | smallest distance | highest-priority incident |
| Element | `{vertex, dist}` stored **by value** | `Incident *` **pointer** to a record owned elsewhere |
| Lifetime | created and destroyed inside one `dijkstra_run` call | lives for the whole program |
| Duplicates | expected (lazy method) | never (each incident is inserted once) |

### B6. Shortest paths and *relaxation*

Keep a **tentative distance** `dist[v]` for every vertex: 0 for the source and ∞ ("not
reached yet") for the rest. **Relaxing** a road u→v of length w means:

```
if dist[u] + w < dist[v]:
    dist[v] = dist[u] + w      (found a shorter way to v)
    prev[v] = u                (remember we came from u)
```

`prev[]` stores, for each vertex, the vertex just before it on the best known route.
Following `prev` backwards from any vertex leads back to the source.

### B7. Dijkstra's algorithm

```
dist[all] = ∞ ; dist[source] = 0 ; push (source, 0)
while heap not empty:
    (u, d) = pop smallest
    if u already finalised: skip          ← stale entry (lazy deletion)
    finalise u                            ← dist[u] is now the true shortest distance
    for each road u→v of length w:
        if v not finalised: relax(u, v, w), and if dist[v] improved: push (v, dist[v])
```

**Why is it correct to finalise u?** u has the smallest tentative distance d among all
unfinalised vertices. Any other route to u must at some point leave the finalised set through
some unfinalised vertex x, and dist[x] ≥ d. All road lengths are ≥ 0, so the remaining part of
that route cannot make it shorter. So no route to u is shorter than d. This is a **greedy
algorithm**: it makes the locally best choice, and the argument above shows it is also
globally correct.

**Why negative weights break it.** With roads A→B = 5, A→C = 2 and C→B = −4, Dijkstra
finalises C (2), then B (5). But A→C→B = −2 is shorter, and B is already final. The proof
above relied on "the rest of the route cannot make it shorter", which fails when a weight is
negative. That is why `graph_add_road` rejects any weight < 1.

### B8. Lazy deletion vs decrease-key

When dist[v] improves, the old `(v, oldDist)` entry is still in the heap. There are two
options:

- **Decrease-key:** find the old entry and lower it in place. You need an extra array that
  records each vertex's position in the heap, updated on every swap.
- **Lazy deletion (ours):** push a new `(v, newDist)` entry and leave the old one. The new
  entry is smaller, so it comes out first and v is finalised. The old entry comes out later,
  and the `finalised[u]` check skips it.

Both give O((V+E) log V). Lazy deletion is shorter, has fewer ways to go wrong, and is the
standard modern choice. The cost is a heap holding up to 2E+1 entries instead of V.

### B9. Unreachable vertices and `DC_INF`

A vertex in a different component is never relaxed, so its distance stays at the "∞" marker.
We represent ∞ as **−1** (`DC_INF`) rather than `INT_MAX`. That way no code can ever compute
`INT_MAX + w`, which is signed-integer overflow and therefore **undefined behaviour** in C.
`relax()` checks `dist[v] == DC_INF` explicitly before comparing.

---

## C. Code (where things are)

- `src/minheap.h`: the interface and the array-index formulas, as comments.
- `src/minheap.c`: `entry_less`, `swap_entries`, `sift_up`, `sift_down`, `grow` (all
  `static`, i.e. private to the file) and the public functions.
- `src/dijkstra.h`: the `ShortestPaths` result struct and every function's contract.
- `src/dijkstra.c`: helpers `reset_result`, `prepare_result` and `relax`, then the heap
  version, the array version, and the result functions.

---

## D. Code walkthrough

### D1. `minheap.c`

| Function | Key points |
|---|---|
| `entry_less` | The **only** place the ordering is defined. Change it here and the whole heap follows. |
| `sift_up(h, i)` | Loops while `i > 0`; stops as soon as the parent is not larger. |
| `sift_down(h, i)` | `left >= size` means i is a leaf, so stop. Pick the **smaller** child (`right` only if it exists). Swap only if that child is smaller than i. Swapping with the larger child would put a larger value above the smaller one and break the invariant. |
| `grow` | Doubles the capacity with `realloc`. The result goes into a **temporary** pointer (`bigger`). If `realloc` fails it returns `NULL` but the old block is still valid; writing `h->data = realloc(...)` directly would lose (leak) it. It also refuses to double past `INT_MAX`. |
| `minheap_init` | Puts the heap in a safe state (`data = NULL`) **before** validating, so even a failed init can be freed safely. |
| `minheap_free` | Sets `data = NULL` after `free`, so a second call is harmless (`free(NULL)` does nothing). |
| `minheap_push` | Grow if full, write into slot `size`, increment, then sift up. |
| `minheap_pop` | Copy the root out, `size--`, move the last item to the root, sift down. When the heap held one item, nothing is moved. |
| `minheap_check_invariant` | Checks every child against its parent, O(n). Used only by tests. |

**Why does doubling make push O(1) amortised?** Growing from 1 to n copies 1+2+4+…+n/2 < n
items in total, which is less than one copy per push on average.

### D2. `dijkstra.c`: the shared helpers

- **`prepare_result`** validates the arguments, allocates `dist` and `prev` (size V), fills
  them with `DC_INF` and −1, and sets `dist[source] = 0`. Both implementations call it, so
  they cannot disagree about starting conditions.
- **`relax`** is written once and used by both versions. It uses strict `<`: when two routes
  are equally short, the one found **first** is kept. That makes routes deterministic.
- **Overflow argument:** a shortest path has at most V−1 roads, each ≤ 10,000, and V ≤ 1,000.
  So any distance is < 10⁷, and `dist[u] + weight` can never exceed `INT_MAX` (≈ 2.1 × 10⁹).

### D3. `dijkstra_run`: the heap version

1. `finalised` is a `char` array zeroed by `calloc`. One byte per vertex is enough for 0/1.
2. The heap starts with capacity V and grows if needed.
3. **Error handling.** Every resource acquired (`finalised`, the heap, the result arrays) is
   released on every failure path. If a push runs out of memory partway through, the function
   frees the half-finished result and returns `DC_ERR_NOMEM`. It **never returns half an
   answer** that the dispatch engine might trust.
4. In the main loop, `if (finalised[u]) continue;` **is** the lazy deletion. The inner loop
   skips finalised neighbours, because their distance can no longer improve.

### D4. `dijkstra_run_array`: the O(V²) reference

V rounds; each round scans all V vertices for the smallest finite, unfinalised distance (ties
go to the lowest index). When none is left, the remaining vertices are unreachable and the
loop stops early. It shares `relax`, so its *distances* must equal the heap version's. Tests
check this on every source of 61 graphs. This is not a "simpler replacement". It is an
**independent second implementation**, used as a test oracle and later for the Phase 10 speed
comparison.

### D5. `dijkstra_build_path`

- `prev[]` gives the route **backwards** (dest → source). We need it forwards, so there are
  two passes:
  1. **Count** the length by walking `prev`. The walk is capped at V steps: a real path has at
     most V vertices, so more steps means `prev` contains a cycle. That can only happen if the
     data is corrupted, and the cap turns a potential infinite loop into `DC_ERR_INVALID`.
     The walk also rejects out-of-range indices.
  2. `malloc` exactly `len` ints and fill them **from the end** while walking backwards.
- **Ownership:** the caller receives a new array and must `free` it. In Phase 6 that array is
  stored directly in the `Allocation` record (blueprint §6), so it is allocated once and never
  copied.
- An unreachable destination returns `DC_ERR_NOT_FOUND`; the destination equal to the source
  gives a path of length 1.

### D6. `dijkstra_validate_path`

This function deliberately does **not** use Dijkstra's arrays. It re-checks a route against
the graph: correct start, correct end, a real road between every consecutive pair, and lengths
that add up to the claimed distance. The sum is a `long`, because a test can pass any path.

### D7. Tests worth understanding

- **`certify_result`** in `test_dijkstra.c` proves a result is correct **without trusting
  Dijkstra**:
  1. every finite `dist[v]` is achieved by a real, validated route, so it is *not too small*;
  2. for every road u–v, `dist[u] ≤ dist[v] + w`, and a neighbour of a reachable vertex is
     reachable.

  Condition 2 implies that `dist[x]` is ≤ the length of *any* route to x (add the inequality
  along the route). Together with condition 1, `dist[x]` is exactly the shortest distance.
- **Symmetry test:** `dist(u,v) == dist(v,u)` for all pairs. This is the property blueprint
  decision **D4** relies on (one Dijkstra from the incident instead of one per resource).
- **Random-number detail:** in the heap stress test, `vertex` and `dist` are generated in
  **separate statements**. Writing `minheap_push(&h, rnd(), rnd())` would be legal C, but the
  **order in which function arguments are evaluated is unspecified**. Two compilers could
  produce different values and the test would not be reproducible.

---

## E. Example execution (hand trace, reference city)

### E1. Dijkstra from 0 (Central Hospital)

Adjacency-list order (from Phase 1): `0: 3,1` · `1: 5,2,0` · `2: 4,1` · `3: 6,5,0` ·
`4: 7,2` · `5: 7,3,1` · `6: 7,3` · `7: 6,5,4`.
Heap contents are shown **sorted for readability**; the real array is only heap-ordered.

| Step | Pop | Action | Updates (dist / prev) | Heap afterwards |
|---|---|---|---|---|
| 0 | — | init | d0 = 0 | (0,0) |
| 1 | (0,0) | finalise 0 | d3 = 7 /p 0, d1 = 4 /p 0 | (1,4) (3,7) |
| 2 | (1,4) | finalise 1 | d5 = 10 /p 1, d2 = 7 /p 1 | (2,7) (3,7) (5,10) |
| 3 | (2,7) | finalise 2 (**tie with 3: lower vertex first**) | d4 = 12 /p 2 | (3,7) (5,10) (4,12) |
| 4 | (3,7) | finalise 3 | d6 = 16 /p 3; **d5: 7+2 = 9 < 10** → p 3 | (5,9) (5,10) (4,12) (6,16) |
| 5 | (5,9) | finalise 5 | d7 = 17 /p 5 | (5,10) (4,12) (6,16) (7,17) |
| 6 | (5,10) | **stale**: 5 already final, skip | — | (4,12) (6,16) (7,17) |
| 7 | (4,12) | finalise 4 | **d7: 12+4 = 16 < 17** → p 4 | (6,16) (7,16) (7,17) |
| 8 | (6,16) | finalise 6 (tie with 7) | 7 via 6 = 19, no change | (7,16) (7,17) |
| 9 | (7,16) | finalise 7 | all neighbours final | (7,17) |
| 10 | (7,17) | **stale**, skip | — | empty |

Result (what `test_reference_from_hospital` expects):

| v | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| dist | 0 | 4 | 7 | 7 | 12 | 9 | 16 | 16 | ∞ | ∞ |
| prev | −1 | 0 | 1 | 0 | 2 | 3 | 3 | 4 | −1 | −1 |

Totals: **10 pushes, 10 pops, 2 stale entries**. Vertices 8 and 9 are never touched.

**Route 0 → 7** from `prev`: 7 ← 4 ← 2 ← 1 ← 0, so the path is `[0, 1, 2, 4, 7]`, and
4 + 3 + 5 + 4 = 16 ✓.

### E2. Dijkstra from 5 (Old Town): the blueprint dispatch example

| v | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| dist | 9 | 6 | 9 | 2 | 12 | 0 | 11 | 8 | ∞ | ∞ |
| prev | 3 | 5 | 1 | 5 | 7 | −1 | 3 | 5 | −1 | −1 |

The route to 6 has two equally short options: 5–3–6 = 2+9 and 5–7–6 = 8+3, both 11. Vertex 3
is finalised (at distance 2) before 7 (at distance 8), so 6 is first reached via 3. Strict `<`
keeps that route, so `prev[6] = 3`. The ambulance at 0 is 9 away via `[5, 3, 0]`, which
dispatch will reverse to 0 → 3 → 5, exactly as in blueprint §10.

---

## F. Testing

### F1. How to build and check (MSYS2 UCRT64 shell, in the project folder)

```bash
make all
```
```bash
make analyze
```
```bash
make test
```

Without make (one test program as an example):

```bash
mkdir -p build && gcc -std=c11 -Wall -Wextra -Wpedantic -g -Isrc -Itests -o build/test_dijkstra tests/test_dijkstra.c tests/reference_city.c src/common.c src/graph.c src/minheap.c src/dijkstra.c
```

`make test` runs every program even if an earlier one fails, and exits non-zero if any failed
**or could not be started**. On the lead developer's laptop, Smart App Control currently
stops the Phase 2 programs from starting. That shows up as `Permission denied` and is recorded
as **not executed**, never as passed.

### F2. What the tests validate

**`test_minheap.c`**

| Test | Validates | Kind |
|---|---|---|
| `test_init_validation` | capacity 0 / negative / `NULL` rejected; safe empty state; double free harmless | failure/edge |
| `test_empty_heap_errors` | pop/peek on empty → `DC_ERR_EMPTY`; `NULL` arguments; push after free rejected | failure |
| `test_documented_example` | the exact array layout drawn in `minheap.c`; peek does not remove; pop order | normal |
| `test_tie_break_and_duplicates` | equal distances pop by vertex; duplicate pairs allowed | edge |
| `test_growth` | capacity 1 grows to ≥ 100; worst-case (decreasing) insert order | stress |
| `test_random_stress` | 1,000 random pushes: invariant after **every** push and pop; non-decreasing pop order | stress |
| `test_interleaved_operations` | push/pop mixed as in Dijkstra | normal |

**`test_dijkstra.c`**

| Test | Validates | Kind |
|---|---|---|
| `test_invalid_arguments` | `NULL` graph, source −1 / 10, `NULL` output, for **both** versions; failed results safe to free | failure |
| `test_reference_from_hospital` | full dist/prev tables of trace E1; route 0→7 | normal |
| `test_reference_from_old_town` | table E2; route 5→3→0; equal-length tie keeps the first route | normal/edge |
| `test_island_and_unreachable` | island distances; unreachable → `DC_ERR_NOT_FOUND`, path `NULL`, length 0 | disconnected |
| `test_path_to_self_and_bad_destinations` | path of length 1; invalid destinations; use after free rejected | edge |
| `test_validate_path_rejects_bad_routes` | wrong total, start or end; non-adjacent step; off-graph vertex; empty/`NULL` | failure |
| `test_corrupted_prev_chain` | a `prev` cycle and an out-of-range `prev` → `DC_ERR_INVALID`, **no infinite loop** | robustness |
| `test_small_graphs` | 1 vertex; graph with no roads | edge |
| `test_reference_all_sources` | all 10 sources: heap == array distances, both certified, symmetry | integration |
| `test_random_graphs` | 60 random graphs (1–40 vertices, sparse to dense, weights 1–20): heap == array, every result certified | stress |

**Not covered (stated honestly):** out-of-memory paths are reviewed by reading the code only
(we cannot force `malloc` to fail), and no Phase 2 test has been executed yet.

---

## G. Complexity

V = vertices, E = undirected roads (2E adjacency-list nodes), n = heap size.

| Operation | Time | Space |
|---|---|---|
| `minheap_push` | O(log n), amortised (doubling) | — |
| `minheap_pop` | O(log n) | — |
| `minheap_peek`, `is_empty`, `size` | O(1) | — |
| `minheap_check_invariant` | O(n) | O(1) |
| **`dijkstra_run`** | **O((V + E) log V)** | **O(V + E)** |
| `dijkstra_run_array` | O(V² + E) = O(V²) | O(V) |
| `dijkstra_build_path` | O(L), L = path length ≤ V | O(L) |
| `dijkstra_validate_path` | O(L · deg) (one road lookup per step) | O(1) |

**Deriving the heap version's bound.** Each adjacency-list node is examined once, when its
owner is finalised, and causes **at most one push**. So pushes ≤ 2E + 1, and the same number
of pops. Each push or pop costs O(log(2E+1)), and log E ≤ log V² = 2 log V, so this is
O(log V). Total: O((V + E) log V). Space: `dist`, `prev` and `finalised` are O(V); the heap
is O(E).

**Which is faster?** For sparse road networks (E ≈ 1.5V), the heap version is about
O(V log V) versus O(V²); with V = 1000 that is roughly 10⁴ versus 10⁶ steps. For a very dense
graph (E ≈ V²) the array version, O(V²), beats O(V² log V). Phase 10 will **measure** this
rather than assume it.

---

## H. Viva preparation

**Basic**

1. *What is a min-heap?* A complete binary tree, stored in an array, in which every parent ≤
   its children, so the minimum is at index 0.
2. *Index formulas?* parent (i−1)/2, left 2i+1, right 2i+2 (0-based).
3. *Why does Dijkstra need a priority queue?* It repeatedly needs the unfinalised vertex with
   the smallest distance. A min-heap gives it in O(log V) instead of an O(V) scan.
4. *What does relaxation mean?* Checking whether going through u gives v a shorter distance,
   and if so updating `dist[v]` and `prev[v]`.

**Tricky**

5. *Why might the same vertex be in the heap twice?* Lazy deletion: each improvement pushes a
   new entry. Only the first one popped is used; the rest are stale and skipped.
6. *Why not just sort the vertices by distance once?* Distances change while the algorithm
   runs, so a one-time sort becomes wrong immediately.
7. *Why does Dijkstra fail with negative weights?* Finalising a vertex assumes the rest of any
   other route can't shorten it. A negative road breaks that assumption (example in B7).
8. *Why is ∞ stored as −1 and not `INT_MAX`?* `INT_MAX + w` would overflow a signed int,
   which is undefined behaviour. −1 can never be a real distance, so it is a safe marker.
9. *Two routes have the same length. Which does DispatchCore report?* The first one found:
   `relax` uses strict `<`, and the heap breaks distance ties by the lower vertex number.
   This is deterministic.
10. *How do you know your Dijkstra is correct, not just consistent with itself?* Three
    independent checks: hand-computed tables, an independent O(V²) implementation, and
    `certify_result`, which proves optimality from the graph alone.
11. *How can `dijkstra_build_path` loop forever?* Only if `prev` contained a cycle, which
    correct code never produces. The walk is capped at V steps, and the test injects a cycle
    to prove the cap works.

**Complexity**

12. *Complexity of each version?* Heap: O((V+E) log V). Array: O(V²).
13. *When is the array version better?* On dense graphs where E is close to V².
14. *Why is the heap's push amortised O(log n)?* Occasional doubling copies the array, but the
    copying averaged over all pushes is O(1) each.
15. *Maximum heap size in our Dijkstra?* 2E + 1 entries, at most one per adjacency-list node
    plus the source.

**Pointers and memory**

16. *Why `bigger = realloc(...)` instead of `h->data = realloc(...)`?* If `realloc` fails it
    returns `NULL` and the old block is still allocated. Assigning directly would lose the
    only pointer to it: a leak, plus a crash on the next push.
17. *Who frees the path array?* The caller. In Phase 6 it is stored in the `Allocation` record
    and freed when the history list is destroyed.
18. *Why does `minheap_free` set `data = NULL`?* So a second `minheap_free`, or a later push,
    is safe instead of a double free or a use-after-free.
19. *Does Dijkstra modify the graph?* No. It takes `const Graph *`, so the compiler rejects
    any attempt to modify it.

**Integration**

20. *How will dispatch use this?* For the top incident it calls `dijkstra_run(g,
    incident_location)` **once**, reads `dist[r.location]` for every compatible available
    resource, picks the minimum (ties → lower resource ID), and builds and reverses the route
    with `dijkstra_build_path`.
21. *Why does one run from the incident give each resource's distance to the incident?* Roads
    are undirected, so dist(u,v) = dist(v,u). `test_reference_all_sources` checks this
    symmetry.

---

## I. Team explanation

| Member | Must understand | Independently validate |
|---|---|---|
| **M1 (lead)** | All of it, especially the error paths in `dijkstra_run` and path ownership | Approve the `dijkstra.h` interface; Phase 6 depends on it |
| **M2 (heaps)** — validator for `minheap` | B1–B5, D1, the amortised doubling argument | (1) Hand-trace the pushes in `test_documented_example` and the pops that follow; draw the tree after each. (2) Explain why `sift_down` must pick the **smaller** child. (3) Review `minheap.c` line by line. The Phase 3 max-heap will reuse this exact structure. |
| **M3 (graph)** — primary validator for Dijkstra | B6–B9, D2–D6, trace E1 | (1) Redo trace E1 **without looking**, then compare. (2) Produce the full table from source 7 by hand and add it as a new test. (3) Explain `certify_result` in your own words. (4) Run `make analyze`. |
| **M4 (records)** | `DC_INF`, `DC_ERR_NOT_FOUND` for unreachable, path ownership | Check that unreachable situations will be reported clearly when dispatch arrives (the Phase 6 contract) |
| **M5 (testing)** | Evidence levels in `TEST_REPORT.md`; deterministic `test_random` | Keep the test report current. If Smart App Control allows a run, or the team runs `make test` on another machine, record the full output, date and machine. Prepare a timing experiment design for heap vs array (Phase 10). |

---

## J. Integration

- **Depends on:** `graph.h` (read-only `const Graph *`), `minheap.h`, `common.h`. The
  dependency direction is `dijkstra → graph` and `dijkstra → minheap`. Nothing in Phase 1
  depends on Phase 2.
- **`minheap.c` is independent of the graph.** It knows only about integers, so it can be
  tested alone (that is why `test_minheap` links only `minheap.c` and `common.c`).
- **Used next by:** `dispatch.c` (Phase 6) and the "Find shortest route" menu item (Phase 9).
- **Build:** `make all`, `make analyze`, `make test` (see F1).

### Suggested Git steps (for you to run; I have not run any Git command)

If you have not yet initialised the repository, see the steps at the end of
`docs/PHASE1_GRAPH.md` first. Then:

```bash
git switch -c feature/phase2-dijkstra
```
```bash
git status
```
```bash
git add src/minheap.h src/minheap.c src/dijkstra.h src/dijkstra.c tests/test_minheap.c tests/test_dijkstra.c tests/test_util.h Makefile docs/PHASE2_DIJKSTRA.md docs/TEST_REPORT.md docs/PHASE1_GRAPH.md docs/PHASE0_BLUEPRINT.md
```
```bash
git commit -m "Phase 2: binary min-heap and Dijkstra with route reconstruction and tests"
```

Then push the branch and open a Pull Request with **Member 3** (Dijkstra) and **Member 2**
(min-heap) as reviewers.
