/*
 * test_dispatch.c — integration tests for the dispatch engine (Phase 6):
 * priority queue + graph + Dijkstra + compatibility + resources + history.
 *
 * Expected values are hand-calculated in docs/PHASE6_DISPATCH.md.
 *
 * Build and run:   make test
 */
#include "dispatch.h"
#include "compatibility.h"
#include "dijkstra.h"
#include "reference_city.h"
#include "test_util.h"

#include <stdlib.h>

/* ---------- helpers ---------- */

static int setup(AppState *app)
{
    Graph *g = build_reference_city();
    if (g == NULL) {
        app_init(app, NULL);
        return 0;
    }
    if (app_init(app, g) != DC_OK) {
        graph_destroy(g);
        return 0;
    }
    return 1;
}

/* Registers units of the given types at the given locations (IDs 1, 2, ...). */
static int add_units(AppState *app, const ResourceType *types, const int *locations, int n)
{
    for (int i = 0; i < n; i++)
        if (app_register_resource(app, types[i], locations[i], NULL) != DC_OK)
            return 0;
    return 1;
}

/* The reference fleet R1..R6 from blueprint §10. */
static int add_reference_fleet(AppState *app)
{
    const ResourceType types[6] = {RES_AMBULANCE, RES_AMBULANCE, RES_FIRE_UNIT,
                                   RES_RESCUE_UNIT, RES_FIRE_UNIT, RES_AMBULANCE};
    const int locations[6] = {0, 6, 2, 4, 7, 8};
    return add_units(app, types, locations, 6);
}

static Incident *add_incident(AppState *app, IncidentType type, int location, int severity)
{
    Incident *inc = NULL;
    app_register_incident(app, type, location, severity, &inc);
    return inc;
}

static int route_equals(const int *route, int len, const int *expected, int expected_len)
{
    if (route == NULL || len != expected_len)
        return 0;
    for (int i = 0; i < len; i++)
        if (route[i] != expected[i])
            return 0;
    return 1;
}

/* ---------- tests ---------- */

static void test_blueprint_example(void)
{
    TEST_SECTION("blueprint example: MEDICAL severity 4 at Old Town -> R1, distance 9");
    AppState app;
    CHECK(setup(&app));
    if (app.graph == NULL)
        return;
    CHECK(add_reference_fleet(&app));
    Incident *inc = add_incident(&app, INC_MEDICAL, 5, 4);
    CHECK(inc != NULL);
    if (inc == NULL) {
        app_free(&app);
        return;
    }

    /* 1. Selection alone changes nothing. */
    DispatchSelection sel;
    CHECK_EQ_INT(dispatch_select_resource(&app, inc, &sel), DISPATCH_OK);
    CHECK(sel.resource != NULL && sel.resource->id == 1);
    CHECK_EQ_INT(sel.distance, 9);
    const int expected_route[3] = {0, 3, 5};
    CHECK(route_equals(sel.route, sel.route_len, expected_route, 3));
    CHECK_EQ_INT(sel.compatible_units, 3);   /* R1, R2, R6 */
    CHECK_EQ_INT(sel.available_units, 3);
    CHECK_EQ_INT(sel.reachable_units, 2);    /* R6 is on the island */
    dispatch_selection_free(&sel);
    CHECK_EQ_INT(inc->status, INC_PENDING);
    CHECK_EQ_INT(app_find_resource(&app, 1)->status, RES_AVAILABLE);
    CHECK_EQ_INT(incident_heap_size(&app.pending), 1);

    /* 2. Dispatch for real. */
    DispatchResult result;
    printf("\n");
    CHECK_EQ_INT(dispatch_next(&app, stdout, &result), DC_OK);
    CHECK_EQ_INT(result.outcome, DISPATCH_OK);
    CHECK_EQ_INT(result.incident_id, 1001);
    CHECK_EQ_INT(result.resource_id, 1);
    CHECK_EQ_INT(result.distance, 9);
    CHECK(result.resource_id != 5);          /* the closer fire unit was never an option */

    Resource *r1 = app_find_resource(&app, 1);
    CHECK_EQ_INT(r1->status, RES_DISPATCHED);
    CHECK_EQ_INT(r1->assigned_incident_id, 1001);
    CHECK(r1->total_distance == 9);
    CHECK_EQ_INT(inc->status, INC_DISPATCHED);
    CHECK_EQ_INT(inc->assigned_resource_id, 1);
    CHECK_EQ_INT(incident_heap_size(&app.pending), 0);

    const Allocation *a = result.allocation;
    CHECK(a != NULL);
    if (a != NULL) {
        CHECK_EQ_INT(a->id, 1);
        CHECK_EQ_INT(a->from_location, 0);
        CHECK_EQ_INT(a->to_location, 5);
        CHECK(route_equals(a->route, a->route_len, expected_route, 3));
        CHECK(dijkstra_validate_path(app.graph, a->route, a->route_len, 0, 5, 9));
    }
    printf("\n");
    dispatch_print_active(&app, stdout);
    CHECK_EQ_INT(app_check_consistency(&app, stdout), 0);
    app_free(&app);
}

