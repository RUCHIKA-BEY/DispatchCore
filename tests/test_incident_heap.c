/*
 * test_incident_heap.c — unit tests for the incident max-heap (Phase 3).
 *
 * The 7-incident example is traced by hand in docs/PHASE3_INCIDENTS.md.
 *
 * Build and run:   make test
 */
#include "incident.h"
#include "incident_heap.h"
#include "test_util.h"

#include <stdlib.h>

#define NUM_LOCATIONS 10

/* Creates an incident and hands it to the store (the owner).
 * Returns the borrowed pointer, or NULL on failure. */
static Incident *add_incident(IncidentStore *store, int id, IncidentType type,
                              int location, int severity, long seq)
{
    Incident *inc = NULL;
    if (incident_create(id, type, location, severity, seq, NUM_LOCATIONS, &inc) != DC_OK)
        return NULL;
    if (incident_store_add(store, inc) != DC_OK) {
        free(inc);
        return NULL;
    }
    return inc;
}

/* Compares the IDs currently in the heap array, slot by slot. */
static void check_heap_ids(const IncidentHeap *h, const int *expected, int n)
{
    CHECK_EQ_INT(h->size, n);
    int mismatches = 0;
    for (int i = 0; i < n && i < h->size; i++) {
        if (h->data[i]->id != expected[i]) {
            if (mismatches == 0)
                printf("  heap slot %d: got %d, expected %d\n", i, h->data[i]->id, expected[i]);
            mismatches++;
        }
    }
    CHECK_EQ_INT(mismatches, 0);
}

static void test_init_and_empty(void)
{
    TEST_SECTION("init, empty heap, invalid input");
    IncidentHeap h;
    Incident *out = NULL;
    CHECK_EQ_INT(incident_heap_init(&h, 0), DC_ERR_INVALID);
    CHECK_EQ_INT(incident_heap_init(NULL, 4), DC_ERR_INVALID);
    CHECK_EQ_INT(incident_heap_init(&h, 4), DC_OK);
    CHECK(incident_heap_is_empty(&h));

    Incident dummy;
    out = &dummy;
    CHECK_EQ_INT(incident_heap_extract_max(&h, &out), DC_ERR_EMPTY);
    CHECK(out == NULL);
    CHECK_EQ_INT(incident_heap_peek(&h, &out), DC_ERR_EMPTY);
    CHECK_EQ_INT(incident_heap_extract_max(&h, NULL), DC_ERR_INVALID);
    CHECK_EQ_INT(incident_heap_insert(&h, NULL), DC_ERR_INVALID);

    incident_heap_free(&h);
    incident_heap_free(&h);
    incident_heap_free(NULL);
    CHECK_EQ_INT(incident_heap_insert(&h, &dummy), DC_ERR_INVALID);   /* freed heap */
}

static void test_rejects_non_pending(void)
{
    TEST_SECTION("only PENDING incidents can be inserted");
    IncidentStore store;
    IncidentHeap h;
    CHECK_EQ_INT(incident_store_init(&store, 4), DC_OK);
    CHECK_EQ_INT(incident_heap_init(&h, 4), DC_OK);

    Incident *inc = add_incident(&store, 1, INC_FIRE, 2, 5, 1);
    CHECK(inc != NULL);
    if (inc != NULL) {
        CHECK_EQ_INT(incident_set_status(inc, INC_CANCELLED), DC_OK);
        CHECK_EQ_INT(incident_heap_insert(&h, inc), DC_ERR_STATE);
        CHECK_EQ_INT(incident_heap_size(&h), 0);
    }
    incident_heap_free(&h);
    incident_store_free(&store);
}

static void test_seven_incident_example(void)
{
    TEST_SECTION("7-incident example (hand trace in PHASE3_INCIDENTS.md)");
    IncidentStore store;
    IncidentHeap h;
    CHECK_EQ_INT(incident_store_init(&store, 8), DC_OK);
    CHECK_EQ_INT(incident_heap_init(&h, 2), DC_OK);   /* small: also exercises growth */

    /*            id    type                   loc sev seq */
    add_incident(&store, 1001, INC_MEDICAL,           5, 3, 1);
    add_incident(&store, 1002, INC_FIRE,              2, 5, 2);
    add_incident(&store, 1003, INC_ROAD_ACCIDENT,     6, 3, 3);
    add_incident(&store, 1004, INC_STRUCTURAL_RESCUE, 4, 4, 4);
    add_incident(&store, 1005, INC_MEDICAL,           0, 5, 5);
    add_incident(&store, 1006, INC_FIRE,              7, 1, 6);
    add_incident(&store, 1007, INC_ROAD_ACCIDENT,     1, 4, 7);
    CHECK_EQ_INT(store.count, 7);

    int insert_failures = 0;
    for (int i = 0; i < store.count; i++) {
        if (incident_heap_insert(&h, store.items[i]) != DC_OK)
            insert_failures++;
        if (!incident_heap_check_invariant(&h))
            insert_failures++;
    }
    CHECK_EQ_INT(insert_failures, 0);

    const int after_inserts[7] = {1002, 1005, 1007, 1001, 1004, 1006, 1003};
    check_heap_ids(&h, after_inserts, 7);

    printf("\n");
    incident_heap_print_array(&h, stdout);
    printf("\n");

    Incident *top = NULL;
    CHECK_EQ_INT(incident_heap_peek(&h, &top), DC_OK);
    CHECK(top != NULL && top->id == 1002);
    CHECK_EQ_INT(incident_heap_size(&h), 7);     /* peek does not remove */

    CHECK_EQ_INT(incident_heap_extract_max(&h, &top), DC_OK);
    CHECK(top != NULL && top->id == 1002);
    CHECK(top == store.items[1]);                /* the same record, not a copy */
    const int after_first_extract[6] = {1005, 1004, 1007, 1001, 1003, 1006};
    check_heap_ids(&h, after_first_extract, 6);

    const int remaining_order[6] = {1005, 1004, 1007, 1001, 1003, 1006};
    int order_mismatches = 0;
    for (int k = 0; k < 6; k++) {
        if (incident_heap_extract_max(&h, &top) != DC_OK || top->id != remaining_order[k])
            order_mismatches++;
        if (!incident_heap_check_invariant(&h))
            order_mismatches++;
    }
    CHECK_EQ_INT(order_mismatches, 0);
    CHECK(incident_heap_is_empty(&h));

    /* Extracting does not free or change the records: the store still owns
     * all 7 and they are still PENDING (dispatch changes status in Phase 6). */
    CHECK_EQ_INT(store.count, 7);
    CHECK_EQ_INT(store.items[1]->status, INC_PENDING);

    incident_heap_free(&h);
    incident_store_free(&store);
}

