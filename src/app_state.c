/*
 * app_state.c — creation, destruction, registration and lookup for the
 * single AppState object (see app_state.h).
 */
#include "app_state.h"

#include <limits.h>
#include <stdlib.h>

static void reset(AppState *app)
{
    app->graph = NULL;
    app->incidents.items = NULL;
    app->incidents.count = 0;
    app->incidents.capacity = 0;
    app->resources.items = NULL;
    app->resources.count = 0;
    app->resources.capacity = 0;
    app->pending.data = NULL;
    app->pending.size = 0;
    app->pending.capacity = 0;
    app->incident_index = NULL;
    app->resource_index = NULL;
    history_init(&app->history);
    app->next_incident_id = APP_FIRST_INCIDENT_ID;
    app->next_resource_id = APP_FIRST_RESOURCE_ID;
    app->next_allocation_id = APP_FIRST_ALLOCATION_ID;
    app->clock = 0;
}

/* Frees everything except the graph. Borrowers go first: after this the
 * owners free the records, so no structure is left pointing at freed memory. */
static void free_containers(AppState *app)
{
    incident_heap_free(&app->pending);
    hash_table_destroy(app->incident_index);
    hash_table_destroy(app->resource_index);
    app->incident_index = NULL;
    app->resource_index = NULL;
    incident_store_free(&app->incidents);
    resource_store_free(&app->resources);
    history_free(&app->history);   /* holds IDs only, so its order does not matter */
}

DcStatus app_init(AppState *app, Graph *graph)
{
    if (app == NULL)
        return DC_ERR_INVALID;
    reset(app);
    if (graph == NULL)
        return DC_ERR_INVALID;

    app->incident_index = hash_table_create(APP_INDEX_BUCKETS);
    app->resource_index = hash_table_create(APP_INDEX_BUCKETS);
    int ok = app->incident_index != NULL && app->resource_index != NULL &&
             incident_store_init(&app->incidents, 16) == DC_OK &&
             resource_store_init(&app->resources, 16) == DC_OK &&
             incident_heap_init(&app->pending, 16) == DC_OK;
    if (!ok) {
        free_containers(app);   /* every free function accepts a never-filled struct */
        reset(app);
        return DC_ERR_NOMEM;
    }
    app->graph = graph;         /* ownership is taken only on success */
    return DC_OK;
}

void app_free(AppState *app)
{
    if (app == NULL)
        return;
    free_containers(app);
    graph_destroy(app->graph);
    reset(app);
}

int app_num_locations(const AppState *app)
{
    return (app == NULL || app->graph == NULL) ? 0 : app->graph->num_vertices;
}

/* Adds an already-created incident to the store, the index and (if
 * PENDING) the heap — all three or none. On failure every step is undone
 * and the CALLER still owns inc. Shared by registration and file loading
 * (refactored out of app_register_incident in Phase 7). */
static DcStatus insert_incident(AppState *app, Incident *inc)
{
    /* Step 1: the owner. */
    DcStatus st = incident_store_add(&app->incidents, inc);
    if (st != DC_OK)
        return st;

    /* Step 2: the ID index. */
    st = hash_table_insert(app->incident_index, inc->id, inc);
    if (st != DC_OK) {
        Incident *undo = NULL;
        incident_store_remove_last(&app->incidents, &undo);   /* undo == inc */
        return st;
    }

    /* Step 3: the priority queue (only pending incidents wait there). */
    if (inc->status == INC_PENDING) {
        st = incident_heap_insert(&app->pending, inc);
        if (st != DC_OK) {
            Incident *undo = NULL;
            hash_table_remove(app->incident_index, inc->id);
            incident_store_remove_last(&app->incidents, &undo);
            return st;
        }
    }
    return DC_OK;
}

/* Same all-or-nothing rule for resources (no heap involved). */
static DcStatus insert_resource(AppState *app, Resource *r)
{
    DcStatus st = resource_store_add(&app->resources, r);
    if (st != DC_OK)
        return st;
    st = hash_table_insert(app->resource_index, r->id, r);
    if (st != DC_OK) {
        Resource *undo = NULL;
        resource_store_remove_last(&app->resources, &undo);
        return st;
    }
    return DC_OK;
}