static void test_failure_reasons(void)
{
    TEST_SECTION("each failure reason keeps the incident pending");
    AppState app;
    DispatchSelection sel;
    DispatchResult result;

    /* a) only the island ambulance: compatible, available, unreachable */
    CHECK(setup(&app));
    const ResourceType island_type[1] = {RES_AMBULANCE};
    const int island_loc[1] = {8};
    CHECK(add_units(&app, island_type, island_loc, 1));
    Incident *inc = add_incident(&app, INC_MEDICAL, 5, 4);
    CHECK(inc != NULL);
    if (inc != NULL) {
        CHECK_EQ_INT(dispatch_select_resource(&app, inc, &sel), DISPATCH_UNREACHABLE);
        CHECK(sel.compatible_units == 1 && sel.available_units == 1 && sel.reachable_units == 0);
        dispatch_selection_free(&sel);
        CHECK_EQ_INT(dispatch_next(&app, stdout, &result), DC_OK);
        CHECK_EQ_INT(result.outcome, DISPATCH_NOTHING_DISPATCHABLE);
        CHECK_EQ_INT(result.kept_pending, 1);
        CHECK_EQ_INT(inc->status, INC_PENDING);
        CHECK_EQ_INT(inc->dispatch_attempts, 1);
        CHECK_EQ_INT(incident_heap_size(&app.pending), 1);   /* not lost */
    }
    CHECK_EQ_INT(app_check_consistency(&app, stdout), 0);
    app_free(&app);

    /* b) a fire with only ambulances: no compatible unit */
    CHECK(setup(&app));
    const ResourceType amb[2] = {RES_AMBULANCE, RES_AMBULANCE};
    const int amb_loc[2] = {2, 0};
    CHECK(add_units(&app, amb, amb_loc, 2));
    inc = add_incident(&app, INC_FIRE, 2, 5);
    if (inc != NULL) {
        CHECK_EQ_INT(dispatch_select_resource(&app, inc, &sel), DISPATCH_NO_COMPATIBLE_UNIT);
        dispatch_selection_free(&sel);
        CHECK_EQ_INT(dispatch_next(&app, stdout, &result), DC_OK);
        CHECK_EQ_INT(result.outcome, DISPATCH_NOTHING_DISPATCHABLE);
        CHECK_EQ_INT(inc->status, INC_PENDING);
    }
    app_free(&app);

    /* c) one ambulance, two medical calls: the second finds it busy */
    CHECK(setup(&app));
    const ResourceType one[1] = {RES_AMBULANCE};
    const int one_loc[1] = {0};
    CHECK(add_units(&app, one, one_loc, 1));
    Incident *first = add_incident(&app, INC_MEDICAL, 1, 5);
    Incident *second = add_incident(&app, INC_MEDICAL, 3, 4);
    CHECK_EQ_INT(dispatch_next(&app, NULL, &result), DC_OK);
    CHECK_EQ_INT(result.outcome, DISPATCH_OK);
    CHECK(first != NULL && result.incident_id == first->id);
    if (second != NULL) {
        CHECK_EQ_INT(dispatch_select_resource(&app, second, &sel), DISPATCH_ALL_UNITS_BUSY);
        dispatch_selection_free(&sel);
    }
    CHECK_EQ_INT(dispatch_next(&app, stdout, &result), DC_OK);
    CHECK_EQ_INT(result.outcome, DISPATCH_NOTHING_DISPATCHABLE);
    CHECK_EQ_INT(app_find_resource(&app, 1)->assigned_incident_id, first != NULL ? first->id : -1);
    CHECK_EQ_INT(app_check_consistency(&app, stdout), 0);
    app_free(&app);

    /* d) the only ambulance is out of service */
    CHECK(setup(&app));
    CHECK(add_units(&app, one, one_loc, 1));
    CHECK_EQ_INT(resource_set_out_of_service(app_find_resource(&app, 1)), DC_OK);
    inc = add_incident(&app, INC_MEDICAL, 1, 3);
    if (inc != NULL) {
        CHECK_EQ_INT(dispatch_select_resource(&app, inc, &sel), DISPATCH_ALL_UNITS_BUSY);
        dispatch_selection_free(&sel);
    }
    app_free(&app);
}

