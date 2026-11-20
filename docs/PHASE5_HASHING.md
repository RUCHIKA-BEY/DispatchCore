# Phase 5 — Hash Tables and the Application State

> **Verification status (2026-11-19 17:42)**
>
> | Program | Compiled, 0 warnings | `make analyze` clean | Hand-reviewed | **Executed** |
> |---|---|---|---|---|
> | `test_hash_table` | ✅ | ✅ | ✅ | ❌ **not yet** (blocked by Smart App Control) |
> | `test_app_state` | ✅ | ✅ | ✅ | ❌ **not yet** (blocked by Smart App Control) |
>
> No Phase 5 test has been run, so **none is marked as passed**. See `docs/TEST_REPORT.md`.

## Files in this phase

| File | Status | Contents |
|---|---|---|
| `src/hash_table.h` / `src/hash_table.c` | **new** | separate-chaining hash table: int key → borrowed pointer; insert, find, remove, statistics, debug view |
| `src/app_state.h` / `src/app_state.c` | **new** | `AppState`: owns all data; all-or-nothing registration; typed lookups; consistency checker |
| `tests/test_hash_table.c` | **new** | 5 test functions: forced collisions, prime vs non-prime, 10,000 keys, random operations vs a simple array |
| `tests/test_app_state.c` | **new** | 6 test functions |
| `src/incident.h`, `src/incident.c` | **modified** | **added** `incident_store_remove_last` (nothing else changed) |
| `src/resource.h`, `src/resource.c` | **modified** | **added** `resource_store_remove_last` (nothing else changed) |
| `Makefile` | **modified** | two new test programs; `APP_DEPS` group |
| `docs/TEST_REPORT.md` | **modified** | Phase 5 rows and run log |
| `docs/PHASE3_INCIDENTS.md`, `docs/PHASE4_RESOURCES.md` | **modified** | corrected one viva answer each (see design detail 4) |

### Design details decided in this phase (please review)

1. **Fixed bucket count of 101** (blueprint D10). With 1,000 incidents the load factor is
   about 9.9, so a lookup walks about 10 nodes. That is still fast, but no longer "O(1)" in
   spirit. **Rehashing** (doubling the buckets when the load factor exceeds about 0.75) is
   the standard fix and is listed as an optional extension.
2. **`AppState` arrives in this phase** (blueprint roadmap). The allocation history field is
   added in Phase 6, when the history module exists.
3. **"Remove last" was added to both stores.** It is needed to undo a half-finished
   registration (see B6). These are the only changes to Phase 3/4 source files.
4. **Correction to earlier documents.** Phases 3 and 4 said Phase 5 would make *adding* a
   record O(1). It does not: the stores keep their own duplicate scan as a safety check, so
   registration stays O(N). **Lookups by ID** are what become O(1) on average. The two viva
   answers have been corrected.
5. **The clock advances on every registration** (incidents *and* resources), as blueprint
   D5 states ("+1 per event"). Incident sequence numbers are therefore unique but not always
   consecutive.
6. **`app_init` takes ownership of the graph only if it succeeds.** On failure, the caller
   still owns the graph and must destroy it.

---

## A. Purpose

- **`hash_table`** answers *"give me the record with ID 1007"* in O(1) average time instead
  of scanning a list. The menu needs this constantly: complete incident 1007, cancel 1003,
  look up resource 4.
- **`app_state`** brings Phases 1–5 together into one object. It owns everything, registers
  records into **every** structure they belong to (or none), offers typed lookups, and can
  **check itself** for inconsistencies.

---

## B. Concepts you need first

### B1. Hashing

A **hash function** turns a key into an array index:

```
h(key) = key mod M        (M = number of buckets)
h(1001) = 1001 mod 101 = 92   → look only in bucket 92
```

Instead of comparing the key with every record, we compute where it must be and look only
there.

### B2. Collisions and separate chaining

Different keys can produce the same index: h(1001) = h(1102) = 92. This is a
**collision**, and it is unavoidable whenever there are more possible keys than buckets.
**Separate chaining** gives each bucket a **linked list** of every key that hashed there:

