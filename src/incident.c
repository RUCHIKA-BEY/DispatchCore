/*
 * incident.c — incident records, validation, priority rule, state machine
 * and the owning store (see incident.h).
 */
#include "incident.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* ---------- name tables ----------
 * Read-only lookup tables indexed by the enum value. `static const` means
 * they are private to this file and can never be modified, so they are not
 * "global state" in the harmful sense. Their order MUST match the enums. */

static const char *const TYPE_NAMES[INC_TYPE_COUNT] = {
    "MEDICAL", "ROAD_ACCIDENT", "FIRE", "STRUCTURAL_RESCUE"
};

static const char *const STATUS_NAMES[INC_STATUS_COUNT] = {
    "PENDING", "DISPATCHED", "COMPLETED", "CANCELLED"
};

static const char *const SEVERITY_LABELS[INCIDENT_SEVERITY_MAX + 1] = {
    "?", "Minor", "Moderate", "Serious", "Severe", "Critical"   /* index 0 unused */
};

static int is_valid_type(IncidentType type)
{
    return (int)type >= 0 && (int)type < INC_TYPE_COUNT;
}

static int is_valid_status(IncidentStatus status)
{
    return (int)status >= 0 && (int)status < INC_STATUS_COUNT;
}

const char *incident_type_name(IncidentType type)
{
    return is_valid_type(type) ? TYPE_NAMES[type] : "?";
}

const char *incident_status_name(IncidentStatus status)
{
    return is_valid_status(status) ? STATUS_NAMES[status] : "?";
}

const char *incident_severity_label(int severity)
{
    if (severity < INCIDENT_SEVERITY_MIN || severity > INCIDENT_SEVERITY_MAX)
        return "?";
    return SEVERITY_LABELS[severity];
}

DcStatus incident_type_from_name(const char *name, IncidentType *out)
{
    if (name == NULL || out == NULL)
        return DC_ERR_INVALID;
    for (int t = 0; t < INC_TYPE_COUNT; t++) {
        if (strcmp(name, TYPE_NAMES[t]) == 0) {
            *out = (IncidentType)t;
            return DC_OK;
        }
    }
    return DC_ERR_NOT_FOUND;
}

DcStatus incident_status_from_name(const char *name, IncidentStatus *out)
{
    if (name == NULL || out == NULL)
        return DC_ERR_INVALID;
    for (int s = 0; s < INC_STATUS_COUNT; s++) {
        if (strcmp(name, STATUS_NAMES[s]) == 0) {
            *out = (IncidentStatus)s;
            return DC_OK;
        }
    }
    return DC_ERR_NOT_FOUND;
}

/* ---------- creation and validation ---------- */

DcStatus incident_validate(int id, IncidentType type, int location_id,
                           int severity, long reported_seq, int num_locations)
{
    if (id < 1)
        return DC_ERR_INVALID;
    if (!is_valid_type(type))
        return DC_ERR_INVALID;
    if (severity < INCIDENT_SEVERITY_MIN || severity > INCIDENT_SEVERITY_MAX)
        return DC_ERR_INVALID;
    if (reported_seq < 0)
        return DC_ERR_INVALID;
    if (num_locations < 1 || location_id < 0 || location_id >= num_locations)
        return DC_ERR_INVALID;
    return DC_OK;
}

DcStatus incident_create(int id, IncidentType type, int location_id, int severity,
                         long reported_seq, int num_locations, Incident **out)
{
    if (out == NULL)
        return DC_ERR_INVALID;
    *out = NULL;

    DcStatus st = incident_validate(id, type, location_id, severity,
                                    reported_seq, num_locations);
    if (st != DC_OK)
        return st;

    Incident *inc = malloc(sizeof *inc);
    if (inc == NULL)
        return DC_ERR_NOMEM;

    inc->id = id;
    inc->type = type;
    inc->location_id = location_id;
    inc->severity = severity;
    inc->reported_seq = reported_seq;
    inc->status = INC_PENDING;                 /* every new incident starts here */
    inc->assigned_resource_id = INCIDENT_NO_RESOURCE;
    inc->dispatch_attempts = 0;

    *out = inc;
    return DC_OK;
}

DcStatus incident_restore(const Incident *fields, int num_locations, Incident **out)
{
    if (out == NULL)
        return DC_ERR_INVALID;
    *out = NULL;
    if (fields == NULL)
        return DC_ERR_INVALID;

    DcStatus st = incident_validate(fields->id, fields->type, fields->location_id,
                                    fields->severity, fields->reported_seq, num_locations);
    if (st != DC_OK)
        return st;
    if (!is_valid_status(fields->status) || fields->dispatch_attempts < 0)
        return DC_ERR_INVALID;

    int needs_resource = fields->status == INC_DISPATCHED || fields->status == INC_COMPLETED;
    if (needs_resource ? fields->assigned_resource_id < 1
                       : fields->assigned_resource_id != INCIDENT_NO_RESOURCE)
        return DC_ERR_INVALID;

    Incident *inc = malloc(sizeof *inc);
    if (inc == NULL)
        return DC_ERR_NOMEM;
    *inc = *fields;            /* struct assignment copies every field */
    *out = inc;
    return DC_OK;
}

/* ---------- priority ---------- */

int incident_higher_priority(const Incident *a, const Incident *b)
{
    if (a->severity != b->severity)
        return a->severity > b->severity;          /* rule 1: more severe first   */
    if (a->reported_seq != b->reported_seq)
        return a->reported_seq < b->reported_seq;  /* rule 2: reported earlier    */
    return a->id < b->id;                          /* rule 3: defensive tie-break */
}

