/*
 * evaluation.c — strategy comparison and timing (see evaluation.h).
 */
#include "evaluation.h"

#include "dijkstra.h"
#include "report.h"

#include <limits.h>
#include <stdlib.h>
#include <time.h>

/* Upper limits that keep every time value far from overflow. */
#define EVAL_MAX_TIME 1000000      /* arrival_step and service_steps */

/* ---------- strategies ---------- */

EvalStrategy eval_strategy_at(int i)
{
    EvalStrategy s;
    s.queue = (i >= 2) ? QUEUE_PRIORITY : QUEUE_FIFO;
    s.unit = (i % 2 == 1) ? UNIT_NEAREST : UNIT_FIRST_FIT;
    return s;
}

const char *eval_strategy_name(EvalStrategy s)
{
    if (s.queue == QUEUE_FIFO)
        return s.unit == UNIT_FIRST_FIT ? "baseline (FIFO + first fit)" : "FIFO + nearest";
    return s.unit == UNIT_FIRST_FIT ? "priority + first fit" : "optimised (priority + nearest)";
}

int eval_travel_steps(int distance)
{
    if (distance <= 0)
        return 0;
    return (distance + EVAL_SPEED - 1) / EVAL_SPEED;   /* integer ceiling */
}

int eval_random(uint32_t *state, int bound)
{
    if (state == NULL || bound <= 0)
        return 0;
    *state = *state * 1103515245u + 12345u;            /* wraps mod 2^32: defined */
    return (int)((*state >> 16) % (uint32_t)bound);
}

/* ---------- cities ---------- */

Graph *eval_clone_graph(const Graph *src)
{
    if (src == NULL)
        return NULL;
    Graph *g = graph_create(src->num_vertices);
    if (g == NULL)
        return NULL;
    for (int v = 0; v < src->num_vertices; v++) {
        if (graph_set_location_name(g, v, src->locations[v].name) != DC_OK) {
            graph_destroy(g);
            return NULL;
        }
    }
    /* Each undirected road appears in both lists; copy it once (u < dest). */
    for (int u = 0; u < src->num_vertices; u++) {
        for (const AdjNode *n = src->adj[u]; n != NULL; n = n->next) {
            if (n->dest > u && graph_add_road(g, u, n->dest, n->weight) != DC_OK) {
                graph_destroy(g);
                return NULL;
            }
        }
    }
    return g;
}

Graph *eval_make_grid_city(int rows, int cols, uint32_t seed)
{
    if (rows < 1 || cols < 1 || rows > DC_MAX_VERTICES / cols)
        return NULL;                         /* also avoids rows*cols overflow */
    Graph *g = graph_create(rows * cols);
    if (g == NULL)
        return NULL;
    char name[DC_NAME_MAX];
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int v = r * cols + c;
            snprintf(name, sizeof name, "Block %d-%d", r, c);
            int ok = graph_set_location_name(g, v, name) == DC_OK;
            if (ok && c + 1 < cols)
                ok = graph_add_road(g, v, v + 1, 1 + eval_random(&seed, 9)) == DC_OK;
            if (ok && r + 1 < rows)
                ok = graph_add_road(g, v, v + cols, 1 + eval_random(&seed, 9)) == DC_OK;
            if (!ok) {
                graph_destroy(g);
                return NULL;
            }
        }
    }
    return g;
}

/* ---------- scenarios ---------- */

