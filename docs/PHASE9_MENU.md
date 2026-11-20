# Phase 9 — Full Menu & Integration

Owner: **Member 5**, with M1 integrating. Status: the program and all tests compile with
0 warnings and are statically analysed clean. **Nothing in this phase has been executed
yet**: Smart App Control blocked `dispatchcore.exe`, `test_input` and `test_menu` on
2026-11-19 21:20 (see `docs/TEST_REPORT.md`).

**New files:**
- `src/input.h`, `src/input.c`, `src/menu.h`, `src/menu.c`, `src/main.c`;
- `tests/test_input.c`, `tests/test_menu.c`;
- `tests/e2e/data/` (a copy of the reference map), `tests/e2e/day1.in`/`.expect`,
  `tests/e2e/bad_input.in`/`.expect`, `tests/e2e/run_e2e.sh`.

**Changed files:**
- `Makefile`: the program target `build/dispatchcore`, `test_input`, `test_menu`, and the
  new `make e2e` and `make run` targets;
- `.gitignore`: ignores `data/report.txt`.

**No earlier `.c`/`.h` file was modified.**

### Decisions for you to review

1. **Menu numbering follows the blueprint (§13).**
   - Option 17 (road closures) is listed as `[not implemented]`. This optional feature was
     never built, and `roads.csv` only supports OPEN.
   - Option 20 says "Phase 10".
   - **New option 23** writes the full Phase 8 report to `<data folder>/report.txt`.
2. **One flat menu**, not sub-menus. You never answered this Phase 0 question; a flat menu
   is faster to demo. Only "Allocation history" asks a follow-up question (which filter).
3. **Autosave is on by default.** After every action that changes records, the three record
   files are saved. `--no-autosave` turns this off, and the tests use it so the fixtures
   are never written.
4. **When autosave is off, exit does not save.** It warns that unsaved changes are being
   discarded. The menu line says `(autosave is OFF)` so this is never a surprise.
5. **A failed save never stops the program.** The data stays in memory, a warning is
   printed, and option 22 can retry.
6. **A refused load stops the program without saving** (as promised in the Phase 7 doc,
   question 20), with exit status 1.
7. **Taking a unit out of service asks y/n first.** It is the only reversible-but-surprising
   action. Cancelling an incident does not ask; the blueprint did not ask for it, but we
   can add it if you want.
8. **The first run on `data/` creates `resources.csv`, `incidents.csv` and
   `allocations.csv`** there, on the first change. Today `data/` holds only the two map
   files. Decide whether those three files should be committed as demo data or ignored.

---

## A. Purpose

Phases 1–8 built and tested the engine module by module. Phase 9 turns them into **one
program** that a person can use:

```
main.c ── parses arguments, loads data (file_manager), starts the menu
  └─ menu.c ── shows options, asks questions (input.c), calls the engine,
                decides when to save
        ├─ app_state / dispatch    registration, dispatch, complete, cancel
        ├─ graph / dijkstra        network listing, shortest route
        ├─ report                  history, statistics, report file
        └─ file_manager            saving
```

The menu holds **no business rules of its own**. Every rule lives in the engine and is
already tested; the menu only collects answers and shows the results. That is why the
menu can stay thin and why bugs found here are almost always input or wording problems.

## B. Concepts

**B1. Why not `scanf("%d", &x)`?** If the user types `abc`, `scanf` fails *and leaves `abc`
in the input buffer*. The next `scanf` sees the same `abc` and fails again, and a menu loop
spins forever. `scanf` also accepts `12abc` as 12, and gives undefined behaviour when the
number is too large to fit.

Our rule is **read one whole line with `fgets`, then parse it with `strtol`**:

| Check | How `input_parse_int` detects it |
|---|---|
| empty line | only spaces after skipping whitespace → `PARSE_EMPTY` |
| no digits (`abc`, `-`) | `end == start` after `strtol` → `PARSE_NOT_NUMBER` |
| trailing garbage (`12abc`, `1.5`, `1 2`) | something other than spaces after `end` → `PARSE_NOT_NUMBER` |
| too big for `long` | `errno == ERANGE` → `PARSE_OUT_OF_RANGE` |
| outside the allowed range | compared as `long` with min/max → `PARSE_OUT_OF_RANGE` |

