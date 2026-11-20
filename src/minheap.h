/*
 * minheap.h — binary min-heap of (vertex, distance) pairs, used as the
 * priority queue inside Dijkstra's algorithm.
 *
 * Ordering: smaller `dist` comes out first; on equal `dist`, the smaller
 * `vertex` comes out first. This makes the pop order fully deterministic.
 *
 * Array layout (0-based):  parent(i) = (i-1)/2,  left(i) = 2i+1,  right(i) = 2i+2
 * Invariant: for every i > 0, data[parent(i)] is not "greater" than data[i].
 *
 * Depends on: common.h only.
 */
#ifndef DC_MINHEAP_H
#define DC_MINHEAP_H

#include "common.h"

typedef struct {
    int vertex;   /* graph vertex index          */
    int dist;     /* tentative distance from the source */
} MinHeapEntry;

typedef struct {
    MinHeapEntry *data;   /* data[0] is always the smallest entry */
    int size;             /* number of entries currently stored   */
    int capacity;         /* allocated slots; doubles when full   */
} MinHeap;

/* Prepares an empty heap with room for initial_capacity entries (>= 1).
 * On failure the heap is left in a safe empty state (data == NULL). */
DcStatus minheap_init(MinHeap *h, int initial_capacity);

/* Releases the array and resets the heap to empty. Safe on NULL or on an
 * already-freed heap. */
void minheap_free(MinHeap *h);

/* Adds an entry, growing the array if needed. O(log n) (amortised). */
DcStatus minheap_push(MinHeap *h, int vertex, int dist);

/* Removes the smallest entry and copies it to *out. O(log n).
 * Returns DC_ERR_EMPTY if the heap is empty. */
DcStatus minheap_pop(MinHeap *h, MinHeapEntry *out);

/* Copies the smallest entry to *out without removing it. O(1). */
DcStatus minheap_peek(const MinHeap *h, MinHeapEntry *out);

int minheap_is_empty(const MinHeap *h);
int minheap_size(const MinHeap *h);

/* Test helper: 1 if the heap invariant holds for every element, else 0. O(n). */
int minheap_check_invariant(const MinHeap *h);

#endif /* DC_MINHEAP_H */
