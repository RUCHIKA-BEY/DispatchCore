# DispatchCore — Demo Script (about 12 minutes)

Every input below comes from `tests/e2e/day1.in`, and every "you should see" line
was produced by the **real program** in the end-to-end run on 2026-11-19 at 21:41 (37/37
checks passed). The full transcript is in
`docs/evidence/2026-11-19_2141_e2e_day1_transcript.txt`.

**Safety rule for the demo:** use the demo map with `--no-autosave`, so no file is written.
Only Part B writes, and only into a scratch copy under `build/`.

---

## 0. Before the session (do this 15 minutes early)

1. Open the **MSYS2 UCRT64** shell and change into the project folder.
2. Build everything:

   ```bash
   make
   ```

3. **Smart App Control check.** Run this:

   ```bash
   ./build/dispatchcore.exe --help
   ```

   - If it prints the usage line, use the live demo (Part A).
   - If it prints `Permission denied`, use the **fallback** (Part C). Do not try to work
     around the block.
4. Have these open in an editor: `docs/PHASE6_DISPATCH.md` §E,
   `docs/PHASE10_EVALUATION.md` §F, and `docs/TEST_REPORT.md`.
5. Agree who speaks when:
   - **M1:** intro and the dispatch flow;
   - **M3:** routes;
   - **M2:** the queue;
   - **M4:** hashing and files;
   - **M5:** testing and results.

## Part A — live demo

Start the program:

```bash
./build/dispatchcore.exe --data tests/e2e/data --no-autosave
```

You should see: `Ready: 10 locations, 0 units, 0 incidents, 0 allocations. Autosave is OFF.`

| # | Speaker | Type | You should see | Point to make |
|---|---|---|---|---|
| 1 | M1 | — | the menu | "One flat menu; every input is validated." |
| 2 | M4 | `6` `1` `0` | `Registered unit 1: AMBULANCE at 0 Central Hospital. Status AVAILABLE.` | The store owns the record; the hash index borrows it. |
| 3 | M4 | `6` `2` `2` | `Registered unit 2: FIRE_UNIT at 2 Fire Station North. ...` | |
| 4 | M2 | `1` `1` `5` `4` | `Registered incident 1001: MEDICAL, severity 4 (Severe), at 5 Old Town. Status PENDING.` | It goes into the store, the index **and** the heap: all or nothing. |
| 5 | M2 | `1` `3` `4` `5` | `Registered incident 1002: FIRE, severity 5 (Critical), ...` | It was reported later, but it is more severe. |
| 6 | M2 | `2` | 1002 is listed **above** 1001 | The max-heap orders by severity, then report order. This view works on a **copy** of the heap. |
| 7 | M1 | `11` | `Dispatched unit 2 (FIRE_UNIT) to incident 1002 ...` then `distance 5, route: 2 Fire Station North > 4 Industrial Zone` | The filters run compatible → available → reachable → nearest, with one Dijkstra from the incident. |
| 8 | M3 | `11` | `Dispatched unit 1 (AMBULANCE) to incident 1001 ...` then `distance 9, route: 0 Central Hospital > 3 Riverside > 5 Old Town` | 0-3-5 = 9 beats 0-1-5 = 10. The path is reversed so the route runs unit → incident. |
| 9 | M1 | `11` | `No pending incidents.` | |
| 10 | M1 | `13` | both active dispatches, with routes | |
| 11 | M1 | `14` `1002` | `Incident 1002 completed. Unit 2 is available again at 4 Industrial Zone.` | The unit is released **at the scene**, not back at base. |
| 12 | M1 | `5` `1001` | `Incident 1001 is DISPATCHED; only PENDING incidents can be cancelled.` | The state machine refuses illegal changes. |
| 13 | M4 | `10` `2` `y` | `Unit 2 is now OUT_OF_SERVICE.` | It asks y/n first; a DISPATCHED unit would be refused. |
| 14 | M3 | `15` | the network listing, then `The city has 2 separate groups of locations; no road joins them.` | BFS counts the connected parts; 8–9 is an island. |
| 15 | M3 | `16` `0` `9` | `No road connects 0 Central Hospital and 9 Lake Camp.` | DC_INF means unreachable. |
| 16 | M3 | `16` `0` `7` | `Shortest distance 16. Route: 0 Central Hospital > 1 City Square > 2 Fire Station North > 4 Industrial Zone > 7 University` | The alternatives are 17, 18 and 19. |
| 17 | M4 | `21` | hash buckets, then the raw heap array | "The heap array is **not** sorted, only heap-ordered." Point out the chains. |
| 18 | M5 | `19` | `Allocations: 2 (active 1, completed 1)` and `Cross-check: all counters agree` | Independent counters agree, which is a built-in self-test. |
| 19 | M5 | `20` | three comparison tables | Point at the CRIT_RESP column under heavy load: about 40 → about 5. "Simulation only." |
| 20 | M5 | `abc`, then `99` | `"abc" is not a whole number.` then `Please type a number from 0 to 23.` | `fgets` + `strtol`: no infinite loop on bad input. |
| 21 | M1 | `0` | `Autosave is off: unsaved changes are discarded.` then `Goodbye.` | |

