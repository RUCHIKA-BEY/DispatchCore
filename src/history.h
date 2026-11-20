/*
 * history.h — allocation records kept in a singly linked list.
 *
 *   head -> [alloc 1] -> [alloc 2] -> [alloc 3] -> NULL
 *                                        ^
 *   tail ---------------------------------+     (O(1) append)
 *
 * One Allocation is created per successful dispatch and is never removed:
 * on completion its status changes from ACTIVE to COMPLETED. The list is
 * therefore the permanent, ordered history of the simulation.
 *
 * OWNERSHIP: the list owns every Allocation node AND each node's route
 * array. Records refer to incidents and resources by ID (not pointer), so
 * the history never holds pointers into the stores.
 *
 * Depends on: common.h only.
 */
#ifndef DC_HISTORY_H
#define DC_HISTORY_H

#include <stdio.h>
#include "common.h"

typedef enum {
    ALLOC_ACTIVE = 0,
    ALLOC_COMPLETED,
    ALLOC_STATUS_COUNT
} AllocationStatus;

typedef struct Allocation {
    int   id;                  /* unique, >= 1, in dispatch order          */
    int   incident_id;
    int   resource_id;
    int   from_location;       /* resource position at dispatch            */
    int   to_location;         /* incident location                        */
    int   distance;            /* route length                             */
    int  *route;               /* owned: vertices from_location..to_location */
    int   route_len;           /* >= 1 (1 when the unit is already there)  */
    AllocationStatus status;
    long  dispatch_seq;        /* clock value at dispatch                  */
    long  complete_seq;        /* clock value at completion, -1 while active */
    struct Allocation *next;
} Allocation;

typedef struct {
    Allocation *head;
    Allocation *tail;
    int count;
} AllocationList;

const char *allocation_status_name(AllocationStatus status);   /* "ACTIVE", ... or "?" */
DcStatus allocation_status_from_name(const char *name, AllocationStatus *out);

void history_init(AllocationList *list);

/* Frees every node and every route array. Safe on NULL and twice. */
void history_free(AllocationList *list);

/* Appends a new ACTIVE allocation. On success the list TAKES OWNERSHIP of
 * `route` (do not free it afterwards) and the new node is returned.
 * Returns NULL on invalid input (id/incident/resource < 1, distance < 0,
 * route NULL, route_len < 1, route not starting at from_location or not
 * ending at to_location) or out of memory — in that case the caller still
 * owns `route`. */
Allocation *history_append(AllocationList *list, int id, int incident_id,
                           int resource_id, int from_location, int to_location,
                           int distance, int *route, int route_len, long dispatch_seq);

Allocation *history_find(const AllocationList *list, int allocation_id);           /* O(h) */
Allocation *history_find_active_for_incident(const AllocationList *list, int incident_id); /* O(h) */

/* ACTIVE -> COMPLETED. DC_ERR_STATE if already completed. */
DcStatus history_mark_completed(Allocation *a, long complete_seq);

/* Number of allocations with the given status (-1 = all). */
int history_count_status(const AllocationList *list, int status_filter);

/* Writes a route as "0 > 3 > 5". */
void history_print_route_ids(FILE *out, const int *route, int route_len);

/* Lists allocations oldest first; status_filter = -1 for all. */
void history_print(const AllocationList *list, FILE *out, int status_filter);

#endif /* DC_HISTORY_H */