**B2. Over-long lines.** `fgets` stops when the buffer is full. If we did nothing, the rest
of that line would be read as the *next* answer. `input_read_line` notices that there is no
`'\n'`, throws the rest of the line away up to the `'\n'`, and reports `INPUT_TOO_LONG`.

With a 128-byte buffer, the longest accepted line is 126 characters: 126 characters plus
`'\n'` plus `'\0'` fill all 128 bytes. A line of 127 characters is rejected. The tests check
exactly this boundary.

**B3. End of input is a normal event.** When stdin ends (Ctrl+Z then Enter on Windows,
Ctrl+D on Linux, or the end of a script file), `fgets` returns NULL. The input functions
then return `INPUT_EOF`, never "try again". Every menu action passes `MENU_EOF` upwards,
and `menu_run` leaves the loop, prints `End of input.`, and applies the exit saving rule.
Without this, a scripted test that ended early would loop forever.

**B4. Dependency injection with `Console`.** The input and menu code never use `stdin` or
`stdout` directly. They use the `FILE *in` and `FILE *out` inside a `Console` struct:
- `main` passes the real stdin and stdout;
- the tests pass files.

This lets `test_menu` run the *real* menu from a script file inside a unit-test program.
Passing what a function needs, instead of letting it reach for globals, is called
**dependency injection**.

**B5. The "dirty flag".** `MenuSession.dirty` is set by `after_change()` whenever records
change, and cleared by a successful save. It answers "is there anything to lose?" in O(1),
without comparing the data with the files. Exit uses it:

| autosave | dirty at exit | what happens | exit status |
|---|---|---|---|
| on | no | nothing to save | 0 |
| on | yes (an autosave failed) | save again; if it fails, warn | 0 if saved, 1 if not |
| off | no | nothing | 0 |
| off | yes | warning: changes discarded | 0 (the user asked for this mode) |

**B6. What counts as a change?** Each action is classified, so that a save happens exactly
when records change:

| Action | Changes records? |
|---|---|
| registering an incident or a unit, cancelling, completing, changing service status | yes |
| "dispatch next" | yes if any incident was tried: a success changes statuses, and a failure increments `dispatch_attempts` |
| "dispatch all" | yes if the queue was not empty |
| all views, searches, routes and reports | no (they take `const` data) |

