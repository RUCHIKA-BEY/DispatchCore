/*
 * test_app_state.c — tests for AppState: registration keeps the store, the
 * hash index and the heap in step; lookups by ID; consistency checking
 * (Phase 5).
 *
 * Build and run:   make test
 */
#include "app_state.h"
#include "reference_city.h"
#include "test_util.h"

#include <stdlib.h>

/* Builds an AppState around a fresh reference city. Returns 1 on success. */
static int setup(AppState *app)
{
    Graph *g = build_reference_city();
    if (g == NULL) {
        app_init(app, NULL);      /* fails, but leaves *app in a safe empty state */
        return 0;
    }
    if (app_init(app, g) != DC_OK) {
        graph_destroy(g);         /* init failed: we still own the graph */
        return 0;
    }
    return 1;
}

static void test_init_and_free(void)
{
    TEST_SECTION("app_init / app_free");
    AppState app;
    CHECK_EQ_INT(app_init(&app, NULL), DC_ERR_INVALID);
    app_free(&app);                         /* safe after a failed init */
    CHECK_EQ_INT(app_init(NULL, NULL), DC_ERR_INVALID);

    CHECK(setup(&app));
    CHECK_EQ_INT(app_num_locations(&app), REF_CITY_VERTICES);
    CHECK_EQ_INT(app.next_incident_id, APP_FIRST_INCIDENT_ID);
    CHECK_EQ_INT(app.next_resource_id, APP_FIRST_RESOURCE_ID);
    CHECK_EQ_INT(app_check_consistency(&app, stdout), 0);
    app_free(&app);                         /* also destroys the graph */
    app_free(&app);                         /* second call is harmless */
    app_free(NULL);
}

static void test_register_incidents(void)
{
    TEST_SECTION("registering incidents: IDs, sequence numbers, heap, index");
    AppState app;
    CHECK(setup(&app));
    if (app.graph == NULL)
        return;

    /* the Phase 3 example, registered through the application layer */
    const IncidentType types[7] = {INC_MEDICAL, INC_FIRE, INC_ROAD_ACCIDENT,
                                   INC_STRUCTURAL_RESCUE, INC_MEDICAL, INC_FIRE,
                                   INC_ROAD_ACCIDENT};
    const int locations[7] = {5, 2, 6, 4, 0, 7, 1};
    const int severities[7] = {3, 5, 3, 4, 5, 1, 4};
    int failures = 0;
    for (int i = 0; i < 7; i++) {
        Incident *inc = NULL;
        if (app_register_incident(&app, types[i], locations[i], severities[i], &inc) != DC_OK ||
            inc == NULL || inc->id != 1001 + i || inc->reported_seq != i)
            failures++;
    }
    CHECK_EQ_INT(failures, 0);
    CHECK_EQ_INT(app.incidents.count, 7);
    CHECK_EQ_INT(hash_table_count(app.incident_index), 7);
    CHECK_EQ_INT(incident_heap_size(&app.pending), 7);
    CHECK_EQ_INT(app.next_incident_id, 1008);
    CHECK_EQ_INT(app.clock, 7);

    /* lookup returns the very record owned by the store */
    CHECK(app_find_incident(&app, 1004) == app.incidents.items[3]);
    CHECK(app_find_incident(&app, 1008) == NULL);
    CHECK(app_find_incident(&app, 1) == NULL);

    Incident *top = NULL;
    CHECK_EQ_INT(incident_heap_peek(&app.pending, &top), DC_OK);
    CHECK(top != NULL && top->id == 1002);   /* severity 5, reported first */

    CHECK_EQ_INT(app_check_consistency(&app, stdout), 0);
    app_free(&app);
}

static void test_rejected_registration_changes_nothing(void)
{
    TEST_SECTION("a rejected registration leaves every structure unchanged");
    AppState app;
    CHECK(setup(&app));
    if (app.graph == NULL)
        return;

    CHECK_EQ_INT(app_register_incident(&app, INC_FIRE, 2, 5, NULL), DC_OK);

    Incident dummy;
    Incident *inc = &dummy;
    CHECK_EQ_INT(app_register_incident(&app, INC_FIRE, 2, 9, &inc), DC_ERR_INVALID);  /* severity */
    CHECK(inc == NULL);
    CHECK_EQ_INT(app_register_incident(&app, INC_FIRE, 42, 3, NULL), DC_ERR_INVALID); /* location */
    CHECK_EQ_INT(app_register_incident(&app, INC_TYPE_COUNT, 2, 3, NULL), DC_ERR_INVALID);
    CHECK_EQ_INT(app_register_resource(&app, RES_AMBULANCE, -1, NULL), DC_ERR_INVALID);
    CHECK_EQ_INT(app_register_incident(NULL, INC_FIRE, 2, 3, NULL), DC_ERR_INVALID);

    CHECK_EQ_INT(app.incidents.count, 1);
    CHECK_EQ_INT(hash_table_count(app.incident_index), 1);
    CHECK_EQ_INT(incident_heap_size(&app.pending), 1);
    CHECK_EQ_INT(app.resources.count, 0);
    CHECK_EQ_INT(app.next_incident_id, 1002);   /* no ID was used up */
    CHECK_EQ_INT(app.next_resource_id, 1);
    CHECK_EQ_INT(app.clock, 1);
    CHECK_EQ_INT(app_check_consistency(&app, stdout), 0);
    app_free(&app);
}

