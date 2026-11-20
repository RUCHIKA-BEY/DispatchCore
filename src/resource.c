/*
 * resource.c — resource records, state machine and owning store
 * (see resource.h).
 */
#include "resource.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* ---------- name tables (order MUST match the enums) ---------- */

static const char *const TYPE_NAMES[RES_TYPE_COUNT] = {
    "AMBULANCE", "FIRE_UNIT", "RESCUE_UNIT"
};

static const char *const STATUS_NAMES[RES_STATUS_COUNT] = {
    "AVAILABLE", "DISPATCHED", "OUT_OF_SERVICE"
};

static int is_valid_type(ResourceType type)
{
    return (int)type >= 0 && (int)type < RES_TYPE_COUNT;
}

static int is_valid_status(ResourceStatus status)
{
    return (int)status >= 0 && (int)status < RES_STATUS_COUNT;
}

const char *resource_type_name(ResourceType type)
{
    return is_valid_type(type) ? TYPE_NAMES[type] : "?";
}

const char *resource_status_name(ResourceStatus status)
{
    return is_valid_status(status) ? STATUS_NAMES[status] : "?";
}

DcStatus resource_type_from_name(const char *name, ResourceType *out)
{
    if (name == NULL || out == NULL)
        return DC_ERR_INVALID;
    for (int t = 0; t < RES_TYPE_COUNT; t++) {
        if (strcmp(name, TYPE_NAMES[t]) == 0) {
            *out = (ResourceType)t;
            return DC_OK;
        }
    }
    return DC_ERR_NOT_FOUND;
}

DcStatus resource_status_from_name(const char *name, ResourceStatus *out)
{
    if (name == NULL || out == NULL)
        return DC_ERR_INVALID;
    for (int s = 0; s < RES_STATUS_COUNT; s++) {
        if (strcmp(name, STATUS_NAMES[s]) == 0) {
            *out = (ResourceStatus)s;
            return DC_OK;
        }
    }
    return DC_ERR_NOT_FOUND;
}

/* ---------- creation ---------- */

static int is_valid_location(int location_id, int num_locations)
{
    return num_locations >= 1 && location_id >= 0 && location_id < num_locations;
}

DcStatus resource_validate(int id, ResourceType type, int location_id, int num_locations)
{
    if (id < 1 || !is_valid_type(type) || !is_valid_location(location_id, num_locations))
        return DC_ERR_INVALID;
    return DC_OK;
}

DcStatus resource_create(int id, ResourceType type, int location_id,
                         int num_locations, Resource **out)
{
    if (out == NULL)
        return DC_ERR_INVALID;
    *out = NULL;

    DcStatus st = resource_validate(id, type, location_id, num_locations);
    if (st != DC_OK)
        return st;

    Resource *r = malloc(sizeof *r);
    if (r == NULL)
        return DC_ERR_NOMEM;

    r->id = id;
    r->type = type;
    r->location_id = location_id;
    r->status = RES_AVAILABLE;                   /* new units are ready for duty */
    r->assigned_incident_id = RESOURCE_NO_INCIDENT;
    r->total_dispatches = 0;
    r->total_distance = 0;

    *out = r;
    return DC_OK;
}

DcStatus resource_restore(const Resource *fields, int num_locations, Resource **out)
{
    if (out == NULL)
        return DC_ERR_INVALID;
    *out = NULL;
    if (fields == NULL)
        return DC_ERR_INVALID;

    DcStatus st = resource_validate(fields->id, fields->type, fields->location_id, num_locations);
    if (st != DC_OK)
        return st;
    if (!is_valid_status(fields->status) || fields->total_dispatches < 0 ||
        fields->total_distance < 0)
        return DC_ERR_INVALID;
    if (fields->status == RES_DISPATCHED ? fields->assigned_incident_id < 1
                                         : fields->assigned_incident_id != RESOURCE_NO_INCIDENT)
        return DC_ERR_INVALID;   /* the module invariant must hold for loaded data too */

    Resource *r = malloc(sizeof *r);
    if (r == NULL)
        return DC_ERR_NOMEM;
    *r = *fields;
    *out = r;
    return DC_OK;
}

/* ---------- state machine ----------
 *
 *               assign                     set_out_of_service
 *   DISPATCHED ◄──────── AVAILABLE ──────────────────────────► OUT_OF_SERVICE
 *              ────────►           ◄──────────────────────────
 *               release                  return_to_service
 *
 * There is no final state: units return to duty again and again.
 * DISPATCHED <-> OUT_OF_SERVICE is forbidden: a unit finishes its job first. */

int resource_can_transition(ResourceStatus from, ResourceStatus to)
{
    switch (from) {
    case RES_AVAILABLE:      return to == RES_DISPATCHED || to == RES_OUT_OF_SERVICE;
    case RES_DISPATCHED:     return to == RES_AVAILABLE;
    case RES_OUT_OF_SERVICE: return to == RES_AVAILABLE;
    case RES_STATUS_COUNT:   return 0;
    }
    return 0;
}

int resource_is_available(const Resource *r)
{
    return r != NULL && r->status == RES_AVAILABLE;
}