DcStatus eval_generate(EvalScenario *sc, const char *name, const Graph *city,
                       uint32_t seed, int num_units, int num_incidents, int max_gap)
{
    if (sc == NULL)
        return DC_ERR_INVALID;
    sc->name = name;
    sc->city = city;
    sc->units = NULL;
    sc->incidents = NULL;
    sc->num_units = 0;
    sc->num_incidents = 0;
    if (city == NULL || num_units < 0 || num_incidents < 0 || max_gap < 0 ||
        max_gap > 1000 || num_incidents > EVAL_MAX_TIME / 1001)
        return DC_ERR_INVALID;               /* keeps every arrival <= EVAL_MAX_TIME */

    /* malloc(0) may return NULL legally, so always ask for at least 1 element. */
    sc->units = malloc((size_t)(num_units > 0 ? num_units : 1) * sizeof *sc->units);
    sc->incidents = malloc((size_t)(num_incidents > 0 ? num_incidents : 1) * sizeof *sc->incidents);
    if (sc->units == NULL || sc->incidents == NULL) {
        eval_scenario_free(sc);
        return DC_ERR_NOMEM;
    }

    int v = city->num_vertices;
    for (int i = 0; i < num_units; i++) {
        sc->units[i].type = (ResourceType)(i % RES_TYPE_COUNT);
        sc->units[i].location = eval_random(&seed, v);
    }
    int t = 0;
    for (int i = 0; i < num_incidents; i++) {
        t += eval_random(&seed, max_gap + 1);
        EvalIncidentSpec *s = &sc->incidents[i];
        s->arrival_step = t;
        s->type = (IncidentType)eval_random(&seed, INC_TYPE_COUNT);
        s->location = eval_random(&seed, v);
        s->severity = INCIDENT_SEVERITY_MIN + eval_random(&seed, INCIDENT_SEVERITY_MAX);
        s->service_steps = 1 + eval_random(&seed, 6);
    }
    sc->num_units = num_units;
    sc->num_incidents = num_incidents;
    return DC_OK;
}

void eval_scenario_free(EvalScenario *sc)
{
    if (sc == NULL)
        return;
    free(sc->units);
    free(sc->incidents);
    sc->units = NULL;
    sc->incidents = NULL;
    sc->num_units = 0;
    sc->num_incidents = 0;
}

static int scenario_is_valid(const EvalScenario *sc)
{
    if (sc == NULL || sc->city == NULL || sc->num_units < 0 || sc->num_incidents < 0)
        return 0;
    if ((sc->num_units > 0 && sc->units == NULL) ||
        (sc->num_incidents > 0 && sc->incidents == NULL))
        return 0;
    int v = sc->city->num_vertices;
    for (int i = 0; i < sc->num_units; i++) {
        const EvalUnitSpec *u = &sc->units[i];
        if ((int)u->type < 0 || u->type >= RES_TYPE_COUNT || u->location < 0 || u->location >= v)
            return 0;
    }
    for (int i = 0; i < sc->num_incidents; i++) {
        const EvalIncidentSpec *s = &sc->incidents[i];
        if ((int)s->type < 0 || s->type >= INC_TYPE_COUNT || s->location < 0 ||
            s->location >= v || s->severity < INCIDENT_SEVERITY_MIN ||
            s->severity > INCIDENT_SEVERITY_MAX || s->arrival_step < 0 ||
            s->arrival_step > EVAL_MAX_TIME || s->service_steps < 0 ||
            s->service_steps > EVAL_MAX_TIME)
            return 0;
        if (i > 0 && s->arrival_step < sc->incidents[i - 1].arrival_step)
            return 0;                        /* arrivals must be in time order */
    }
    return 1;
}

/* ---------- one simulation run ---------- */

/* FIFO queue: scan the incidents in REGISTRATION order (= arrival order) and
 * serve every PENDING one that can be served. One pass is enough: units are
 * only used up during a pass, never freed, so an incident that fails now
 * would also fail later in the same pass.
 * The heap is not used; dispatched incidents stay in it as stale entries,
 * exactly like cancelled ones (lazy deletion), which is allowed by the
 * AppState invariants. */
static DcStatus fifo_pass(AppState *app, UnitRule rule)
{
    for (int i = 0; i < app->incidents.count; i++) {
        Incident *inc = app->incidents.items[i];
        if (inc->status != INC_PENDING)
            continue;
        DispatchSelection sel;
        DispatchOutcome outcome = dispatch_select_resource_rule(app, inc, rule, &sel);
        if (outcome == DISPATCH_OK) {
            const Allocation *a = NULL;
            DcStatus st = dispatch_assign(app, inc, &sel, &a);
            dispatch_selection_free(&sel);
            if (st != DC_OK)
                return st;
        } else {
            dispatch_selection_free(&sel);
            if (outcome == DISPATCH_ERROR)
                return DC_ERR_NOMEM;
            inc->dispatch_attempts++;
        }
    }
    return DC_OK;
}