DcStatus app_register_incident(AppState *app, IncidentType type, int location_id,
                               int severity, Incident **out)
{
    if (out != NULL)
        *out = NULL;
    if (app == NULL || app->graph == NULL)
        return DC_ERR_INVALID;
    if (app->next_incident_id == INT_MAX)
        return DC_ERR_INVALID;               /* ID space exhausted */

    Incident *inc = NULL;
    DcStatus st = incident_create(app->next_incident_id, type, location_id, severity,
                                  app->clock, app_num_locations(app), &inc);
    if (st != DC_OK)
        return st;                           /* nothing to undo */

    st = insert_incident(app, inc);
    if (st != DC_OK) {
        free(inc);                           /* still ours: insert undid everything */
        return st;
    }

    /* Only now, when everything succeeded, advance the counters. */
    app->next_incident_id++;
    app->clock++;
    if (out != NULL)
        *out = inc;
    return DC_OK;
}

DcStatus app_register_resource(AppState *app, ResourceType type, int location_id,
                               Resource **out)
{
    if (out != NULL)
        *out = NULL;
    if (app == NULL || app->graph == NULL)
        return DC_ERR_INVALID;
    if (app->next_resource_id == INT_MAX)
        return DC_ERR_INVALID;

    Resource *r = NULL;
    DcStatus st = resource_create(app->next_resource_id, type, location_id,
                                  app_num_locations(app), &r);
    if (st != DC_OK)
        return st;

    st = insert_resource(app, r);
    if (st != DC_OK) {
        free(r);
        return st;
    }

    app->next_resource_id++;
    app->clock++;
    if (out != NULL)
        *out = r;
    return DC_OK;
}

/* ---------- loading from files (Phase 7) ---------- */

DcStatus app_add_loaded_incident(AppState *app, Incident *inc)
{
    if (app == NULL || app->graph == NULL || inc == NULL)
        return DC_ERR_INVALID;
    return insert_incident(app, inc);   /* counters are fixed in app_finish_loading */
}

DcStatus app_add_loaded_resource(AppState *app, Resource *r)
{
    if (app == NULL || app->graph == NULL || r == NULL)
        return DC_ERR_INVALID;
    return insert_resource(app, r);
}

DcStatus app_add_loaded_allocation(AppState *app, int id, int incident_id, int resource_id,
                                   int from_location, int to_location, int distance,
                                   int *route, int route_len, AllocationStatus status,
                                   long dispatch_seq, long complete_seq)
{
    if (app == NULL || app->graph == NULL)
        return DC_ERR_INVALID;
    if (dispatch_seq < 0)
        return DC_ERR_INVALID;
    /* The completion time must agree with the status BEFORE anything is added. */
    if (status == ALLOC_ACTIVE ? complete_seq != -1
                               : (status != ALLOC_COMPLETED || complete_seq < dispatch_seq))
        return DC_ERR_INVALID;
    if (history_find(&app->history, id) != NULL)
        return DC_ERR_DUPLICATE;

    Allocation *a = history_append(&app->history, id, incident_id, resource_id,
                                   from_location, to_location, distance,
                                   route, route_len, dispatch_seq);
    if (a == NULL)
        return DC_ERR_INVALID;          /* caller still owns route */
    if (status == ALLOC_COMPLETED)
        history_mark_completed(a, complete_seq);   /* checked above: cannot fail */
    return DC_OK;
}

void app_finish_loading(AppState *app)
{
    if (app == NULL)
        return;
    long clock = app->clock;
    int next_incident = APP_FIRST_INCIDENT_ID;
    int next_resource = APP_FIRST_RESOURCE_ID;
    int next_allocation = APP_FIRST_ALLOCATION_ID;

    for (int i = 0; i < app->incidents.count; i++) {
        const Incident *inc = app->incidents.items[i];
        if (inc->id >= next_incident)
            next_incident = inc->id + 1;
        if (inc->reported_seq >= clock)
            clock = inc->reported_seq + 1;
    }
    for (int i = 0; i < app->resources.count; i++)
        if (app->resources.items[i]->id >= next_resource)
            next_resource = app->resources.items[i]->id + 1;
    for (const Allocation *a = app->history.head; a != NULL; a = a->next) {
        if (a->id >= next_allocation)
            next_allocation = a->id + 1;
        if (a->dispatch_seq >= clock)
            clock = a->dispatch_seq + 1;
        if (a->complete_seq >= clock)
            clock = a->complete_seq + 1;
    }
    /* IDs at INT_MAX would overflow "+ 1"; restore/validate rules keep IDs
     * far below that (they are parsed with an upper bound in Phase 7). */
    app->next_incident_id = next_incident;
    app->next_resource_id = next_resource;
    app->next_allocation_id = next_allocation;
    app->clock = clock;
}

/* The only place where the index's void * values are converted back to
 * their real types. Everything else uses these typed functions. */
Incident *app_find_incident(const AppState *app, int id)
{
    return app == NULL ? NULL : (Incident *)hash_table_find(app->incident_index, id);
}

