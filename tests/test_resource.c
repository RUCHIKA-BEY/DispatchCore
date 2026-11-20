/*
 * test_resource.c — unit tests for resource records, the resource state
 * machine (assign / release / service changes) and the store (Phase 4).
 *
 * Build and run:   make test
 */
#include "resource.h"
#include "test_util.h"

#include <stdlib.h>

#define NUM_LOCATIONS 10

/* The module's key invariant: DISPATCHED <=> an incident is assigned. */
static int invariant_holds(const Resource *r)
{
    int dispatched = (r->status == RES_DISPATCHED);
    int assigned = (r->assigned_incident_id != RESOURCE_NO_INCIDENT);
    return dispatched == assigned;
}

static Resource make(int id, ResourceType type, int location)
{
    Resource r = {id, type, location, RES_AVAILABLE, RESOURCE_NO_INCIDENT, 0, 0};
    return r;
}

static void test_names(void)
{
    TEST_SECTION("type/status names round-trip");
    for (int t = 0; t < RES_TYPE_COUNT; t++) {
        ResourceType parsed = RES_TYPE_COUNT;
        CHECK_EQ_INT(resource_type_from_name(resource_type_name((ResourceType)t), &parsed), DC_OK);
        CHECK_EQ_INT(parsed, t);
    }
    for (int s = 0; s < RES_STATUS_COUNT; s++) {
        ResourceStatus parsed = RES_STATUS_COUNT;
        CHECK_EQ_INT(resource_status_from_name(resource_status_name((ResourceStatus)s), &parsed), DC_OK);
        CHECK_EQ_INT(parsed, s);
    }
    ResourceType t;
    CHECK_EQ_INT(resource_type_from_name("ambulance", &t), DC_ERR_NOT_FOUND);
    CHECK_EQ_INT(resource_type_from_name("HELICOPTER", &t), DC_ERR_NOT_FOUND);
    CHECK_EQ_INT(resource_type_from_name(NULL, &t), DC_ERR_INVALID);
    CHECK_STR_EQ(resource_type_name(RES_TYPE_COUNT), "?");
    CHECK_STR_EQ(resource_status_name((ResourceStatus)-1), "?");
}

static void test_create(void)
{
    TEST_SECTION("resource_validate / resource_create");
    CHECK_EQ_INT(resource_validate(1, RES_AMBULANCE, 0, NUM_LOCATIONS), DC_OK);
    CHECK_EQ_INT(resource_validate(1, RES_RESCUE_UNIT, 9, NUM_LOCATIONS), DC_OK);
    CHECK_EQ_INT(resource_validate(0, RES_AMBULANCE, 0, NUM_LOCATIONS), DC_ERR_INVALID);
    CHECK_EQ_INT(resource_validate(1, (ResourceType)-1, 0, NUM_LOCATIONS), DC_ERR_INVALID);
    CHECK_EQ_INT(resource_validate(1, RES_TYPE_COUNT, 0, NUM_LOCATIONS), DC_ERR_INVALID);
    CHECK_EQ_INT(resource_validate(1, RES_AMBULANCE, -1, NUM_LOCATIONS), DC_ERR_INVALID);
    CHECK_EQ_INT(resource_validate(1, RES_AMBULANCE, 10, NUM_LOCATIONS), DC_ERR_INVALID);
    CHECK_EQ_INT(resource_validate(1, RES_AMBULANCE, 0, 0), DC_ERR_INVALID);

    Resource *r = NULL;
    CHECK_EQ_INT(resource_create(3, RES_FIRE_UNIT, 2, NUM_LOCATIONS, &r), DC_OK);
    CHECK(r != NULL);
    if (r != NULL) {
        CHECK_EQ_INT(r->id, 3);
        CHECK_EQ_INT(r->type, RES_FIRE_UNIT);
        CHECK_EQ_INT(r->location_id, 2);
        CHECK_EQ_INT(r->status, RES_AVAILABLE);
        CHECK_EQ_INT(r->assigned_incident_id, RESOURCE_NO_INCIDENT);
        CHECK_EQ_INT(r->total_dispatches, 0);
        CHECK(r->total_distance == 0);
        CHECK(resource_is_available(r));
        free(r);
    }
    Resource dummy;
    Resource *bad = &dummy;           /* non-NULL on purpose: must be reset */
    CHECK_EQ_INT(resource_create(4, RES_FIRE_UNIT, 99, NUM_LOCATIONS, &bad), DC_ERR_INVALID);
    CHECK(bad == NULL);
    CHECK_EQ_INT(resource_create(4, RES_FIRE_UNIT, 2, NUM_LOCATIONS, NULL), DC_ERR_INVALID);
}

