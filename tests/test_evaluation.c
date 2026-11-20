/*
 * test_evaluation.c — tests for the Phase 10 strategy comparison.
 *
 * Scenarios T1-T4 are tiny and traced by hand in docs/PHASE10_EVALUATION.md
 * §E; every expected number below comes from those traces. Larger,
 * generated scenarios are checked for PROPERTIES that must hold for any
 * input (validity, reproducibility, equal unserved sets).
 *
 * Build and run:   make test
 */
#include "evaluation.h"
#include "dijkstra.h"
#include "reference_city.h"
#include "test_util.h"

#include <stdlib.h>
#include <string.h>

#define CRIT INCIDENT_SEVERITY_MAX

/* ---------- small helpers ---------- */

static EvalIncidentSpec spec(int arrival, IncidentType type, int location, int severity,
                             int service)
{
    EvalIncidentSpec s;
    s.arrival_step = arrival;
    s.type = type;
    s.location = location;
    s.severity = severity;
    s.service_steps = service;
    return s;
}

static EvalUnitSpec unit(ResourceType type, int location)
{
    EvalUnitSpec u;
    u.type = type;
    u.location = location;
    return u;
}

static EvalScenario scenario(const char *name, const Graph *city, EvalUnitSpec *units,
                             int nu, EvalIncidentSpec *incs, int ni)
{
    EvalScenario sc;
    sc.name = name;
    sc.city = city;
    sc.units = units;
    sc.num_units = nu;
    sc.incidents = incs;
    sc.num_incidents = ni;
    return sc;
}

/* ---------- basic functions ---------- */

static void test_basics(void)
{
    TEST_SECTION("strategies, travel time, random numbers");
    EvalStrategy s0 = eval_strategy_at(0), s1 = eval_strategy_at(1);
    EvalStrategy s2 = eval_strategy_at(2), s3 = eval_strategy_at(3);
    CHECK(s0.queue == QUEUE_FIFO && s0.unit == UNIT_FIRST_FIT);
    CHECK(s1.queue == QUEUE_FIFO && s1.unit == UNIT_NEAREST);
    CHECK(s2.queue == QUEUE_PRIORITY && s2.unit == UNIT_FIRST_FIT);
    CHECK(s3.queue == QUEUE_PRIORITY && s3.unit == UNIT_NEAREST);
    CHECK_STR_EQ(eval_strategy_name(s0), "baseline (FIFO + first fit)");
    CHECK_STR_EQ(eval_strategy_name(s3), "optimised (priority + nearest)");

    CHECK_EQ_INT(eval_travel_steps(0), 0);
    CHECK_EQ_INT(eval_travel_steps(-3), 0);
    CHECK_EQ_INT(eval_travel_steps(1), 1);
    CHECK_EQ_INT(eval_travel_steps(5), 1);
    CHECK_EQ_INT(eval_travel_steps(6), 2);
    CHECK_EQ_INT(eval_travel_steps(15), 3);

    CHECK_EQ_INT(eval_random(NULL, 10), 0);
    uint32_t a = 42u, b = 42u;
    int same = 1, in_range = 1;
    for (int i = 0; i < 1000; i++) {
        int x = eval_random(&a, 7), y = eval_random(&b, 7);
        same &= (x == y);
        in_range &= (x >= 0 && x < 7);
    }
    CHECK(same);                     /* same seed -> same sequence */
    CHECK(in_range);
    CHECK_EQ_INT(eval_random(&a, 0), 0);
}

