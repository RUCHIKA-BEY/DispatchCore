/*
 * test_incident.c — unit tests for incident records, validation, priority
 * rule, state machine and the owning store (Phase 3).
 *
 * Build and run:   make test
 */
#include "incident.h"
#include "test_util.h"

#include <stdlib.h>

#define NUM_LOCATIONS 10   /* the reference city */

static void test_names(void)
{
    TEST_SECTION("type/status names round-trip");
    for (int t = 0; t < INC_TYPE_COUNT; t++) {
        IncidentType parsed = INC_TYPE_COUNT;
        CHECK_EQ_INT(incident_type_from_name(incident_type_name((IncidentType)t), &parsed), DC_OK);
        CHECK_EQ_INT(parsed, t);
    }
    for (int s = 0; s < INC_STATUS_COUNT; s++) {
        IncidentStatus parsed = INC_STATUS_COUNT;
        CHECK_EQ_INT(incident_status_from_name(incident_status_name((IncidentStatus)s), &parsed), DC_OK);
        CHECK_EQ_INT(parsed, s);
    }
    CHECK_STR_EQ(incident_type_name(INC_STRUCTURAL_RESCUE), "STRUCTURAL_RESCUE");
    CHECK_STR_EQ(incident_status_name(INC_CANCELLED), "CANCELLED");

    IncidentType t;
    CHECK_EQ_INT(incident_type_from_name("medical", &t), DC_ERR_NOT_FOUND);   /* case-sensitive */
    CHECK_EQ_INT(incident_type_from_name("FLOOD", &t), DC_ERR_NOT_FOUND);
    CHECK_EQ_INT(incident_type_from_name("", &t), DC_ERR_NOT_FOUND);
    CHECK_EQ_INT(incident_type_from_name(NULL, &t), DC_ERR_INVALID);
    CHECK_EQ_INT(incident_type_from_name("FIRE", NULL), DC_ERR_INVALID);

    CHECK_STR_EQ(incident_type_name(INC_TYPE_COUNT), "?");
    CHECK_STR_EQ(incident_status_name((IncidentStatus)-1), "?");

    CHECK_STR_EQ(incident_severity_label(1), "Minor");
    CHECK_STR_EQ(incident_severity_label(5), "Critical");
    CHECK_STR_EQ(incident_severity_label(0), "?");
    CHECK_STR_EQ(incident_severity_label(6), "?");
}

static void test_validation(void)
{
    TEST_SECTION("incident_validate");
    CHECK_EQ_INT(incident_validate(1001, INC_MEDICAL, 5, 4, 0, NUM_LOCATIONS), DC_OK);
    CHECK_EQ_INT(incident_validate(1, INC_FIRE, 0, 1, 0, NUM_LOCATIONS), DC_OK);   /* minimums */
    CHECK_EQ_INT(incident_validate(1, INC_FIRE, 9, 5, 0, NUM_LOCATIONS), DC_OK);   /* maximums */

    CHECK_EQ_INT(incident_validate(0, INC_MEDICAL, 5, 4, 0, NUM_LOCATIONS), DC_ERR_INVALID);
    CHECK_EQ_INT(incident_validate(-7, INC_MEDICAL, 5, 4, 0, NUM_LOCATIONS), DC_ERR_INVALID);
    CHECK_EQ_INT(incident_validate(1, (IncidentType)-1, 5, 4, 0, NUM_LOCATIONS), DC_ERR_INVALID);
    CHECK_EQ_INT(incident_validate(1, INC_TYPE_COUNT, 5, 4, 0, NUM_LOCATIONS), DC_ERR_INVALID);
    CHECK_EQ_INT(incident_validate(1, INC_MEDICAL, 5, 0, 0, NUM_LOCATIONS), DC_ERR_INVALID);
    CHECK_EQ_INT(incident_validate(1, INC_MEDICAL, 5, 6, 0, NUM_LOCATIONS), DC_ERR_INVALID);
    CHECK_EQ_INT(incident_validate(1, INC_MEDICAL, 5, 4, -1, NUM_LOCATIONS), DC_ERR_INVALID);
    CHECK_EQ_INT(incident_validate(1, INC_MEDICAL, -1, 4, 0, NUM_LOCATIONS), DC_ERR_INVALID);
    CHECK_EQ_INT(incident_validate(1, INC_MEDICAL, 10, 4, 0, NUM_LOCATIONS), DC_ERR_INVALID);
    CHECK_EQ_INT(incident_validate(1, INC_MEDICAL, 0, 4, 0, 0), DC_ERR_INVALID);   /* empty city */
}