static DcStatus dispatch_pass(AppState *app, EvalStrategy strategy)
{
    if (strategy.queue == QUEUE_FIFO)
        return fifo_pass(app, strategy.unit);
    return dispatch_all_rule(app, strategy.unit, NULL) < 0 ? DC_ERR_NOMEM : DC_OK;
}

static void reset_result(EvalResult *res, EvalStrategy strategy, int incidents)
{
    EvalResult zero = {0};
    *res = zero;
    res->strategy = strategy;
    res->incidents = incidents;
    res->max_response = -1;
    res->makespan = -1;
    res->last_step = -1;
    for (int s = 0; s < EVAL_SEVERITY_SLOTS; s++)
        res->response_max_by_sev[s] = -1;
}

/* Per-incident bookkeeping during a run. */
enum { PHASE_WAITING = 0, PHASE_ACTIVE, PHASE_DONE };

DcStatus eval_run(const EvalScenario *sc, EvalStrategy strategy, EvalResult *res)
{
    if (res == NULL)
        return DC_ERR_INVALID;
    reset_result(res, strategy, sc != NULL ? sc->num_incidents : 0);
    if (!scenario_is_valid(sc))
        return DC_ERR_INVALID;

    const int n = sc->num_incidents;
    Graph *g = eval_clone_graph(sc->city);
    if (g == NULL)
        return DC_ERR_NOMEM;
    AppState app;
    if (app_init(&app, g) != DC_OK) {
        graph_destroy(g);                    /* app_init failed: we still own g */
        app_free(&app);
        return DC_ERR_NOMEM;
    }

    size_t slots = (size_t)(n > 0 ? n : 1);
    long long *done_step = malloc(slots * sizeof *done_step);
    int *phase = calloc(slots, sizeof *phase);     /* all PHASE_WAITING */
    DcStatus status = (done_step == NULL || phase == NULL) ? DC_ERR_NOMEM : DC_OK;

    for (int i = 0; status == DC_OK && i < sc->num_units; i++)
        status = app_register_resource(&app, sc->units[i].type, sc->units[i].location, NULL);

    int next = 0;           /* next incident spec to arrive */
    int active = 0;         /* dispatched, not yet completed */
    long long t = (n > 0) ? sc->incidents[0].arrival_step : 0;

    while (status == DC_OK) {
        /* 1. completions due now (units are released at the scene) */
        for (int i = 0; i < next && status == DC_OK; i++) {
            if (phase[i] == PHASE_ACTIVE && done_step[i] <= t) {
                if (dispatch_complete(&app, APP_FIRST_INCIDENT_ID + i, NULL) != DC_OK)
                    status = DC_ERR_STATE;
                phase[i] = PHASE_DONE;
                active--;
                if (done_step[i] > res->makespan)
                    res->makespan = done_step[i];
            }
        }

        /* 2. arrivals now; IDs are assigned in spec order: 1001, 1002, ... */
        while (status == DC_OK && next < n && sc->incidents[next].arrival_step == t) {
            const EvalIncidentSpec *s = &sc->incidents[next];
            Incident *inc = NULL;
            status = app_register_incident(&app, s->type, s->location, s->severity, &inc);
            if (status == DC_OK && (inc == NULL || inc->id != APP_FIRST_INCIDENT_ID + next))
                status = DC_ERR_STATE;
            next++;
        }

        /* 3. dispatch; the new allocations are the nodes after the old tail */
        const Allocation *before = app.history.tail;
        if (status == DC_OK)
            status = dispatch_pass(&app, strategy);
        for (const Allocation *a = before != NULL ? before->next : app.history.head;
             status == DC_OK && a != NULL; a = a->next) {
            int i = a->incident_id - APP_FIRST_INCIDENT_ID;
            if (i < 0 || i >= next || phase[i] != PHASE_WAITING) {
                status = DC_ERR_STATE;
                break;
            }
            const EvalIncidentSpec *s = &sc->incidents[i];
            long long travel = eval_travel_steps(a->distance);
            long long wait = t - s->arrival_step;
            long long response = wait + travel;
            done_step[i] = t + travel + s->service_steps;
            phase[i] = PHASE_ACTIVE;
            active++;

            res->served++;
            res->total_distance += a->distance;
            res->wait_sum += wait;
            res->response_sum += response;
            res->weighted_response_sum += (long long)s->severity * response;
            res->severity_sum += s->severity;
            res->response_count_by_sev[s->severity]++;
            res->response_sum_by_sev[s->severity] += response;
            if (response > res->response_max_by_sev[s->severity])
                res->response_max_by_sev[s->severity] = response;
            if (response > res->max_response)
                res->max_response = response;
        }
        res->last_step = t;
        if (status != DC_OK)
            break;

        /* 4. jump to the next step at which something happens */
        long long next_t = LLONG_MAX;
        if (next < n)
            next_t = sc->incidents[next].arrival_step;
        for (int i = 0; i < next; i++)
            if (phase[i] == PHASE_ACTIVE && done_step[i] < next_t)
                next_t = done_step[i];
        if (next_t == LLONG_MAX)
            break;          /* nothing will ever happen again: the run is over */
        /* Arrivals at step t were all taken, so the next arrival is later.
         * A job dispatched at t with travel 0 and service 0 has done_step == t
         * but was not completed in step 1 (it did not exist yet); it is
         * completed on the next step instead, so time always moves forward. */
        t = (next_t > res->last_step) ? next_t : res->last_step + 1;
    }

    if (status == DC_OK) {
        res->unserved = n - res->served;
        SystemStats st;
        res->valid = app_check_consistency(&app, NULL) == 0 &&
                     report_compute_stats(&app, &st) == DC_OK &&
                     report_cross_check(&st, NULL) == 0 && active == 0;
    }
    free(done_step);
    free(phase);
    app_free(&app);
    return status;
}

