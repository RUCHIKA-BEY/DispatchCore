# Phase 1 — Road Network Graph

> **Verification status (2026-11-19): COMPILED, ANALYSED, EXECUTED ONCE.**
> Compiled with zero warnings (gcc 16.2.0, MSYS2 UCRT64) and passed `make analyze`
> (`-fanalyzer -Werror`). Windows Smart App Control usually blocks our executables, but it
> allowed one run, which printed **`95 checks, 0 failed -> ALL PASSED`**. Only the last lines
> of that run were captured, so the printed road-network listing in section E has **not** yet
> been compared with real output. Full details: `docs/TEST_REPORT.md`.

## Files in this phase

| File | Status | Contents | Depends on |
|---|---|---|---|
| `src/common.h` | new | `DcStatus` error codes, size limits, `DC_INF` | — |
| `src/common.c` | new | `dc_status_str()` | `common.h` |
| `src/graph.h` | new | `AdjNode`, `Location`, `Graph`, graph function prototypes | `common.h`, `<stdio.h>` |
| `src/graph.c` | new | graph implementation, including BFS component count | `graph.h` |
| `tests/test_util.h` | new | `CHECK`, `CHECK_EQ_INT`, `CHECK_STR_EQ`, `TEST_SUMMARY` macros | `<stdio.h>`, `<string.h>` |
| `tests/reference_city.h/.c` | new | `build_reference_city()`, a test fixture reused in Phase 2 | `graph.h` |
| `tests/test_graph.c` | new | 10 test functions, 95 checks | all of the above |
| `data/locations.csv`, `data/roads.csv` | new | the reference city as CSV. **Not read by any code yet**; the loader arrives in Phase 7 | — |
| `Makefile` | new | `make`, `make test`, `make clean` | GNU make + POSIX shell |
| `.gitignore` | new | ignores `build/`, `*.exe`, `*.o`, save temp files | — |

**Two changes from the blueprint:**

1. I added **`src/common.c`**, because `dc_status_str()` needs a `.c` file.
2. I left the `is_closed` field out of `AdjNode` for now. Road closure is an *optional*
   feature, and an unused field is something a viva examiner will ask about. We will add
   it if and when we build that feature.

---

## A. Purpose

The graph is the **map** of DispatchCore. Every incident and every resource sits at a
location, and the dispatch engine needs to know how far apart two locations are by road.
This phase builds only the map: storing locations and roads, validating them, printing them
and counting connected regions. Phase 2 adds shortest paths (Dijkstra) on top of it.

---

## B. Concepts you need first

### B1. Structures and `typedef`

A `struct` groups related variables into one record:

```c
typedef struct {
    int  id;
    char name[32];
} Location;
```

`typedef` lets us write `Location` instead of `struct Location`. A struct that points to
**its own type** (a linked-list node) needs a tag name, because the `typedef` name does not
exist yet inside its own definition:

```c
typedef struct AdjNode {
    int dest;
    int weight;
    struct AdjNode *next;   /* must say "struct AdjNode" here */
} AdjNode;
```

### B2. Pointers

A pointer stores a memory **address**.

- `AdjNode *p` means "p holds the address of an AdjNode".
- `p->dest` is shorthand for `(*p).dest`.
- `NULL` means "points to nothing". We use it to mark the end of a list and an empty list.

**`AdjNode **adj`** is a *pointer to a pointer*. Read it in two steps:

- `adj` is an array (allocated with `calloc`)…
- …whose elements are `AdjNode *`, i.e. list heads.

So `g->adj[3]` is the head of vertex 3's road list, and `g->adj[3]->dest` is the first
neighbour of vertex 3.

### B3. Dynamic memory

| Call | What it does |
|---|---|
| `malloc(n)` | reserves n bytes; the contents are garbage; returns `NULL` on failure |
| `calloc(count, size)` | reserves count×size bytes, **all set to zero**; returns `NULL` on failure |
| `free(p)` | gives the memory back. `free(NULL)` is legal and does nothing |

Three rules we follow everywhere:

1. **Check every allocation** for `NULL`.
2. **Every `malloc` has exactly one matching `free`.**
3. **Never use a pointer after freeing it.** That is why `graph_destroy` saves `node->next`
   *before* calling `free(node)`.

