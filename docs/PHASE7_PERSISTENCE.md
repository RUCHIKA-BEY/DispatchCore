# Phase 7 — Persistence with CSV Files

> **Verification status (2026-11-19 18:04)**
>
> | Program | Compiled, 0 warnings | `make analyze` clean | Hand-reviewed | **Executed** |
> |---|---|---|---|---|
> | `test_file_manager` | ✅ | ✅ | ✅ | ❌ **not yet** (blocked by Smart App Control) |
>
> No Phase 7 test has been run, so **none is marked as passed**. In particular, **persistence
> across restarts has not yet been demonstrated by a real run**; the round-trip tests that
> would demonstrate it are written and compiled. See `docs/TEST_REPORT.md`.

## Files in this phase

| File | Status | Contents |
|---|---|---|
| `src/file_manager.h` / `src/file_manager.c` | **new** | strict CSV reading (lines, fields, numbers), five loaders, the load procedure, safe saving |
| `tests/test_file_manager.c` | **new** | 7 test functions: load, two round trips, malformed rows, CRLF/BOM/missing files, refused loads, safe save |
| `tests/data/*/` | **new** | 7 fixture folders (table in F2) |
| `.gitattributes` | **new** | keeps the fixture bytes exact (no line-ending conversion) |
| `src/incident.[ch]` | **modified** | **added** `incident_restore` |
| `src/resource.[ch]` | **modified** | **added** `resource_restore` |
| `src/app_state.[ch]` | **modified** | **added** `app_add_loaded_*` and `app_finish_loading`; registration code **refactored** into private helpers shared with loading (same behaviour) |
| `Makefile` | **modified** | new test program; `make test` creates `build/test_data/roundtrip` |
| `docs/TEST_REPORT.md` | **modified** | Phase 7 rows and run log |

### Design details decided in this phase (please review)

1. **`locations.csv` and `roads.csv` are input-only.** DispatchCore never writes them. If the
   program rewrote `roads.csv` from the graph, the rows would come out in a different order;
   on the next start the adjacency lists would differ, and so could the route chosen among
   equal-length routes. **The same data must give the same results after a restart.**
2. **A missing or unusable `locations.csv` refuses the load.** This refines blueprint §12,
   which said "empty city": a graph needs at least one location, and IDs must be `0..V−1`
   with no gaps. A missing `roads.csv` is allowed (a city with no roads, with a notice).
   Missing record files load as empty.
3. **Inconsistent records refuse the whole load**, using the Phase 5/6 consistency checker.
   We never "repair" files: a guess could hide a real problem and the next save would make
   the guess permanent.
4. **An unreadable file is not treated as missing.** Only "file does not exist" (`ENOENT`)
   counts as missing. Otherwise a locked or permission-denied `incidents.csv` would load as
   empty, and the next save would overwrite the real data with nothing.
5. **Saving uses two stages:** write and check all three `.tmp` files, then rename each into
   place, keeping a `.bak`. Limitation: if the *second* stage fails half-way (very unlikely;
   for example a file locked by another program), some files are new and some old. The `.bak`
   files allow manual recovery. True multi-file atomicity needs a database or a journal,
   which is out of scope.
6. **The clock is not saved.** On loading it becomes *(largest saved sequence number + 1)*.
   Events that are not stored (cancellations, unit registrations) also used clock ticks, so
   the reloaded clock can be smaller than before the restart. It is always larger than every
   saved value, which is what ordering needs.
7. **Allocation routes are re-validated on load** against the road network with
   `dijkstra_validate_path`. Consequence: if someone edits `roads.csv` so that a saved route
   no longer exists, that allocation row is skipped, and the load is then refused as
   inconsistent if the allocation was ACTIVE.
8. **Line limit 8,192 characters** (`FM_LINE_MAX`), not the blueprint's 512, so that long
   routes fit on one line.

---

## A. Purpose

Without this phase, every incident, unit and dispatch disappears when the program closes.
`file_manager` writes the changing records to CSV files and reads them back on start-up,
**checking every value** on the way in. The loaded state is indistinguishable from the state
that was saved: same records, same IDs, same statuses, same service order.

---

## B. Concepts you need first

### B1. Text files in C

