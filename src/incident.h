/*
 * incident.h — emergency incident records.
 *
 *   Incident       one emergency (type, location, severity, status, ...)
 *   IncidentStore  owns every Incident; keeps them in registration order
 *
 * OWNERSHIP RULE: each Incident is malloc'd individually and owned by the
 * IncidentStore. Other structures (the priority heap, later the hash table)
 * only hold borrowed pointers and must never free an Incident.
 *
 * Severity scale (project rule, not a real triage scale):
 *   1 Minor  2 Moderate  3 Serious  4 Severe  5 Critical
 *
 * Depends on: common.h only. (Location IDs are validated against a count
 * passed in by the caller, so this module does not need graph.h.)
 */
#ifndef DC_INCIDENT_H
#define DC_INCIDENT_H

#include <stdio.h>
#include "common.h"

#define INCIDENT_SEVERITY_MIN 1
#define INCIDENT_SEVERITY_MAX 5
#define INCIDENT_NO_RESOURCE  (-1)

typedef enum {
    INC_MEDICAL = 0,
    INC_ROAD_ACCIDENT,
    INC_FIRE,
    INC_STRUCTURAL_RESCUE,
    INC_TYPE_COUNT          /* number of types; not a real type */
} IncidentType;

typedef enum {
    INC_PENDING = 0,
    INC_DISPATCHED,
    INC_COMPLETED,
    INC_CANCELLED,
    INC_STATUS_COUNT
} IncidentStatus;

typedef struct {
    int            id;                    /* unique, >= 1                          */
    IncidentType   type;
    int            location_id;           /* graph vertex                          */
    int            severity;              /* 1 .. 5                                */
    long           reported_seq;          /* simulated clock at registration, >= 0 */
    IncidentStatus status;
    int            assigned_resource_id;  /* INCIDENT_NO_RESOURCE when none        */
    int            dispatch_attempts;     /* failed dispatch attempts so far       */
} Incident;

typedef struct {
    Incident **items;   /* owned pointers, registration order */
    int count;
    int capacity;
} IncidentStore;

/* ---------- names (used for display now and CSV files in Phase 7) ---------- */

const char *incident_type_name(IncidentType type);      /* "MEDICAL", ... or "?" */
const char *incident_status_name(IncidentStatus status); /* "PENDING", ... or "?" */
const char *incident_severity_label(int severity);       /* "Critical", ... or "?" */

/* Exact, case-sensitive match of the names above. */
DcStatus incident_type_from_name(const char *name, IncidentType *out);
DcStatus incident_status_from_name(const char *name, IncidentStatus *out);

/* ---------- creation and validation ---------- */

/* Checks every field of a new incident. num_locations is the number of
 * vertices in the road graph (valid locations are 0 .. num_locations-1). */
DcStatus incident_validate(int id, IncidentType type, int location_id,
                           int severity, long reported_seq, int num_locations);

/* Validates, then mallocs a new PENDING incident with no resource assigned.
 * On success *out points to it (the caller must hand it to an IncidentStore
 * or free it). On failure *out is NULL. */
DcStatus incident_create(int id, IncidentType type, int location_id, int severity,
                         long reported_seq, int num_locations, Incident **out);

/* (Added in Phase 7.) Re-creates an incident read from a file, in ANY
 * status. Every field of *fields is validated, including the status and
 * its agreement with assigned_resource_id (DISPATCHED/COMPLETED need a
 * resource >= 1; PENDING/CANCELLED must have INCIDENT_NO_RESOURCE) and
 * dispatch_attempts >= 0. Normal code must use incident_create instead. */
DcStatus incident_restore(const Incident *fields, int num_locations, Incident **out);

/* ---------- priority ---------- */

/* 1 if a must be served before b:
 *   higher severity first; equal severity -> smaller reported_seq first;
 *   (defensive) equal seq -> smaller id first. Never true for a == b. */
int incident_higher_priority(const Incident *a, const Incident *b);

/* ---------- status transitions ---------- */

/* 1 if the state machine allows from -> to (see docs/PHASE3_INCIDENTS.md). */
int incident_can_transition(IncidentStatus from, IncidentStatus to);

/* Applies a legal transition; returns DC_ERR_STATE (and changes nothing)
 * for an illegal one. */
DcStatus incident_set_status(Incident *inc, IncidentStatus to);

/* ---------- display ---------- */

void incident_print_header(FILE *out);
/* location_name may be NULL, in which case only the location ID is shown. */
void incident_print_row(FILE *out, const Incident *inc, const char *location_name);

/* ---------- store (owner of all incidents) ---------- */

DcStatus incident_store_init(IncidentStore *store, int initial_capacity);

/* Frees every Incident and the array. Safe on NULL / already freed. */
void incident_store_free(IncidentStore *store);

/* Takes ownership of inc. Rejects NULL and duplicate IDs (DC_ERR_DUPLICATE).
 * On failure ownership stays with the caller. */
DcStatus incident_store_add(IncidentStore *store, Incident *inc);

/* (Added in Phase 5.) Removes the most recently added incident and gives
 * ownership back to the caller via *out (the caller must free it).
 * Used only to undo a registration that failed half-way. O(1).
 * DC_ERR_EMPTY if the store is empty. */
DcStatus incident_store_remove_last(IncidentStore *store, Incident **out);

/* Linear search by ID, O(n); NULL if absent. Phase 5 adds an O(1) hash-table
 * index; this function stays as a simple, obviously-correct reference. */
Incident *incident_store_find(const IncidentStore *store, int id);

/* Prints every incident (optionally only one status). Pass status_filter = -1
 * for all. Location names are not available here, so IDs are shown. */
void incident_store_print(const IncidentStore *store, FILE *out, int status_filter);

#endif /* DC_INCIDENT_H */