```
bucket 92: -> 1102 -> 1001 -> NULL
```

To find a key: hash it, then walk that one (short) chain.

The alternative is **open addressing**: on a collision, try the next free slot in the array.
Chaining is easier to implement correctly (deletion in open addressing needs "tombstone"
markers), it never "fills up", and it reuses the linked lists you already know.

### B3. Load factor and complexity

**Load factor** α = (number of keys) / (number of buckets) is the average chain length.

| | Average | Worst |
|---|---|---|
| find / insert / remove | **O(1 + α)** | **O(n)**: every key in one chain |

If α stays bounded (for example ≤ 1, by rehashing), the average is O(1). The worst case
happens with a bad hash function or adversarial keys. `test_prime_bucket_count` shows a real
example.

### B4. Why a prime number of buckets?

`key mod M` only "sees" the part of the key that M can distinguish. If many keys share a
factor with M, they collide. For example, with M = 10, every multiple of 10 lands in bucket 0.
If M is **prime**, it shares no factor with a regular pattern such as "every 10th ID", so the
keys spread out:

| Keys 10, 20, …, 500 | buckets used | longest chain |
|---|---|---|
| M = 10 | **1** | **50** |
| M = 11 (prime) | 11 | 5 |

### B5. `void *`: a pointer to "anything"

The table stores `void *` values, so one implementation can index both incidents and
resources. The price is that C forgets the real type. `app_find_incident` and
`app_find_resource` are the **only** places that convert back
(`(Incident *)hash_table_find(...)`), so the rest of the code never casts and cannot mix the
two up.

### B6. All-or-nothing updates (rollback)

Registering an incident touches three structures:

```
1. store.add(inc)        ← owner
2. index.insert(id, inc) ← may fail (out of memory)
3. heap.insert(inc)      ← may fail (out of memory)
```

If step 3 fails after steps 1 and 2 succeeded, the incident would be in the store and the
index but **not** in the priority queue, so it would never be dispatched. That is a silent
bug. So each failure **undoes the earlier steps in reverse order**, and the ID counter and
clock advance only after **every** step succeeded. Databases call this a *transaction*.

### B7. The pointer-to-pointer removal technique

To unlink a node from a singly linked list you must change **the pointer that points to
it**. That pointer is either the bucket head or the previous node's `next`. Holding the
*address of that pointer* (`HashNode **link`) handles both cases with one piece of code:

```c
HashNode **link = &buckets[i];        // address of the head pointer
while (*link != NULL) {
    if ((*link)->key == key) { HashNode *dead = *link; *link = dead->next; free(dead); ... }
    link = &(*link)->next;            // address of this node's next pointer
}
```

No `prev` variable and no special case for the head.

---

## C. Code (where things are)

- `src/hash_table.h`: the bucket diagram, the hash formula, the borrowing rule.
- `src/hash_table.c`: create/destroy → `hash_index` → `find_node` (private) → insert, find,
  remove → statistics and printing.
- `src/app_state.h`: the ownership table of every field.
- `src/app_state.c`: `reset` and `free_containers` (private) → init/free → registration →
  typed lookups → consistency checker.

---

## D. Code walkthrough

### D1. `hash_index`

```c
return (int)((unsigned)key % (unsigned)num_buckets);
```

In C, `-7 % 5` is **−2**, which is an invalid array index. Converting to `unsigned` first is
well defined (it wraps modulo 2³²) and `%` on unsigned values is never negative. Our IDs are
positive, but a generic table must not crash on any `int`. `test_create_and_hash_function`
checks every key from −1000 to 1000.

### D2. `hash_table_insert`

1. Reject a `NULL` table or `NULL` value. `NULL` is reserved to mean "not found" in
   `hash_table_find`, so storing it would make the two indistinguishable.
2. Reject duplicates **before** allocating, leaving the table unchanged.
3. Allocate a node and insert it at the **head** of its chain (O(1)).

### D3. `hash_table_remove`

This uses the technique from B7. Only the node is freed, never the value, because the value
is a borrowed pointer to a record owned by a store.

