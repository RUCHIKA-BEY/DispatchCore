/*
 * incident_heap.c — binary max-heap of Incident pointers (see incident_heap.h).
 *
 * The structure mirrors minheap.c (Phase 2). The differences are the
 * element type (Incident * instead of {vertex, dist}) and the comparison
 * (incident_higher_priority instead of "smaller distance").
 *
 * Swapping two slots swaps two POINTERS (8 bytes each); the Incident records
 * themselves never move in memory.
 */
#include "incident_heap.h"

#include <limits.h>
#include <stdlib.h>

static void swap_slots(Incident **a, Incident **b)
{
    Incident *tmp = *a;
    *a = *b;
    *b = tmp;
}

/* Moves data[i] up while it outranks its parent ("heapify-up"). */
static void heapify_up(IncidentHeap *h, int i)
{
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (!incident_higher_priority(h->data[i], h->data[parent]))
            break;
        swap_slots(&h->data[i], &h->data[parent]);
        i = parent;
    }
}

/* Moves data[i] down while a child outranks it ("heapify-down"). */
static void heapify_down(IncidentHeap *h, int i)
{
    for (;;) {
        int left = 2 * i + 1;
        if (left >= h->size)
            break;                                   /* leaf */

        int best = left;                             /* higher-priority child */
        int right = left + 1;
        if (right < h->size && incident_higher_priority(h->data[right], h->data[left]))
            best = right;

        if (!incident_higher_priority(h->data[best], h->data[i]))
            break;
        swap_slots(&h->data[i], &h->data[best]);
        i = best;
    }
}

static DcStatus grow(IncidentHeap *h)
{
    if (h->capacity > INT_MAX / 2)
        return DC_ERR_NOMEM;
    int new_capacity = h->capacity * 2;
    Incident **bigger = realloc(h->data, (size_t)new_capacity * sizeof *bigger);
    if (bigger == NULL)
        return DC_ERR_NOMEM;          /* old array still valid */
    h->data = bigger;
    h->capacity = new_capacity;
    return DC_OK;
}

DcStatus incident_heap_init(IncidentHeap *h, int initial_capacity)
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

void incident_heap_free(IncidentHeap *h)
{
    if (h == NULL)
        return;
    free(h->data);       /* the array of pointers only — incidents are borrowed */
    h->data = NULL;
    h->size = 0;
    h->capacity = 0;
}

DcStatus incident_heap_insert(IncidentHeap *h, Incident *inc)
{
    if (h == NULL || h->data == NULL || inc == NULL)
        return DC_ERR_INVALID;
    if (inc->status != INC_PENDING)
        return DC_ERR_STATE;      /* only pending incidents wait in the queue */

    if (h->size == h->capacity) {
        DcStatus st = grow(h);
        if (st != DC_OK)
            return st;
    }
    h->data[h->size] = inc;       /* first free slot keeps the tree complete */
    h->size++;
    heapify_up(h, h->size - 1);
    return DC_OK;
}

DcStatus incident_heap_extract_max(IncidentHeap *h, Incident **out)
{
    if (out == NULL)
        return DC_ERR_INVALID;
    *out = NULL;
    if (h == NULL)
        return DC_ERR_INVALID;
    if (h->size == 0)
        return DC_ERR_EMPTY;

    *out = h->data[0];
    h->size--;
    if (h->size > 0) {
        h->data[0] = h->data[h->size];   /* last slot moves to the root */
        heapify_down(h, 0);
    }
    return DC_OK;
}

DcStatus incident_heap_peek(const IncidentHeap *h, Incident **out)
{
    if (out == NULL)
        return DC_ERR_INVALID;
    *out = NULL;
    if (h == NULL)
        return DC_ERR_INVALID;
    if (h->size == 0)
        return DC_ERR_EMPTY;
    *out = h->data[0];
    return DC_OK;
}

int incident_heap_size(const IncidentHeap *h)
{
    return h == NULL ? 0 : h->size;
}

int incident_heap_is_empty(const IncidentHeap *h)
{
    return h == NULL || h->size == 0;
}

int incident_heap_check_invariant(const IncidentHeap *h)
{
    if (h == NULL)
        return 0;
    for (int i = 1; i < h->size; i++) {
        if (incident_higher_priority(h->data[i], h->data[(i - 1) / 2]))
            return 0;   /* a child outranks its parent */
    }
    return 1;
}

DcStatus incident_heap_copy(const IncidentHeap *src, IncidentHeap *dst)
{
    if (src == NULL || dst == NULL)
        return DC_ERR_INVALID;
    int capacity = src->size > 0 ? src->size : 1;
    DcStatus st = incident_heap_init(dst, capacity);
    if (st != DC_OK)
        return st;
    for (int i = 0; i < src->size; i++)
        dst->data[i] = src->data[i];   /* copying the array keeps the heap shape */
    dst->size = src->size;
    return DC_OK;
}

void incident_heap_print_array(const IncidentHeap *h, FILE *out)
{
    if (out == NULL)
        return;
    if (h == NULL || h->size == 0) {
        fprintf(out, "(heap is empty)\n");
        return;
    }
    fprintf(out, "%-5s %-6s %-6s %-4s %6s  %s\n",
            "SLOT", "PARENT", "ID", "SEV", "SEQ", "STATUS");
    for (int i = 0; i < h->size; i++) {
        const Incident *inc = h->data[i];
        if (i == 0)
            fprintf(out, "%-5d %-6s ", i, "-");
        else
            fprintf(out, "%-5d %-6d ", i, (i - 1) / 2);
        fprintf(out, "%-6d %-4d %6ld  %s\n", inc->id, inc->severity,
                inc->reported_seq, incident_status_name(inc->status));
    }
}

int incident_heap_print_priority_order(const IncidentHeap *h, FILE *out, int pending_only)
{
    if (h == NULL || out == NULL)
        return 0;

    IncidentHeap copy;
    if (incident_heap_copy(h, &copy) != DC_OK)
        return -1;

    incident_print_header(out);
    int printed = 0;
    Incident *inc = NULL;
    while (incident_heap_extract_max(&copy, &inc) == DC_OK) {
        if (pending_only && inc->status != INC_PENDING)
            continue;            /* lazily deleted (e.g. cancelled) */
        incident_print_row(out, inc, NULL);
        printed++;
    }
    if (printed == 0)
        fprintf(out, "(no pending incidents)\n");

    incident_heap_free(&copy);   /* frees the copied array, not the incidents */
    return printed;
}