/* ---------- state machine ----------
 *
 *   PENDING ──dispatch──► DISPATCHED ──complete──► COMPLETED
 *      │
 *      └──cancel──► CANCELLED
 *
 * COMPLETED and CANCELLED are final. A failed dispatch attempt does not
 * change the status (the incident simply stays PENDING). */

int incident_can_transition(IncidentStatus from, IncidentStatus to)
{
    switch (from) {
    case INC_PENDING:    return to == INC_DISPATCHED || to == INC_CANCELLED;
    case INC_DISPATCHED: return to == INC_COMPLETED;
    case INC_COMPLETED:  return 0;
    case INC_CANCELLED:  return 0;
    case INC_STATUS_COUNT: return 0;
    }
    return 0;   /* out-of-range value */
}

DcStatus incident_set_status(Incident *inc, IncidentStatus to)
{
    if (inc == NULL)
        return DC_ERR_INVALID;
    if (!incident_can_transition(inc->status, to))
        return DC_ERR_STATE;
    inc->status = to;
    return DC_OK;
}

/* ---------- display ---------- */

void incident_print_header(FILE *out)
{
    if (out == NULL)
        return;
    fprintf(out, "%-6s %-18s %-26s %-12s %6s  %-10s %s\n",
            "ID", "TYPE", "LOCATION", "SEVERITY", "SEQ", "STATUS", "RESOURCE");
}

void incident_print_row(FILE *out, const Incident *inc, const char *location_name)
{
    if (out == NULL || inc == NULL)
        return;

    char location[48];   /* "<id> <name>": at most 11 + 1 + 31 chars + '\0' */
    if (location_name != NULL)
        snprintf(location, sizeof location, "%d %s", inc->location_id, location_name);
    else
        snprintf(location, sizeof location, "%d", inc->location_id);

    char severity[16];
    snprintf(severity, sizeof severity, "%d %s", inc->severity,
             incident_severity_label(inc->severity));

    fprintf(out, "%-6d %-18s %-26s %-12s %6ld  %-10s ",
            inc->id, incident_type_name(inc->type), location, severity,
            inc->reported_seq, incident_status_name(inc->status));
    if (inc->assigned_resource_id == INCIDENT_NO_RESOURCE)
        fprintf(out, "-\n");
    else
        fprintf(out, "%d\n", inc->assigned_resource_id);
}

/* ---------- store ---------- */

DcStatus incident_store_init(IncidentStore *store, int initial_capacity)
{
    if (store == NULL)
        return DC_ERR_INVALID;
    store->items = NULL;
    store->count = 0;
    store->capacity = 0;
    if (initial_capacity < 1)
        return DC_ERR_INVALID;

    store->items = malloc((size_t)initial_capacity * sizeof *store->items);
    if (store->items == NULL)
        return DC_ERR_NOMEM;
    store->capacity = initial_capacity;
    return DC_OK;
}

void incident_store_free(IncidentStore *store)
{
    if (store == NULL)
        return;
    /* The store is the OWNER, so it frees the records themselves. */
    for (int i = 0; i < store->count; i++)
        free(store->items[i]);
    free(store->items);
    store->items = NULL;
    store->count = 0;
    store->capacity = 0;
}

Incident *incident_store_find(const IncidentStore *store, int id)
{
    if (store == NULL)
        return NULL;
    for (int i = 0; i < store->count; i++) {
        if (store->items[i]->id == id)
            return store->items[i];
    }
    return NULL;
}

DcStatus incident_store_add(IncidentStore *store, Incident *inc)
{
    if (store == NULL || store->items == NULL || inc == NULL)
        return DC_ERR_INVALID;
    if (incident_store_find(store, inc->id) != NULL)
        return DC_ERR_DUPLICATE;

    if (store->count == store->capacity) {
        if (store->capacity > INT_MAX / 2)
            return DC_ERR_NOMEM;
        int new_capacity = store->capacity * 2;
        /* Only the array of POINTERS moves; the Incident records stay where
         * they are, so pointers held by the heap remain valid. */
        Incident **bigger = realloc(store->items, (size_t)new_capacity * sizeof *bigger);
        if (bigger == NULL)
            return DC_ERR_NOMEM;
        store->items = bigger;
        store->capacity = new_capacity;
    }
    store->items[store->count++] = inc;
    return DC_OK;
}

DcStatus incident_store_remove_last(IncidentStore *store, Incident **out)
{
    if (out == NULL)
        return DC_ERR_INVALID;
    *out = NULL;
    if (store == NULL)
        return DC_ERR_INVALID;
    if (store->count == 0)
        return DC_ERR_EMPTY;
    store->count--;
    *out = store->items[store->count];   /* ownership returns to the caller */
    store->items[store->count] = NULL;
    return DC_OK;
}

void incident_store_print(const IncidentStore *store, FILE *out, int status_filter)
{
    if (store == NULL || out == NULL)
        return;
    incident_print_header(out);
    int shown = 0;
    for (int i = 0; i < store->count; i++) {
        const Incident *inc = store->items[i];
        if (status_filter >= 0 && (int)inc->status != status_filter)
            continue;
        incident_print_row(out, inc, NULL);
        shown++;
    }
    if (shown == 0)
        fprintf(out, "(no incidents)\n");
}