static void test_transition_table(void)
{
    TEST_SECTION("status transition table");
    const int expected[RES_STATUS_COUNT][RES_STATUS_COUNT] = {
        /*              AVAIL DISP OOS */
        /* AVAILABLE */ {0,    1,   1},
        /* DISPATCHED*/ {1,    0,   0},
        /* OUT_OF_SVC*/ {1,    0,   0},
    };
    int mismatches = 0;
    for (int from = 0; from < RES_STATUS_COUNT; from++)
        for (int to = 0; to < RES_STATUS_COUNT; to++)
            if (resource_can_transition((ResourceStatus)from, (ResourceStatus)to) != expected[from][to])
                mismatches++;
    CHECK_EQ_INT(mismatches, 0);
}

static void test_assign_and_release(void)
{
    TEST_SECTION("assign / release: no double assignment, statistics, relocation");
    Resource r = make(1, RES_AMBULANCE, 0);

    CHECK_EQ_INT(resource_assign(&r, 1001, 9), DC_OK);
    CHECK_EQ_INT(r.status, RES_DISPATCHED);
    CHECK_EQ_INT(r.assigned_incident_id, 1001);
    CHECK_EQ_INT(r.total_dispatches, 1);
    CHECK(r.total_distance == 9);
    CHECK(!resource_is_available(&r));

    /* THE core rule: a dispatched unit cannot be given a second incident,
     * and the failed attempt must not touch the statistics. */
    CHECK_EQ_INT(resource_assign(&r, 1002, 4), DC_ERR_STATE);
    CHECK_EQ_INT(r.assigned_incident_id, 1001);
    CHECK_EQ_INT(r.total_dispatches, 1);
    CHECK(r.total_distance == 9);

    /* cannot go out of service in the middle of a job */
    CHECK_EQ_INT(resource_set_out_of_service(&r), DC_ERR_STATE);

    /* invalid release leaves everything unchanged */
    CHECK_EQ_INT(resource_release(&r, 10, NUM_LOCATIONS), DC_ERR_INVALID);
    CHECK_EQ_INT(r.status, RES_DISPATCHED);
    CHECK_EQ_INT(r.location_id, 0);

    CHECK_EQ_INT(resource_release(&r, 5, NUM_LOCATIONS), DC_OK);
    CHECK_EQ_INT(r.status, RES_AVAILABLE);
    CHECK_EQ_INT(r.assigned_incident_id, RESOURCE_NO_INCIDENT);
    CHECK_EQ_INT(r.location_id, 5);                 /* moved to the incident scene */
    CHECK_EQ_INT(resource_release(&r, 5, NUM_LOCATIONS), DC_ERR_STATE);  /* not dispatched */

    /* reusable: second job accumulates statistics */
    CHECK_EQ_INT(resource_assign(&r, 1003, 6), DC_OK);
    CHECK_EQ_INT(r.total_dispatches, 2);
    CHECK(r.total_distance == 15);

    /* invalid assignment arguments change nothing */
    Resource fresh = make(2, RES_AMBULANCE, 0);
    CHECK_EQ_INT(resource_assign(&fresh, 0, 5), DC_ERR_INVALID);
    CHECK_EQ_INT(resource_assign(&fresh, 1001, -1), DC_ERR_INVALID);
    CHECK_EQ_INT(resource_assign(NULL, 1001, 5), DC_ERR_INVALID);
    CHECK_EQ_INT(fresh.status, RES_AVAILABLE);
    CHECK_EQ_INT(fresh.total_dispatches, 0);
    CHECK_EQ_INT(resource_assign(&fresh, 1001, 0), DC_OK);   /* distance 0 = same location */
}

static void test_service_changes(void)
{
    TEST_SECTION("out of service / return to service");
    Resource r = make(1, RES_RESCUE_UNIT, 4);
    CHECK_EQ_INT(resource_return_to_service(&r), DC_ERR_STATE);   /* already in service */
    CHECK_EQ_INT(resource_set_out_of_service(&r), DC_OK);
    CHECK_EQ_INT(r.status, RES_OUT_OF_SERVICE);
    CHECK(!resource_is_available(&r));
    CHECK_EQ_INT(resource_set_out_of_service(&r), DC_ERR_STATE);
    CHECK_EQ_INT(resource_assign(&r, 1001, 3), DC_ERR_STATE);     /* cannot be sent */
    CHECK_EQ_INT(r.assigned_incident_id, RESOURCE_NO_INCIDENT);
    CHECK_EQ_INT(resource_return_to_service(&r), DC_OK);
    CHECK(resource_is_available(&r));
    CHECK_EQ_INT(resource_set_out_of_service(NULL), DC_ERR_INVALID);
    CHECK(!resource_is_available(NULL));
}

