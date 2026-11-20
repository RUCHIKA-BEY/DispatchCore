/*
 * minheap.c — binary min-heap implementation (see minheap.h).
 *
 * Example: after pushing (v4,12) (v1,6) (v3,2) (v7,8) the array is
 *
 *   index:   0       1       2       3
 *   data:  (3,2)  (7,8)  (1,6)  (4,12)
 *
 *              (3,2)
 *             /     \
 *          (7,8)   (1,6)
 *          /
 *       (4,12)
 *
 * Each parent's dist is <= its children's, so the root holds the minimum.
 * Siblings are NOT ordered relative to each other — a heap is not sorted.
 */
#include "minheap.h"

#include <limits.h>
#include <stdlib.h>

/* 1 if a must come out of the heap before b. */
static int entry_less(MinHeapEntry a, MinHeapEntry b)
{
    if (a.dist != b.dist)
        return a.dist < b.dist;
    return a.vertex < b.vertex;   /* deterministic tie-break */
}

static void swap_entries(MinHeapEntry *a, MinHeapEntry *b)
{
    MinHeapEntry tmp = *a;
    *a = *b;
    *b = tmp;
}

/* Moves data[i] up while it is smaller than its parent. */
static void sift_up(MinHeap *h, int i)
{
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (!entry_less(h->data[i], h->data[parent]))
            break;                                  /* invariant restored */
        swap_entries(&h->data[i], &h->data[parent]);
        i = parent;
    }
}

/* Moves data[i] down while one of its children is smaller than it. */
static void sift_down(MinHeap *h, int i)
{
    for (;;) {
        int left = 2 * i + 1;
        if (left >= h->size)
            break;                                  /* i is a leaf */

        int smallest = left;
        int right = left + 1;
        if (right < h->size && entry_less(h->data[right], h->data[left]))
            smallest = right;

        if (!entry_less(h->data[smallest], h->data[i]))
            break;                                  /* invariant restored */
        swap_entries(&h->data[i], &h->data[smallest]);
        i = smallest;
    }
}

/* Doubles the capacity. Uses a temporary pointer so that a failed realloc
 * does not lose the existing array. */
static DcStatus grow(MinHeap *h)
{
    if (h->capacity > INT_MAX / 2)
        return DC_ERR_NOMEM;   /* doubling would overflow int */

    int new_capacity = h->capacity * 2;
    MinHeapEntry *bigger = realloc(h->data, (size_t)new_capacity * sizeof *bigger);
    if (bigger == NULL)
        return DC_ERR_NOMEM;   /* h->data is still valid and unchanged */

    h->data = bigger;
    h->capacity = new_capacity;
    return DC_OK;
}

DcStatus minheap_init(MinHeap *h, int initial_capacity)
{
    if (h == NULL)
        return DC_ERR_INVALID;

    h->data = NULL;
    h->size = 0;
    h->capacity = 0;
    if (initial_capacity < 1)
        return DC_ERR_INVALID;

    h->data = malloc((size_t)initial_capacity * sizeof *h->data);
    if (h->data == NULL)
        return DC_ERR_NOMEM;
    h->capacity = initial_capacity;
    return DC_OK;
}

void minheap_free(MinHeap *h)
{
    if (h == NULL)
        return;
    free(h->data);
    h->data = NULL;   /* prevents a double free if called twice */
    h->size = 0;
    h->capacity = 0;
}

DcStatus minheap_push(MinHeap *h, int vertex, int dist)
{
    if (h == NULL || h->data == NULL)
        return DC_ERR_INVALID;

    if (h->size == h->capacity) {
        DcStatus st = grow(h);
        if (st != DC_OK)
            return st;
    }

    /* 1. place the new entry in the first free slot (keeps the tree complete)
     * 2. sift it up to restore the heap order */
    h->data[h->size].vertex = vertex;
    h->data[h->size].dist = dist;
    h->size++;
    sift_up(h, h->size - 1);
    return DC_OK;
}

DcStatus minheap_pop(MinHeap *h, MinHeapEntry *out)
{
    if (h == NULL || out == NULL)
        return DC_ERR_INVALID;
    if (h->size == 0)
        return DC_ERR_EMPTY;

    *out = h->data[0];

    /* 1. move the last entry into the root (keeps the tree complete)
     * 2. sift it down to restore the heap order */
    h->size--;
    if (h->size > 0) {
        h->data[0] = h->data[h->size];
        sift_down(h, 0);
    }
    return DC_OK;
}

DcStatus minheap_peek(const MinHeap *h, MinHeapEntry *out)
{
    if (h == NULL || out == NULL)
        return DC_ERR_INVALID;
    if (h->size == 0)
        return DC_ERR_EMPTY;
    *out = h->data[0];
    return DC_OK;
}

int minheap_is_empty(const MinHeap *h)
{
    return h == NULL || h->size == 0;
}

int minheap_size(const MinHeap *h)
{
    return h == NULL ? 0 : h->size;
}

int minheap_check_invariant(const MinHeap *h)
{
    if (h == NULL)
        return 0;
    for (int i = 1; i < h->size; i++) {
        int parent = (i - 1) / 2;
        if (entry_less(h->data[i], h->data[parent]))
            return 0;   /* a child is smaller than its parent */
    }
    return 1;
}