### D4. `hash_table_stats` / `hash_table_print`

These walk every bucket to compute used buckets, longest chain and load factor. The debug
view prints only non-empty buckets. It will be menu item 21 ("Hash table debug view") in
Phase 9.

### D5. `app_init` / `app_free`

- `reset` puts every field in a known empty state **first**, so even a failed init leaves
  something `app_free` can safely process.
- `free_containers` frees **borrowers before owners**: the heap and both indexes, then the two
  stores, which free the records. If the records were freed first, the heap and indexes would
  briefly hold dangling pointers. Nothing reads them here, but the habit avoids real bugs
  elsewhere.
- `app_free` then destroys the graph and resets, so a second call is harmless.

### D6. `app_register_incident`

Validation and creation (`incident_create`) come first; if that fails, nothing needs undoing.
The three steps then follow, with the rollback described in B6. `next_incident_id++` and
`clock++` happen **only on success**, so a rejected registration never "uses up" an ID.
`test_rejected_registration_changes_nothing` checks that.

### D7. `app_check_consistency`

It checks, and reports, each of these:

- index size = store size, for both kinds of record;
- every stored record is found in its index **at the same address**;
- every location is a valid graph vertex;
- every PENDING incident is in the heap, and every heap entry is a stored incident;
- the heap invariant;
- the resource invariant (DISPATCHED ⇔ assigned);
- every ID is below the next ID to be issued.

It is deliberately simple and slow, O(N·n). It is a **checking tool** for tests now and for
loading CSV files in Phase 7. `test_consistency_checker_detects_problems` breaks the state on
purpose, to prove the checker is not just always returning 0.

---

## E. Example execution

### E1. Hashing the first incidents (101 buckets)

| ID | 1001 | 1002 | 1003 | 1004 | 1005 | 1006 | 1007 | 1102 |
|---|---|---|---|---|---|---|---|---|
| bucket | 92 | 93 | 94 | 95 | 96 | 97 | 98 | **92** |

Sequential IDs fill consecutive buckets with no collisions until the IDs wrap around 101.
Incident 1102 then collides with 1001 and joins bucket 92's chain: `92: -> 1102 -> 1001`.

### E2. Forced collision trace (`test_forced_collisions`, 5 buckets)

Keys 1, 6, 11 and 16 are all ≡ 1 (mod 5), so they all go to bucket 1:

| Operation | Chain in bucket 1 |
|---|---|
| insert 1, 6, 11, 16 | 16 → 11 → 6 → 1 |
| remove 11 (middle) | 16 → 6 → 1 |
| remove 16 (head) | 6 → 1 |
| remove 1 (tail) | 6 |
| remove 21 (absent) | 6 (`DC_ERR_NOT_FOUND`) |
| remove 6 (last) | empty |

Predicted debug view after the four inserts:

```
  bucket   1: -> 16 -> 11 -> 6 -> 1
  4 keys in 5 buckets (1 used), load factor 0.80, longest chain 4
```

### E3. Predicted resource index for the reference fleet

```
  bucket   1: -> 1
  bucket   2: -> 2
  ...
  bucket   6: -> 6
  6 keys in 101 buckets (6 used), load factor 0.06, longest chain 1
```

### E4. 1,000 incidents (`test_many_registrations`)