/* ---------- printing ---------- */

#define EVAL_NUM_BUF 32

static void format_avg(char *buf, size_t size, long long sum, long long count)
{
    if (count <= 0)
        snprintf(buf, size, "-");
    else
        snprintf(buf, size, "%.2f", (double)sum / (double)count);
}

void eval_print_header(FILE *out)
{
    if (out == NULL)
        return;
    fprintf(out, "%-30s %6s %6s %9s %8s %8s %9s %8s %9s %8s %5s\n",
            "STRATEGY", "SERVED", "UNSERV", "DISTANCE", "AVG_WAIT", "AVG_RESP",
            "CRIT_RESP", "CRIT_MAX", "WTD_RESP", "MAKESPAN", "VALID");
}

void eval_print_row(FILE *out, const EvalResult *r)
{
    if (out == NULL || r == NULL)
        return;
    char wait[EVAL_NUM_BUF], resp[EVAL_NUM_BUF], crit[EVAL_NUM_BUF], wtd[EVAL_NUM_BUF];
    format_avg(wait, sizeof wait, r->wait_sum, r->served);
    format_avg(resp, sizeof resp, r->response_sum, r->served);
    format_avg(crit, sizeof crit, r->response_sum_by_sev[INCIDENT_SEVERITY_MAX],
               r->response_count_by_sev[INCIDENT_SEVERITY_MAX]);
    format_avg(wtd, sizeof wtd, r->weighted_response_sum, r->severity_sum);
    fprintf(out, "%-30s %6d %6d %9lld %8s %8s %9s %8lld %9s %8lld %5s\n",
            eval_strategy_name(r->strategy), r->served, r->unserved, r->total_distance,
            wait, resp, crit, r->response_max_by_sev[INCIDENT_SEVERITY_MAX], wtd,
            r->makespan, r->valid ? "yes" : "NO");
}