We write `malloc(sizeof *g)` rather than `malloc(sizeof(Graph))`. It always allocates the
right size for whatever type `g` points to, even if that type is renamed later.

### B4. Singly linked lists

A chain of nodes, each pointing to the next:

```
head -> [3 | w7] -> [1 | w4] -> NULL
```

**Insert at head** takes O(1) and needs exactly two pointer assignments:

```c
new_node->next = head;   /* 1. new node points to the old first node */
head = new_node;         /* 2. head now points to the new node       */
```

The order matters. If you swap the two lines, the old list is lost (a memory leak, and
the roads disappear).

### B5. Graphs

- **Vertex** = a location. **Edge** = a road. **Weight** = the road's length.
- **Undirected**: a road can be driven both ways, so u–v and v–u are the same road.
- **Degree** of a vertex = the number of roads touching it.
- **Connected component** = a maximal group of vertices that can all reach each other.
- **Handshake lemma**: the sum of all degrees = 2 × number of edges, because each road
  has two ends. We use this as a test check.

**Adjacency matrix vs adjacency list:**

| | Matrix `int m[V][V]` | List (our choice) |
|---|---|---|
| Memory | O(V²) | O(V + E) |
| "Is there a road u–v?" | O(1) | O(deg(u)) |
| "Visit all neighbours of u" | O(V) | O(deg(u)) |
| Good for | dense graphs | **sparse graphs, such as road networks** |

A city of 1000 junctions with about 3 roads each needs 1,000,000 matrix cells, but only
about 6,000 list nodes. Dijkstra visits neighbours constantly, so lists win.

### B6. Breadth-first search (BFS)

BFS explores a graph level by level using a **queue** (first in, first out):

1. Put the start vertex in the queue and mark it visited.
2. Repeatedly take the front vertex and enqueue every unvisited neighbour, marking it.
3. When the queue is empty, every vertex reachable from the start has been visited.

To count components, start a BFS from every vertex that is still unvisited. Each new BFS
means a new component.

---

## C. Code

The complete files are in the repository; this section explains where things are:

- `src/common.h`, `src/common.c`: status codes and limits.
- `src/graph.h`: the interface. Read this first; every function has a contract comment.
- `src/graph.c`: the implementation.
- `tests/test_util.h`, `tests/reference_city.[ch]`, `tests/test_graph.c`: the tests.
- `Makefile`.

---

## D. Code walkthrough

### D1. `common.h`

- **`DcStatus`**: every function that can fail *returns* a status instead of printing.
  Why: the graph module must not decide how errors are shown. The menu (Phase 9) prints
  them, the tests compare them, and the CSV loader (Phase 7) counts them.
- **`DC_MAX_WEIGHT 10000` and `DC_MAX_VERTICES 1000`**: these bound the longest possible
  route at 999 × 10000 < 10⁷. That is far below `INT_MAX` (≈2.1×10⁹), so a sum of road
  lengths can never overflow an `int`.
- **`DC_INF (-1)`**: not used yet. Phase 2 uses it for "unreachable".
- `common.h` includes **no** other project header, so it can never be part of a
  circular include.

### D2. `graph.h`: the three structs

```
Graph
 ├─ num_vertices = 10
 ├─ num_edges    = 11
 ├─ locations ──► [ {0,"Central Hospital"}, {1,"City Square"}, ... ]   (array)
 └─ adj ────────► [ head0, head1, ..., head9 ]                           (array of pointers)
                      │
                      ▼
                    AdjNode{dest 3, w 7} ──► AdjNode{dest 1, w 4} ──► NULL
```

- `Location.id` always equals its array index. This is design decision **D2**: finding a
  location by ID is just `locations[id]`, O(1), with no search and no hash table.
- `num_edges` counts **undirected** roads. The reference city has 11 roads but 22 list nodes.

### D3. `graph_create`

1. Reject sizes outside 1..1000, because a graph with 0 locations is meaningless.
2. `malloc` the `Graph` struct itself.
3. `calloc` two arrays. `calloc` is used for `adj` specifically because it **zero-fills**,
   so every list head starts as `NULL` (no roads) without a loop.
4. If either array fails, free everything allocated so far and return `NULL`. Freeing a
   `NULL` pointer is harmless, so one cleanup block handles every failure combination.