static void test_register_reference_fleet(void)
{
    TEST_SECTION("registering the reference fleet R1-R6");
    AppState app;
    CHECK(setup(&app));
    if (app.graph == NULL)
        return;

    const ResourceType types[6] = {RES_AMBULANCE, RES_AMBULANCE, RES_FIRE_UNIT,
                                   RES_RESCUE_UNIT, RES_FIRE_UNIT, RES_AMBULANCE};
    const int locations[6] = {0, 6, 2, 4, 7, 8};
    int failures = 0;
    for (int i = 0; i < 6; i++) {
        Resource *r = NULL;
        if (app_register_resource(&app, types[i], locations[i], &r) != DC_OK ||
            r == NULL || r->id != i + 1)
            failures++;
    }
    CHECK_EQ_INT(failures, 0);
    CHECK_EQ_INT(app.resources.count, 6);
    CHECK_EQ_INT(hash_table_count(app.resource_index), 6);

    Resource *r5 = app_find_resource(&app, 5);
    CHECK(r5 != NULL && r5->type == RES_FIRE_UNIT && r5->location_id == 7);
    CHECK(app_find_resource(&app, 7) == NULL);
    CHECK(app_find_resource(NULL, 1) == NULL);

    /* registration order is kept for the baseline strategy */
    int order_ok = 1;
    for (int i = 0; i < app.resources.count; i++)
        if (app.resources.items[i]->id != i + 1)
            order_ok = 0;
    CHECK(order_ok);

    printf("\n  Resource index (debug view):\n");
    hash_table_print(app.resource_index, stdout);
    CHECK_EQ_INT(app_check_consistency(&app, stdout), 0);
    app_free(&app);
}

static void test_consistency_checker_detects_problems(void)
{
    TEST_SECTION("the consistency checker really detects broken states");
    AppState app;
    CHECK(setup(&app));
    if (app.graph == NULL)
        return;

    Incident *inc = NULL;
    Resource *r = NULL;
    CHECK_EQ_INT(app_register_incident(&app, INC_MEDICAL, 5, 4, &inc), DC_OK);
    CHECK_EQ_INT(app_register_resource(&app, RES_AMBULANCE, 0, &r), DC_OK);
    CHECK_EQ_INT(app_check_consistency(&app, NULL), 0);
    if (inc == NULL || r == NULL) {
        app_free(&app);
        return;
    }

    /* Break the resource invariant on purpose, then repair it. */
    r->assigned_incident_id = 1001;            /* "assigned" but still AVAILABLE */
    printf("  (one problem is expected below)\n");
    CHECK_EQ_INT(app_check_consistency(&app, stdout), 1);
    r->assigned_incident_id = RESOURCE_NO_INCIDENT;

    /* Remove an incident from the index behind the app's back. */
    CHECK_EQ_INT(hash_table_remove(app.incident_index, inc->id), DC_OK);
    CHECK(app_check_consistency(&app, NULL) >= 2);  /* size mismatch + missing entry */
    CHECK_EQ_INT(hash_table_insert(app.incident_index, inc->id, inc), DC_OK);
    CHECK_EQ_INT(app_check_consistency(&app, NULL), 0);
    app_free(&app);
}

static void test_many_registrations(void)
{
    TEST_SECTION("1000 incidents and 300 resources stay consistent");
    AppState app;
    CHECK(setup(&app));
    if (app.graph == NULL)
        return;

    uint32_t seed = 99u;
    int failures = 0;
    for (int k = 0; k < 1000; k++) {
        IncidentType type = (IncidentType)test_random(&seed, INC_TYPE_COUNT);
        int location = test_random(&seed, REF_CITY_VERTICES);
        int severity = 1 + test_random(&seed, 5);
        if (app_register_incident(&app, type, location, severity, NULL) != DC_OK)
            failures++;
    }
    for (int k = 0; k < 300; k++) {
        ResourceType type = (ResourceType)test_random(&seed, RES_TYPE_COUNT);
        int location = test_random(&seed, REF_CITY_VERTICES);
        if (app_register_resource(&app, type, location, NULL) != DC_OK)
            failures++;
    }
    CHECK_EQ_INT(failures, 0);
    CHECK_EQ_INT(app.incidents.count, 1000);
    CHECK_EQ_INT(app.resources.count, 300);

    int lookup_errors = 0;
    for (int id = 1001; id < 2001; id++)
        if (app_find_incident(&app, id) == NULL || app_find_incident(&app, id)->id != id)
            lookup_errors++;
    CHECK_EQ_INT(lookup_errors, 0);

    HashStats s;
    hash_table_stats(app.incident_index, &s);
    CHECK_EQ_INT(s.used_buckets, APP_INDEX_BUCKETS);   /* 1000 sequential IDs use every bucket */
    printf("  incident index: load factor %.2f, longest chain %d\n",
           s.load_factor, s.longest_chain);

    CHECK_EQ_INT(app_check_consistency(&app, stdout), 0);
    app_free(&app);
}

int main(void)
{
    test_init_and_free();
    test_register_incidents();
    test_rejected_registration_changes_nothing();
    test_register_reference_fleet();
    test_consistency_checker_detects_problems();
    test_many_registrations();
    return TEST_SUMMARY();
}