Resource *app_find_resource(const AppState *app, int id)
{
    return app == NULL ? NULL : (Resource *)hash_table_find(app->resource_index, id);
}

/* ---------- consistency check ---------- */

static int report(FILE *out, const char *message, int id)
{
    if (out != NULL)
        fprintf(out, "  consistency: %s (id %d)\n", message, id);
    return 1;
}

static int heap_contains(const IncidentHeap *h, const Incident *inc)
{
    for (int i = 0; i < h->size; i++)
        if (h->data[i] == inc)
            return 1;
    return 0;
}

int app_check_consistency(const AppState *app, FILE *out)
{
    if (app == NULL || app->graph == NULL)
        return report(out, "application state not initialised", -1);

    int problems = 0;
    const int num_locations = app_num_locations(app);

    /* incidents: store <-> index, valid locations, pending <-> heap */
    if (hash_table_count(app->incident_index) != app->incidents.count)
        problems += report(out, "incident index size differs from store size",
                           app->incidents.count);
    for (int i = 0; i < app->incidents.count; i++) {
        const Incident *inc = app->incidents.items[i];
        if (app_find_incident(app, inc->id) != inc)
            problems += report(out, "incident missing from index or wrong pointer", inc->id);
        if (inc->location_id < 0 || inc->location_id >= num_locations)
            problems += report(out, "incident at invalid location", inc->id);
        if (inc->status == INC_PENDING && !heap_contains(&app->pending, inc))
            problems += report(out, "pending incident missing from heap", inc->id);
        if (inc->id >= app->next_incident_id)
            problems += report(out, "incident ID not below next_incident_id", inc->id);
    }
    if (!incident_heap_check_invariant(&app->pending))
        problems += report(out, "heap invariant violated", -1);
    for (int i = 0; i < app->pending.size; i++) {
        const Incident *inc = app->pending.data[i];
        if (app_find_incident(app, inc->id) != inc)
            problems += report(out, "heap holds an incident not in the store", inc->id);
    }

    /* resources: store <-> index, valid locations, dispatch invariant */
    if (hash_table_count(app->resource_index) != app->resources.count)
        problems += report(out, "resource index size differs from store size",
                           app->resources.count);
    for (int i = 0; i < app->resources.count; i++) {
        const Resource *r = app->resources.items[i];
        if (app_find_resource(app, r->id) != r)
            problems += report(out, "resource missing from index or wrong pointer", r->id);
        if (r->location_id < 0 || r->location_id >= num_locations)
            problems += report(out, "resource at invalid location", r->id);
        if ((r->status == RES_DISPATCHED) != (r->assigned_incident_id != RESOURCE_NO_INCIDENT))
            problems += report(out, "resource status and assignment disagree", r->id);
        if (r->id >= app->next_resource_id)
            problems += report(out, "resource ID not below next_resource_id", r->id);
    }

    /* allocations (Phase 6): every ACTIVE allocation links a DISPATCHED
     * incident and a DISPATCHED resource that point at each other */
    int active = 0;
    for (const Allocation *a = app->history.head; a != NULL; a = a->next) {
        if (a->id >= app->next_allocation_id)
            problems += report(out, "allocation ID not below next_allocation_id", a->id);
        const Incident *inc = app_find_incident(app, a->incident_id);
        const Resource *r = app_find_resource(app, a->resource_id);
        if (inc == NULL || r == NULL) {
            problems += report(out, "allocation refers to an unknown incident or resource", a->id);
            continue;
        }
        if (a->status == ALLOC_ACTIVE) {
            active++;
            if (inc->status != INC_DISPATCHED || inc->assigned_resource_id != r->id)
                problems += report(out, "active allocation's incident is not dispatched to it", a->id);
            if (r->status != RES_DISPATCHED || r->assigned_incident_id != inc->id)
                problems += report(out, "active allocation's resource is not assigned to it", a->id);
        } else if (inc->status != INC_COMPLETED) {
            problems += report(out, "completed allocation's incident is not completed", a->id);
        }
        if (a->to_location != inc->location_id)
            problems += report(out, "allocation destination differs from incident location", a->id);
    }
    for (int i = 0; i < app->incidents.count; i++) {
        const Incident *inc = app->incidents.items[i];
        if (inc->status == INC_DISPATCHED &&
            history_find_active_for_incident(&app->history, inc->id) == NULL)
            problems += report(out, "dispatched incident has no active allocation", inc->id);
    }
    if (active != resource_store_count_status(&app->resources, RES_DISPATCHED))
        problems += report(out, "active allocations differ from dispatched resources", active);
    return problems;
}