static void test_create(void)
{
    TEST_SECTION("incident_create");
    Incident *inc = NULL;
    CHECK_EQ_INT(incident_create(1001, INC_ROAD_ACCIDENT, 6, 3, 12, NUM_LOCATIONS, &inc), DC_OK);
    CHECK(inc != NULL);
    if (inc != NULL) {
        CHECK_EQ_INT(inc->id, 1001);
        CHECK_EQ_INT(inc->type, INC_ROAD_ACCIDENT);
        CHECK_EQ_INT(inc->location_id, 6);
        CHECK_EQ_INT(inc->severity, 3);
        CHECK_EQ_INT(inc->reported_seq, 12);
        CHECK_EQ_INT(inc->status, INC_PENDING);
        CHECK_EQ_INT(inc->assigned_resource_id, INCIDENT_NO_RESOURCE);
        CHECK_EQ_INT(inc->dispatch_attempts, 0);
        free(inc);
    }

    Incident dummy;
    Incident *bad = &dummy;             /* non-NULL on purpose: must be reset to NULL */
    CHECK_EQ_INT(incident_create(1002, INC_FIRE, 6, 9, 13, NUM_LOCATIONS, &bad), DC_ERR_INVALID);
    CHECK(bad == NULL);
    CHECK_EQ_INT(incident_create(1002, INC_FIRE, 6, 3, 13, NUM_LOCATIONS, NULL), DC_ERR_INVALID);
}

static Incident make(int id, int severity, long seq)
{
    Incident inc = {id, INC_MEDICAL, 0, severity, seq, INC_PENDING, INCIDENT_NO_RESOURCE, 0};
    return inc;
}

static void test_priority_rule(void)
{
    TEST_SECTION("incident_higher_priority");
    Incident sev5_late  = make(1, 5, 50);
    Incident sev4_early = make(2, 4, 1);
    Incident sev4_late  = make(3, 4, 9);
    Incident twin_low   = make(10, 4, 9);   /* same severity and seq as sev4_late */

    /* rule 1: severity beats reporting time */
    CHECK(incident_higher_priority(&sev5_late, &sev4_early));
    CHECK(!incident_higher_priority(&sev4_early, &sev5_late));
    /* rule 2: same severity -> earlier report first */
    CHECK(incident_higher_priority(&sev4_early, &sev4_late));
    CHECK(!incident_higher_priority(&sev4_late, &sev4_early));
    /* rule 3: identical severity and seq (should never happen) -> lower ID */
    CHECK(incident_higher_priority(&sev4_late, &twin_low));
    CHECK(!incident_higher_priority(&twin_low, &sev4_late));
    /* an incident never outranks itself */
    CHECK(!incident_higher_priority(&sev4_late, &sev4_late));
}

