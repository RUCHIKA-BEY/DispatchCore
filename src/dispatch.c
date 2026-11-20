/*
 * dispatch.c — the dispatch engine (see dispatch.h).
 */
#include "dispatch.h"
#include "compatibility.h"
#include "dijkstra.h"

#include <limits.h>
#include <stdlib.h>

const char *dispatch_outcome_text(DispatchOutcome outcome)
{
    switch (outcome) {
    case DISPATCH_OK:                   return "dispatched";
    case DISPATCH_NO_PENDING:           return "no pending incidents";
    case DISPATCH_NO_COMPATIBLE_UNIT:   return "no compatible unit is registered";
    case DISPATCH_ALL_UNITS_BUSY:       return "all compatible units are busy or out of service";
    case DISPATCH_UNREACHABLE:          return "no available compatible unit can reach the location";
    case DISPATCH_NOTHING_DISPATCHABLE: return "no pending incident can be served right now";
    case DISPATCH_ERROR:                return "internal error (out of memory or inconsistent data)";
    }
    return "unknown outcome";
}

/* ---------- display helpers ---------- */

void dispatch_print_route(FILE *out, const Graph *g, const int *route, int route_len)
{
    if (out == NULL || route == NULL)
        return;
    for (int i = 0; i < route_len; i++) {
        const char *name = graph_location_name(g, route[i]);
        fprintf(out, "%s%d %s", i > 0 ? " > " : "", route[i], name != NULL ? name : "?");
    }
}

static void log_incident(FILE *log, const AppState *app, const Incident *inc)
{
    const char *place = graph_location_name(app->graph, inc->location_id);
    fprintf(log, "incident %d (%s, severity %d %s) at %d %s",
            inc->id, incident_type_name(inc->type), inc->severity,
            incident_severity_label(inc->severity), inc->location_id,
            place != NULL ? place : "?");
}

/* ---------- selection (read-only) ---------- */

static void reset_selection(DispatchSelection *sel)
{
    sel->outcome = DISPATCH_ERROR;
    sel->resource = NULL;
    sel->distance = -1;
    sel->route = NULL;
    sel->route_len = 0;
    sel->compatible_units = 0;
    sel->available_units = 0;
    sel->reachable_units = 0;
}

void dispatch_selection_free(DispatchSelection *sel)
{
    if (sel == NULL)
        return;
    free(sel->route);
    sel->route = NULL;
    sel->route_len = 0;
}

/* Reverses an array in place: the path from Dijkstra runs incident -> unit,
 * but the unit drives unit -> incident. */
static void reverse(int *a, int n)
{
    for (int i = 0, j = n - 1; i < j; i++, j--) {
        int tmp = a[i];
        a[i] = a[j];
        a[j] = tmp;
    }
}

DispatchOutcome dispatch_select_resource(const AppState *app, const Incident *inc,
                                         DispatchSelection *sel)
{
    return dispatch_select_resource_rule(app, inc, UNIT_NEAREST, sel);
}

DispatchOutcome dispatch_select_resource_rule(const AppState *app, const Incident *inc,
                                              UnitRule rule, DispatchSelection *sel)
{
    if (sel == NULL)
        return DISPATCH_ERROR;
    reset_selection(sel);
    if (app == NULL || app->graph == NULL || inc == NULL || inc->status != INC_PENDING)
        return sel->outcome = DISPATCH_ERROR;

    /* Filters 1 and 2: compatibility, then availability (one pass, O(R)). */
    for (int i = 0; i < app->resources.count; i++) {
        const Resource *r = app->resources.items[i];
        if (!compat_is_compatible(inc->type, r->type))
            continue;
        sel->compatible_units++;
        if (resource_is_available(r))
            sel->available_units++;
    }
    if (sel->compatible_units == 0)
        return sel->outcome = DISPATCH_NO_COMPATIBLE_UNIT;
    if (sel->available_units == 0)
        return sel->outcome = DISPATCH_ALL_UNITS_BUSY;

    /* One Dijkstra from the incident gives every unit's distance. */
    ShortestPaths sp;
    if (dijkstra_run(app->graph, inc->location_id, &sp) != DC_OK)
        return sel->outcome = DISPATCH_ERROR;

    /* Filter 3 (reachability) and the choice.
     *   UNIT_NEAREST   : shortest distance; tie -> lower ID (the real engine)
     *   UNIT_FIRST_FIT : the first reachable unit in registration order,
     *                    whatever its distance (Phase 10 baseline) */
    Resource *best = NULL;
    int best_dist = -1;
    for (int i = 0; i < app->resources.count; i++) {
        Resource *r = app->resources.items[i];
        if (!compat_is_eligible(r, inc->type))
            continue;
        int d = shortest_paths_distance(&sp, r->location_id);
        if (d == DC_INF)
            continue;                                   /* unreachable */
        sel->reachable_units++;
        if (best == NULL ||
            (rule == UNIT_NEAREST && (d < best_dist || (d == best_dist && r->id < best->id)))) {
            best = r;
            best_dist = d;
        }
    }
    if (best == NULL) {
        shortest_paths_free(&sp);
        return sel->outcome = DISPATCH_UNREACHABLE;
    }

    int *path = NULL, len = 0;
    DcStatus st = dijkstra_build_path(&sp, best->location_id, &path, &len);
    shortest_paths_free(&sp);
    if (st != DC_OK)
        return sel->outcome = DISPATCH_ERROR;
    reverse(path, len);                                 /* unit -> incident */

    sel->resource = best;
    sel->distance = best_dist;
    sel->route = path;
    sel->route_len = len;
    return sel->outcome = DISPATCH_OK;
}