DcStatus eval_compare(const EvalScenario *sc, FILE *out, EvalResult *results)
{
    if (sc == NULL)
        return DC_ERR_INVALID;
    eval_print_header(out);
    for (int i = 0; i < EVAL_STRATEGY_COUNT; i++) {
        EvalResult r;
        DcStatus st = eval_run(sc, eval_strategy_at(i), &r);
        if (st != DC_OK) {
            if (out != NULL)
                fprintf(out, "%-30s run failed: %s\n",
                        eval_strategy_name(eval_strategy_at(i)), dc_status_str(st));
            return st;
        }
        eval_print_row(out, &r);
        if (results != NULL)
            results[i] = r;
    }
    return DC_OK;
}

/* "baseline -> optimised" line for one measure (averages). */
static void print_change(FILE *out, const char *what, long long sum_a, long long count_a,
                         long long sum_b, long long count_b)
{
    char a[EVAL_NUM_BUF], b[EVAL_NUM_BUF];
    format_avg(a, sizeof a, sum_a, count_a);
    format_avg(b, sizeof b, sum_b, count_b);
    fprintf(out, "  %-38s %10s -> %s\n", what, a, b);
}

static DcStatus run_one(FILE *out, const char *name, const Graph *city, uint32_t seed,
                        int units, int incidents, int gap)
{
    EvalScenario sc;
    DcStatus st = eval_generate(&sc, name, city, seed, units, incidents, gap);
    if (st != DC_OK)
        return st;
    fprintf(out, "\n=== %s: %d locations, %d units, %d incidents (seed %u, gaps 0-%d steps) ===\n",
            name, city->num_vertices, units, incidents, (unsigned)seed, gap);
    EvalResult r[EVAL_STRATEGY_COUNT];
    st = eval_compare(&sc, out, r);
    if (st == DC_OK) {
        const EvalResult *base = &r[0], *opt = &r[EVAL_STRATEGY_COUNT - 1];
        const int crit = INCIDENT_SEVERITY_MAX;
        fprintf(out, "Baseline -> optimised:\n");
        print_change(out, "average response, critical (sev 5)",
                     base->response_sum_by_sev[crit], base->response_count_by_sev[crit],
                     opt->response_sum_by_sev[crit], opt->response_count_by_sev[crit]);
        print_change(out, "severity-weighted average response",
                     base->weighted_response_sum, base->severity_sum,
                     opt->weighted_response_sum, opt->severity_sum);
        print_change(out, "average response, all incidents",
                     base->response_sum, base->served, opt->response_sum, opt->served);
        print_change(out, "average distance per dispatch",
                     base->total_distance, base->served, opt->total_distance, opt->served);
    }
    eval_scenario_free(&sc);
    return st;
}

DcStatus eval_run_suite(const Graph *city, FILE *out)
{
    if (out == NULL)
        return DC_ERR_INVALID;
    fprintf(out, "STRATEGY COMPARISON (academic simulation; times in simulated steps)\n"
                 "Units travel %d distance units per step. Response = wait + travel time.\n"
                 "CRIT_* = severity 5 only. WTD_RESP = severity-weighted average response.\n"
                 "Every strategy runs on its own copy of the same scenario.\n", EVAL_SPEED);
    DcStatus st = DC_OK;
    if (city != NULL) {
        st = run_one(out, "current map, light load", city, 101u, 6, 60, 3);
        if (st == DC_OK)
            st = run_one(out, "current map, heavy load", city, 202u, 6, 150, 1);
    }
    Graph *grid = eval_make_grid_city(10, 10, 303u);
    if (grid == NULL)
        return DC_ERR_NOMEM;
    if (st == DC_OK)
        st = run_one(out, "10 x 10 grid, heavy load", grid, 404u, 15, 400, 1);
    graph_destroy(grid);
    return st;
}

/* ---------- timing ---------- */

