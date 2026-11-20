/*
 * test_history.c — unit tests for the allocation history linked list
 * (Phase 6).
 *
 * Build and run:   make test
 */
#include "history.h"
#include "test_util.h"

#include <stdlib.h>

/* Makes a malloc'd route array (the history takes ownership of it). */
static int *make_route(const int *vertices, int len)
{
    int *route = malloc((size_t)len * sizeof *route);
    if (route != NULL)
        for (int i = 0; i < len; i++)
            route[i] = vertices[i];
    return route;
}

static void test_names(void)
{
    TEST_SECTION("allocation status names");
    AllocationStatus s = ALLOC_STATUS_COUNT;
    CHECK_STR_EQ(allocation_status_name(ALLOC_ACTIVE), "ACTIVE");
    CHECK_STR_EQ(allocation_status_name(ALLOC_COMPLETED), "COMPLETED");
    CHECK_STR_EQ(allocation_status_name(ALLOC_STATUS_COUNT), "?");
    CHECK_EQ_INT(allocation_status_from_name("COMPLETED", &s), DC_OK);
    CHECK_EQ_INT(s, ALLOC_COMPLETED);
    CHECK_EQ_INT(allocation_status_from_name("DONE", &s), DC_ERR_NOT_FOUND);
    CHECK_EQ_INT(allocation_status_from_name(NULL, &s), DC_ERR_INVALID);
}

static void test_append_and_order(void)
{
    TEST_SECTION("append keeps order; tail pointer; find");
    AllocationList list;
    history_init(&list);
    CHECK(list.head == NULL && list.tail == NULL && list.count == 0);

    const int r1[3] = {0, 3, 5};
    const int r2[1] = {4};                 /* unit already at the scene */
    const int r3[2] = {2, 1};
    Allocation *a1 = history_append(&list, 1, 1001, 1, 0, 5, 9, make_route(r1, 3), 3, 7);
    Allocation *a2 = history_append(&list, 2, 1004, 4, 4, 4, 0, make_route(r2, 1), 1, 8);
    Allocation *a3 = history_append(&list, 3, 1007, 2, 2, 1, 3, make_route(r3, 2), 2, 9);
    CHECK(a1 != NULL && a2 != NULL && a3 != NULL);
    CHECK_EQ_INT(list.count, 3);
    CHECK(list.head == a1);
    CHECK(list.tail == a3);
    CHECK(a1 != NULL && a1->next == a2);
    CHECK(a3 != NULL && a3->next == NULL);
    if (a1 != NULL) {
        CHECK_EQ_INT(a1->status, ALLOC_ACTIVE);
        CHECK_EQ_INT(a1->complete_seq, -1);
        CHECK_EQ_INT(a1->route_len, 3);
        CHECK(a1->route != NULL && a1->route[2] == 5);
    }

    CHECK(history_find(&list, 2) == a2);
    CHECK(history_find(&list, 4) == NULL);
    CHECK(history_find_active_for_incident(&list, 1007) == a3);
    CHECK(history_find_active_for_incident(&list, 1002) == NULL);

    printf("\n");
    history_print(&list, stdout, -1);
    printf("\n");

    history_free(&list);       /* frees 3 nodes and 3 route arrays */
    CHECK(list.head == NULL && list.tail == NULL && list.count == 0);
    history_free(&list);       /* harmless */
    history_free(NULL);
}