/* ---------- commit (changes state) ---------- */

/* Applies a successful selection. The only step that can fail (out of
 * memory) is the history append, so it is done FIRST: if it fails nothing
 * has changed. The remaining steps cannot fail, because the selection was
 * made a moment ago from this same state (unit AVAILABLE, incident PENDING). */
DcStatus dispatch_assign(AppState *app, Incident *inc, DispatchSelection *sel,
                         const Allocation **out)
{
    if (app == NULL || inc == NULL || sel == NULL || out == NULL ||
        sel->outcome != DISPATCH_OK || sel->resource == NULL)
        return DC_ERR_INVALID;
    Resource *r = sel->resource;
    if (app->next_allocation_id == INT_MAX || !resource_is_available(r) ||
        inc->status != INC_PENDING)
        return DC_ERR_STATE;

    Allocation *a = history_append(&app->history, app->next_allocation_id, inc->id, r->id,
                                   r->location_id, inc->location_id, sel->distance,
                                   sel->route, sel->route_len, app->clock);
    if (a == NULL)
        return DC_ERR_NOMEM;
    sel->route = NULL;              /* ownership moved into the history */
    sel->route_len = 0;

    if (resource_assign(r, inc->id, sel->distance) != DC_OK ||
        incident_set_status(inc, INC_DISPATCHED) != DC_OK)
        return DC_ERR_STATE;        /* unreachable by construction; reported, not hidden */
    inc->assigned_resource_id = r->id;

    app->next_allocation_id++;
    app->clock++;
    *out = a;
    return DC_OK;
}

/* ---------- dispatch next / all ---------- */

static void reset_result(DispatchResult *result)
{
    result->outcome = DISPATCH_NO_PENDING;
    result->incident_id = -1;
    result->resource_id = -1;
    result->distance = -1;
    result->allocation = NULL;
    result->attempted = 0;
    result->kept_pending = 0;
    result->discarded = 0;
}

DcStatus dispatch_next(AppState *app, FILE *log, DispatchResult *result)
{
    return dispatch_next_rule(app, UNIT_NEAREST, log, result);
}

DcStatus dispatch_next_rule(AppState *app, UnitRule rule, FILE *log, DispatchResult *result)
{
    if (result == NULL)
        return DC_ERR_INVALID;
    reset_result(result);
    if (app == NULL || app->graph == NULL)
        return DC_ERR_INVALID;

    int start_size = incident_heap_size(&app->pending);
    if (start_size == 0) {
        if (log != NULL)
            fprintf(log, "No pending incidents.\n");
        return DC_OK;
    }

    /* At most start_size incidents can be set aside, so this array is
     * always large enough. Allocating it BEFORE touching the heap means an
     * allocation failure leaves the queue exactly as it was. */
    Incident **skipped = malloc((size_t)start_size * sizeof *skipped);
    if (skipped == NULL) {
        result->outcome = DISPATCH_ERROR;
        return DC_ERR_NOMEM;
    }
    int n_skipped = 0;
    DcStatus status = DC_OK;
    int dispatched = 0;

    Incident *inc = NULL;
    while (incident_heap_extract_max(&app->pending, &inc) == DC_OK) {
        if (inc->status != INC_PENDING) {
            result->discarded++;          /* cancelled: lazy deletion completes here */
            continue;
        }
        result->attempted++;

        DispatchSelection sel;
        DispatchOutcome outcome = dispatch_select_resource_rule(app, inc, rule, &sel);

        if (outcome == DISPATCH_OK) {
            const Allocation *a = NULL;
            status = dispatch_assign(app, inc, &sel, &a);
            if (status == DC_OK) {
                dispatched = 1;
                result->outcome = DISPATCH_OK;
                result->incident_id = inc->id;
                result->resource_id = sel.resource->id;
                result->distance = sel.distance;
                result->allocation = a;
                if (log != NULL) {
                    fprintf(log, "Dispatched unit %d (%s) to ", sel.resource->id,
                            resource_type_name(sel.resource->type));
                    log_incident(log, app, inc);
                    fprintf(log, "\n  distance %d, route: ", sel.distance);
                    dispatch_print_route(log, app->graph, a->route, a->route_len);
                    fprintf(log, "\n");
                }
            }
            dispatch_selection_free(&sel);  /* frees the route only if not handed over */
            if (status != DC_OK)
                skipped[n_skipped++] = inc;
            break;                          /* success or error: the pass ends */
        }
        dispatch_selection_free(&sel);

        skipped[n_skipped++] = inc;         /* keep it: it goes back into the queue */
        if (outcome == DISPATCH_ERROR) {
            status = DC_ERR_NOMEM;
            break;
        }
        inc->dispatch_attempts++;
        result->kept_pending++;
        if (log != NULL) {
            fprintf(log, "Kept pending: ");
            log_incident(log, app, inc);
            fprintf(log, "\n  reason: %s", dispatch_outcome_text(outcome));
            if (outcome == DISPATCH_NO_COMPATIBLE_UNIT) {
                fprintf(log, " (needs ");
                compat_print_options(log, inc->type);
                fprintf(log, ")");
            }
            fprintf(log, "\n");
        }
    }

    /* Put back everything that was set aside. The heap never needs to grow
     * here (it held at least this many entries a moment ago), so these
     * inserts cannot run out of memory. */
    for (int i = 0; i < n_skipped; i++) {
        DcStatus st = incident_heap_insert(&app->pending, skipped[i]);
        if (st != DC_OK && status == DC_OK)
            status = st;
    }
    free(skipped);

    if (status != DC_OK) {
        result->outcome = DISPATCH_ERROR;
        return status;
    }
    if (!dispatched) {
        result->outcome = (result->attempted == 0) ? DISPATCH_NO_PENDING
                                                   : DISPATCH_NOTHING_DISPATCHABLE;
        if (log != NULL)
            fprintf(log, "%s.\n", result->attempted == 0 ? "No pending incidents"
                                  : "No pending incident can be served right now");
    }
    return DC_OK;
}

