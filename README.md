# DispatchCore — Emergency Resource Allocation & Dispatch System

A **CSE 2253 Data Structures** course project in C11.

> **Academic simulation only.** DispatchCore models a small imaginary city to demonstrate
> data structures and algorithms. Its distances, times, severities and rules are made up.
> It must never be used, or presented, as a tool for real emergency services.

DispatchCore answers one question, over and over:

> *Which pending emergency should be served next, and which available, suitable unit can
> reach it by the shortest road distance?*

---

## 1. What it does

- **Registers incidents** (medical, road accident, fire, structural rescue) with a
  severity from 1 to 5, and **units** (ambulances, fire units, rescue units) at locations
  in a city.
- **Queues pending incidents by priority:** higher severity first, then the earlier
  report.
- **Dispatches the best unit:**
  1. keep only compatible units;
  2. of those, keep the available ones;
  3. of those, keep the ones that can reach the incident by road;
  4. send the one with the shortest road distance (ties go to the lower unit ID).

  If an incident cannot be served yet, it stays queued without blocking the others.
- **Completes or cancels incidents.** A unit is released at the incident's location.
- **Keeps a full allocation history** and produces reports: statistics, history filters,
  completed incidents, a unit-utilisation ranking, and a report file.
- **Saves and loads everything as CSV files**, with strict validation and a safe saving
  procedure.
- **Compares dispatch strategies** (baseline vs optimised) in a reproducible simulation,
  and **times** the key data structures.

## 2. Data structures and algorithms (all written by hand)

| Structure / algorithm | Where | Used for |
|---|---|---|
| Weighted undirected graph, adjacency lists | `graph.c` | the road network |
| Breadth-first search | `graph.c` | counting separate parts of the city |
| Binary **min**-heap | `minheap.c` | Dijkstra's "closest unvisited location" |
| **Dijkstra**: heap version, plus an O(V²) array version used as a test oracle | `dijkstra.c` | shortest distances and routes |
| Binary **max**-heap of incident pointers, with lazy deletion | `incident_heap.c` | the priority queue of pending incidents |
| Hash tables with separate chaining (101 buckets) | `hash_table.c` | finding incidents and units by ID in O(1) on average |
| Singly linked list with a tail pointer | `history.c` | the permanent allocation history |
| Dynamic arrays of pointers | `incident.c`, `resource.c` | the record stores, in registration order |
| Lookup table | `compatibility.c` | which unit types suit which incident types |
| Insertion sort | `report.c` | ranking units by workload |
| Linear congruential generator, discrete-event simulation | `evaluation.c` | reproducible strategy experiments |

No external data-structure libraries are used, only the C standard library.

## 3. Project layout

```
Makefile              build, test, e2e, bench, analyze, run, clean
README.md             this file
src/                  the program (C11)
  common.h/.c           status codes, limits
  graph, minheap, dijkstra                      road network and shortest paths
  incident, incident_heap, resource, compatibility   records and rules
  hash_table, app_state                         indexes; AppState owns all live data
  history, dispatch                             allocations; the dispatch engine
  file_manager                                  CSV loading and saving
  report                                        statistics and reports
  input, menu, main                             safe input, menu, program entry
  evaluation                                    strategy comparison and timing
tests/                one unit-test program per module, plus test_menu (scripted menu)
  test_util.h           tiny CHECK macros (no framework)
  reference_city.*      the 10-location test city
  data/                 CSV fixtures (valid, malformed, CRLF, ...)
  e2e/                  scripts for the real program, plus expected lines
data/                 live data: locations.csv, roads.csv (record files appear on first save)
docs/                 the blueprint, one teaching document per phase, test report, viva
                      notes, demo script, raw evidence logs
```

## 4. Building and running

The toolchain is **MSYS2 UCRT64** (GCC and GNU make). Start the "MSYS2 UCRT64" shell and
change into the project folder. To build:

```bash
make
```

To start the program on the live `data/` folder (it saves after every change):

```bash
make run
```

To try it out without changing any files, use the demo map:

```bash
./build/dispatchcore.exe --data tests/e2e/data --no-autosave
```

Command-line options:

| Option | Meaning |
|---|---|
| `--data DIR` | the folder with the CSV files (default `data`) |
| `--no-autosave` | never write files, unless menu option 22 is chosen |
| `--echo` | print every answer read, for scripted runs (`program < script.in`) |
| `--benchmark` | print the timing measurements and exit |
| `--help` | show the usage line |

Exit status: 0 normal; 1 the data could not be loaded or the final save failed; 2 wrong
arguments.

### The menu

1–5 incidents · 6–10 units · 11–14 dispatch · 15–16 road network · 18–21 reports and
the strategy comparison · 22 save · 23 write a report file · 0 exit.

Option 17 (road closures) is an optional feature that was **not implemented**.

## 5. Data files