static void test_unit_rules(void)
{
    TEST_SECTION("UNIT_FIRST_FIT vs UNIT_NEAREST in the dispatch engine");
    Graph *g = build_reference_city();
    AppState app;
    if (g == NULL || app_init(&app, g) != DC_OK) {
        graph_destroy(g);
        CHECK(0);
        return;
    }
    /* Registration order matters for first fit: U1 far away, U2 close. */
    CHECK_EQ_INT(app_register_resource(&app, RES_AMBULANCE, 6, NULL), DC_OK);   /* U1 */
    CHECK_EQ_INT(app_register_resource(&app, RES_AMBULANCE, 0, NULL), DC_OK);   /* U2 */
    Incident *inc = NULL;
    CHECK_EQ_INT(app_register_incident(&app, INC_MEDICAL, 1, 3, &inc), DC_OK);
    if (inc != NULL) {
        DispatchSelection sel;
        CHECK_EQ_INT(dispatch_select_resource_rule(&app, inc, UNIT_NEAREST, &sel), DISPATCH_OK);
        CHECK(sel.resource != NULL && sel.resource->id == 2);
        CHECK_EQ_INT(sel.distance, 4);                 /* 0-1 */
        dispatch_selection_free(&sel);

        CHECK_EQ_INT(dispatch_select_resource_rule(&app, inc, UNIT_FIRST_FIT, &sel), DISPATCH_OK);
        CHECK(sel.resource != NULL && sel.resource->id == 1);
        CHECK_EQ_INT(sel.distance, 15);                /* 6-7-4-2-1 = 3+4+5+3 */
        CHECK(dijkstra_validate_path(app.graph, sel.route, sel.route_len, 6, 1, 15));

        /* dispatch_assign applies exactly this selection */
        const Allocation *a = NULL;
        CHECK_EQ_INT(dispatch_assign(&app, inc, &sel, &a), DC_OK);
        CHECK(a != NULL && a->resource_id == 1 && a->distance == 15);
        CHECK(sel.route == NULL);                      /* ownership moved to history */
        dispatch_selection_free(&sel);
        CHECK_EQ_INT(inc->status, INC_DISPATCHED);
        CHECK_EQ_INT(app_check_consistency(&app, stdout), 0);  /* stale heap entry is OK */

        CHECK_EQ_INT(dispatch_assign(NULL, inc, &sel, &a), DC_ERR_INVALID);
        sel.outcome = DISPATCH_ALL_UNITS_BUSY;
        CHECK_EQ_INT(dispatch_assign(&app, inc, &sel, &a), DC_ERR_INVALID);
    }
    app_free(&app);
}

static void test_cities(void)
{
    TEST_SECTION("graph copy and grid cities");
    Graph *ref = build_reference_city();
    CHECK(ref != NULL);
    if (ref == NULL)
        return;
    Graph *copy = eval_clone_graph(ref);
    CHECK(copy != NULL && copy != ref);
    if (copy != NULL) {
        CHECK_EQ_INT(copy->num_vertices, 10);
        CHECK_EQ_INT(copy->num_edges, 11);
        int same_roads = 1, same_names = 1;
        for (int u = 0; u < 10; u++) {
            same_names &= strcmp(graph_location_name(copy, u), graph_location_name(ref, u)) == 0;
            for (int v = 0; v < 10; v++)
                same_roads &= graph_road_weight(copy, u, v) == graph_road_weight(ref, u, v);
        }
        CHECK(same_roads);
        CHECK(same_names);
        ShortestPaths a, b;
        CHECK_EQ_INT(dijkstra_run(ref, 0, &a), DC_OK);
        CHECK_EQ_INT(dijkstra_run(copy, 0, &b), DC_OK);
        int same_dist = a.dist != NULL && b.dist != NULL;
        for (int v = 0; same_dist && v < 10; v++)
            same_dist = a.dist[v] == b.dist[v];
        CHECK(same_dist);
        shortest_paths_free(&a);
        shortest_paths_free(&b);
        graph_destroy(copy);
    }
    CHECK(eval_clone_graph(NULL) == NULL);
    graph_destroy(ref);

    Graph *grid = eval_make_grid_city(3, 4, 1u);
    CHECK(grid != NULL);
    if (grid != NULL) {
        CHECK_EQ_INT(grid->num_vertices, 12);
        CHECK_EQ_INT(grid->num_edges, 17);            /* 3*3 across + 2*4 down */
        CHECK_EQ_INT(graph_count_components(grid), 1);
        CHECK_STR_EQ(graph_location_name(grid, 5), "Block 1-1");
        int weights_ok = 1;
        for (int u = 0; u < 12; u++)
            for (const AdjNode *n = grid->adj[u]; n != NULL; n = n->next)
                weights_ok &= n->weight >= 1 && n->weight <= 9;
        CHECK(weights_ok);
        Graph *again = eval_make_grid_city(3, 4, 1u);
        int same = again != NULL;
        for (int u = 0; same && u < 12; u++)
            for (int v = 0; v < 12; v++)
                same &= graph_road_weight(grid, u, v) == graph_road_weight(again, u, v);
        CHECK(same);                                   /* reproducible */
        graph_destroy(again);
        graph_destroy(grid);
    }
    CHECK(eval_make_grid_city(0, 5, 1u) == NULL);
    CHECK(eval_make_grid_city(1001, 1, 1u) == NULL);
    CHECK(eval_make_grid_city(40, 40, 1u) == NULL);    /* 1600 > DC_MAX_VERTICES */
}