| Call | Meaning |
|---|---|
| `fopen(path, "r")` / `"w"` | open for reading / create-or-truncate for writing; `NULL` on failure, with the reason in `errno` |
| `fgets(buf, size, f)` | reads **one line**, at most `size − 1` characters, keeping the `'\n'`; `NULL` at end of file |
| `fprintf(f, ...)` | formatted write; returns a **negative** number on error |
| `ferror(f)` | was there any read/write error on this stream? |
| `fclose(f)` | flushes buffered data to disk; **can fail**, so the result must be checked when writing |
| `rename(old, new)`, `remove(path)` | file-system operations; return 0 on success |

**Buffered output:** `fprintf` usually only fills a memory buffer. The data really reaches
the disk at `fclose` (or `fflush`), which is why a full disk may only be reported by
`fclose`.

### B2. CSV, and its limits

*Comma-Separated Values*: one record per line, fields separated by commas, the first line
naming the columns. Simple and human-readable, but:

- fields cannot contain commas or newlines (we have no quoting; names are validated to avoid
  them);
- there are no types: "4", "four" and " 4" are all just text, so **every value must be
  validated**;
- the whole file is rewritten on each save, O(N);
- several files cannot be updated atomically together (design detail 5);
- only one program instance may use the files at a time.

### B3. Robust parsing with `strtoll`

`atoi("12abc")` returns 12 and `atoi("abc")` returns 0, so errors are silently hidden.
`strtoll(s, &end, 10)` tells us **where it stopped** (`end`) and reports overflow with
`errno = ERANGE`. Our `parse_ll` accepts a field only if:

1. it starts with a digit or `-` (rejects `""`, `" 4"`, `"+4"`);
2. `end` reached the end of the string (rejects `"4x"`, `"4.0"`);
3. there was no overflow (rejects `99999999999999999999`);
4. the value is within the given `[min, max]`.

### B4. Long lines and line endings

`fgets` with a buffer of 8,192 characters stops early on a longer line, and the next call
would read the *rest of the same line* as if it were a new row. `read_line` detects that
case, discards the rest of the line, and reports it as one skipped row.

Windows text files end lines with `\r\n`. In text mode, Windows C libraries usually convert
that to `\n`, but files copied from elsewhere, or read on Linux, keep the `\r`, which would
end up stuck to the last field (`"PENDING\r"` would not match `"PENDING"`). We strip it
explicitly. Excel's "CSV UTF-8" adds three invisible bytes (`EF BB BF`, the *byte-order
mark*) before the header; we skip them.

### B5. Two-stage safe save

```
write resources.csv.tmp, incidents.csv.tmp, allocations.csv.tmp     ← check every write and fclose
   any failure → delete the .tmp files; the real files were never touched
then for each file:  X.bak deleted → X renamed to X.bak → X.tmp renamed to X
```

A crash in the middle of *writing* can therefore only damage a `.tmp` file. Windows'
`rename` refuses to overwrite an existing file, which is why the old file is first moved to
`.bak`.

### B6. Restoring state vs creating it

Normal code creates incidents as PENDING (`incident_create`). A file can contain any status,
so loading uses **separate** functions (`incident_restore`, `resource_restore`) that accept
any status but check the rules that link the fields (for example, DISPATCHED ⇒ has a unit).
Keeping them separate means normal code cannot create, say, a COMPLETED incident by
accident.

### B7. Function pointers

`fm_save_records` holds the three writers in an array of function pointers:

```c
typedef int (*RecordWriter)(FILE *f, const AppState *app);
const RecordWriter writers[3] = {write_resources, write_incidents, write_allocations};
```

so one loop applies the same safe procedure to each file. A function's name, used without
`()`, is its address.

---

## C. Code (where things are)

`src/file_manager.c` has four numbered sections:

1. **Helpers:** `make_path`, `parse_ll`/`parse_int`/`parse_long`, `split_fields`, the
   `Reader` struct with `read_line`, `open_reader` (header and byte-order mark check),
   `close_reader` (read-error check).
2. **Loaders:** `load_locations`, `load_roads`, `load_resources`, `load_incidents`,
   `parse_route` + `load_allocations`.
3. **`fm_load`:** order, ownership of the graph, final consistency check.
4. **Saving:** three writers, `write_tmp`, `replace_with_tmp`, `fm_save_records`.

---

## D. Code walkthrough

### D1. Row handling: one shape for every loader

```
read_line → too long?  → skip (warning)
          → empty?     → ignore silently
          → split      → wrong field count? → skip
          → parse each field strictly        → any failure? → skip
          → *_restore (cross-field rules)    → invalid? → skip
          → app_add_loaded_* (duplicates)    → duplicate? → skip, free the record
          → loaded++
```