**B7. Exit status (a program's return value).** It lets scripts and the e2e runner see
what happened:
- 0: normal;
- 1: the data could not be loaded, or the final save failed;
- 2: wrong command-line arguments;
- 126 (set by the shell, not by us): the program could not be started at all, which is
  what Smart App Control causes.

## C. Code tour

### `input.h` / `input.c`

| Function | Notes |
|---|---|
| `input_read_line` | Uses `fgets`. Removes `"\n"` or `"\r\n"`. Accepts a last line without `'\n'`. Discards the tail of an over-long line. Returns `INPUT_EOF` on end of input or a read error. |
| `input_parse_int` | Implements B1. Writes `*out` only on success. Casts to `unsigned char` before calling `isspace`, because passing a negative `char` is undefined behaviour. |
| `read_answer` (static) | Reads a line; echoes it in `--echo` mode; prints the "too long" message. |
| `input_read_int` | Prompt `"<text> (min-max): "`, flushed so the user sees it before the program waits. Loops with a reason until the answer is valid or input ends. |
| `input_read_yes_no` | Accepts `y`, `yes`, `n`, `no` in any letter case, with spaces allowed around the word. |

### `menu.h` / `menu.c`

- **`MenuSession`**: borrows the `AppState` and the `Console`, and holds the data folder,
  the autosave setting, the dirty flag and the number of failed saves.
- **Saving helpers**: `save_now` (saves, then prints `(saved to DIR)` or a warning) and
  `after_change` (sets dirty, and saves if autosave is on).
- **Question helpers**: `ask_location` (lists the locations if there are 30 or fewer),
  `ask_id`, `ask_incident_type` (shows which unit types each incident type needs),
  `ask_resource_type` and `ask_severity`. Menus count from 1; the enums count from 0, so
  the helpers subtract 1.
- **One static function per option** (`opt_*`). Each returns `MENU_CONTINUE` or
  `MENU_EOF`. Error messages explain *why*, for example "Incident 1001 is DISPATCHED; only
  PENDING incidents can be cancelled."
- **`menu_handle`**: a `switch` over the choice.
- **`menu_run`**: the loop, followed by the exit rule (B5).

| Option | Engine call(s) |
|---|---|
| 1 / 6 | `app_register_incident` / `app_register_resource` |
| 2 | `incident_heap_print_priority_order` (works on a copy of the heap, so the real queue is untouched) |
| 3 / 7 / 8 | `incident_store_print` / `resource_store_print` |
| 4 / 9 | `app_find_incident` / `app_find_resource` (O(1) hash lookups), then the history filtered by incident or unit |
| 5 / 14 | `dispatch_cancel` / `dispatch_complete` |
| 10 | `resource_set_out_of_service` / `resource_return_to_service`, after asking y/n |
| 11 / 12 / 13 | `dispatch_next` / `dispatch_all` / `dispatch_print_active` |
| 15 | `graph_print`, plus `graph_count_components` (warns about separate parts of the city) |
| 16 | `dijkstra_run`, `dijkstra_build_path`, `dispatch_print_route` |
| 18 / 19 / 23 | `report_print_history` / statistics with cross-check and utilisation / `report_write_file` |
| 21 | `hash_table_print` for both indexes, and `incident_heap_print_array` |
| 22 | `fm_save_records` |

### `main.c`

1. Parse the arguments `--data DIR`, `--no-autosave`, `--echo` and `--help`; anything
   else gives exit status 2.
2. Print the academic-simulation notice.
3. Call `fm_load`. If it refuses, print the reasons and exit with 1 **without saving**.
4. Create the `Console` for stdin/stdout, start the menu, free everything, and return the
   menu's exit status.

## D. Walkthrough: one choice, end to end

The user types `14`, then `1002` ("Complete incident 1002"):

```
menu_run   → input_read_int("Choice", 0, 23)  reads "14"      → menu_handle(14)
opt_complete → ask_id → input_read_int("Incident ID...", 1, 100000000) reads "1002"
             → dispatch_complete(app, 1002, out)
                  hash lookup 1002 → DISPATCHED → release unit at incident location
                  incident COMPLETED, allocation completed, clock++
                  prints "Incident 1002 completed. Unit 2 is available again at ..."
             → DC_OK → after_change → dirty = 1 → autosave → fm_save_records → dirty = 0
menu_run   → shows the menu again
```

If the user had typed `1003` (a PENDING incident), `dispatch_complete` returns
`DC_ERR_STATE`. The menu looks the incident up, finds it is PENDING, and prints the
reason. Nothing is changed and nothing is saved.

## E. Worked example: the "day1" script (used by `test_menu` and `make e2e`)

Start: the reference city, with no units, incidents or allocations; the clock is 0.

| # | Input | Expected effect (hand-derived) |
|---|---|---|
| 1 | `6 1 0` | Unit 1 AMBULANCE at 0 Central Hospital (clock 0 → 1) |
| 2 | `6 2 2` | Unit 2 FIRE_UNIT at 2 Fire Station North (1 → 2) |
| 3 | `1 1 5 4` | Incident 1001 MEDICAL at 5 Old Town, severity 4 Severe (reported_seq 2) |
| 4 | `1 3 4 5` | Incident 1002 FIRE at 4 Industrial Zone, severity 5 Critical (reported_seq 3) |
| 5 | `2` | The queue lists 1002 before 1001 (severity 5 > 4) |
| 6 | `11` | 1002 is first. Its only compatible unit is unit 2 (fire). Dijkstra from 4: distance to 2 is 5 (road 2–4). Route `2 > 4`. |
| 7 | `11` | 1001 (MEDICAL) can use only unit 1. Distance 0→3→5 = 7+2 = **9**; 0→1→5 = 4+6 = 10. Route `0 > 3 > 5`. |
| 8 | `11` | The queue is empty: `No pending incidents.` |
| 9 | `14 1002` | Completed. Unit 2 is released at 4 Industrial Zone. |
| 10 | `5 1001` | Refused: 1001 is DISPATCHED. |
| 11 | `10 2 y` | Unit 2 (AVAILABLE) goes OUT_OF_SERVICE. |
| 12 | `7` | No unit is available (unit 1 is busy, unit 2 is out of service): `(no resources)` |
| 13 | `4 1001` | Incident row, plus history line `#2 ACTIVE: incident 1001 MEDICAL (severity 4) at 5 Old Town` |
| 14 | `15` (only in `tests/e2e/day1.in`, not in `test_menu`) | `Road network: 10 locations, 11 roads`, and a warning about 2 separate groups (8–9 is an island) |
| 15 | `16 0 9` | `No road connects 0 Central Hospital and 9 Lake Camp.` |
| 16 | `16 0 7` | **16** via 0-1-2-4-7 (4+3+5+4). Alternatives: 0-3-5-7 = 17, 0-1-5-7 = 18, 0-3-6-7 = 19. |
| 17 | `19` | `Allocations: 2 (active 1, completed 1)`, and `Cross-check: all counters agree` |
| 18 | `abc`, `99` | `"abc" is not a whole number.`, then `Please type a number from 0 to 23.` |
| 19 | `17`, `20` | The "not implemented" and "Phase 10" messages |
| 20 | `0` | Autosave is off and the session is dirty, so `Autosave is off: unsaved changes are discarded.`, then `Goodbye.` |

`test_menu` also checks the final state:
- 1001 is DISPATCHED;
- 1002 is COMPLETED;
- unit 2 is OUT_OF_SERVICE at location 4;
- there are 2 allocations;
- `app_check_consistency` reports 0 problems.

**The `bad_input` script** sends: an empty line, `abc`, `12abc`, `99`, `-1`, spaces only,
then "search incident" with ID `0` (out of range), then ID `5` (does not exist), then
"search unit" `77`, then exit. Each produces its own message, listed in `bad_input.expect`.

## F. Testing

| Test | What it covers |
|---|---|
| `test_input` | Every row of the B1 table. The 126/127-character boundary and the discarded tail. `\r\n`, a last line without `\n`, and staying at end of input. Re-prompt messages counted exactly (4 rejections → 5 prompts). Echo output byte for byte. Silent console. Yes/no in any letter case. |
| `test_menu` | The day1 script through the real menu (24 message checks plus the final state). Autosave writes `resources.csv` with the exact row `1,RESCUE_UNIT,4,AVAILABLE,-1,0,0`, and reloading that folder gives the same unit, with the next ID being 2: this is the persistence round trip. Input ending mid-action creates nothing and exits cleanly. Empty input. NULL sessions. |
| `make e2e` (`run_e2e.sh`) | The **real executable**, including `main.c`: `day1` and `bad_input` with `--no-autosave --echo`, each `.expect` line matched as a whole line. A missing data folder gives exit 1 and its message; an unknown argument gives exit 2. A blocked program (exit 126) is reported as **NOT RUN**, never as passed or failed. |

**Manual restart test** (Phase 7 promise; do this on a machine where the program runs):
1. `make run`. Register a unit and an incident, then choose 0.
2. `make run` again. Options 3 and 8 must list the same records. A new incident must get
   the next free ID, not reuse one.

**Evidence status:**
- Compiled ✅
- Analysed ✅ (33 files)
- Reviewed ✅
- **Executed ❌**: all blocked on 2026-11-19 21:20. The logs are in `docs/evidence/`.

## G. Complexity

The menu adds no algorithms of its own; each option costs what its engine call costs:

| Option | Cost |
|---|---|
| Read one answer | O(line length) |
| Register (1, 6) | O(N) (duplicate scan in the store, Phase 3/4) + O(log N) heap + O(1) hash |
| Pending view (2) | O(P log P) (extracting from a copy of the heap) |
| Search (4, 9) | O(1) average lookup + O(H) history filter |
| Dispatch next (11) | Up to P incidents × one Dijkstra each, O((V+E) log V) + O(R) |
| Route (16) | O((V+E) log V) + O(V) path |
| Statistics (19) | O(N + P + R² + H) |
| Autosave | O(N + R + H + total route length), the full rewrite of three files |

Autosave rewrites every record after each change. That is O(total data) per action:
acceptable for a classroom-sized city, and the price of a simple, safe file format. (An
append-only log would be faster, but much harder to keep consistent.)

## H. Viva questions (with short answers)

1. **Why `fgets` + `strtol` instead of `scanf`?** `scanf` leaves bad input in the buffer
   (an infinite loop), accepts `12abc`, and cannot detect overflow. With `fgets` + `strtol`
   we see the whole line and check every case.
2. **How do you detect `12abc`?** `strtol` sets `end` to the first character it did not
   use. If anything other than spaces remains after it, the input is rejected.
3. **What happens on a 300-character line?** `fgets` returns the first 127 characters
   without `'\n'`. We discard the rest up to `'\n'`, say "too long" and ask again. The
   tail is never mistaken for the next answer.
4. **Why does EOF need special handling?** At EOF, `fgets` returns NULL every time. A
   "try again" loop would never end. We return `INPUT_EOF`, and the menu exits cleanly.
5. **Why is input/output passed in a `Console` instead of using stdin/stdout?** So that
   tests can run the real menu from files (dependency injection). The same code serves the
   user and the tests.
6. **What is the dirty flag, and why not just save at exit?** It records unsaved changes.
   Autosave after every change means a crash loses at most the current action; the flag
   tells exit whether anything is still unsaved.
7. **Why doesn't a failed save stop the program?** The data in memory is still correct.
   Stopping would *lose* it. Better to warn and let the user fix the folder and retry.
8. **Why does a refused load stop without saving?** Saving an empty state would overwrite
   the damaged-but-recoverable files with nothing.
9. **Why does the menu ask y/n before taking a unit out of service?** It removes the unit
   from every future dispatch. A slip of the finger should not do that silently.
10. **What does exit status 126 mean in the e2e results?** The shell could not start the
    program (here: Smart App Control). Nothing ran, so the result is NOT RUN, not FAIL.

## I. Team tasks

| Member | Task |
|---|---|
| M5 (owner) | Present B1–B3 and the day1 trace (E). Run `make test` and `make e2e` on any machine where they run, and log the result. Do the manual restart test (F). |
| M1 | Decide decision 8 (commit demo CSVs or not). Write the demo script for Phase 11 from day1. |
| M2 | Check the queue order shown by option 2 against Phase 3's heap rules. |
| M3 | Verify the route answers in E (steps 6, 7, 15, 16) by running Dijkstra by hand. |
| M4 | Try to break loading: start the program on `tests/data/bad_header`, `gap` and `inconsistent` (copies only), and confirm the refusal messages and exit status 1. |

## J. Integration

- **Run it** (from the project root, in the MSYS2 UCRT64 shell):

  ```bash
  make
  ```

  ```bash
  ./build/dispatchcore.exe --data data
  ```

  For a demo that writes no files:

  ```bash
  ./build/dispatchcore.exe --data tests/e2e/data --no-autosave
  ```

- **Phase 10** will replace option 20 with the strategy comparison (`evaluation.c`). It will
  run the same scenario under the baseline and optimised strategies and print two
  `SystemStats` side by side.
- **Phase 11** will use the day1 script as the basis of the demo, and document `--echo`
  for recorded demos.
- **Git (for you to do manually):**
  - new files: `src/input.*`, `src/menu.*`, `src/main.c`, `tests/test_input.c`,
    `tests/test_menu.c`, the `tests/e2e/` folder, the 3 new logs in `docs/evidence/`, and
    this document;
  - modified: `Makefile`, `.gitignore` and `docs/TEST_REPORT.md`.
  - `tests/e2e/*.in` should keep exact bytes. The existing `.gitattributes` rule covers
    only `tests/data/**`, so consider adding `tests/e2e/** -text` too.

  I ran no Git commands that change anything. One read-only `git status` was
  accidentally included in a shell command; Git is not installed in MSYS2, so it did
  nothing.