/* ---------- hand-traced scenarios ---------- */

static void test_t1_two_units(void)
{
    TEST_SECTION("T1: two ambulances, two incidents at step 0 (PHASE10 doc §E1)");
    Graph *city = build_reference_city();
    if (city == NULL) {
        CHECK(0);
        return;
    }
    EvalUnitSpec units[2] = {unit(RES_AMBULANCE, 0), unit(RES_AMBULANCE, 6)};
    EvalIncidentSpec incs[2] = {spec(0, INC_MEDICAL, 1, 2, 4),     /* A */
                                spec(0, INC_MEDICAL, 5, 5, 4)};    /* B, critical */
    EvalScenario sc = scenario("T1", city, units, 2, incs, 2);

    EvalResult r[EVAL_STRATEGY_COUNT];
    printf("\n");
    CHECK_EQ_INT(eval_compare(&sc, stdout, r), DC_OK);

    /* FIFO (both unit rules): A gets U1 (4), B gets U2 (11) */
    for (int i = 0; i <= 1; i++) {
        CHECK_EQ_INT(r[i].served, 2);
        CHECK(r[i].total_distance == 15);
        CHECK(r[i].wait_sum == 0);
        CHECK(r[i].response_sum == 4);                /* A 1 + B 3 */
        CHECK(r[i].weighted_response_sum == 17);      /* 2*1 + 5*3 */
        CHECK(r[i].severity_sum == 7);
        CHECK(r[i].response_sum_by_sev[CRIT] == 3);
        CHECK(r[i].response_max_by_sev[CRIT] == 3);
        CHECK(r[i].makespan == 7);                    /* B: 0 + 3 + 4 */
        CHECK(r[i].valid);
    }
    /* PRIORITY (both unit rules): B gets U1 (9), A gets U2 (15) */
    for (int i = 2; i <= 3; i++) {
        CHECK_EQ_INT(r[i].served, 2);
        CHECK(r[i].total_distance == 24);
        CHECK(r[i].response_sum == 5);                /* B 2 + A 3 */
        CHECK(r[i].weighted_response_sum == 16);      /* 5*2 + 2*3 */
        CHECK(r[i].response_sum_by_sev[CRIT] == 2);
        CHECK(r[i].max_response == 3);
        CHECK(r[i].makespan == 7);                    /* A: 0 + 3 + 4 */
        CHECK(r[i].valid);
    }
    graph_destroy(city);
}

static void test_t2_one_unit(void)
{
    TEST_SECTION("T2: one ambulance, minor first vs critical first (PHASE10 doc §E2)");
    Graph *city = build_reference_city();
    if (city == NULL) {
        CHECK(0);
        return;
    }
    EvalUnitSpec units[1] = {unit(RES_AMBULANCE, 0)};
    EvalIncidentSpec incs[2] = {spec(0, INC_MEDICAL, 1, 1, 2),     /* P, minor */
                                spec(0, INC_MEDICAL, 3, 5, 2)};    /* Q, critical */
    EvalScenario sc = scenario("T2", city, units, 1, incs, 2);

    EvalResult r[EVAL_STRATEGY_COUNT];
    printf("\n");
    CHECK_EQ_INT(eval_compare(&sc, stdout, r), DC_OK);
    for (int i = 0; i <= 1; i++) {                    /* FIFO: P then Q */
        CHECK(r[i].total_distance == 12);             /* 4 + 8 */
        CHECK(r[i].wait_sum == 3);                    /* Q waits 0 -> 3 */
        CHECK(r[i].response_sum == 6);                /* P 1 + Q 5 */
        CHECK(r[i].weighted_response_sum == 26);      /* 1*1 + 5*5 */
        CHECK(r[i].response_sum_by_sev[CRIT] == 5);
        CHECK(r[i].makespan == 7);
        CHECK(r[i].last_step == 7);
        CHECK(r[i].valid);
    }
    for (int i = 2; i <= 3; i++) {                    /* PRIORITY: Q then P */
        CHECK(r[i].total_distance == 15);             /* 7 + 8 */
        CHECK(r[i].wait_sum == 4);                    /* P waits 0 -> 4 */
        CHECK(r[i].response_sum == 8);                /* Q 2 + P 6 */
        CHECK(r[i].weighted_response_sum == 16);      /* 5*2 + 1*6 */
        CHECK(r[i].response_sum_by_sev[CRIT] == 2);
        CHECK(r[i].response_max_by_sev[1] == 6);
        CHECK(r[i].makespan == 8);
        CHECK(r[i].valid);
    }
    graph_destroy(city);
}