Every skip prints `path:line: reason - row skipped` and increments `skipped`. One bad row
never changes anything: each step either succeeds completely or the row is dropped.

### D2. `load_locations`

The graph's size is only known after reading the whole file, so names are first collected
into a temporary array indexed by ID, with `seen_at[id]` remembering the line number (which
also detects duplicates). Afterwards:

- no valid rows, or a gap in `0..max_id` → refuse;
- `graph_create(max_id + 1)`;
- set each name; an invalid name → refuse, reporting its line number.

### D3. `load_roads`

Accepts only `OPEN`. `CLOSED` is recognised but reported as unsupported, ready for the
optional road-closure feature. `graph_add_road` already rejects unknown locations,
self-loops and duplicates; the loader turns its status code into a readable reason.

### D4. `load_allocations` and `parse_route`

`parse_route` counts the `|` separators. A simple path cannot have more vertices than the
graph, so a longer one is refused **before** allocating. It then parses each vertex strictly.
The route must then pass `dijkstra_validate_path` (real roads, stated length) and
`app_add_loaded_allocation` (unique ID; `complete_seq` agrees with the status). The route
array's ownership passes to the history only on success; otherwise the loader frees it.

### D5. `fm_load`

```
app_init(app, NULL)      ← deliberately fails: guarantees *app is empty and safe to free
locations → graph        ← refuse on failure
roads                    ← refuse on read error / bad header
app_init(app, graph)     ← AppState now owns the graph
resources, incidents, allocations
app_finish_loading       ← next IDs and clock
app_check_consistency    ← any problem → app_free, DC_ERR_STATE
```

On every failure path, everything allocated so far is freed, and the caller receives an
empty, safe `AppState`.

### D6. `fm_save_records`

`write_tmp` checks every `fprintf`, then `ferror`, then the result of `fclose`; on failure it
deletes its `.tmp`. If any of the three fails, all `.tmp` files are deleted and the originals
were never touched. Otherwise `replace_with_tmp` swaps each file in, keeping a `.bak`, and
tries to restore the old file if the final rename fails.

### D7. Changes to earlier modules

- `incident_restore` / `resource_restore`: full validation plus the status/assignment rules;
  copies the whole struct with `*inc = *fields`.
- `app_state.c`: the three insertion steps from Phase 5 were moved into
  `insert_incident`/`insert_resource`, so registration and loading share **one** copy of the
  rollback logic. The heap insert now happens only for PENDING incidents, which is always the
  case for registration, so registration behaves exactly as before.
- `app_finish_loading`: next ID = largest + 1; clock = largest saved sequence + 1.

---

## E. Example execution

### E1. Loading `tests/data/valid`

| File | Content |
|---|---|
| resources | R1 DISPATCHED to 1001 (1 trip, 9); R2 AVAILABLE at 1 (1 trip, 15); R3–R6 available |
| incidents | 1001 DISPATCHED (R1) · 1002 PENDING · 1003 CANCELLED · 1004 COMPLETED (R2) |
| allocations | 1: 1004 ← R2, 6 → 1, distance 15, COMPLETED (10, 12) · 2: 1001 ← R1, 0 → 3 → 5, distance 9, ACTIVE (11) |

After loading: next IDs **1005 / 7 / 3**; clock = 12 + 1 = **13**; only 1002 is in the heap;
consistency **0**. A `dispatch_next` then sends **R3** (fire unit already at 2) to 1002 as
allocation **3** with `dispatch_seq` **13**.

### E2. Predicted warnings for `tests/data/malformed` (first lines)

```
  tests/data/malformed/locations.csv:8: duplicate location_id - row skipped
  tests/data/malformed/locations.csv:9: bad location_id - row skipped
  tests/data/malformed/locations.csv:12: expected 2 fields - row skipped
  tests/data/malformed/roads.csv:6: duplicate road - row skipped
  tests/data/malformed/roads.csv:10: invalid road (unknown location or self-loop) - row skipped
  ...
```

Expected totals:

| File | Loaded | Skipped | Why skipped |
|---|---|---|---|
| locations | 10 | 3 | duplicate ID 5; `abc`; three fields (`10,Far,Away`) |
| roads | 11 | 7 | duplicate; self-loop; location 42; `abc`; zero length; 3 fields; `CLOSED` (the blank line is ignored, not counted) |
| resources | 2 | 6 | `HELICOPTER`; location 99; duplicate ID 1; DISPATCHED with no incident; 6 fields; `" 4"` |
| incidents | 2 | 9 | severity 9; `FLOOD`; seq −4; duplicate 1001; PENDING with a unit; `pending`; seq overflow; 9 fields; a 9,029-character line |
| allocations | 0 | 5 | route 0 > 5 is not a road; wrong distance; `x` in route; `FINISHED`; ACTIVE with a completion time |

### E3. Round trip

```
load valid → dispatch 1002 → complete 1001 → register 2 incidents → dispatch_all
→ save to build/test_data/roundtrip → load it again → compare
```

`compare_states` checks every saved field of every record **in order**, every route vertex,
the next IDs, and that both heaps release the pending incidents **in the same order**.

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

`make test` creates `build/test_data/roundtrip` first. If you run
`build/test_file_manager.exe` by hand, create that folder first and run it **from the
project folder**, because the fixture paths are relative.

### F2. Fixture folders

| Folder | Purpose | Expected result |
|---|---|---|
| `valid` | a consistent mid-simulation state | loads; E1 |
| `malformed` | every kind of bad row | loads good rows; table E2 |
| `crlf` | CRLF line endings + byte-order mark | loads; names have no stray `\r` |
| `minimal` | map only | loads; record files reported missing |
| `bad_header` | wrong header in `incidents.csv` | **refused**, `DC_ERR_IO` |
| `inconsistent` | DISPATCHED incident with no allocation | **refused**, `DC_ERR_STATE` |
| `gap` | location IDs 0, 1, 3 | **refused**, `DC_ERR_IO` |
| (no folder) | nothing at all | **refused**: `locations.csv` missing |

### F3. What the tests validate

| Test | Validates |
|---|---|
| `test_load_valid` | counts; every restored field; next IDs and clock; the loaded state can dispatch, complete and register correctly |
| `test_round_trip` | E3: save → reload → identical; consistent; reloaded clock ≤ original |
| `test_round_trip_after_random_simulation` | the same after 600 random operations (many statuses, cancellations, completed allocations) |
| `test_malformed_rows_are_skipped` | table E2, with exact counts and the surviving records |
| `test_crlf_bom_and_missing_files` | line endings and byte-order mark; missing optional files |
| `test_refused_loads` | bad header, inconsistent, gap, missing folder, `NULL` arguments: each refused **and leaves a safe empty state** |
| `test_safe_save` | `.bak` holds the previous version (header + 4 incidents); no `.tmp` left; saving to a missing folder fails cleanly |

**Not covered:** a full disk or a locked file during saving (cannot be simulated reliably);
out-of-memory paths; concurrent use. No Phase 7 test has been executed yet.

---

## G. Complexity

N = total records, L = total route length, h = allocations, V/E = graph size.

