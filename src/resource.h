/*
 * resource.h — emergency response units (ambulances, fire units, rescue units).
 *
 *   Resource       one unit: type, current location, status, statistics
 *   ResourceStore  owns every Resource; keeps them in REGISTRATION ORDER
 *                  (the baseline strategy in Phase 10 depends on that order)
 *
 * OWNERSHIP RULE: same as incidents. Each Resource is malloc'd individually
 * and owned by the ResourceStore; other structures only borrow pointers.
 *
 * KEY INVARIANT (enforced by this module):
 *   status == RES_DISPATCHED   <=>   assigned_incident_id != RESOURCE_NO_INCIDENT
 * That is why there is no general "set status" function: the only way into
 * or out of DISPATCHED is resource_assign() / resource_release(), which
 * update the status and the assignment together.
 *
 * Depends on: common.h only.
 */
#ifndef DC_RESOURCE_H
#define DC_RESOURCE_H

#include <stdio.h>
#include "common.h"

#define RESOURCE_NO_INCIDENT (-1)

typedef enum {
    RES_AMBULANCE = 0,
    RES_FIRE_UNIT,
    RES_RESCUE_UNIT,
    RES_TYPE_COUNT
} ResourceType;

typedef enum {
    RES_AVAILABLE = 0,
    RES_DISPATCHED,
    RES_OUT_OF_SERVICE,
    RES_STATUS_COUNT
} ResourceStatus;

typedef struct {
    int            id;                    /* unique, >= 1                          */
    ResourceType   type;
    int            location_id;           /* current graph vertex                  */
    ResourceStatus status;
    int            assigned_incident_id;  /* RESOURCE_NO_INCIDENT unless DISPATCHED */
    int            total_dispatches;      /* statistics                            */
    long long      total_distance;        /* statistics: sum of route lengths.
                                             long long, not long: long is only 32
                                             bits on Windows and could overflow   */
} Resource;

typedef struct {
    Resource **items;   /* owned pointers, registration order */
    int count;
    int capacity;
} ResourceStore;

/* ---------- names ---------- */

const char *resource_type_name(ResourceType type);        /* "AMBULANCE", ... or "?" */
const char *resource_status_name(ResourceStatus status);  /* "AVAILABLE", ... or "?" */
DcStatus resource_type_from_name(const char *name, ResourceType *out);
DcStatus resource_status_from_name(const char *name, ResourceStatus *out);

/* ---------- creation ---------- */

DcStatus resource_validate(int id, ResourceType type, int location_id, int num_locations);

/* Validates, then mallocs a new AVAILABLE resource with zeroed statistics.
 * On failure *out is NULL. */
DcStatus resource_create(int id, ResourceType type, int location_id,
                         int num_locations, Resource **out);

/* (Added in Phase 7.) Re-creates a resource read from a file, in ANY
 * status. Validates every field, including the invariant
 * DISPATCHED <=> assigned_incident_id >= 1, and non-negative statistics.
 * Normal code must use resource_create instead. */
DcStatus resource_restore(const Resource *fields, int num_locations, Resource **out);

/* ---------- state machine ---------- */

/* 1 if the state machine allows from -> to (see docs/PHASE4_RESOURCES.md). */
int resource_can_transition(ResourceStatus from, ResourceStatus to);

/* 1 if the unit can be sent right now (status AVAILABLE). */
int resource_is_available(const Resource *r);

/* AVAILABLE -> DISPATCHED: records the incident and adds the route length
 * to the statistics. Errors: DC_ERR_INVALID (NULL, incident_id < 1,
 * distance < 0), DC_ERR_STATE (not AVAILABLE). Nothing changes on error. */
DcStatus resource_assign(Resource *r, int incident_id, int distance);

/* DISPATCHED -> AVAILABLE: clears the assignment and moves the unit to
 * new_location (the incident scene). Errors: DC_ERR_INVALID (NULL, bad
 * location), DC_ERR_STATE (not DISPATCHED). Nothing changes on error. */
DcStatus resource_release(Resource *r, int new_location, int num_locations);

/* AVAILABLE -> OUT_OF_SERVICE, and back. A DISPATCHED unit must be released
 * first (DC_ERR_STATE). */
DcStatus resource_set_out_of_service(Resource *r);
DcStatus resource_return_to_service(Resource *r);

/* ---------- display ---------- */

void resource_print_header(FILE *out);
void resource_print_row(FILE *out, const Resource *r, const char *location_name);

/* ---------- store (owner of all resources) ---------- */

DcStatus resource_store_init(ResourceStore *store, int initial_capacity);
void     resource_store_free(ResourceStore *store);           /* frees the resources */
DcStatus resource_store_add(ResourceStore *store, Resource *r);   /* takes ownership */
Resource *resource_store_find(const ResourceStore *store, int id); /* O(n); NULL if absent */

/* (Added in Phase 5.) Removes the most recently added resource and returns
 * ownership to the caller via *out. Used only to undo a failed
 * registration. O(1). DC_ERR_EMPTY if the store is empty. */
DcStatus resource_store_remove_last(ResourceStore *store, Resource **out);

/* Number of resources with the given status (status_filter = -1 counts all). */
int resource_store_count_status(const ResourceStore *store, int status_filter);

/* Lists resources in registration order; status_filter = -1 for all. */
void resource_store_print(const ResourceStore *store, FILE *out, int status_filter);

#endif /* DC_RESOURCE_H */