static double ms_between(clock_t start, clock_t end)
{
    return 1000.0 * (double)(end - start) / (double)CLOCKS_PER_SEC;
}

static DcStatus time_dijkstra(FILE *out, int side)
{
    Graph *g = eval_make_grid_city(side, side, 7u);
    if (g == NULL)
        return DC_ERR_NOMEM;
    const int runs = 50;
    int v = g->num_vertices, agree = 1;
    DcStatus st = DC_OK;

    clock_t start = clock();
    for (int k = 0; k < runs && st == DC_OK; k++) {
        ShortestPaths sp;
        st = dijkstra_run(g, (k * v) / runs, &sp);
        shortest_paths_free(&sp);
    }
    double heap_ms = ms_between(start, clock());

    start = clock();
    for (int k = 0; k < runs && st == DC_OK; k++) {
        ShortestPaths sp;
        st = dijkstra_run_array(g, (k * v) / runs, &sp);
        shortest_paths_free(&sp);
    }
    double array_ms = ms_between(start, clock());

    /* Correctness alongside speed: both versions must give equal distances. */
    ShortestPaths a, b;
    if (st == DC_OK && dijkstra_run(g, 0, &a) == DC_OK) {
        if (dijkstra_run_array(g, 0, &b) == DC_OK) {
            for (int i = 0; i < v; i++)
                if (a.dist[i] != b.dist[i])
                    agree = 0;
            shortest_paths_free(&b);
        }
        shortest_paths_free(&a);
    }
    if (st == DC_OK)
        fprintf(out, "  %4d locations %5d roads   heap %9.1f ms   array %9.1f ms   same distances: %s\n",
                v, g->num_edges, heap_ms, array_ms, agree ? "yes" : "NO");
    graph_destroy(g);
    return st;
}

static DcStatus time_lookup(FILE *out, int n)
{
    Graph *g = graph_create(1);
    if (g == NULL)
        return DC_ERR_NOMEM;
    AppState app;
    if (app_init(&app, g) != DC_OK) {
        graph_destroy(g);
        app_free(&app);
        return DC_ERR_NOMEM;
    }
    DcStatus st = DC_OK;
    for (int i = 0; i < n && st == DC_OK; i++)
        st = app_register_resource(&app, RES_AMBULANCE, 0, NULL);

    const int lookups = 100000;
    long found_hash = 0, found_linear = 0;
    clock_t start = clock();
    for (int k = 0; k < lookups && st == DC_OK; k++)
        found_hash += app_find_resource(&app, 1 + k % n) != NULL;
    double hash_ms = ms_between(start, clock());

    start = clock();
    for (int k = 0; k < lookups && st == DC_OK; k++)
        found_linear += resource_store_find(&app.resources, 1 + k % n) != NULL;
    double linear_ms = ms_between(start, clock());

    if (st == DC_OK)
        fprintf(out, "  %5d units   %d lookups   hash %8.1f ms   linear %8.1f ms   found %ld / %ld\n",
                n, lookups, hash_ms, linear_ms, found_hash, found_linear);
    app_free(&app);
    return st;
}

DcStatus eval_benchmark(FILE *out)
{
    if (out == NULL)
        return DC_ERR_INVALID;
    fprintf(out, "TIMING (clock(); depends on this machine and its load — compare rows,\n"
                 "not absolute values; repeat the run to see the variation)\n");
    fprintf(out, "Dijkstra, 50 sources per grid city:\n");
    const int sides[3] = {10, 20, 31};
    for (int i = 0; i < 3; i++) {
        DcStatus st = time_dijkstra(out, sides[i]);
        if (st != DC_OK)
            return st;
    }
    fprintf(out, "ID lookup (hash index vs linear scan of the store):\n");
    const int sizes[3] = {1000, 2000, 4000};
    for (int i = 0; i < 3; i++) {
        DcStatus st = time_lookup(out, sizes[i]);
        if (st != DC_OK)
            return st;
    }
    return DC_OK;
}