5. Give each location a default name with `snprintf`, which never writes more than
   `DC_NAME_MAX` bytes. That makes it immune to buffer overflow, unlike `sprintf`.

### D4. `graph_destroy`

Two levels of cleanup, **inner first**:

1. For each vertex, walk its list and free every node. `next` is copied **before**
   `free(node)`, because after `free`, reading `node->next` is undefined behaviour.
2. Free the two arrays, then the `Graph` itself.

Freeing the arrays first would lose the only pointers to the list nodes, which is a leak.
`graph_destroy(NULL)` returns immediately, so cleanup code can call it without checking.

### D5. `graph_set_location_name` and `is_valid_location_name`

`is_valid_location_name` is `static`, so it is visible only inside `graph.c`. It is a
private helper and not part of the module's interface.

A name is rejected if it is `NULL`, empty, 32 or more characters long (no room for `'\0'`),
or contains `,` `\r` or `\n`. Commas and newlines would break the CSV files in Phase 7.
We validate **now**, at the point of entry, so bad data can never get into the system.
`strpbrk(name, ",\r\n")` returns the first position of any of those characters, or `NULL`
if there is none.

A rejected rename changes nothing, because validation happens before the copy.

### D6. `graph_add_road`: the most important function

```c
if (!valid(u) || !valid(v))        return DC_ERR_INVALID;
if (u == v)                        return DC_ERR_INVALID;   /* self-loop */
if (weight < 1 || weight > MAX)    return DC_ERR_INVALID;
if (graph_road_weight(g,u,v) != -1) return DC_ERR_DUPLICATE;
```

**All checks happen before any change.** That gives the function a useful guarantee:
*it either fully succeeds or leaves the graph exactly as it was.*

Then:

```c
AdjNode *to_v = malloc(sizeof *to_v);
AdjNode *to_u = malloc(sizeof *to_u);
if (to_v == NULL || to_u == NULL) { free(to_v); free(to_u); return DC_ERR_NOMEM; }
```

Both nodes are allocated **before** either is linked in. If we linked `to_v` first and then
the second `malloc` failed, the graph would contain a one-way road, silently breaking the
"undirected" promise. This is the same all-or-nothing idea used in databases.

Finally, each node is inserted at the head of its list (B4), and `num_edges++`.

**Why reject the duplicate instead of updating its weight?** A silent update would hide data
errors such as the same road appearing twice in `roads.csv` with different lengths. Rejecting
makes the problem visible. **Cost:** the duplicate check walks u's list, so adding a road is
O(deg(u)) rather than O(1). For road networks the degree is tiny (2–4), so this is a good
trade.

**Why weight ≥ 1?** Dijkstra's algorithm (Phase 2) is only correct for non-negative weights,
and a zero-length road between two distinct places makes no sense in our simulation.

### D7. `graph_road_weight` and `graph_degree`

Both walk one adjacency list. `graph_road_weight` returns `-1` for "no road". That is safe
because real weights are always ≥ 1. Phase 2 tests use it to check that every step of a
route is a real road.

### D8. `graph_count_components`: BFS

- `visited` is a `char` array (1 byte per vertex is enough for 0/1), zeroed by `calloc`.
- `queue` is a plain array of size V used as a queue with two indices: `head` (next to
  remove) and `tail` (next free slot). Each vertex is enqueued **at most once** because it
  is marked visited *when enqueued*. So V slots are always enough, and we need no circular
  buffer and no linked-list queue.
- The outer `for` loop starts a new BFS at each vertex that is still unvisited. Each start
  is a new component.

**Why mark when enqueuing, not when dequeuing?** If we marked on dequeue, a vertex with
several visited neighbours could be enqueued several times and overflow the array.

### D9. `graph_print`

`%-20s` left-aligns the name in a 20-character column, and `%2d` right-aligns the index, so
the output lines up. It writes to a `FILE *`, so the same function can print to `stdout`
now and to a report file later.

### D10. `test_util.h`

- `CHECK(cond)` counts a check and prints the file, line and condition text (`#cond`
  turns the expression into a string) if it fails.
- `CHECK_EQ_INT` also prints the actual and expected values, which saves debugging time.
- The `do { ... } while (0)` wrapper makes each macro act like a single statement, so it
  is safe inside an `if` without braces.