static void test_random_operation_sequence(void)
{
    TEST_SECTION("5000 random operations never break the invariant");
    Resource fleet[5];
    for (int i = 0; i < 5; i++)
        fleet[i] = make(i + 1, (ResourceType)(i % RES_TYPE_COUNT), i);

    uint32_t seed = 4u;
    int violations = 0, successes = 0;
    for (int step = 0; step < 5000; step++) {
        Resource *r = &fleet[test_random(&seed, 5)];
        int op = test_random(&seed, 4);
        DcStatus st = DC_ERR_INVALID;
        switch (op) {
        case 0: st = resource_assign(r, 1 + test_random(&seed, 100), test_random(&seed, 50)); break;
        case 1: st = resource_release(r, test_random(&seed, NUM_LOCATIONS), NUM_LOCATIONS); break;
        case 2: st = resource_set_out_of_service(r); break;
        default: st = resource_return_to_service(r); break;
        }
        if (st == DC_OK)
            successes++;
        for (int i = 0; i < 5; i++)
            if (!invariant_holds(&fleet[i]))
                violations++;
    }
    CHECK_EQ_INT(violations, 0);
    CHECK(successes > 0);   /* the sequence really exercised the transitions */

    /* statistics consistency: dispatch count never negative, distance sum sane */
    int bad_stats = 0;
    for (int i = 0; i < 5; i++)
        if (fleet[i].total_dispatches < 0 || fleet[i].total_distance < 0)
            bad_stats++;
    CHECK_EQ_INT(bad_stats, 0);
}

static void test_store(void)
{
    TEST_SECTION("resource store: order, duplicates, counts");
    ResourceStore store;
    CHECK_EQ_INT(resource_store_init(&store, 0), DC_ERR_INVALID);
    CHECK_EQ_INT(resource_store_init(&store, 1), DC_OK);

    const int ids[4] = {7, 2, 9, 4};   /* registration order is NOT sorted by ID */
    int failures = 0;
    for (int i = 0; i < 4; i++) {
        Resource *r = NULL;
        if (resource_create(ids[i], (ResourceType)(i % RES_TYPE_COUNT), i, NUM_LOCATIONS, &r) != DC_OK ||
            resource_store_add(&store, r) != DC_OK) {
            failures++;
            free(r);
        }
    }
    CHECK_EQ_INT(failures, 0);
    CHECK_EQ_INT(store.count, 4);
    int order_ok = 1;
    for (int i = 0; i < 4 && i < store.count; i++)
        if (store.items[i]->id != ids[i])
            order_ok = 0;
    CHECK(order_ok);                   /* the baseline strategy relies on this order */

    Resource *dup = NULL;
    CHECK_EQ_INT(resource_create(9, RES_AMBULANCE, 0, NUM_LOCATIONS, &dup), DC_OK);
    CHECK_EQ_INT(resource_store_add(&store, dup), DC_ERR_DUPLICATE);
    free(dup);
    CHECK_EQ_INT(resource_store_add(&store, NULL), DC_ERR_INVALID);

    Resource *r9 = resource_store_find(&store, 9);
    CHECK(r9 != NULL);
    CHECK(resource_store_find(&store, 1) == NULL);
    if (r9 != NULL)
        CHECK_EQ_INT(resource_assign(r9, 1001, 3), DC_OK);
    Resource *r4 = resource_store_find(&store, 4);
    if (r4 != NULL)
        CHECK_EQ_INT(resource_set_out_of_service(r4), DC_OK);

    CHECK_EQ_INT(resource_store_count_status(&store, -1), 4);
    CHECK_EQ_INT(resource_store_count_status(&store, RES_AVAILABLE), 2);
    CHECK_EQ_INT(resource_store_count_status(&store, RES_DISPATCHED), 1);
    CHECK_EQ_INT(resource_store_count_status(&store, RES_OUT_OF_SERVICE), 1);

    printf("\n");
    resource_store_print(&store, stdout, -1);
    printf("\n");
    resource_store_print(&store, stdout, RES_AVAILABLE);
    printf("\n");

    resource_store_free(&store);
    CHECK(store.items == NULL && store.count == 0);
    resource_store_free(&store);
    resource_store_free(NULL);
}

int main(void)
{
    test_names();
    test_create();
    test_transition_table();
    test_assign_and_release();
    test_service_changes();
    test_random_operation_sequence();
    test_store();
    return TEST_SUMMARY();
}