static void test_ties(void)
{
    TEST_SECTION("tie-breaking: equal severity -> earlier seq; equal seq -> lower ID");
    IncidentStore store;
    IncidentHeap h;
    CHECK_EQ_INT(incident_store_init(&store, 4), DC_OK);
    CHECK_EQ_INT(incident_heap_init(&h, 4), DC_OK);

    /* inserted in the "wrong" order on purpose */
    add_incident(&store, 30, INC_FIRE, 1, 4, 9);
    add_incident(&store, 20, INC_FIRE, 1, 4, 9);   /* same severity AND seq as 30 */
    add_incident(&store, 10, INC_FIRE, 1, 4, 3);
    for (int i = 0; i < store.count; i++)
        incident_heap_insert(&h, store.items[i]);

    const int expected[3] = {10, 20, 30};
    Incident *top = NULL;
    for (int k = 0; k < 3; k++) {
        CHECK_EQ_INT(incident_heap_extract_max(&h, &top), DC_OK);
        CHECK(top != NULL && top->id == expected[k]);
    }
    incident_heap_free(&h);
    incident_store_free(&store);
}

static void test_copy_and_priority_listing(void)
{
    TEST_SECTION("copy is independent; listing skips cancelled (lazy deletion)");
    IncidentStore store;
    IncidentHeap h;
    CHECK_EQ_INT(incident_store_init(&store, 4), DC_OK);
    CHECK_EQ_INT(incident_heap_init(&h, 4), DC_OK);

    add_incident(&store, 1, INC_MEDICAL, 5, 2, 1);
    add_incident(&store, 2, INC_FIRE, 2, 5, 2);
    add_incident(&store, 3, INC_STRUCTURAL_RESCUE, 4, 3, 3);
    for (int i = 0; i < store.count; i++)
        incident_heap_insert(&h, store.items[i]);

    IncidentHeap copy;
    Incident *top = NULL;
    CHECK_EQ_INT(incident_heap_copy(&h, &copy), DC_OK);
    CHECK_EQ_INT(incident_heap_extract_max(&copy, &top), DC_OK);
    CHECK_EQ_INT(incident_heap_size(&copy), 2);
    CHECK_EQ_INT(incident_heap_size(&h), 3);     /* original untouched */
    incident_heap_free(&copy);

    /* Cancel incident 3 while it is still inside the heap. */
    CHECK_EQ_INT(incident_set_status(store.items[2], INC_CANCELLED), DC_OK);

    printf("\n");
    CHECK_EQ_INT(incident_heap_print_priority_order(&h, stdout, 1), 2);  /* 2 and 1 */
    printf("\n");
    CHECK_EQ_INT(incident_heap_print_priority_order(&h, stdout, 0), 3);  /* all three */
    printf("\n");
    CHECK_EQ_INT(incident_heap_size(&h), 3);     /* printing never modifies the heap */
    CHECK(incident_heap_check_invariant(&h));

    incident_heap_free(&h);
    incident_store_free(&store);
}

static void test_random_stress(void)
{
    TEST_SECTION("2000 random incidents: invariant always holds, order is correct");
    enum { N = 2000 };
    IncidentStore store;
    IncidentHeap h;
    CHECK_EQ_INT(incident_store_init(&store, 16), DC_OK);
    CHECK_EQ_INT(incident_heap_init(&h, 1), DC_OK);

    uint32_t seed = 7u;
    int failures = 0;
    for (int k = 0; k < N; k++) {
        int severity = 1 + test_random(&seed, 5);
        int location = test_random(&seed, NUM_LOCATIONS);
        Incident *inc = add_incident(&store, k + 1, INC_MEDICAL, location, severity, k);
        if (inc == NULL || incident_heap_insert(&h, inc) != DC_OK)
            failures++;
        if (!incident_heap_check_invariant(&h))
            failures++;
    }
    CHECK_EQ_INT(failures, 0);
    CHECK_EQ_INT(incident_heap_size(&h), N);

    int order_errors = 0, extracted = 0;
    Incident *previous = NULL, *current = NULL;
    while (incident_heap_extract_max(&h, &current) == DC_OK) {
        /* the next incident must never outrank the one before it */
        if (previous != NULL && incident_higher_priority(current, previous))
            order_errors++;
        if (!incident_heap_check_invariant(&h))
            order_errors++;
        previous = current;
        extracted++;
    }
    CHECK_EQ_INT(extracted, N);
    CHECK_EQ_INT(order_errors, 0);

    incident_heap_free(&h);
    incident_store_free(&store);
}

int main(void)
{
    test_init_and_empty();
    test_rejects_non_pending();
    test_seven_incident_example();
    test_ties();
    test_copy_and_priority_listing();
    test_random_stress();
    return TEST_SUMMARY();
}