static void test_head_of_line_blocking_avoided(void)
{
    TEST_SECTION("an unservable top incident does not block the next one");
    AppState app;
    CHECK(setup(&app));
    if (app.graph == NULL)
        return;
    const ResourceType types[1] = {RES_AMBULANCE};
    const int locations[1] = {0};
    CHECK(add_units(&app, types, locations, 1));
    Incident *fire = add_incident(&app, INC_FIRE, 2, 5);       /* top priority, no fire unit */
    Incident *medical = add_incident(&app, INC_MEDICAL, 5, 2); /* lower priority, servable */
    if (fire == NULL || medical == NULL) {
        app_free(&app);
        return;
    }

    DispatchResult result;
    printf("\n");
    CHECK_EQ_INT(dispatch_next(&app, stdout, &result), DC_OK);
    CHECK_EQ_INT(result.outcome, DISPATCH_OK);
    CHECK_EQ_INT(result.incident_id, medical->id);
    CHECK_EQ_INT(result.attempted, 2);
    CHECK_EQ_INT(result.kept_pending, 1);
    CHECK_EQ_INT(fire->status, INC_PENDING);
    CHECK_EQ_INT(fire->dispatch_attempts, 1);

    Incident *top = NULL;
    CHECK_EQ_INT(incident_heap_peek(&app.pending, &top), DC_OK);
    CHECK(top == fire);                     /* back in the queue, still first */

    /* A second command tries the fire once more and stops: no endless loop. */
    CHECK_EQ_INT(dispatch_next(&app, stdout, &result), DC_OK);
    CHECK_EQ_INT(result.outcome, DISPATCH_NOTHING_DISPATCHABLE);
    CHECK_EQ_INT(result.attempted, 1);
    CHECK_EQ_INT(fire->dispatch_attempts, 2);
    CHECK_EQ_INT(app_check_consistency(&app, stdout), 0);
    app_free(&app);
}

static void test_equal_distance_tie(void)
{
    TEST_SECTION("equal distances: the lower resource ID wins");
    /* From Old Town (5), locations 0 and 2 are both exactly 9 away. */
    const int via_two[3] = {2, 1, 5};
    const int via_zero[3] = {0, 3, 5};
    const ResourceType amb[2] = {RES_AMBULANCE, RES_AMBULANCE};

    for (int order = 0; order < 2; order++) {
        AppState app;
        CHECK(setup(&app));
        const int locations[2] = {order == 0 ? 2 : 0, order == 0 ? 0 : 2};
        CHECK(add_units(&app, amb, locations, 2));
        Incident *inc = add_incident(&app, INC_MEDICAL, 5, 3);
        DispatchSelection sel;
        if (inc != NULL) {
            CHECK_EQ_INT(dispatch_select_resource(&app, inc, &sel), DISPATCH_OK);
            CHECK(sel.resource != NULL && sel.resource->id == 1);
            CHECK_EQ_INT(sel.distance, 9);
            CHECK(route_equals(sel.route, sel.route_len, order == 0 ? via_two : via_zero, 3));
            dispatch_selection_free(&sel);
        }
        app_free(&app);
    }
}

