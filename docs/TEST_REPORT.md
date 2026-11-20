# DispatchCore — Test Report

This file records **what has actually been verified, how, and when**. It never reports a
test as passed unless the test program was really executed and its output was seen.
Owner: Member 5. Every phase appends to it.

## 1. Evidence levels used in this report

| Level | Meaning | What it proves |
|---|---|---|
| **C — Compiled** | `make all` built the program with `-std=c11 -Wall -Wextra -Wpedantic` and **zero warnings** | Syntax, types and interfaces are consistent; nothing about runtime behaviour |
| **A — Statically analysed** | `make analyze` passed: `-O2 -Wshadow -Wformat=2 -Wnull-dereference -Wstrict-prototypes -Wmissing-prototypes -fanalyzer -Werror` | GCC found no leak, double free, use-after-free, NULL dereference or uninitialised read **on the paths it explored, one `.c` file at a time**. It is not a proof of correctness |
| **R — Logically reviewed** | Code and expected values checked by hand (traces in the phase documents) | Human reasoning only; errors are possible |
| **E — Executed** | The test program ran and its output was observed | The checks in that program really passed **on that run** |

## 2. Known limitation: Windows Smart App Control

The lead developer's laptop has **Windows Smart App Control enabled**. The team has
**decided to keep it enabled** (2026-11-19). Smart App Control blocks unsigned executables
that have no established reputation, which includes every program we compile. The Windows
Code Integrity log records the block as event 3077 (*"did not meet the Enterprise signing
level requirements"*).

Observed behaviour:

- **Compiling works normally.** GCC, `make` and the other MSYS2 tools are allowed to run.
- **Running our test programs is usually blocked**: `Permission denied`, exit status 126.
- **Blocking is not consistent.** On 2026-11-19, `build/test_graph.exe` was blocked at 14:08,
  ran once at about 14:20, and was blocked again at 14:21, with no setting changed in
  between. We cannot predict or control this. No attempt was made to get around the policy.

Consequences:

- On this laptop, most tests can reach only levels **C + A + R**.
- Level **E** evidence must come from a run that Smart App Control happens to allow, or from
  any other machine where the team chooses to run `make test`. Every such run must be
  logged in section 4 with the date, machine and compiler version.

## 3. Status matrix

| Test program | Phase | Checks | C | A | R | E | Result |
|---|---|---|---|---|---|---|---|
| `test_graph` | 1 | 95 | ✅ | ✅ | ✅ | ✅ (2 runs) | **95 / 95 passed on 2026-11-19 ~14:20 and 21:41** (full output kept for 21:41) |
| `test_minheap` | 2 | see source | ✅ | ✅ | ✅ | ❌ blocked | **Not yet executed** |
| `test_dijkstra` | 2 | see source | ✅ | ✅ | ✅ | ❌ blocked | **Not yet executed** |
| `test_incident` | 3 | 89 | ✅ | ✅ | ✅ | ✅ (2 runs, full output kept) | **89 / 89 passed on 2026-11-19 20:48 and 21:41** |
| `test_incident_heap` | 3 | see source | ✅ | ✅ | ✅ | ❌ blocked | **Not yet executed** |
| `test_resource` | 4 | 98 | ✅ | ✅ | ✅ | ✅ (2 runs, full output kept) | **98 / 98 passed on 2026-11-19 20:48 and 21:41** |
| `test_compatibility` | 4 | 22 | ✅ | ✅ | ✅ | ✅ (1 run, full output kept) | **22 / 22 passed on 2026-11-19 21:41** |
| `test_hash_table` | 5 | 67 | ✅ | ✅ | ✅ | ✅ (2 runs, full output kept) | **67 / 67 passed on 2026-11-19 20:48 and 21:32** |
| `test_app_state` | 5 (extended in 6) | see source | ✅ | ✅ | ✅ | ❌ blocked | **Not yet executed** |
| `test_history` | 6 | 51 | ✅ | ✅ | ✅ | ✅ (2 runs, full output kept) | **51 / 51 passed on 2026-11-19 21:32 and 21:41** |
| `test_dispatch` | 6 | 165 | ✅ | ✅ | ✅ | ✅ (1 run, full output kept) | **165 / 165 passed on 2026-11-19 21:32** (with the Phase 10 version of `dispatch.c`) |
| `test_file_manager` | 7 | 81 | ✅ | ✅ | ✅ | ✅ (2 runs, full output kept) | **81 / 81 passed on 2026-11-19 21:32 and 21:41** |
| `test_report` | 8 (fixed in 11) | 154 | ✅ | ✅ | ✅ | ⚠️ 1 run of the OLD version | **136 / 154 passed on 2026-11-19 21:41; 18 FAILED**, all caused by the test reading the report file in binary mode (see the log entry). The corrected test has **not yet been executed**. |
| `test_input` | 9 | 73 | ✅ | ✅ | ✅ | ✅ (2 runs, full output kept) | **73 / 73 passed on 2026-11-19 21:32 and 21:41** |
| `test_menu` | 9 (changed in 10) | 52 | ✅ | ✅ | ✅ | ⚠️ 1 run of the OLD version | **51 / 52 passed on 2026-11-19 21:32; 1 FAILED** (a wrong expectation in the test, see the log entry). The corrected test has **not yet been executed**. |
| `test_evaluation` | 10 | 180 | ✅ | ✅ | ✅ | ✅ (1 run, full output kept) | **180 / 180 passed on 2026-11-19 21:32** |
| e2e: `dispatchcore` on `tests/e2e/*.in` | 9 | 37 | ✅ | ✅ | ✅ | ✅ (1 run, full output kept) | **37 / 37 passed on 2026-11-19 21:41** (the real program, including `main.c`) |

Note: the Phase 1 `test_graph` result above comes from the source as it was on 2026-11-19
~14:20. That source (`graph.c`, `test_graph.c`) has not changed since. The three 20:48
results come from the sources as they are after Phase 8 (Phase 8 did not change
`incident.c`, `resource.c` or `hash_table.c`).

## 4. Execution log

### 2026-11-19 — lead developer's laptop

- Environment: Windows 11 Home 10.0.26300, MSYS2 UCRT64, `gcc.exe (Rev4, Built by MSYS2 project) 16.2.0`, GNU Make 4.4.1.
- `make all`: exit 0, **no warnings** (all three test programs).
- `make analyze`: exit 0, **no diagnostics**. Files analysed: `src/common.c`, `src/graph.c`,
  `src/minheap.c`, `src/dijkstra.c`, `tests/reference_city.c`, `tests/test_graph.c`,
  `tests/test_minheap.c`, `tests/test_dijkstra.c`.
  - The analyzer setup was itself checked first: a small file with a deliberate memory leak
    produced `-Wanalyzer-malloc-leak` when compiled with `-c`. The same file produced
    **nothing** with `-fsyntax-only`, so the `analyze` target was changed to use `-c`.
- `make test`, last 8 lines of real output (the earlier lines were not captured):

```
(no road network loaded)

95 checks, 0 failed -> ALL PASSED
===== build/test_minheap.exe =====
/bin/sh: line 4: ./build/test_minheap.exe: Permission denied
===== build/test_dijkstra.exe =====
/bin/sh: line 4: ./build/test_dijkstra.exe: Permission denied
make: *** [Makefile:62: test] Error 1
```

  The summary line matches the hand-predicted 95 checks in `docs/PHASE1_GRAPH.md`.
  The printed road-network listing was **not** captured on this run, so it has not yet been
  compared with the predicted listing.
- Re-running `./build/test_graph.exe` straight afterwards: `Permission denied`, exit 126.

### 2026-11-19 17:11 — Phase 3, lead developer's laptop

- Same environment as above.
- `make clean && make all`: exit 0, **no warnings** (5 test programs).
- `make analyze`: exit 0, **no diagnostics** on 12 files (the 8 above plus `src/incident.c`,
  `src/incident_heap.c`, `tests/test_incident.c`, `tests/test_incident_heap.c`).
- `make test`: **all 5 programs blocked** (`Permission denied`), including `test_graph`,
  which had run once at 14:20. Nothing was executed, so nothing new is marked as passed.

### 2026-11-19 17:36 — Phase 4, lead developer's laptop

- Same environment as above.
- `make clean && make all`: exit 0, **no warnings** (7 test programs).
- `make analyze`: exit 0, **no diagnostics**; 16 object files produced (the 12 above plus
  `src/resource.c`, `src/compatibility.c`, `tests/test_resource.c`,
  `tests/test_compatibility.c`), confirming every file was analysed.
- `make test`: **all 7 programs blocked** (`Permission denied`). Nothing executed.

### 2026-11-19 17:42 — Phase 5, lead developer's laptop

- Same environment as above.
- Phase 5 **modified two Phase 3/4 source files**: `incident_store_remove_last` was added to
  `src/incident.[ch]` and `resource_store_remove_last` to `src/resource.[ch]`. No existing
  function changed. Their earlier test programs were rebuilt and re-analysed.
- `make clean && make all`: exit 0, **no warnings** (9 test programs).
- `make analyze`: exit 0, **no diagnostics**; 20 object files (the 16 above plus
  `src/hash_table.c`, `src/app_state.c`, `tests/test_hash_table.c`, `tests/test_app_state.c`).
- `make test` was invoked twice in a row: **9 of 9 programs blocked** each time. Nothing
  executed.

### 2026-11-19 17:55 — Phase 6, lead developer's laptop

- Same environment as above.
- Phase 6 **modified** `src/app_state.h` and `src/app_state.c`: added the `history` list and
  `next_allocation_id`, freeing of the history, and allocation rules in
  `app_check_consistency`. No other earlier file changed.
- `make clean && make all`: exit 0, **no warnings** (11 test programs).
- `make analyze`: exit 0, **no diagnostics**; 24 object files (the 20 above plus
  `src/history.c`, `src/dispatch.c`, `tests/test_history.c`, `tests/test_dispatch.c`).
- `make test` (once, output saved to `build/test_run.log`): **11 of 11 programs blocked**.
  Nothing executed.

### 2026-11-19 18:04 — Phase 7, lead developer's laptop

- Same environment as above.
- Phase 7 **modified** earlier files:
  - `src/incident.[ch]`: added `incident_restore`;
  - `src/resource.[ch]`: added `resource_restore`;
  - `src/app_state.[ch]`: added `app_add_loaded_incident`, `app_add_loaded_resource`,
    `app_add_loaded_allocation` and `app_finish_loading`. It also refactored the bodies of
    `app_register_incident`/`app_register_resource` into the shared private helpers
    `insert_incident`/`insert_resource`; their behaviour is unchanged.
- New CSV fixtures in `tests/data/` (valid, malformed, bad_header, inconsistent, gap, crlf,
  minimal) and a `.gitattributes` file that keeps their bytes exact. The CRLF line endings,
  byte-order mark and 9,029-character line were confirmed with `od -c` and `awk`.
- `make clean && make all`: exit 0, **no warnings** (12 test programs).
- `make analyze`: exit 0, **no diagnostics**; 26 object files.
- `make test` (once): **12 of 12 programs blocked**. Nothing executed.

### 2026-11-19 20:48 — Phase 8, lead developer's laptop

- Same environment as above.
- Phase 8 added `src/report.[ch]` and `tests/test_report.c`. **No earlier source file was
  changed**; only the `Makefile` gained the new module and test program.
- `make clean && make all`: exit 0, **no warnings** (13 test programs).
- `make analyze`: exit 0, **no diagnostics**; 28 object files (the 26 above plus
  `src/report.c` and `tests/test_report.c`).
- `make test` (once; exit status 2 because some programs were blocked). The full output
  (117 lines) is kept unedited in `docs/evidence/2026-11-19_2048_phase8_make_test.log`.
  - **Executed, all checks passed** (no `FAIL` line anywhere in the log):
    `test_incident` 89 / 89, `test_resource` 98 / 98, `test_hash_table` 67 / 67.
    These three test programs are unchanged since their phases, so this is their first
    execution evidence.
  - **Blocked** (`Permission denied`): the other 10 programs, including the new
    `test_report`. Smart App Control allowed some programs and blocked others in the same
    run, again with no setting changed. Nothing was done to influence which ran.

### 2026-11-19 21:20 — Phase 9, lead developer's laptop

- Same environment as above.
- Phase 9 added `src/input.[ch]`, `src/menu.[ch]`, `src/main.c`, `tests/test_input.c`,
  `tests/test_menu.c`, and `tests/e2e/` (`data/` copy of the reference map, `day1.in`,
  `bad_input.in`, `*.expect`, `run_e2e.sh`). **No earlier source file was changed**;
  `Makefile` and `.gitignore` were extended.
- `make clean && make all`: exit 0, **no warnings** (the program `build/dispatchcore.exe`
  and 15 test programs).
- `make analyze`: exit 0, **no diagnostics**; 33 object files (the 28 above plus
  `src/input.c`, `src/menu.c`, `src/main.c`, `tests/test_input.c`, `tests/test_menu.c`).
- `make test` (once): **15 of 15 programs blocked**, including the three that ran at 20:48.
  Nothing executed. Log: `docs/evidence/2026-11-19_2120_phase9_make_test.log`.
- `make e2e`, first run: `dispatchcore.exe` blocked in all 4 runs. The first version of
  `run_e2e.sh` counted each blocked run as failed checks ("0 passed, 37 failed"). Every
  output file contained only `Permission denied`, so **no check was actually evaluated**.
  Log: `docs/evidence/2026-11-19_2120_phase9_make_e2e.log`.
- `run_e2e.sh` was then corrected to report exit status 126 as **NOT RUN** instead of FAIL.
  Second `make e2e`: "0 passed, 0 failed, 4 runs NOT RUN (blocked)". Log:
  `docs/evidence/2026-11-19_2120_phase9_make_e2e_rerun.log`.
- `data/` still holds only `locations.csv` and `roads.csv`: no run wrote to it.

### 2026-11-19 21:32 — Phase 10, lead developer's laptop

- Same environment as above.
- Phase 10 added `src/evaluation.[ch]` and `tests/test_evaluation.c`. It **modified**
  earlier files:
  - `src/dispatch.[ch]`: added `UnitRule`, `dispatch_select_resource_rule`,
    `dispatch_next_rule` and `dispatch_all_rule`; the private `commit` became the public
    `dispatch_assign`. The old functions are now wrappers with `UNIT_NEAREST`, so their
    behaviour is unchanged;
  - `src/menu.c` (option 20 runs the comparison) and `src/main.c` (`--benchmark`);
  - `tests/test_menu.c`: its option-20 expectations;
  - `Makefile` (`make bench`).
- `make clean && make all`: exit 0, **no warnings** (program + 16 test programs).
- `make analyze`: exit 0, **no diagnostics**; 35 object files.
- `make test` (once). Full log: `docs/evidence/2026-11-19_2132_phase10_make_test.log`.
  - **Executed, all checks passed:** `test_hash_table` 67/67, `test_history` 51/51,
    `test_dispatch` 165/165, `test_file_manager` 81/81, `test_input` 73/73,
    `test_evaluation` 180/180.
    - `test_dispatch` passing means the Phase 6 behaviour survived the Phase 10 change to
      `dispatch.c`.
    - `test_evaluation` passing confirms the hand-traced T1–T4 values and the validity of
      every generated run.
  - **Executed, 1 FAILURE:** `test_menu`, 51/52. Line 193 expected the saved row
    `1,RESCUE_UNIT,4,AVAILABLE,-1,0,0\n`.
    - The file really contains `...,0,0\r\n`, confirmed byte by byte with `od -c`.
      `fm_save_records` opens files in text mode, so Windows writes `\r\n`.
    - **The program was right and the test expectation was wrong.** The same test's reload
      of that folder passed, as did the Phase 7 round-trip tests in this run.
    - The check now accepts either line ending.
  - **Blocked:** the other 9 programs.
- Re-run of the corrected `test_menu` only (after rebuilding it and re-analysing
  `tests/test_menu.c`, which was clean): **blocked**. Log:
  `docs/evidence/2026-11-19_2132_phase10_test_menu_rerun.log`. **The fix is therefore not
  yet verified by execution.**
- `make e2e`: 4 runs NOT RUN (blocked). `make bench`: blocked, so **no timing numbers exist
  yet**.
- `data/` is unchanged.

### 2026-11-19 21:41 — Phase 11 final run, lead developer's laptop

- Same environment as above. **No source file in `src/` changed in Phase 11.**
- `make clean && make all`: exit 0, **no warnings** (program + 16 test programs).
- `make analyze`: exit 0, **no diagnostics**; 35 object files.
- `make test` (once). Log: `docs/evidence/2026-11-19_2141_phase11_test_run.log`.
  - **Executed, all checks passed:**
    - first execution evidence: `test_compatibility` 22/22;
    - repeat passes: `test_graph` 95/95, `test_incident` 89/89, `test_resource` 98/98,
      `test_history` 51/51, `test_file_manager` 81/81, `test_input` 73/73.
  - **Executed, 18 FAILURES:** `test_report`, 136/154, its first execution.
    - **Every failure is in one test function (`test_full_report_file`).** The test reads
      the written `report.txt` back in binary mode (`"rb"`) and expects lines ending in
      `\n`.
    - `report_write_file` writes in text mode, so on Windows the file holds `\r\n`
      (confirmed with `od -c`).
    - The same report lines printed to the screen in the same run match the expected text
      exactly, character for character (log lines around "DispatchCore report").
    - **The reporting code was right; the test was wrong.** This is the same mistake as in
      `test_menu` at 21:32.
    - The 3 `CROSS-CHECK FAILED` lines in this log are **expected output**: they come from
      the test that plants mismatches on purpose, and those checks passed.
    - Fix: `read_file` in `tests/test_report.c` now opens in text mode (`"r"`).
  - **Blocked:** the other 8 programs.
- Re-run of the corrected `test_report` and `test_menu` (rebuilt; `tests/test_report.c`
  re-analysed clean): **both blocked**. Log:
  `docs/evidence/2026-11-19_2142_phase11_fixed_tests_rerun.log`. **Neither fix is
  verified by execution yet.**
- `make e2e`: **37 / 37 checks passed**, the first execution of the real program.
  - It covers the day1 and bad_input scripts, plus the missing-folder (exit 1) and
    unknown-argument (exit 2) cases.
  - Log: `docs/evidence/2026-11-19_2141_phase11_e2e_run.log`.
- `make bench` ran. Log: `docs/evidence/2026-11-19_2141_phase11_bench_run.log`.
  - It was measured with `clock()`, whose resolution here is coarse (1 ms steps), so
    small values such as "0.0 ms" mean "below the resolution". Only one run was made,
    so the variation between runs is unknown.

  | Measurement | Small | Medium | Large |
  |---|---|---|---|
  | Dijkstra, 50 sources, heap / array (ms) | 100 loc: 0.0 / 2.0 | 400 loc: 4.0 / 15.0 | 961 loc: 9.0 / 67.0 |
  | 100,000 ID lookups, hash / linear (ms) | 1000 units: 1.0 / 46.0 | 2000 units: 1.0 / 108.0 | 4000 units: 5.0 / 208.0 |

  Both versions reported equal distances, and both lookups found all 100,000 IDs.
- `data/` is unchanged.

## 5. Not yet covered by any test

- Out-of-memory paths (`DC_ERR_NOMEM`): reviewed by reading the code only (level R).
- Valgrind / AddressSanitizer runtime memory checks: not available with this setup.
  `-fanalyzer` (level A) is the substitute and is weaker than a runtime check.
- **Never executed yet:** `test_minheap`, `test_dijkstra`, `test_incident_heap`,
  `test_app_state`.
- **Executed only in an older version, fix not yet re-run:** `test_report`, `test_menu`.
- Interactive use by a person, including the manual restart test in the Phase 9 doc §F,
  has not been done or recorded.