static void test_append_rejects_bad_input(void)
{
    TEST_SECTION("invalid appends are refused and the caller keeps the route");
    AllocationList list;
    history_init(&list);
    const int r[3] = {0, 3, 5};
    int *route = make_route(r, 3);
    CHECK(route != NULL);

    CHECK(history_append(&list, 0, 1001, 1, 0, 5, 9, route, 3, 0) == NULL);     /* id */
    CHECK(history_append(&list, 1, 0, 1, 0, 5, 9, route, 3, 0) == NULL);        /* incident */
    CHECK(history_append(&list, 1, 1001, 0, 0, 5, 9, route, 3, 0) == NULL);     /* resource */
    CHECK(history_append(&list, 1, 1001, 1, 0, 5, -1, route, 3, 0) == NULL);    /* distance */
    CHECK(history_append(&list, 1, 1001, 1, 0, 5, 9, NULL, 3, 0) == NULL);      /* no route */
    CHECK(history_append(&list, 1, 1001, 1, 0, 5, 9, route, 0, 0) == NULL);     /* empty */
    CHECK(history_append(&list, 1, 1001, 1, 3, 5, 9, route, 3, 0) == NULL);     /* wrong start */
    CHECK(history_append(&list, 1, 1001, 1, 0, 6, 9, route, 3, 0) == NULL);     /* wrong end */
    CHECK(history_append(NULL, 1, 1001, 1, 0, 5, 9, route, 3, 0) == NULL);
    CHECK_EQ_INT(list.count, 0);
    CHECK(list.head == NULL);

    free(route);               /* every append failed: we still own it */
    history_free(&list);
}

static void test_completion_and_counts(void)
{
    TEST_SECTION("ACTIVE -> COMPLETED; counting by status");
    AllocationList list;
    history_init(&list);
    const int r[2] = {1, 2};
    Allocation *a1 = history_append(&list, 1, 1001, 1, 1, 2, 3, make_route(r, 2), 2, 5);
    Allocation *a2 = history_append(&list, 2, 1002, 2, 1, 2, 3, make_route(r, 2), 2, 6);
    CHECK(a1 != NULL && a2 != NULL);
    if (a1 == NULL || a2 == NULL) {
        history_free(&list);
        return;
    }

    CHECK_EQ_INT(history_mark_completed(a1, 4), DC_ERR_INVALID);  /* before dispatch time */
    CHECK_EQ_INT(history_mark_completed(a1, 10), DC_OK);
    CHECK_EQ_INT(a1->status, ALLOC_COMPLETED);
    CHECK_EQ_INT(a1->complete_seq, 10);
    CHECK_EQ_INT(history_mark_completed(a1, 11), DC_ERR_STATE);   /* only once */
    CHECK_EQ_INT(history_mark_completed(NULL, 11), DC_ERR_INVALID);

    /* a completed allocation is no longer "active" for its incident */
    CHECK(history_find_active_for_incident(&list, 1001) == NULL);
    CHECK(history_find_active_for_incident(&list, 1002) == a2);

    CHECK_EQ_INT(history_count_status(&list, -1), 2);
    CHECK_EQ_INT(history_count_status(&list, ALLOC_ACTIVE), 1);
    CHECK_EQ_INT(history_count_status(&list, ALLOC_COMPLETED), 1);
    history_free(&list);
}

static void test_many_appends(void)
{
    TEST_SECTION("5000 appends: O(1) tail append keeps order");
    AllocationList list;
    history_init(&list);
    int failures = 0;
    for (int k = 1; k <= 5000; k++) {
        const int r[2] = {k % 10, (k + 1) % 10};
        int *route = make_route(r, 2);
        if (history_append(&list, k, 1000 + k, 1, r[0], r[1], 5, route, 2, k) == NULL) {
            failures++;
            free(route);
        }
    }
    CHECK_EQ_INT(failures, 0);
    CHECK_EQ_INT(list.count, 5000);
    int order_errors = 0, expected = 1;
    for (const Allocation *a = list.head; a != NULL; a = a->next)
        if (a->id != expected++)
            order_errors++;
    CHECK_EQ_INT(order_errors, 0);
    CHECK(list.tail != NULL && list.tail->id == 5000);
    history_free(&list);
}

int main(void)
{
    test_names();
    test_append_and_order();
    test_append_rejects_bad_input();
    test_completion_and_counts();
    test_many_appends();
    return TEST_SUMMARY();
}
