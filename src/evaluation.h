/*
 * evaluation.h — baseline vs optimised dispatch strategies (Phase 10).
 *
 * A STRATEGY is two independent choices:
 *   queue order : FIFO     — serve incidents in the order they were reported
 *                 PRIORITY — the max-heap order (severity, then report time)
 *   unit rule   : FIRST_FIT — first compatible, available, reachable unit in
 *                             registration order
 *                 NEAREST   — the closest such unit (the real engine)
 *   baseline  = FIFO + FIRST_FIT        optimised = PRIORITY + NEAREST
 * The two mixed strategies are also run, so the report shows WHICH of the
 * two ideas causes each difference (a 2 x 2 "factorial" experiment).
 *
 * A SCENARIO is a city, a fleet and a timeline of incidents. Every run
 * builds its OWN AppState from a copy of the city, so experiments never
 * touch the live data, and every strategy sees exactly the same input.
 *
 * SIMULATED TIME (steps, not the event clock of Phases 5-8):
 *   - an incident appears at its arrival_step;
 *   - a dispatched unit needs travel = ceil(distance / EVAL_SPEED) steps to
 *     reach the scene, then service_steps on scene; it is released at the
 *     scene at step  dispatch + travel + service;
 *   - wait     = dispatch_step - arrival_step
 *     response = wait + travel   (when help ARRIVES — the key measure)
 * Inside one step the order is: completions, then arrivals, then dispatch.
 * The simulation jumps straight to the next step at which something
 * happens (an arrival or a completion); nothing can change in between.
 *
 * This is an academic model: no traffic, constant speed, one unit per
 * incident. Its numbers compare strategies; they say nothing about real
 * emergency services.
 *
 * Depends on: app_state.h, dispatch.h, graph.h, report.h.
 */
#ifndef DC_EVALUATION_H
#define DC_EVALUATION_H

#include <stdint.h>
#include <stdio.h>
#include "app_state.h"
#include "dispatch.h"

#define EVAL_SPEED 5                 /* distance units travelled per step */
#define EVAL_STRATEGY_COUNT 4
#define EVAL_SEVERITY_SLOTS (INCIDENT_SEVERITY_MAX + 1)   /* index = severity */

typedef enum {
    QUEUE_FIFO = 0,
    QUEUE_PRIORITY
} QueueOrder;

typedef struct {
    QueueOrder queue;
    UnitRule   unit;
} EvalStrategy;

typedef struct {
    ResourceType type;
    int location;
} EvalUnitSpec;

typedef struct {
    int arrival_step;      /* >= 0; specs must be in non-decreasing order */
    IncidentType type;
    int location;
    int severity;          /* 1..5 */
    int service_steps;     /* >= 0: time on scene */
} EvalIncidentSpec;

typedef struct {
    const char *name;
    const Graph *city;               /* borrowed; copied for every run */
    EvalUnitSpec *units;
    int num_units;
    EvalIncidentSpec *incidents;
    int num_incidents;
} EvalScenario;

/* Everything measured in one run. Sums are integers (exact in tests);
 * averages are computed only for printing. */
typedef struct {
    EvalStrategy strategy;
    int incidents;
    int served;                      /* dispatched before the end            */
    int unserved;                    /* never dispatched: no compatible unit
                                        can ever reach them                  */
    long long total_distance;
    long long wait_sum;              /* over served incidents                */
    long long response_sum;
    long long weighted_response_sum; /* sum of severity * response           */
    long long severity_sum;          /* sum of severities of served incidents */
    int  response_count_by_sev[EVAL_SEVERITY_SLOTS];
    long long response_sum_by_sev[EVAL_SEVERITY_SLOTS];
    long long response_max_by_sev[EVAL_SEVERITY_SLOTS];   /* -1 if none */
    long long max_response;          /* -1 if nothing served                 */
    long long makespan;              /* step of the last completion, -1 none */
    long long last_step;             /* last simulated step processed        */
    int  valid;                      /* 1 if consistency + cross-check passed */
} EvalResult;

/* ---------- strategies ---------- */

/* i = 0 baseline, 1 FIFO+nearest, 2 priority+first fit, 3 optimised. */
EvalStrategy eval_strategy_at(int i);
const char *eval_strategy_name(EvalStrategy s);

/* ceil(distance / EVAL_SPEED); 0 for distance <= 0. */
int eval_travel_steps(int distance);

/* ---------- cities and scenarios ---------- */

/* An independent copy of g (same locations, names and roads). The order
 * of roads inside the adjacency lists may differ, so among routes of EQUAL
 * length a different one may be reported; distances are identical.
 * NULL on invalid input or out of memory. Free with graph_destroy. */
Graph *eval_clone_graph(const Graph *g);

/* rows x cols grid city: location r*cols + c named "Block r-c", roads to
 * the right and downwards with lengths 1..9 from the seeded generator.
 * Always connected. NULL if rows*cols is outside 1..DC_MAX_VERTICES. */
Graph *eval_make_grid_city(int rows, int cols, uint32_t seed);

/* Own linear congruential generator (same constants as tests/test_util.h):
 * returns 0..bound-1. rand() is not used because its sequence differs
 * between C libraries, which would make experiments irreproducible. */
int eval_random(uint32_t *state, int bound);

/* Fills *sc with a reproducible scenario on `city`: unit i has type
 * i % 3 (so every incident type has some unit) at a random location;
 * incident arrivals advance by 0..max_gap steps; type, location, severity
 * 1..5 and service 1..6 steps are random. Arrays are malloc'd: release
 * them with eval_scenario_free. DC_ERR_INVALID / DC_ERR_NOMEM. */
DcStatus eval_generate(EvalScenario *sc, const char *name, const Graph *city,
                       uint32_t seed, int num_units, int num_incidents, int max_gap);

/* Frees the arrays of a scenario made by eval_generate (not the city). */
void eval_scenario_free(EvalScenario *sc);

/* ---------- running ---------- */

/* Simulates one strategy on one scenario (see the time model above).
 * DC_ERR_INVALID for a malformed scenario (bad location, type, severity,
 * negative times, arrivals out of order), DC_ERR_NOMEM, or DC_ERR_STATE if
 * the engine reported an inconsistency. */
DcStatus eval_run(const EvalScenario *sc, EvalStrategy strategy, EvalResult *res);

void eval_print_header(FILE *out);
void eval_print_row(FILE *out, const EvalResult *res);

/* Runs all four strategies on sc and prints a table. results (optional,
 * may be NULL) receives EVAL_STRATEGY_COUNT results in eval_strategy_at order. */
DcStatus eval_compare(const EvalScenario *sc, FILE *out, EvalResult *results);

/* The standard experiment set used by menu option 20: two generated
 * scenarios on `city` (light and heavy load) and one on a 10 x 10 grid.
 * Fixed seeds, so every run prints the same numbers on every machine. */
DcStatus eval_run_suite(const Graph *city, FILE *out);

/* ---------- timing (machine-dependent; never used by the tests) ---------- */

/* Times Dijkstra (min-heap vs O(V^2) array) on grid cities of growing
 * size, and ID lookup (hash index vs linear store scan). Prints
 * milliseconds measured with clock(). DC_ERR_NOMEM on failure. */
DcStatus eval_benchmark(FILE *out);

#endif /* DC_EVALUATION_H */
