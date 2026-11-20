/*
 * test_minheap.c — unit tests for the Dijkstra min-heap (Phase 2).
 *
 * Build and run:   make test
 */
#include "minheap.h"
#include "test_util.h"

#include <stddef.h>

static void test_init_validation(void)
{
    TEST_SECTION("minheap_init validation");
    MinHeap h;
    CHECK_EQ_INT(minheap_init(&h, 0), DC_ERR_INVALID);
    CHECK(h.data == NULL && h.size == 0);
    CHECK_EQ_INT(minheap_init(&h, -4), DC_ERR_INVALID);
    CHECK_EQ_INT(minheap_init(NULL, 4), DC_ERR_INVALID);

    CHECK_EQ_INT(minheap_init(&h, 4), DC_OK);
    CHECK(minheap_is_empty(&h));
    CHECK_EQ_INT(minheap_size(&h), 0);
    CHECK_EQ_INT(h.capacity, 4);
    minheap_free(&h);
    CHECK(h.data == NULL && h.capacity == 0);
    minheap_free(&h);        /* second free must be harmless */
    minheap_free(NULL);
}

static void test_empty_heap_errors(void)
{
    TEST_SECTION("pop/peek on an empty heap");
    MinHeap h;
    MinHeapEntry e;
    CHECK_EQ_INT(minheap_init(&h, 2), DC_OK);
    CHECK_EQ_INT(minheap_pop(&h, &e), DC_ERR_EMPTY);
    CHECK_EQ_INT(minheap_peek(&h, &e), DC_ERR_EMPTY);
    CHECK_EQ_INT(minheap_pop(&h, NULL), DC_ERR_INVALID);
    CHECK_EQ_INT(minheap_push(NULL, 1, 1), DC_ERR_INVALID);
    minheap_free(&h);
    /* pushing into a freed heap is rejected, not a crash */
    CHECK_EQ_INT(minheap_push(&h, 1, 1), DC_ERR_INVALID);
}

static void test_documented_example(void)
{
    TEST_SECTION("array layout matches the example in minheap.c");
    MinHeap h;
    CHECK_EQ_INT(minheap_init(&h, 4), DC_OK);
    minheap_push(&h, 4, 12);
    minheap_push(&h, 1, 6);
    minheap_push(&h, 3, 2);
    minheap_push(&h, 7, 8);
    CHECK_EQ_INT(h.size, 4);
    CHECK(h.data[0].vertex == 3 && h.data[0].dist == 2);
    CHECK(h.data[1].vertex == 7 && h.data[1].dist == 8);
    CHECK(h.data[2].vertex == 1 && h.data[2].dist == 6);
    CHECK(h.data[3].vertex == 4 && h.data[3].dist == 12);
    CHECK(minheap_check_invariant(&h));

    MinHeapEntry e;
    CHECK_EQ_INT(minheap_peek(&h, &e), DC_OK);
    CHECK_EQ_INT(e.vertex, 3);
    CHECK_EQ_INT(h.size, 4);            /* peek does not remove */

    const int expected_order[4] = {3, 1, 7, 4};   /* dist 2, 6, 8, 12 */
    for (int k = 0; k < 4; k++) {
        CHECK_EQ_INT(minheap_pop(&h, &e), DC_OK);
        CHECK_EQ_INT(e.vertex, expected_order[k]);
        CHECK(minheap_check_invariant(&h));
    }
    CHECK(minheap_is_empty(&h));
    minheap_free(&h);
}