| Operation | Time | Space |
|---|---|---|
| parse one field | O(field length) | O(1) |
| `load_locations` | O(rows + V) | O(DC_MAX_VERTICES) temporary arrays |
| `load_roads` | O(rows · deg) (duplicate check per road) | — |
| `load_resources` / `load_incidents` | O(rows · N) (store's duplicate scan) + O(n log n) heap inserts | — |
| `load_allocations` | O(h²) (duplicate-ID check walks the history) + O(L · deg) route validation | — |
| `app_finish_loading` | O(N + h) | — |
| consistency check | O(N · n + N · h) | — |
| `fm_save_records` | O(N + L) | O(1) extra |

Loading is dominated by quadratic duplicate checks, which is fine for hundreds or a few
thousand records. Using the hash indexes for the duplicate checks would make it near-linear.
That is a possible improvement to discuss in the viva.

---

## H. Viva preparation

**Basic**

1. *Which files does DispatchCore write?* Only `resources.csv`, `incidents.csv` and
   `allocations.csv`.
2. *What happens to a malformed row?* It is skipped with a `file:line: reason` warning and
   counted; good rows still load.
3. *What happens if `incidents.csv` is missing?* The program starts with no incidents.
4. *What if `locations.csv` is missing?* The load is refused: there is no map to work on.

**Tricky**

5. *Why not use `atoi`?* It cannot report errors: `atoi("abc")` is 0 and `atoi("12x")` is 12.
   `strtoll` with `end` and `errno` lets us reject both.
6. *Why never rewrite `roads.csv`?* Rewriting changes the row order, which changes the
   adjacency-list order, which can change which equal-length route is reported after a
   restart.
7. *Why refuse inconsistent data instead of fixing it?* A guessed fix would be saved on the
   next save and the evidence would be lost. Refusing keeps the original files intact for a
   person to inspect.
8. *Why is "cannot open" different from "not found"?* If a locked file were treated as
   missing, the program would start empty and the next save would erase the real data.
9. *What does `fclose` have to do with saving safely?* Buffered data is only written at
   `fclose`, so a disk-full error may only appear there. We check it before replacing the old
   file.
10. *Is saving atomic?* Writing is, per file: a crash only damages a `.tmp`. Across three
    files it is not fully atomic (design detail 5).
11. *Why can the clock be smaller after a restart?* Cancellations and unit registrations
    advance it but are not stored as sequence numbers. It is still larger than every stored
    value, which is all ordering needs.
12. *How do you know a saved route is still valid?* `dijkstra_validate_path` re-checks every
    road and the total distance while loading.
13. *What is a byte-order mark?* Three bytes (`EF BB BF`) some programs, such as Excel, put
    at the start of a UTF-8 file. Without skipping them, the header comparison would fail.

**Complexity**

14. *Cost of saving?* O(N + L): one pass over every record.
15. *What dominates loading time?* The duplicate-ID checks, O(N²) in the worst case.
    Checking duplicates through the hash index would bring it close to O(N).

**Pointers and memory**

16. *Who frees a route parsed from the file?* The loader, if validation fails; otherwise the
    history takes it over and frees it in `history_free`.
17. *Why does `fm_load` call `app_init(app, NULL)` first?* That call always fails, but it
    leaves `*app` in a known empty state, so every early `return` hands back something the
    caller can safely `app_free`.
18. *What does `*inc = *fields` do?* Struct assignment: it copies every field of the
    validated struct into the newly allocated record.

**Integration**

19. *When will the program save?* Phase 9: after every state-changing menu action
    (auto-save) and from the Save menu item. A failed save is reported and the in-memory data
    stays intact.
20. *How does the menu start up?* `fm_load` from `data/`; if it refuses, print the warnings
    and stop **without saving anything**.

---

## I. Team explanation

| Member | Must understand | Independently validate |
|---|---|---|
| **M1 (lead)** | Design details 1–8; D5 and D6 | Decide the start-up behaviour when a load is refused (Phase 9) |
| **M2 (priority queue)** | Why the heap is rebuilt from PENDING incidents only, and why the service order survives a restart | Check that `compare_states` really compares the service order |
| **M3 (graph)** | Design detail 1 (determinism); `load_locations`/`load_roads`; route re-validation | Create a fixture with a 3-location line city and predict its load report |
| **M4 (records)** — primary validator | All of it | (1) Predict table E2 line by line from the fixture files. (2) Add one more malformed incident row of your own design and update the expected counts. (3) Open each `.csv` from a round trip and check it by eye. (4) Review `file_manager.c` |
| **M5 (testing)** | Fixtures, the round-trip comparison, the not-covered list | Write the manual restart test for Phase 9: start, register, quit, start again, check the listings; keep `TEST_REPORT.md` current |

---

## J. Integration

- **Dependencies introduced:** `file_manager → app_state, dijkstra`. Nothing depends on
  `file_manager` except the program's start-up and save code (Phase 9) and the tests.
- **Used next by:**
  - Phase 8: no file changes; reports read the loaded state.
  - Phase 9: `main.c` calls `fm_load("data")` at start and `fm_save_records` after each change.
  - Phase 10: scenarios are loaded from `data/scenarios/*` into **separate** `AppState`s,
    never saved.
- **Build:** `make all`, `make analyze`, `make test`.

### Suggested Git steps (for you to run; I have not run any Git command)

```bash
git switch -c feature/phase7-persistence
```
```bash
git status
```
```bash
git add .gitattributes src/file_manager.h src/file_manager.c src/incident.h src/incident.c src/resource.h src/resource.c src/app_state.h src/app_state.c tests/test_file_manager.c tests/data Makefile docs/PHASE7_PERSISTENCE.md docs/TEST_REPORT.md
```
```bash
git commit -m "Phase 7: CSV persistence with strict validation, safe saving and fixtures"
```

Then push and open a Pull Request with **Member 4** as the primary reviewer.

**Note:** add `.gitattributes` in the **same** commit as `tests/data`. If the fixtures are
committed first, Git may already have converted the CRLF fixture to plain line endings.