int dispatch_all(AppState *app, FILE *log)
{
    return dispatch_all_rule(app, UNIT_NEAREST, log);
}

int dispatch_all_rule(AppState *app, UnitRule rule, FILE *log)
{
    int count = 0;
    DispatchResult result;
    for (;;) {
        if (dispatch_next_rule(app, rule, log, &result) != DC_OK)
            return -1;
        if (result.outcome != DISPATCH_OK)
            return count;
        count++;
    }
}

/* ---------- completion and cancellation ---------- */

DcStatus dispatch_complete(AppState *app, int incident_id, FILE *log)
{
    if (app == NULL)
        return DC_ERR_INVALID;
    Incident *inc = app_find_incident(app, incident_id);
    if (inc == NULL)
        return DC_ERR_NOT_FOUND;
    if (inc->status != INC_DISPATCHED)
        return DC_ERR_STATE;

    Resource *r = app_find_resource(app, inc->assigned_resource_id);
    Allocation *a = history_find_active_for_incident(&app->history, inc->id);
    if (r == NULL || a == NULL || r->status != RES_DISPATCHED ||
        r->assigned_incident_id != inc->id)
        return DC_ERR_STATE;      /* inconsistent data: refuse rather than guess */

    /* All checks passed; release first because it validates the location. */
    DcStatus st = resource_release(r, inc->location_id, app_num_locations(app));
    if (st != DC_OK)
        return st;
    incident_set_status(inc, INC_COMPLETED);       /* DISPATCHED -> COMPLETED: legal */
    history_mark_completed(a, app->clock);
    app->clock++;

    if (log != NULL) {
        const char *place = graph_location_name(app->graph, inc->location_id);
        fprintf(log, "Incident %d completed. Unit %d is available again at %d %s.\n",
                inc->id, r->id, r->location_id, place != NULL ? place : "?");
    }
    return DC_OK;
}

DcStatus dispatch_cancel(AppState *app, int incident_id, FILE *log)
{
    if (app == NULL)
        return DC_ERR_INVALID;
    Incident *inc = app_find_incident(app, incident_id);
    if (inc == NULL)
        return DC_ERR_NOT_FOUND;

    DcStatus st = incident_set_status(inc, INC_CANCELLED);   /* only PENDING may cancel */
    if (st != DC_OK)
        return st;
    app->clock++;
    if (log != NULL)
        fprintf(log, "Incident %d cancelled.\n", inc->id);
    return DC_OK;
}

void dispatch_print_active(const AppState *app, FILE *out)
{
    if (app == NULL || out == NULL)
        return;
    int shown = 0;
    for (const Allocation *a = app->history.head; a != NULL; a = a->next) {
        if (a->status != ALLOC_ACTIVE)
            continue;
        const Resource *r = app_find_resource(app, a->resource_id);
        const Incident *inc = app_find_incident(app, a->incident_id);
        fprintf(out, "Allocation %d: unit %d (%s) -> incident %d (%s, severity %d), distance %d\n  route: ",
                a->id, a->resource_id, r != NULL ? resource_type_name(r->type) : "?",
                a->incident_id, inc != NULL ? incident_type_name(inc->type) : "?",
                inc != NULL ? inc->severity : 0, a->distance);
        dispatch_print_route(out, app->graph, a->route, a->route_len);
        fprintf(out, "\n");
        shown++;
    }
    if (shown == 0)
        fprintf(out, "(no active dispatches)\n");
}