static void test_t3_t4_edge_cases(void)
{
    TEST_SECTION("T3 unreachable incident, T4 zero-length job, empty scenario");
    Graph *city = build_reference_city();
    if (city == NULL) {
        CHECK(0);
        return;
    }
    EvalResult r;

    /* T3: the only ambulance is on the mainland, the incident on the island. */
    EvalUnitSpec u3[1] = {unit(RES_AMBULANCE, 0)};
    EvalIncidentSpec i3[1] = {spec(0, INC_MEDICAL, 9, 3, 1)};
    EvalScenario t3 = scenario("T3", city, u3, 1, i3, 1);
    for (int i = 0; i < EVAL_STRATEGY_COUNT; i++) {
        CHECK_EQ_INT(eval_run(&t3, eval_strategy_at(i), &r), DC_OK);
        CHECK_EQ_INT(r.served, 0);
        CHECK_EQ_INT(r.unserved, 1);
        CHECK(r.makespan == -1 && r.max_response == -1 && r.last_step == 0);
        CHECK(r.valid);
    }

    /* T4: unit already at the scene, no service time: dispatched and done
     * at step 2, completed when the clock moves on to step 3. */
    EvalUnitSpec u4[1] = {unit(RES_AMBULANCE, 5)};
    EvalIncidentSpec i4[1] = {spec(2, INC_MEDICAL, 5, 4, 0)};
    EvalScenario t4 = scenario("T4", city, u4, 1, i4, 1);
    CHECK_EQ_INT(eval_run(&t4, eval_strategy_at(3), &r), DC_OK);
    CHECK_EQ_INT(r.served, 1);
    CHECK(r.response_sum == 0 && r.total_distance == 0);
    CHECK(r.makespan == 2);
    CHECK(r.last_step == 3);
    CHECK(r.valid);

    /* No incidents at all. */
    EvalScenario empty = scenario("empty", city, u4, 1, NULL, 0);
    CHECK_EQ_INT(eval_run(&empty, eval_strategy_at(0), &r), DC_OK);
    CHECK_EQ_INT(r.served, 0);
    CHECK_EQ_INT(r.unserved, 0);
    CHECK(r.valid);
    graph_destroy(city);
}

static void test_invalid_scenarios(void)
{
    TEST_SECTION("malformed scenarios are rejected");
    Graph *city = build_reference_city();
    if (city == NULL) {
        CHECK(0);
        return;
    }
    EvalResult r;
    EvalUnitSpec u[1] = {unit(RES_AMBULANCE, 0)};
    EvalIncidentSpec in[2] = {spec(5, INC_MEDICAL, 1, 3, 1), spec(4, INC_MEDICAL, 1, 3, 1)};
    EvalScenario sc = scenario("bad", city, u, 1, in, 2);
    CHECK_EQ_INT(eval_run(&sc, eval_strategy_at(0), &r), DC_ERR_INVALID);   /* 5 then 4 */

    in[1].arrival_step = 6;
    in[1].location = 10;                                                     /* no such place */
    CHECK_EQ_INT(eval_run(&sc, eval_strategy_at(0), &r), DC_ERR_INVALID);
    in[1].location = 1;
    in[1].severity = 0;
    CHECK_EQ_INT(eval_run(&sc, eval_strategy_at(0), &r), DC_ERR_INVALID);
    in[1].severity = 3;
    in[1].service_steps = -1;
    CHECK_EQ_INT(eval_run(&sc, eval_strategy_at(0), &r), DC_ERR_INVALID);
    in[1].service_steps = 1;
    u[0].location = -1;
    CHECK_EQ_INT(eval_run(&sc, eval_strategy_at(0), &r), DC_ERR_INVALID);
    u[0].location = 0;
    CHECK_EQ_INT(eval_run(&sc, eval_strategy_at(0), &r), DC_OK);           /* repaired */

    CHECK_EQ_INT(eval_run(NULL, eval_strategy_at(0), &r), DC_ERR_INVALID);
    CHECK_EQ_INT(eval_run(&sc, eval_strategy_at(0), NULL), DC_ERR_INVALID);
    CHECK_EQ_INT(eval_compare(NULL, NULL, NULL), DC_ERR_INVALID);
    graph_destroy(city);
}

/* ---------- generated scenarios: properties ---------- */

