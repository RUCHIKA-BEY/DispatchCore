# Phase 11 — Documentation & Demo

Owner: **all members**. This phase adds documentation only. **No file in `src/` changed.**
One test helper changed: `tests/test_report.c`, see §F.

**New files:**
- `README.md`;
- `docs/VIVA_NOTES.md`;
- `docs/DEMO_SCRIPT.md`;
- this file;
- six evidence files in `docs/evidence/`: four logs from the final 21:41 run, plus two
  e2e transcripts.

**Changed files:**
- `docs/TEST_REPORT.md`: the final run;
- `docs/PHASE10_EVALUATION.md`: the timing numbers now exist;
- `tests/test_report.c`: reads the report file in text mode.

---

## A. Purpose

The code is finished. Phase 11 makes the project **understandable, demonstrable and
honest** for the people who will judge it:

| Reader | Document |
|---|---|
| Anyone opening the repository | `README.md`: what it is, how to build and run it, the evidence status |
| The examiner during the demo | `docs/DEMO_SCRIPT.md`: 21 live steps with real expected output, plus a fallback |
| Each team member before the viva | `docs/VIVA_NOTES.md`: the whole flow, 10 key ideas, numbers, hard questions |
| Someone checking a claim | `docs/TEST_REPORT.md` and the raw logs in `docs/evidence/` |
| Someone learning one module | `docs/PHASE1`–`PHASE10`: sections A–J for each module |

## B. Principles used for the documents

1. **Every result quoted comes from a log file.** The README, viva notes and demo script
   quote only numbers that appear in `docs/evidence/`. Hand-traced values are labelled as
   traces.
2. **Limitations are stated, not hidden:**
   - the Smart App Control gaps;
   - the two test mistakes found by real runs;
   - unimplemented options (road closures);
   - O(N) registration and O(H) completion;
   - saving the three files together, but not atomically;
   - "simulation only".
3. **One source of truth per fact.** Test status lives in `TEST_REPORT.md`; the other
   documents summarise it and link to it, so they cannot drift apart silently.
4. **The demo never touches live data:** `--no-autosave`, or a scratch copy under
   `build/`.

## C. What the final run showed (2026-11-19 21:41)

- **Built and analysed clean:** program + 16 tests, 0 warnings; `make analyze` clean on
  35 files.
- **Passed in real runs:**
  - first-time evidence: `test_compatibility` 22/22 and the **end-to-end tests 37/37**
    (the real program);
  - repeat passes: `test_graph`, `test_incident`, `test_resource`, `test_history`,
    `test_file_manager` and `test_input`.
- **Timing ran once:**
  - Dijkstra on 961 locations: heap 9 ms vs array 67 ms;
  - 4000 units: hash 5 ms vs linear 208 ms for 100,000 lookups.
- **`test_report` ran for the first time: 136/154.** All 18 failures were in the test
  itself (see §F).

## D. Submission checklist (for you to run; I ran no Git commands)

1. Build and run everything once more. These commands are safe; none touches `data/`:

   ```bash
   make clean
   ```

   ```bash
   make
   ```

   ```bash
   make analyze
   ```

   ```bash
   make test
   ```

   ```bash
   make e2e
   ```

2. If `test_report` and `test_menu` run, add their results to `TEST_REPORT.md`. That would
   verify the two test fixes.
3. Optionally, run `make test` on a teammate's machine without Smart App Control. Record
   the machine, date and compiler in `TEST_REPORT.md` §4. This would cover the four
   never-executed programs.
4. Do the manual restart test (`docs/PHASE9_MENU.md` §F, or `DEMO_SCRIPT.md` Part B).
5. Consider adding `tests/e2e/** -text` to `.gitattributes`. It keeps the script bytes
   exact, as the existing rule already does for `tests/data/**`.
6. Decide whether `data/resources.csv`, `incidents.csv` and `allocations.csv` (created by
   a real run) should be committed as demo data, or left out.
7. Review the open decisions listed at the top of each phase document (Phases 7–10).
8. Commit when you are satisfied. The project is not a Git repository on this machine yet,
   and Git is not installed in MSYS2.

## E. Known limitations (also in the README)

| Area | Limitation | Possible improvement |
|---|---|---|
| Model | No traffic, real maps, one-way roads, road closures or multi-unit incidents | Directed graph; road `status`; incident needs a list of units |
| Registration | O(N) duplicate scan in the store | Check the hash index instead: O(1) average |
| Completion | O(H) search for the active allocation | Store an allocation pointer or ID in the incident |
| Loading the heap | n inserts, O(n log n) | Bottom-up heapify, O(n) |
| Saving | Three files, renamed one by one | A single combined file, or a journal |
| Evaluation | Greedy strategies, constant speed | Look-ahead or batch assignment; travel-time model |
| Testing | 4 programs never executed here; no runtime memory checker | Run on another machine; AddressSanitizer where available |

## F. The last test fix

`test_report`'s first real run (21:41) failed 18 checks. All 18 were in one function,
`test_full_report_file`, and had the same cause:

- **What happened:** `report_write_file` writes in text mode, so on Windows each line ends
  in `\r\n`. The test read the file in binary mode (`"rb"`) and expected `\n`.
- **Evidence that the program was right:** the same report printed to the screen in the
  same run matches the expected lines exactly.
- **The fix:** `read_file` now opens the file in text mode (`"r"`), which turns `\r\n` back
  into `\n` on Windows and changes nothing on other systems.
- **Status:** compiled, and re-analysed clean. **The re-run was blocked**, so the fix is
  not yet verified by execution.

`test_menu` had the same mistake in Phase 10.

**Lesson for the viva:** writing and reading a file in different modes is a classic
portability bug. Static review missed it twice; real execution found it immediately.

## G. Complexity

There is nothing new in this phase. VIVA_NOTES §3 summarises the costs, and each phase
document §G has the details.

## H. Viva questions about the documentation itself

1. **How can the examiner check a number in the README?** It links to `TEST_REPORT.md`,
   which names the log file in `docs/evidence/` that contains it.
2. **Why keep failed runs in the evidence folder?** They show the tests can fail, and
   they document a real bug in the tests and its fix. Deleting them would hide evidence.
3. **Why does the demo use `--no-autosave`?** So that a demo can never change or damage
   the real data files.

## I. Team tasks before the viva

| Member | Task |
|---|---|
| All | Read `VIVA_NOTES.md` §1–2 until you can explain the flow without notes. Do the hand traces in §7. |
| M1 | Rehearse Part A of `DEMO_SCRIPT.md` twice, with the speaker hand-overs. |
| M5 | Run the submission checklist (§D), update `TEST_REPORT.md`, and prepare the Part C fallback files. |
| M2, M3, M4 | Each prepares the "likely interruption" answers for their area (DEMO_SCRIPT, last table). |

## J. Where everything is

See the README §8 for the document map and §3 for the source layout.

The project is complete through Phase 11. Optional extensions (road closures, multi-unit
incidents, visualisation) were deliberately left out under the blueprint's "core first"
rule.