static void test_unit_already_at_scene(void)
{
    TEST_SECTION("a unit at the incident location: distance 0, one-vertex route");
    AppState app;
    CHECK(setup(&app));
    const ResourceType amb[1] = {RES_AMBULANCE};
    const int loc[1] = {5};
    CHECK(add_units(&app, amb, loc, 1));
    CHECK(add_incident(&app, INC_MEDICAL, 5, 3) != NULL);
    DispatchResult result;
    CHECK_EQ_INT(dispatch_next(&app, NULL, &result), DC_OK);
    CHECK_EQ_INT(result.outcome, DISPATCH_OK);
    CHECK_EQ_INT(result.distance, 0);
    CHECK(result.allocation != NULL && result.allocation->route_len == 1 &&
          result.allocation->route[0] == 5);
    CHECK_EQ_INT(app_check_consistency(&app, stdout), 0);
    app_free(&app);
}

static void test_cancel(void)
{
    TEST_SECTION("cancellation and lazy deletion");
    AppState app;
    CHECK(setup(&app));
    const ResourceType amb[1] = {RES_AMBULANCE};
    const int loc[1] = {0};
    CHECK(add_units(&app, amb, loc, 1));
    Incident *urgent = add_incident(&app, INC_MEDICAL, 5, 5);
    Incident *minor = add_incident(&app, INC_MEDICAL, 5, 1);
    if (urgent == NULL || minor == NULL) {
        app_free(&app);
        return;
    }

    CHECK_EQ_INT(dispatch_cancel(&app, urgent->id, stdout), DC_OK);
    CHECK_EQ_INT(urgent->status, INC_CANCELLED);
    CHECK_EQ_INT(dispatch_cancel(&app, urgent->id, NULL), DC_ERR_STATE);  /* twice */
    CHECK_EQ_INT(dispatch_cancel(&app, 9999, NULL), DC_ERR_NOT_FOUND);
    CHECK_EQ_INT(incident_heap_size(&app.pending), 2);   /* still physically there */
    CHECK_EQ_INT(app_check_consistency(&app, stdout), 0);

    DispatchResult result;
    CHECK_EQ_INT(dispatch_next(&app, NULL, &result), DC_OK);
    CHECK_EQ_INT(result.outcome, DISPATCH_OK);
    CHECK_EQ_INT(result.incident_id, minor->id);         /* the cancelled one was skipped */
    CHECK_EQ_INT(result.discarded, 1);
    CHECK_EQ_INT(incident_heap_size(&app.pending), 0);   /* lazy deletion finished */
    CHECK_EQ_INT(dispatch_cancel(&app, minor->id, NULL), DC_ERR_STATE);   /* dispatched */
    app_free(&app);
}

static void test_complete_and_redispatch(void)
{
    TEST_SECTION("completion releases the unit at the scene; it can be sent again");
    AppState app;
    CHECK(setup(&app));
    if (app.graph == NULL)
        return;
    CHECK(add_reference_fleet(&app));
    Incident *inc = add_incident(&app, INC_MEDICAL, 5, 4);
    DispatchResult result;
    CHECK_EQ_INT(dispatch_next(&app, NULL, &result), DC_OK);
    if (inc == NULL || result.outcome != DISPATCH_OK) {
        app_free(&app);
        return;
    }

    CHECK_EQ_INT(dispatch_complete(&app, 9999, NULL), DC_ERR_NOT_FOUND);
    printf("\n");
    CHECK_EQ_INT(dispatch_complete(&app, inc->id, stdout), DC_OK);
    Resource *r1 = app_find_resource(&app, 1);
    CHECK_EQ_INT(inc->status, INC_COMPLETED);
    CHECK_EQ_INT(r1->status, RES_AVAILABLE);
    CHECK_EQ_INT(r1->location_id, 5);                     /* moved to Old Town */
    CHECK_EQ_INT(r1->assigned_incident_id, RESOURCE_NO_INCIDENT);
    const Allocation *a = history_find(&app.history, 1);
    CHECK(a != NULL && a->status == ALLOC_COMPLETED && a->complete_seq > a->dispatch_seq);
    CHECK_EQ_INT(dispatch_complete(&app, inc->id, NULL), DC_ERR_STATE);   /* twice */

    /* A new call at the hospital: R1 (now at 5, distance 9) beats R2 (at 6, 16). */
    Incident *next = add_incident(&app, INC_MEDICAL, 0, 3);
    CHECK(next != NULL);
    if (next != NULL)
        CHECK_EQ_INT(dispatch_complete(&app, next->id, NULL), DC_ERR_STATE);  /* pending */
    CHECK_EQ_INT(dispatch_next(&app, stdout, &result), DC_OK);
    CHECK_EQ_INT(result.outcome, DISPATCH_OK);
    CHECK_EQ_INT(result.resource_id, 1);
    CHECK_EQ_INT(result.distance, 9);
    const int back[3] = {5, 3, 0};
    CHECK(result.allocation != NULL &&
          route_equals(result.allocation->route, result.allocation->route_len, back, 3));
    CHECK(r1->total_dispatches == 2 && r1->total_distance == 18);
    CHECK_EQ_INT(app_check_consistency(&app, stdout), 0);
    app_free(&app);
}