static int same_scenario(const EvalScenario *a, const EvalScenario *b)
{
    if (a->num_units != b->num_units || a->num_incidents != b->num_incidents)
        return 0;
    for (int i = 0; i < a->num_units; i++)
        if (a->units[i].type != b->units[i].type || a->units[i].location != b->units[i].location)
            return 0;
    for (int i = 0; i < a->num_incidents; i++) {
        const EvalIncidentSpec *x = &a->incidents[i], *y = &b->incidents[i];
        if (x->arrival_step != y->arrival_step || x->type != y->type ||
            x->location != y->location || x->severity != y->severity ||
            x->service_steps != y->service_steps)
            return 0;
    }
    return 1;
}

static void test_generated(void)
{
    TEST_SECTION("generated scenarios: valid, reproducible, same unserved set");
    Graph *city = build_reference_city();
    if (city == NULL) {
        CHECK(0);
        return;
    }
    EvalScenario a, b, c;
    CHECK_EQ_INT(eval_generate(&a, "gen", city, 99u, 6, 80, 2), DC_OK);
    CHECK_EQ_INT(eval_generate(&b, "gen", city, 99u, 6, 80, 2), DC_OK);
    CHECK_EQ_INT(eval_generate(&c, "gen", city, 100u, 6, 80, 2), DC_OK);
    CHECK(same_scenario(&a, &b));                 /* same seed: identical */
    CHECK(!same_scenario(&a, &c));                /* other seed: different */

    int ranges_ok = 1;
    for (int i = 0; i < a.num_units; i++)
        ranges_ok &= a.units[i].type == (ResourceType)(i % RES_TYPE_COUNT) &&
                     a.units[i].location >= 0 && a.units[i].location < 10;
    for (int i = 0; i < a.num_incidents; i++) {
        const EvalIncidentSpec *s = &a.incidents[i];
        ranges_ok &= s->severity >= 1 && s->severity <= 5 && s->service_steps >= 1 &&
                     s->service_steps <= 6 && s->location >= 0 && s->location < 10;
        if (i > 0)
            ranges_ok &= s->arrival_step >= a.incidents[i - 1].arrival_step &&
                         s->arrival_step <= a.incidents[i - 1].arrival_step + 2;
    }
    CHECK(ranges_ok);

    EvalResult r[EVAL_STRATEGY_COUNT];
    printf("\n");
    CHECK_EQ_INT(eval_compare(&a, stdout, r), DC_OK);
    int all_valid = 1, totals_ok = 1, same_unserved = 1;
    for (int i = 0; i < EVAL_STRATEGY_COUNT; i++) {
        all_valid &= r[i].valid;
        totals_ok &= r[i].served + r[i].unserved == 80;
        same_unserved &= r[i].unserved == r[0].unserved;
    }
    CHECK(all_valid);
    CHECK(totals_ok);
    CHECK(same_unserved);    /* units never leave their part of the city, so the
                                same incidents are unreachable for every strategy */

    EvalResult again;        /* the same run twice gives the same numbers */
    CHECK_EQ_INT(eval_run(&a, eval_strategy_at(3), &again), DC_OK);
    CHECK(again.served == r[3].served && again.total_distance == r[3].total_distance &&
          again.response_sum == r[3].response_sum && again.makespan == r[3].makespan);

    eval_scenario_free(&a);
    eval_scenario_free(&b);
    eval_scenario_free(&c);
    eval_scenario_free(&c);                       /* safe twice */
    CHECK(a.units == NULL && a.num_incidents == 0);
    CHECK_EQ_INT(eval_generate(&a, "bad", NULL, 1u, 1, 1, 1), DC_ERR_INVALID);
    CHECK_EQ_INT(eval_generate(&a, "bad", city, 1u, -1, 1, 1), DC_ERR_INVALID);
    graph_destroy(city);
}

static void test_suite_runs(void)
{
    TEST_SECTION("the standard suite (menu option 20) runs and every run is valid");
    Graph *city = build_reference_city();
    if (city == NULL) {
        CHECK(0);
        return;
    }
    printf("\n");
    CHECK_EQ_INT(eval_run_suite(city, stdout), DC_OK);
    CHECK_EQ_INT(eval_run_suite(NULL, stdout), DC_OK);   /* grid scenario only */
    CHECK_EQ_INT(eval_run_suite(city, NULL), DC_ERR_INVALID);
    graph_destroy(city);
}

int main(void)
{
    test_basics();
    test_unit_rules();
    test_cities();
    test_t1_two_units();
    test_t2_one_unit();
    test_t3_t4_edge_cases();
    test_invalid_scenarios();
    test_generated();
    test_suite_runs();
    return TEST_SUMMARY();
}
