/*
 * history.c — allocation history as a singly linked list with a tail
 * pointer (see history.h).
 */
#include "history.h"

#include <stdlib.h>
#include <string.h>

static const char *const STATUS_NAMES[ALLOC_STATUS_COUNT] = { "ACTIVE", "COMPLETED" };

const char *allocation_status_name(AllocationStatus status)
{
    if ((int)status < 0 || (int)status >= ALLOC_STATUS_COUNT)
        return "?";
    return STATUS_NAMES[status];
}

DcStatus allocation_status_from_name(const char *name, AllocationStatus *out)
{
    if (name == NULL || out == NULL)
        return DC_ERR_INVALID;
    for (int s = 0; s < ALLOC_STATUS_COUNT; s++) {
        if (strcmp(name, STATUS_NAMES[s]) == 0) {
            *out = (AllocationStatus)s;
            return DC_OK;
        }
    }
    return DC_ERR_NOT_FOUND;
}

void history_init(AllocationList *list)
{
    if (list == NULL)
        return;
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}

void history_free(AllocationList *list)
{
    if (list == NULL)
        return;
    Allocation *a = list->head;
    while (a != NULL) {
        Allocation *next = a->next;   /* save before freeing the node */
        free(a->route);               /* the route belongs to the node */
        free(a);
        a = next;
    }
    history_init(list);               /* back to a valid empty list */
}

Allocation *history_append(AllocationList *list, int id, int incident_id,
                           int resource_id, int from_location, int to_location,
                           int distance, int *route, int route_len, long dispatch_seq)
{
    if (list == NULL || id < 1 || incident_id < 1 || resource_id < 1 || distance < 0)
        return NULL;
    if (route == NULL || route_len < 1)
        return NULL;
    if (route[0] != from_location || route[route_len - 1] != to_location)
        return NULL;   /* the route must really connect the two locations */

    Allocation *a = malloc(sizeof *a);
    if (a == NULL)
        return NULL;   /* caller still owns route */

    a->id = id;
    a->incident_id = incident_id;
    a->resource_id = resource_id;
    a->from_location = from_location;
    a->to_location = to_location;
    a->distance = distance;
    a->route = route;              /* ownership moves into the node */
    a->route_len = route_len;
    a->status = ALLOC_ACTIVE;
    a->dispatch_seq = dispatch_seq;
    a->complete_seq = -1;
    a->next = NULL;

    /* Append at the tail in O(1). Without the tail pointer we would have to
     * walk the whole list to find the last node: O(h). */
    if (list->tail == NULL)
        list->head = a;            /* first node */
    else
        list->tail->next = a;
    list->tail = a;
    list->count++;
    return a;
}

Allocation *history_find(const AllocationList *list, int allocation_id)
{
    if (list == NULL)
        return NULL;
    for (Allocation *a = list->head; a != NULL; a = a->next)
        if (a->id == allocation_id)
            return a;
    return NULL;
}

Allocation *history_find_active_for_incident(const AllocationList *list, int incident_id)
{
    if (list == NULL)
        return NULL;
    for (Allocation *a = list->head; a != NULL; a = a->next)
        if (a->incident_id == incident_id && a->status == ALLOC_ACTIVE)
            return a;
    return NULL;
}

DcStatus history_mark_completed(Allocation *a, long complete_seq)
{
    if (a == NULL || complete_seq < a->dispatch_seq)
        return DC_ERR_INVALID;
    if (a->status != ALLOC_ACTIVE)
        return DC_ERR_STATE;
    a->status = ALLOC_COMPLETED;
    a->complete_seq = complete_seq;
    return DC_OK;
}

int history_count_status(const AllocationList *list, int status_filter)
{
    if (list == NULL)
        return 0;
    if (status_filter < 0)
        return list->count;
    int n = 0;
    for (const Allocation *a = list->head; a != NULL; a = a->next)
        if ((int)a->status == status_filter)
            n++;
    return n;
}

void history_print_route_ids(FILE *out, const int *route, int route_len)
{
    if (out == NULL || route == NULL)
        return;
    for (int i = 0; i < route_len; i++) {
        if (i > 0)
            fputs(" > ", out);
        fprintf(out, "%d", route[i]);
    }
}

void history_print(const AllocationList *list, FILE *out, int status_filter)
{
    if (list == NULL || out == NULL)
        return;
    fprintf(out, "%-5s %-8s %-8s %-10s %8s %8s %8s  %s\n",
            "ALLOC", "INCIDENT", "RESOURCE", "STATUS", "DISTANCE", "SENT", "DONE", "ROUTE");
    int shown = 0;
    for (const Allocation *a = list->head; a != NULL; a = a->next) {
        if (status_filter >= 0 && (int)a->status != status_filter)
            continue;
        fprintf(out, "%-5d %-8d %-8d %-10s %8d %8ld ",
                a->id, a->incident_id, a->resource_id,
                allocation_status_name(a->status), a->distance, a->dispatch_seq);
        if (a->complete_seq < 0)
            fprintf(out, "%8s  ", "-");
        else
            fprintf(out, "%8ld  ", a->complete_seq);
        history_print_route_ids(out, a->route, a->route_len);
        fprintf(out, "\n");
        shown++;
    }
    if (shown == 0)
        fprintf(out, "(no allocations)\n");
}