static void test_state_machine(void)
{
    TEST_SECTION("status transition table");
    /* expected[from][to]: 1 = allowed */
    const int expected[INC_STATUS_COUNT][INC_STATUS_COUNT] = {
        /*            PEND DISP COMP CANC */
        /* PENDING */ {0,   1,   0,   1},
        /* DISPATCH*/ {0,   0,   1,   0},
        /* COMPLETE*/ {0,   0,   0,   0},
        /* CANCELL */ {0,   0,   0,   0},
    };
    int mismatches = 0;
    for (int from = 0; from < INC_STATUS_COUNT; from++)
        for (int to = 0; to < INC_STATUS_COUNT; to++)
            if (incident_can_transition((IncidentStatus)from, (IncidentStatus)to) != expected[from][to])
                mismatches++;
    CHECK_EQ_INT(mismatches, 0);

    Incident inc = make(1, 3, 1);
    CHECK_EQ_INT(incident_set_status(&inc, INC_COMPLETED), DC_ERR_STATE);  /* skip a step */
    CHECK_EQ_INT(inc.status, INC_PENDING);                                 /* unchanged   */
    CHECK_EQ_INT(incident_set_status(&inc, INC_DISPATCHED), DC_OK);
    CHECK_EQ_INT(incident_set_status(&inc, INC_CANCELLED), DC_ERR_STATE); /* no recall   */
    CHECK_EQ_INT(incident_set_status(&inc, INC_COMPLETED), DC_OK);
    CHECK_EQ_INT(incident_set_status(&inc, INC_PENDING), DC_ERR_STATE);   /* final state */
    CHECK_EQ_INT(inc.status, INC_COMPLETED);

    Incident other = make(2, 3, 2);
    CHECK_EQ_INT(incident_set_status(&other, INC_CANCELLED), DC_OK);
    CHECK_EQ_INT(incident_set_status(&other, INC_DISPATCHED), DC_ERR_STATE);
    CHECK_EQ_INT(incident_set_status(NULL, INC_CANCELLED), DC_ERR_INVALID);
}

static void test_store(void)
{
    TEST_SECTION("incident store: ownership, duplicates, growth");
    IncidentStore store;
    CHECK_EQ_INT(incident_store_init(&store, 0), DC_ERR_INVALID);
    CHECK_EQ_INT(incident_store_init(&store, 1), DC_OK);   /* forces many regrowths */

    Incident *first = NULL;
    CHECK_EQ_INT(incident_create(1000, INC_FIRE, 2, 5, 0, NUM_LOCATIONS, &first), DC_OK);
    CHECK_EQ_INT(incident_store_add(&store, first), DC_OK);

    int failures = 0;
    for (int k = 1; k < 50; k++) {
        Incident *inc = NULL;
        if (incident_create(1000 + k, (IncidentType)(k % INC_TYPE_COUNT), k % NUM_LOCATIONS,
                            1 + k % 5, k, NUM_LOCATIONS, &inc) != DC_OK ||
            incident_store_add(&store, inc) != DC_OK) {
            failures++;
            free(inc);
        }
    }
    CHECK_EQ_INT(failures, 0);
    CHECK_EQ_INT(store.count, 50);
    CHECK(store.capacity >= 50);

    /* The pointer array was reallocated several times, but the record
     * itself never moved: the pointer we kept is still the stored one. */
    CHECK(store.items[0] == first);
    CHECK(incident_store_find(&store, 1000) == first);
    CHECK(incident_store_find(&store, 1049) != NULL);
    CHECK(incident_store_find(&store, 999) == NULL);

    Incident *dup = NULL;
    CHECK_EQ_INT(incident_create(1007, INC_MEDICAL, 1, 2, 99, NUM_LOCATIONS, &dup), DC_OK);
    CHECK_EQ_INT(incident_store_add(&store, dup), DC_ERR_DUPLICATE);
    CHECK_EQ_INT(store.count, 50);
    free(dup);             /* rejected: ownership stayed with us, so we free it */

    CHECK_EQ_INT(incident_store_add(&store, NULL), DC_ERR_INVALID);

    /* Visual check only: first 50 rows of a filtered listing (all are PENDING). */
    printf("\n");
    incident_store_print(&store, stdout, INC_PENDING);
    printf("\n");

    incident_store_free(&store);   /* frees all 50 incidents */
    CHECK(store.items == NULL && store.count == 0);
    incident_store_free(&store);   /* harmless second call */
    incident_store_free(NULL);
}

int main(void)
{
    test_names();
    test_validation();
    test_create();
    test_priority_rule();
    test_state_machine();
    test_store();
    return TEST_SUMMARY();
}