| File | Header | Written by the program? |
|---|---|---|
| `locations.csv` | `location_id,name` | no (input only) |
| `roads.csv` | `from_id,to_id,distance,status` | no (input only; status must be `OPEN`) |
| `resources.csv` | `resource_id,type,location_id,status,assigned_incident_id,total_dispatches,total_distance` | yes |
| `incidents.csv` | `incident_id,type,location_id,severity,reported_seq,status,assigned_resource_id,dispatch_attempts` | yes |
| `allocations.csv` | `allocation_id,incident_id,resource_id,from_location,to_location,distance,route,status,dispatch_seq,complete_seq` | yes |

**How bad data is handled:**
- A malformed row is skipped with a `file:line: reason` warning.
- A wrong header, a missing or broken map, or records that contradict each other make
  the program **refuse to start without saving anything**.

**How saving stays safe:** all three record files are first written to `.tmp` files. Only
when all three are complete are the old files renamed to `.bak` and the new ones moved
into place.

## 6. Testing

| Command | What it does |
|---|---|
| `make test` | builds and runs all 16 unit-test programs |
| `make e2e` | runs the real program on `tests/e2e/*.in` and checks the expected lines |
| `make bench` | timing: heap vs array Dijkstra, and hash vs linear lookup |
| `make analyze` | GCC `-fanalyzer` plus strict warnings on all 35 source files, with warnings treated as errors |

**Important: execution evidence on the lead developer's laptop is limited.** Windows Smart
App Control is enabled there. It blocks most compiled programs, unpredictably, with
`Permission denied`. Every program compiles with zero warnings and passes the static
analysis, but only some test programs have been allowed to run.

[`docs/TEST_REPORT.md`](docs/TEST_REPORT.md) is the authority on what actually ran. It
records what passed, what failed, and what has **never** been executed. The raw outputs
are in [`docs/evidence/`](docs/evidence/).

At the time of writing:
- **10 of 16 test programs** have passed every check in real runs, and so have the
  end-to-end tests (37/37);
- **2 programs** (`test_report`, `test_menu`) ran and had failures. Every failure was a
  mistake in the test (it expected `\n` line endings where Windows writes `\r\n`), not in
  the program. Both are fixed, but **not yet re-run**;
- **4 programs** have never been allowed to run (`test_minheap`, `test_dijkstra`,
  `test_incident_heap`, `test_app_state`).

## 7. Results in one paragraph

From real output, in simulation only: in the strategy comparison (menu option 20,
[`docs/PHASE10_EVALUATION.md`](docs/PHASE10_EVALUATION.md)), the optimised strategy
(priority queue + nearest unit) cut the average response to critical incidents under heavy
load from **about 40 to about 5 steps** on the reference city, and from **about 36 to about
6** on a 100-location grid. It also travelled less in total. The priority queue causes most
of the critical-incident gain; the nearest-unit rule mainly saves distance.

In one timing run, heap-based Dijkstra on 961 locations took 9 ms against 67 ms for the
array version, and hash lookups were about 40 times faster than a linear scan with 4000
units.

## 8. Documentation

| Document | Contents |
|---|---|
| [`docs/PHASE0_BLUEPRINT.md`](docs/PHASE0_BLUEPRINT.md) | requirements, architecture, design decisions, plan |
| `docs/PHASE1_GRAPH.md` … `docs/PHASE10_EVALUATION.md` | one lesson per phase: purpose, concepts, code tour, walkthrough, worked example, tests, complexity, viva questions, team tasks, integration |
| [`docs/TEST_REPORT.md`](docs/TEST_REPORT.md) | evidence levels, the Smart App Control limitation, and the execution log |
| [`docs/VIVA_NOTES.md`](docs/VIVA_NOTES.md) | revision notes for the viva: the whole flow plus likely questions |
| [`docs/DEMO_SCRIPT.md`](docs/DEMO_SCRIPT.md) | a timed demo with exact inputs and expected outputs, plus a fallback plan |
| [`docs/PHASE11_DOCS.md`](docs/PHASE11_DOCS.md) | the final phase: submission checklist and known limitations |

## 9. Team

| Member | Role |
|---|---|
| M1 | Lead developer and architect: `common`, `app_state`, `dispatch`, `main`, integration |
| M2 | Priority queues and algorithm analysis |
| M3 | Graph, Dijkstra and route validation |
| M4 | Resources, compatibility, hashing and CSV records |
| M5 | Testing, evaluation and test report |

## 10. Known limitations

- **Not modelled:** traffic, real maps, one-way roads, multi-unit incidents, road
  closures, real time (a simulated clock is used instead).
- **Registration is O(N):** registering a record scans the store for a duplicate ID, even
  though lookups use the hash index.
- **Completion is O(H):** completing an incident searches the history list for the active
  allocation.
- **The three record files are saved together, but not atomically.** If the final
  renaming step fails partway, the files can be left mixed; the `.bak` copies allow
  recovery by hand.
- **Line endings depend on the platform:** saved CSV files use the platform's line endings
  (`\r\n` on Windows). The loader accepts both.