- The two counters are `static` variables in a header. That is acceptable **only** because
  each test program is a separate executable that includes the header once. Production
  code (`src/`) has no such variables.

### D11. `Makefile`

```make
$(TEST_GRAPH): tests/test_graph.c ... $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_graph.c $(FIXTURES) $(GRAPH_SRC) $(CORE_SRC)
```

- The left side is the **target**. After the colon come the **prerequisites**: if any of
  them changes, make rebuilds the target.
- `| $(BUILD)` is an *order-only* prerequisite: "the build folder must exist", without
  forcing a rebuild every time the folder's timestamp changes.
- `$@` means "the target name".
- `-Isrc -Itests` tells gcc where to look for `#include "..."` headers.
- `-std=c11 -Wall -Wextra -Wpedantic`: strict standard C with most warnings on. **Treat
  every warning as a bug to fix.**
- `-g` adds debug information for `gdb`.
- For now we compile all sources directly into each test executable. Once the main program
  exists (Phase 9), we will switch to separate `.o` object files so that only changed files
  are recompiled.

---

## E. Example execution (expected output, worked out by hand)

`build_reference_city()` adds the roads in this order:
0–1, 0–3, 1–2, 1–5, 2–4, 3–5, 3–6, 4–7, 5–7, 6–7, 8–9.

**Trace for vertex 0** (head insertion):

| Step | Road added | `adj[0]` afterwards |
|---|---|---|
| start | — | `NULL` |
| 1 | 0–1 (4) | `1(4) -> NULL` |
| 2 | 0–3 (7) | `3(7) -> 1(4) -> NULL` |

The newest road always appears first. Doing the same for every vertex, `graph_print` should
produce:

```
Road network: 10 locations, 11 roads
  [ 0] Central Hospital    : -> 3 Riverside (7) -> 1 City Square (4)
  [ 1] City Square         : -> 5 Old Town (6) -> 2 Fire Station North (3) -> 0 Central Hospital (4)
  [ 2] Fire Station North  : -> 4 Industrial Zone (5) -> 1 City Square (3)
  [ 3] Riverside           : -> 6 Airport Road (9) -> 5 Old Town (2) -> 0 Central Hospital (7)
  [ 4] Industrial Zone     : -> 7 University (4) -> 2 Fire Station North (5)
  [ 5] Old Town            : -> 7 University (8) -> 3 Riverside (2) -> 1 City Square (6)
  [ 6] Airport Road        : -> 7 University (3) -> 3 Riverside (9)
  [ 7] University          : -> 6 Airport Road (3) -> 5 Old Town (8) -> 4 Industrial Zone (4)
  [ 8] Hill Village        : -> 9 Lake Camp (5)
  [ 9] Lake Camp           : -> 8 Hill Village (5)
```

**BFS component count on the reference city:**

- Start at 0: the queue visits 0, 3, 1, 6, 5, 2, 7, 4. That is 8 vertices, **component 1**.
- 1–7 are already visited and skipped.
- Start at 8: visits 8, 9. **Component 2**.
- 9 is already visited. Result: **2**.

Full predicted run of `make test`:

```
[test] graph_create rejects invalid sizes
[test] graph_create builds an empty graph
[test] vertex validation
[test] location names
[test] graph_add_road stores both directions
[test] graph_add_road rejects invalid roads
[test] reference city structure

Road network: 10 locations, 11 roads
  ... (the 10 lines above) ...

[test] connected components
[test] largest allowed graph (chain of DC_MAX_VERTICES)
[test] graph_destroy(NULL) and graph_print(NULL) are safe
(no road network loaded)

95 checks, 0 failed -> ALL PASSED
```

---

## F. Testing

### F1. Install a compiler (needed once per machine)

The team toolchain is **MSYS2 UCRT64** (decided 2026-11-19). In PowerShell:

```bash
winget install -e --id MSYS2.MSYS2
```