static void test_empty_queue(void)
{
    TEST_SECTION("empty queue / queue holding only cancelled incidents");
    AppState app;
    DispatchResult result;
    CHECK(setup(&app));
    CHECK_EQ_INT(dispatch_next(&app, NULL, &result), DC_OK);
    CHECK_EQ_INT(result.outcome, DISPATCH_NO_PENDING);
    Incident *inc = add_incident(&app, INC_FIRE, 2, 3);
    if (inc != NULL)
        CHECK_EQ_INT(dispatch_cancel(&app, inc->id, NULL), DC_OK);
    CHECK_EQ_INT(dispatch_next(&app, NULL, &result), DC_OK);
    CHECK_EQ_INT(result.outcome, DISPATCH_NO_PENDING);
    CHECK_EQ_INT(result.discarded, 1);
    CHECK_EQ_INT(dispatch_next(NULL, NULL, &result), DC_ERR_INVALID);
    CHECK_EQ_INT(dispatch_next(&app, NULL, NULL), DC_ERR_INVALID);
    app_free(&app);
}

static void test_dispatch_all_seven_incidents(void)
{
    TEST_SECTION("dispatch_all on the Phase 3 example with the reference fleet");
    AppState app;
    CHECK(setup(&app));
    if (app.graph == NULL)
        return;
    CHECK(add_reference_fleet(&app));

    const IncidentType types[7] = {INC_MEDICAL, INC_FIRE, INC_ROAD_ACCIDENT,
                                   INC_STRUCTURAL_RESCUE, INC_MEDICAL, INC_FIRE,
                                   INC_ROAD_ACCIDENT};
    const int locations[7] = {5, 2, 6, 4, 0, 7, 1};
    const int severities[7] = {3, 5, 3, 4, 5, 1, 4};
    for (int i = 0; i < 7; i++)
        CHECK(add_incident(&app, types[i], locations[i], severities[i]) != NULL);

    printf("\n");
    CHECK_EQ_INT(dispatch_all(&app, stdout), 5);

    /* expected allocations in dispatch order (hand trace, PHASE6 doc §E3) */
    const int exp_incident[5] = {1002, 1005, 1004, 1007, 1006};
    const int exp_resource[5] = {3, 1, 4, 2, 5};
    const int exp_distance[5] = {0, 0, 0, 15, 0};
    int k = 0, mismatches = 0;
    for (const Allocation *a = app.history.head; a != NULL; a = a->next, k++) {
        if (k >= 5 || a->incident_id != exp_incident[k] ||
            a->resource_id != exp_resource[k] || a->distance != exp_distance[k])
            mismatches++;
    }
    CHECK_EQ_INT(k, 5);
    CHECK_EQ_INT(mismatches, 0);

    const int route_1007[5] = {6, 7, 4, 2, 1};
    const Allocation *a4 = history_find(&app.history, 4);
    CHECK(a4 != NULL && route_equals(a4->route, a4->route_len, route_1007, 5));

    /* 1001 (MEDICAL at 5) and 1003 (ROAD at 6): only the island ambulance is
     * free, so both stay pending; each was tried in the last two passes. */
    Incident *i1001 = app_find_incident(&app, 1001);
    Incident *i1003 = app_find_incident(&app, 1003);
    CHECK(i1001 != NULL && i1001->status == INC_PENDING && i1001->dispatch_attempts == 2);
    CHECK(i1003 != NULL && i1003->status == INC_PENDING && i1003->dispatch_attempts == 2);
    CHECK_EQ_INT(incident_heap_size(&app.pending), 2);
    CHECK_EQ_INT(app_check_consistency(&app, stdout), 0);
    app_free(&app);
}