Steps 17 and 19 print a lot of output. Scroll only to the lines named above.

## Part B — persistence (optional, 2 minutes)

This uses a scratch copy, so `data/` is never touched. First make the copy:

```bash
mkdir -p build/demo && cp tests/e2e/data/*.csv build/demo/
```

Then start the program on the copy:

```bash
./build/dispatchcore.exe --data build/demo
```

1. Register a unit: type `6`, `3`, `4`. You should see `(saved to build/demo)`, which is
   autosave at work.
2. Exit with `0`. Start the same command again.
3. The start-up report shows `resources 1 loaded`. Option `8` lists the unit.
4. Register another unit. It gets ID **2**: IDs continue after a reload instead of
   restarting.

Point to make: saving goes to `.tmp` files first, and the old files are kept as `.bak`
(run `ls build/demo` to show them).

## Part C — fallback if the program is blocked

Say: *"Windows Smart App Control on this laptop blocks unsigned programs at random. We
kept it enabled on purpose and recorded every run honestly."* Then show:

1. `docs/evidence/2026-11-19_2141_e2e_day1_transcript.txt`: the same demo as Part A,
   produced by the real program. Walk through the same 21 steps.
2. `docs/evidence/2026-11-19_2141_phase11_e2e_run.log`: `37 e2e checks passed, 0 failed`.
3. `docs/evidence/2026-11-19_2132_phase10_make_test.log`: the `test_evaluation`
   strategy tables, with 180/180 checks passed.
4. `docs/evidence/2026-11-19_2141_phase11_bench_run.log`: the timing table.
5. `docs/TEST_REPORT.md` §3: the matrix of what ran, what failed and what never ran.

## Part D — closing (1 minute, M5)

- **Results** (simulation only): the priority queue plus the nearest unit cut the critical
  response under heavy load from about 40 to about 5 steps. The priority queue causes most
  of that gain; the nearest-unit rule saves distance.
- **Honesty:** 4 test programs have never been allowed to run on this laptop. Two tests
  failed when they first ran, because of their own `\r\n` mistake; both are fixed but not
  yet re-run. Everything else listed in the report really passed.
- **Limits:** an academic model; no real maps, traffic or time.

## Likely interruptions, and who answers

| Question | Who | Where to look |
|---|---|---|
| "Why one Dijkstra and not one per unit?" | M1 / M3 | VIVA_NOTES §2 |
| "Show me heapify." | M2 | PHASE3 doc, the 7-incident trace |
| "What if two units are equally close?" | M1 | the lower ID wins (PHASE6 doc) |
| "What if the CSV file is broken?" | M4 | PHASE7 doc. You can show it live: `./build/dispatchcore.exe --data tests/data/bad_header --no-autosave` refuses to start. |
| "How do you know it is correct?" | M5 | TEST_REPORT, the hand traces, the e2e transcript |
