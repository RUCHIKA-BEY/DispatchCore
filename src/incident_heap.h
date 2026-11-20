/*
 * incident_heap.h — binary MAX-heap of Incident pointers: the priority queue
 * of pending emergencies.
 *
 * "Max" is defined by incident_higher_priority(): the root data[0] is the
 * incident that must be served first (highest severity, then earliest).
 *
 * Array layout (0-based):  parent(i) = (i-1)/2,  left(i) = 2i+1,  right(i) = 2i+2
 * Invariant: for every i > 0, data[i] does NOT have higher priority than
 *            data[parent(i)].
 *
 * The heap stores BORROWED pointers. It never frees an Incident; the
 * IncidentStore owns them. incident_heap_free() frees only the pointer array.
 *
 * Cancelled incidents are removed lazily: their status changes to CANCELLED
 * while they stay in the heap; the dispatch engine (Phase 6) discards any
 * non-PENDING incident it extracts.
 *
 * Depends on: incident.h, common.h.
 */
#ifndef DC_INCIDENT_HEAP_H
#define DC_INCIDENT_HEAP_H

#include <stdio.h>
#include "common.h"
#include "incident.h"

typedef struct {
    Incident **data;   /* borrowed pointers; data[0] = highest priority */
    int size;
    int capacity;      /* doubles when full */
} IncidentHeap;

/* Prepares an empty heap (initial_capacity >= 1). On failure data == NULL. */
DcStatus incident_heap_init(IncidentHeap *h, int initial_capacity);

/* Frees the pointer array only (NOT the incidents). Safe on NULL / twice. */
void incident_heap_free(IncidentHeap *h);

/* Inserts a PENDING incident. O(log n) amortised.
 * Errors: DC_ERR_INVALID (NULL), DC_ERR_STATE (status is not PENDING),
 *         DC_ERR_NOMEM. */
DcStatus incident_heap_insert(IncidentHeap *h, Incident *inc);

/* Removes the highest-priority incident and returns it in *out. O(log n).
 * DC_ERR_EMPTY if the heap is empty (then *out is NULL). */
DcStatus incident_heap_extract_max(IncidentHeap *h, Incident **out);

/* Returns the highest-priority incident without removing it. O(1). */
DcStatus incident_heap_peek(const IncidentHeap *h, Incident **out);

int incident_heap_size(const IncidentHeap *h);
int incident_heap_is_empty(const IncidentHeap *h);

/* Test helper: 1 if the max-heap invariant holds everywhere. O(n). */
int incident_heap_check_invariant(const IncidentHeap *h);

/* Makes an independent copy of the pointer array (same borrowed incidents).
 * dst must not be initialised; free it with incident_heap_free. O(n). */
DcStatus incident_heap_copy(const IncidentHeap *src, IncidentHeap *dst);

/* Debug view: the raw array with each slot's index and parent index.
 * This is the heap as it really is — NOT sorted. O(n). */
void incident_heap_print_array(const IncidentHeap *h, FILE *out);

/* Prints incidents in true priority order by extracting from a COPY, so
 * the real heap is unchanged. With pending_only = 1, lazily-deleted
 * (non-PENDING) entries are skipped. O(n log n).
 * Returns the number of incidents printed, or -1 if out of memory. */
int incident_heap_print_priority_order(const IncidentHeap *h, FILE *out, int pending_only);

#endif /* DC_INCIDENT_HEAP_H */