Then open the **"MSYS2 UCRT64"** terminal from the Start menu and run:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc make
```

Check that both work:

```bash
gcc --version
```
```bash
make --version
```

### F2. Build and run

From the project folder, in the MSYS2 UCRT64 shell:

```bash
make test
```

Without make, the equivalent single command is:

```bash
mkdir -p build && gcc -std=c11 -Wall -Wextra -Wpedantic -g -Isrc -Itests -o build/test_graph tests/test_graph.c tests/reference_city.c src/graph.c src/common.c && ./build/test_graph
```

**Pass criteria:** no compiler warnings, the last line reads `ALL PASSED`, and the exit
status is 0 (`echo $?` prints `0`).

### F3. Memory check

Valgrind is not available in our MSYS2-only setup. Instead, we use GCC's static analyzer,
which looks for leaks, double frees and NULL dereferences without running the program:

```bash
make analyze
```

A clean result is a silent run with exit status 0. This is weaker than a runtime memory
checker; see `docs/TEST_REPORT.md`.

### F4. What each test validates

| Test function | Validates | Kind |
|---|---|---|
| `test_create_rejects_bad_sizes` | 0, negative and 1001 vertices are refused | failure |
| `test_create_empty_graph` | counts start at 0, default names, V components | normal |
| `test_vertex_validation` | index bounds, `NULL` graph | edge |
| `test_location_names` | 31 chars accepted, 32 rejected; empty, `NULL`, comma, newline; a failed rename keeps the old name | edge/failure |
| `test_add_road_both_directions` | both directions stored with the same weight; head-insertion order | normal |
| `test_add_road_rejects_bad_input` | self-loop, bad vertex, weights 0 / −3 / 10001, duplicates in both orders; **failures change nothing**; boundary weights 1 and 10000 accepted | failure/edge |
| `test_reference_city` | every degree, handshake lemma, specific weights, no road across islands, 2 components, printed output | integration |
| `test_components` | component count as roads join groups | normal |
| `test_large_chain` | the maximum size (1000 vertices, 999 roads) | stress |
| `test_destroy_null_is_safe` | `NULL` handling in cleanup and print | edge |

**Not covered (stated honestly):** we cannot easily force `malloc` to fail, so the
`DC_ERR_NOMEM` paths are reviewed by reading the code, not by tests. The printed output is
checked by eye, not compared automatically.

---

## G. Complexity

V = vertices, E = undirected roads, deg(u) = roads at u.

| Function | Time | Extra space | Why |
|---|---|---|---|
| `graph_create` | O(V) | O(V) | two arrays plus one loop naming each location |
| `graph_destroy` | O(V + E) | O(1) | visits every head and every one of the 2E nodes |
| `graph_set_location_name` | O(L), L ≤ 31 | O(1) | string scan and copy |
| `graph_add_road` | O(deg(u)) | O(1) | duplicate scan; the insertion itself is O(1) |
| `graph_road_weight` | O(deg(u)) | O(1) | one list walk |
| `graph_degree` | O(deg(v)) | O(1) | one list walk |
| `graph_count_components` | O(V + E) | O(V) | each vertex is enqueued once and each list node read once |
| `graph_print` | O(V + E) | O(1) | |
| **Whole graph** | — | **O(V + E)** | V locations + V heads + 2E nodes |

Building a whole graph of E roads costs O(Σ deg) = O(E · max-degree). For road networks the
maximum degree is a small constant, so this is effectively O(E).

---

## H. Viva preparation

**Basic**

1. *Why an adjacency list and not a matrix?* Road networks are sparse. Lists use O(V+E)
   memory instead of O(V²), and they visit only a vertex's real neighbours, which is what
   Dijkstra and BFS need.
2. *How is an undirected road stored?* As two list nodes, one in each endpoint's list, with
   the same weight. `num_edges` counts it once.
3. *What is `AdjNode **adj`?* A dynamically allocated array of `AdjNode *`. Each element is
   the head of one vertex's linked list.
4. *Why `calloc` for `adj`?* It zero-fills, so every head is `NULL`, meaning no roads,
   without a loop.

**Tricky**

5. *Why insert at the head and not at the tail?* Head insertion is O(1) with no traversal.
   Tail insertion is O(deg) unless we store a tail pointer per vertex. The only side effect
   is reverse display order, which is harmless and documented.
6. *But you scan the list for duplicates anyway, so why not append at the end?* That would
   work too. We kept head insertion because it is the standard, simplest form, and the
   duplicate check could be removed later without touching insertion.
7. *What happens if the second `malloc` in `graph_add_road` fails?* The first node is freed
   and `DC_ERR_NOMEM` is returned. The graph is unchanged, so it never holds a one-way road.
8. *Why must `next` be saved before `free(node)`?* After `free`, the node's memory no longer
   belongs to us. Reading `node->next` is undefined behaviour: it may crash or return garbage.
9. *Why mark BFS vertices when enqueued?* To guarantee each vertex enters the queue once.
   Otherwise the size-V queue could overflow.
10. *Why reject weight 0?* A zero-length road between two different places is meaningless,
    and keeping weights ≥ 1 keeps the "positive weights" assumption simple for Dijkstra.
    Negative weights would break Dijkstra outright.
11. *Can two locations have the same name?* Yes. Identity is the numeric ID; names are only
    labels. A rule against duplicate names could be added if the team wants it.

**Complexity**

12. *Space of the graph?* O(V + E): V `Location`s, V head pointers, 2E nodes.
13. *Time to add a road?* O(deg(u)) because of the duplicate check; without it, O(1).
14. *BFS complexity?* O(V + E): every vertex is enqueued once and every node is read once.

**Pointers and memory**

15. *What is a memory leak? Where could one occur here?* Memory that is allocated but never
    freed. It would happen if `graph_destroy` freed `adj` before the nodes, or if
    `graph_create` returned `NULL` without freeing the arrays it had already allocated.
16. *Why `sizeof *g` instead of `sizeof(Graph)`?* It always matches the pointer's type, even
    if the type changes.
17. *Difference between `g->adj[i]` and `*g->adj[i]`?* The first is a pointer (the list
    head). The second is the `AdjNode` struct it points to, which is invalid if the head is
    `NULL`.

**Integration**

18. *How will Dijkstra use this module?* It reads `g->adj[u]` to visit neighbours and
    `node->weight` for road lengths. It never modifies the graph, which is why it will take
    a `const Graph *`.
19. *Why does the location ID equal the array index?* O(1) lookup with no hash table needed.
    The trade-off is that IDs must be 0..V−1 with no gaps, which the Phase 7 loader will
    enforce.

---

## I. Team explanation

| Member | Must understand | Independently validate |
|---|---|---|
| **M1 (lead)** | All of it, especially the all-or-nothing `graph_add_road` and the ownership in `graph_destroy` | Approve the interface in `graph.h` before Phase 2 depends on it |
| **M2 (heaps)** | Arrays vs linked structures; Big-O of each function | Re-derive the complexity table (section G) without looking, then compare |
| **M3 (graph)** — primary validator | Everything in sections B5, B6 and D6–D8 | (1) Draw the 10 adjacency lists by hand from the road order and compare them with the printed output. (2) Add a test of your own: a 5-vertex star graph (centre degree 4, components 1). (3) Run `make analyze` and confirm it is clean. (4) Review the PR line by line |
| **M4 (records)** | Name validation rules and why commas are banned (CSV) | Check that `data/locations.csv` and `data/roads.csv` match `reference_city.c` exactly |
| **M5 (testing)** | `test_util.h`, the Makefile, exit codes | Run `make test` on a second machine; save the real output with date and `gcc --version` in `docs/TEST_REPORT.md` |

---

## J. Integration

- **Connects to:** nothing yet. This is the bottom layer.
- **Used next by:** `dijkstra.c` (Phase 2), which receives a `const Graph *`;
  `file_manager.c` (Phase 7), which calls `graph_create`, `graph_set_location_name` and
  `graph_add_road` while reading the CSVs; `main.c` (Phase 9), which calls `graph_print`.
- **Dependencies introduced:** `graph.h` → `common.h`, `<stdio.h>`. Nothing depends on
  `graph.c` internals.
- **Existing files modified:** none. Every file in this phase is new.

### Suggested Git steps (for you to run; I have not run any)

```bash
git init
```
```bash
git add .gitignore Makefile src tests data docs
```
```bash
git commit -m "Phase 0 blueprint and Phase 1 adjacency-list graph with tests"
```

If the repository already exists on GitHub, do Phase 1 on its own branch instead:

```bash
git switch -c feature/phase1-graph
```

Then open a Pull Request with Member 3 as the reviewer.