DcStatus resource_assign(Resource *r, int incident_id, int distance)
{
    if (r == NULL || incident_id < 1 || distance < 0)
        return DC_ERR_INVALID;
    if (!resource_can_transition(r->status, RES_DISPATCHED))
        return DC_ERR_STATE;     /* busy or out of service: never double-assign */

    /* status and assignment change TOGETHER, keeping the invariant */
    r->status = RES_DISPATCHED;
    r->assigned_incident_id = incident_id;
    r->total_dispatches++;
    r->total_distance += distance;
    return DC_OK;
}

DcStatus resource_release(Resource *r, int new_location, int num_locations)
{
    if (r == NULL || !is_valid_location(new_location, num_locations))
        return DC_ERR_INVALID;
    if (r->status != RES_DISPATCHED)
        return DC_ERR_STATE;

    r->status = RES_AVAILABLE;
    r->assigned_incident_id = RESOURCE_NO_INCIDENT;
    r->location_id = new_location;   /* the unit is now at the incident scene */
    return DC_OK;
}

/* Shared by the two service changes: only AVAILABLE <-> OUT_OF_SERVICE. */
static DcStatus change_service(Resource *r, ResourceStatus from, ResourceStatus to)
{
    if (r == NULL)
        return DC_ERR_INVALID;
    if (r->status != from)
        return DC_ERR_STATE;
    r->status = to;
    return DC_OK;
}

DcStatus resource_set_out_of_service(Resource *r)
{
    return change_service(r, RES_AVAILABLE, RES_OUT_OF_SERVICE);
}

DcStatus resource_return_to_service(Resource *r)
{
    return change_service(r, RES_OUT_OF_SERVICE, RES_AVAILABLE);
}

/* ---------- display ---------- */

void resource_print_header(FILE *out)
{
    if (out == NULL)
        return;
    fprintf(out, "%-5s %-12s %-26s %-15s %-9s %6s %10s\n",
            "ID", "TYPE", "LOCATION", "STATUS", "INCIDENT", "TRIPS", "DISTANCE");
}

void resource_print_row(FILE *out, const Resource *r, const char *location_name)
{
    if (out == NULL || r == NULL)
        return;

    char location[48];
    if (location_name != NULL)
        snprintf(location, sizeof location, "%d %s", r->location_id, location_name);
    else
        snprintf(location, sizeof location, "%d", r->location_id);

    char incident[16];
    if (r->assigned_incident_id == RESOURCE_NO_INCIDENT)
        snprintf(incident, sizeof incident, "-");
    else
        snprintf(incident, sizeof incident, "%d", r->assigned_incident_id);

    fprintf(out, "%-5d %-12s %-26s %-15s %-9s %6d %10lld\n",
            r->id, resource_type_name(r->type), location,
            resource_status_name(r->status), incident,
            r->total_dispatches, r->total_distance);
}

/* ---------- store ---------- */

DcStatus resource_store_init(ResourceStore *store, int initial_capacity)
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

void resource_store_free(ResourceStore *store)
{
    if (store == NULL)
        return;
    for (int i = 0; i < store->count; i++)
        free(store->items[i]);       /* the store owns the records */
    free(store->items);
    store->items = NULL;
    store->count = 0;
    store->capacity = 0;
}

Resource *resource_store_find(const ResourceStore *store, int id)
{
    if (store == NULL)
        return NULL;
    for (int i = 0; i < store->count; i++) {
        if (store->items[i]->id == id)
            return store->items[i];
    }
    return NULL;
}

DcStatus resource_store_add(ResourceStore *store, Resource *r)
{
    if (store == NULL || store->items == NULL || r == NULL)
        return DC_ERR_INVALID;
    if (resource_store_find(store, r->id) != NULL)
        return DC_ERR_DUPLICATE;

    if (store->count == store->capacity) {
        if (store->capacity > INT_MAX / 2)
            return DC_ERR_NOMEM;
        int new_capacity = store->capacity * 2;
        Resource **bigger = realloc(store->items, (size_t)new_capacity * sizeof *bigger);
        if (bigger == NULL)
            return DC_ERR_NOMEM;
        store->items = bigger;
        store->capacity = new_capacity;
    }
    store->items[store->count++] = r;   /* appended: registration order is kept */
    return DC_OK;
}

DcStatus resource_store_remove_last(ResourceStore *store, Resource **out)
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

int resource_store_count_status(const ResourceStore *store, int status_filter)
{
    if (store == NULL)
        return 0;
    if (status_filter < 0)
        return store->count;
    int n = 0;
    for (int i = 0; i < store->count; i++)
        if ((int)store->items[i]->status == status_filter)
            n++;
    return n;
}

void resource_store_print(const ResourceStore *store, FILE *out, int status_filter)
{
    if (store == NULL || out == NULL)
        return;
    resource_print_header(out);
    int shown = 0;
    for (int i = 0; i < store->count; i++) {
        const Resource *r = store->items[i];
        if (status_filter >= 0 && (int)r->status != status_filter)
            continue;
        resource_print_row(out, r, NULL);
        shown++;
    }
    if (shown == 0)
        fprintf(out, "(no resources)\n");
}