static void test_tie_break_and_duplicates(void)
{
    TEST_SECTION("equal distances pop in vertex order; duplicates allowed");
    MinHeap h;
    MinHeapEntry e;
    CHECK_EQ_INT(minheap_init(&h, 8), DC_OK);
    minheap_push(&h, 9, 5);
    minheap_push(&h, 2, 5);
    minheap_push(&h, 6, 5);
    minheap_push(&h, 2, 5);   /* the same pair twice: Dijkstra's lazy heap does this */
    minheap_push(&h, 0, 7);

    const int expected_vertex[5] = {2, 2, 6, 9, 0};
    for (int k = 0; k < 5; k++) {
        CHECK_EQ_INT(minheap_pop(&h, &e), DC_OK);
        CHECK_EQ_INT(e.vertex, expected_vertex[k]);
    }
    minheap_free(&h);
}

static void test_growth(void)
{
    TEST_SECTION("heap grows from capacity 1");
    MinHeap h;
    MinHeapEntry e;
    CHECK_EQ_INT(minheap_init(&h, 1), DC_OK);
    int failures = 0;
    for (int v = 100; v >= 1; v--) {           /* push in decreasing order */
        if (minheap_push(&h, v, v * 10) != DC_OK)
            failures++;
    }
    CHECK_EQ_INT(failures, 0);
    CHECK_EQ_INT(h.size, 100);
    CHECK(h.capacity >= 100);
    CHECK(minheap_check_invariant(&h));
    CHECK_EQ_INT(minheap_pop(&h, &e), DC_OK);
    CHECK(e.vertex == 1 && e.dist == 10);
    minheap_free(&h);
}

static void test_random_stress(void)
{
    TEST_SECTION("1000 random pushes come out in non-decreasing order");
    MinHeap h;
    MinHeapEntry e;
    CHECK_EQ_INT(minheap_init(&h, 4), DC_OK);

    uint32_t seed = 12345u;
    int invariant_failures = 0;
    for (int k = 0; k < 1000; k++) {
        int vertex = test_random(&seed, 50);
        int dist = test_random(&seed, 500);
        minheap_push(&h, vertex, dist);
        if (!minheap_check_invariant(&h))
            invariant_failures++;
    }
    CHECK_EQ_INT(invariant_failures, 0);

    int order_failures = 0, popped = 0;
    MinHeapEntry previous = {-1, -1};
    while (minheap_pop(&h, &e) == DC_OK) {
        if (popped > 0) {
            int out_of_order = e.dist < previous.dist ||
                               (e.dist == previous.dist && e.vertex < previous.vertex);
            if (out_of_order)
                order_failures++;
        }
        if (!minheap_check_invariant(&h))
            invariant_failures++;
        previous = e;
        popped++;
    }
    CHECK_EQ_INT(popped, 1000);
    CHECK_EQ_INT(order_failures, 0);
    CHECK_EQ_INT(invariant_failures, 0);
    minheap_free(&h);
}

static void test_interleaved_operations(void)
{
    TEST_SECTION("interleaved push/pop (as in Dijkstra)");
    MinHeap h;
    MinHeapEntry e;
    CHECK_EQ_INT(minheap_init(&h, 2), DC_OK);
    minheap_push(&h, 5, 0);
    CHECK_EQ_INT(minheap_pop(&h, &e), DC_OK);
    CHECK_EQ_INT(e.vertex, 5);
    minheap_push(&h, 7, 8);
    minheap_push(&h, 3, 2);
    minheap_push(&h, 1, 6);
    CHECK_EQ_INT(minheap_pop(&h, &e), DC_OK);
    CHECK_EQ_INT(e.vertex, 3);
    minheap_push(&h, 0, 9);
    minheap_push(&h, 6, 11);
    CHECK_EQ_INT(minheap_pop(&h, &e), DC_OK);
    CHECK_EQ_INT(e.vertex, 1);
    CHECK_EQ_INT(minheap_size(&h), 3);
    CHECK(minheap_check_invariant(&h));
    minheap_free(&h);
}

int main(void)
{
    test_init_validation();
    test_empty_heap_errors();
    test_documented_example();
    test_tie_break_and_duplicates();
    test_growth();
    test_random_stress();
    test_interleaved_operations();
    return TEST_SUMMARY();
}