The IDs are 1001–2000. 1000 = 9 × 101 + 91, so 91 buckets hold 10 keys and 10 buckets hold 9:
**every bucket is used, the longest chain is 10, and the load factor is 9.90.** This shows
exactly why rehashing is worth adding once the number of records grows (design detail 1).

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
mkdir -p build && gcc -std=c11 -Wall -Wextra -Wpedantic -g -Isrc -Itests -o build/test_hash_table tests/test_hash_table.c src/hash_table.c src/common.c
```

### F2. What the tests validate

**`test_hash_table.c`**

| Test | Validates | Kind |
|---|---|---|
| `test_create_and_hash_function` | invalid sizes; the formula on known values; **negative keys stay in range** | edge |
| `test_basic_operations` | insert/find/remove; duplicate refused and the original kept; `NULL` value refused; re-insert after removal; values never touched | normal/failure |
| `test_forced_collisions` | trace E2: one chain of 4, head-insert order, removal from middle, head, tail and last; absent key in a non-empty bucket | **collision** |
| `test_prime_bucket_count` | table B4: 1 bucket used vs all 11 | design justification |
| `test_large_and_against_oracle` | 10,000 keys with half removed; 20,000 random operations in a 7-bucket table checked against a plain "present?" array | stress/oracle |

**`test_app_state.c`**

| Test | Validates | Kind |
|---|---|---|
| `test_init_and_free` | `NULL` graph; free after a failed init; double free | failure |
| `test_register_incidents` | IDs 1001–1007; sequence numbers; all three structures updated; lookups return the **stored** record; heap top is 1002 | integration |
| `test_rejected_registration_changes_nothing` | invalid severity, location, type: counts, ID counters and clock unchanged | all-or-nothing |
| `test_register_reference_fleet` | R1–R6; lookups; registration order; debug view E3 | integration |
| `test_consistency_checker_detects_problems` | a broken resource invariant gives exactly 1 problem; a missing index entry is detected; repairs return to 0 | checker |
| `test_many_registrations` | 1,000 incidents + 300 resources; every lookup; all 101 buckets used (E4); consistent | stress |

**Not covered:** the rollback paths for out of memory (code review only; `malloc` cannot be
forced to fail in our setup). Nothing in Phase 5 has been executed yet.

---

## G. Complexity

n = keys in a table, M = buckets, α = n / M, N = records in a store, h = heap size.

| Operation | Average | Worst | Space |
|---|---|---|---|
| `hash_index` | O(1) | O(1) | — |
| `hash_table_insert` | O(1 + α) (duplicate check of one chain) | O(n) | O(1) per node |
| `hash_table_find` | **O(1 + α)** | O(n) | — |
| `hash_table_remove` | O(1 + α) | O(n) | — |
| `hash_table_stats`, `hash_table_print`, `hash_table_destroy` | O(M + n) | O(M + n) | — |
| Whole table | — | — | O(M + n) |
| `app_find_incident` / `app_find_resource` | **O(1 + α)** | O(n) | — |
| `app_register_incident` | O(N) store duplicate scan + O(1 + α) + O(log h) | O(N) | — |
| `app_register_resource` | O(N) + O(1 + α) | O(N) | — |
| `app_check_consistency` | O(N · h) | O(N · h) | — |

**Before and after Phase 5:** finding an incident by ID went from O(N) (linear search in the
store) to O(1 + α) average. With 1,000 incidents and 101 buckets, that means about 10
comparisons instead of up to 1,000.

---

## H. Viva preparation

**Basic**

1. *What is hashing?* Computing an array index from a key, so a lookup goes straight to the
   right place instead of searching.
2. *Your hash function?* The division method: `(unsigned)key mod M`, with M = 101 (prime).
3. *What is a collision and how do you handle it?* Two keys with the same index. Separate
   chaining: each bucket holds a linked list.
4. *What is the load factor?* Keys ÷ buckets: the average chain length.

**Tricky**

5. *Why a prime number of buckets?* Keys with a common pattern (such as multiples of 10)
   would cluster into a few buckets if M shared that factor (table B4).
6. *Why convert to `unsigned` before `%`?* In C, `%` of a negative number is negative, which
   would be an invalid index.
7. *Why can't the table store `NULL` values?* `find` returns `NULL` for "absent", so a stored
   `NULL` could not be told apart from a missing key.
8. *Chaining vs open addressing?* Chaining never fills up and deletion is simple. Open
   addressing is more cache-friendly but needs tombstones for deletion and degrades sharply
   as α approaches 1.
9. *What happens to the record when you remove its key from the index?* Nothing. The index
   borrows the pointer; only the store frees records.
10. *Your load factor reaches 9.9 at 1,000 incidents. Is lookup still O(1)?* On average it is
    O(1 + α) ≈ 11 steps: fast, but growing with n. Rehashing to keep α ≤ 0.75 would restore
    true O(1) average; it is an optional extension (design detail 1).
11. *A registration fails at the heap step. What happens?* The incident is removed from the
    index and the store and freed, and the ID and clock do not advance, so nothing is left
    half-registered.
12. *Explain `HashNode **link` in `remove`.* It holds the address of the pointer that points
    to the current node, so overwriting `*link` unlinks the node whether it is the head or in
    the middle (B7).

**Complexity**

13. *Average and worst case of find?* O(1 + α) average; O(n) worst, when all keys are in one
    chain.
14. *Is registration O(1)?* No: O(N), because the store keeps its duplicate scan. Lookups are
    O(1) average.

**Pointers and memory**

15. *What does `hash_table_destroy` free?* Every node and the bucket array, never the values.
16. *Why does `app_free` free the heap and indexes before the stores?* So that no structure
    ever holds pointers to already-freed records (borrowers before owners).
17. *Why are the casts only in `app_find_*`?* `void *` loses type information. Keeping the
    casts in one typed wrapper means the rest of the program cannot accidentally treat a
    `Resource *` as an `Incident *`.

**Integration**

18. *Why does AppState exist instead of global variables?* Every function's data is visible in
    its parameters, tests can create independent instances, and the strategy comparison
    (Phase 10) can build a second, separate state from a scenario file.
19. *How will Phase 7 use the consistency checker?* After loading the CSV files, to refuse or
    report data whose files contradict each other.

---

## I. Team explanation

| Member | Must understand | Independently validate |
|---|---|---|
| **M1 (lead)** | `AppState` ownership, rollback (B6), the free order (D5), design details 1–6 | Approve `app_state.h`; Phase 6 adds the history and dispatch on top of it |
| **M2 (priority queue)** | How registration inserts into the heap; the heap check in the consistency checker | Confirm that a rejected registration never reaches the heap |
| **M3 (graph)** | `app_num_locations` and location validation | Confirm that every location check uses `graph->num_vertices` |
| **M4 (records)** — primary validator | All of B1–B7, D1–D7, E1–E2, G | (1) Recompute table E1 and trace E2 by hand. (2) Explain why M = 10 fails in table B4. (3) Add a test: keys 1001 and 1102 collide in the 101-bucket index and both are found. (4) Review `hash_table.c` and `app_state.c` line by line. (5) Write a short rehashing design (when, how, cost) as a possible extension. |
| **M5 (testing)** | The oracle test; the consistency checker test | Keep `TEST_REPORT.md` current; plan a timing experiment: linear `*_store_find` vs `app_find_*` for 1,000 / 10,000 records (Phase 10) |

---

## J. Integration

- **Dependencies introduced:** `hash_table → common`;
  `app_state → graph, hash_table, incident, incident_heap, resource`. `app_state` is the first
  module that connects Phases 1, 3, 4 and 5. It does **not** use Dijkstra or compatibility;
  those come together in the dispatch engine (Phase 6).
- **Used next by:**
  - Phase 6: adds the history list to `AppState`; dispatch, completion and cancellation.
  - Phase 7: loading and saving go through `AppState`; the consistency checker runs after
    loading.
  - Phase 9: the menu calls `app_register_*` and `app_find_*`.
- **Build:** `make all`, `make analyze`, `make test`.

### Suggested Git steps (for you to run; I have not run any Git command)

```bash
git switch -c feature/phase5-hashing
```
```bash
git status
```
```bash
git add src/hash_table.h src/hash_table.c src/app_state.h src/app_state.c src/incident.h src/incident.c src/resource.h src/resource.c tests/test_hash_table.c tests/test_app_state.c Makefile docs/PHASE5_HASHING.md docs/TEST_REPORT.md docs/PHASE3_INCIDENTS.md docs/PHASE4_RESOURCES.md
```
```bash
git commit -m "Phase 5: separate-chaining hash table and AppState with all-or-nothing registration"
```

Then push and open a Pull Request with **Member 4** as the reviewer. Mention in the PR
description that `incident.[ch]` and `resource.[ch]` each gained one function.