static void test_random_simulation(void)
{
    TEST_SECTION("3000 random steps: always consistent, compatible, shortest routes");
    AppState app;
    CHECK(setup(&app));
    if (app.graph == NULL)
        return;

    uint32_t seed = 31u;
    int failures = 0;
    for (int i = 0; i < 12; i++) {
        ResourceType type = (ResourceType)test_random(&seed, RES_TYPE_COUNT);
        if (app_register_resource(&app, type, test_random(&seed, REF_CITY_VERTICES), NULL) != DC_OK)
            failures++;
    }

    int inconsistent_steps = 0, bad_allocations = 0, dispatches = 0;
    for (int step = 0; step < 3000; step++) {
        int op = test_random(&seed, 10);
        if (op < 4) {                                        /* new incident */
            IncidentType type = (IncidentType)test_random(&seed, INC_TYPE_COUNT);
            if (app_register_incident(&app, type, test_random(&seed, REF_CITY_VERTICES),
                                      1 + test_random(&seed, 5), NULL) != DC_OK)
                failures++;
        } else if (op < 7) {                                 /* dispatch */
            DispatchResult result;
            if (dispatch_next(&app, NULL, &result) != DC_OK)
                failures++;
            if (result.outcome == DISPATCH_OK) {
                dispatches++;
                const Allocation *a = result.allocation;
                const Incident *inc = app_find_incident(&app, a->incident_id);
                const Resource *r = app_find_resource(&app, a->resource_id);
                ShortestPaths sp;
                int optimal = -2;
                if (dijkstra_run(app.graph, a->to_location, &sp) == DC_OK) {
                    optimal = shortest_paths_distance(&sp, a->from_location);
                    shortest_paths_free(&sp);
                }
                if (inc == NULL || r == NULL || !compat_is_compatible(inc->type, r->type) ||
                    a->distance != optimal ||
                    !dijkstra_validate_path(app.graph, a->route, a->route_len,
                                            a->from_location, a->to_location, a->distance))
                    bad_allocations++;
            }
        } else if (op < 9) {                                 /* complete a random dispatched one */
            int pick = test_random(&seed, app.incidents.count > 0 ? app.incidents.count : 1);
            if (app.incidents.count > 0 && app.incidents.items[pick]->status == INC_DISPATCHED)
                if (dispatch_complete(&app, app.incidents.items[pick]->id, NULL) != DC_OK)
                    failures++;
        } else if (app.incidents.count > 0) {                /* try to cancel a random one */
            int pick = test_random(&seed, app.incidents.count);
            dispatch_cancel(&app, app.incidents.items[pick]->id, NULL);  /* may be refused */
        }
        if (app_check_consistency(&app, NULL) != 0)
            inconsistent_steps++;
    }
    CHECK_EQ_INT(failures, 0);
    CHECK_EQ_INT(inconsistent_steps, 0);
    CHECK_EQ_INT(bad_allocations, 0);
    CHECK(dispatches > 0);
    printf("  %d dispatches, %d allocations in history\n", dispatches, app.history.count);
    app_free(&app);
}

int main(void)
{
    test_blueprint_example();
    test_failure_reasons();
    test_head_of_line_blocking_avoided();
    test_equal_distance_tie();
    test_unit_already_at_scene();
    test_cancel();
    test_complete_and_redispatch();
    test_empty_queue();
    test_dispatch_all_seven_incidents();
    test_random_simulation();
    return TEST_SUMMARY();
}
