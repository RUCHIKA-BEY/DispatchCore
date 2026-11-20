/*
 * dispatch.h — the dispatch engine: chooses which incident to serve and
 * which unit to send, and handles completion and cancellation.
 *
 * Selection rule for one incident (blueprint §10), in this exact order:
 *   1. compatibility  (compat table)        — filter
 *   2. availability   (status AVAILABLE)    — filter
 *   3. reachability   (Dijkstra distance)   — filter
 *   4. shortest road distance               — choose; tie -> lower resource ID
 * Dijkstra runs ONCE per incident, from the incident's location; roads are
 * undirected, so dist[unit location] is the unit's distance to the incident.
 *
 * Depends on: app_state.h (and, in the .c file, dijkstra.h, compatibility.h).
 */
#ifndef DC_DISPATCH_H
#define DC_DISPATCH_H

#include <stdio.h>
#include "app_state.h"

typedef enum {
    DISPATCH_OK = 0,
    DISPATCH_NO_PENDING,            /* no pending incident in the queue          */
    DISPATCH_NO_COMPATIBLE_UNIT,    /* no unit of a suitable type is registered  */
    DISPATCH_ALL_UNITS_BUSY,        /* suitable units exist; none AVAILABLE      */
    DISPATCH_UNREACHABLE,           /* available suitable units; none reachable  */
    DISPATCH_NOTHING_DISPATCHABLE,  /* every pending incident failed (one pass)  */
    DISPATCH_ERROR                  /* out of memory or inconsistent state       */
} DispatchOutcome;

const char *dispatch_outcome_text(DispatchOutcome outcome);

/* The result of choosing a unit for ONE incident. Produced without changing
 * any state. If outcome == DISPATCH_OK, `route` is an owned array (from the
 * unit's location to the incident's location) that must be released with
 * dispatch_selection_free unless ownership is handed to the history. */
typedef struct {
    DispatchOutcome outcome;
    Resource *resource;       /* chosen unit (borrowed), NULL unless OK */
    int  distance;            /* -1 unless OK                           */
    int *route;
    int  route_len;
    int  compatible_units;    /* registered units of a suitable type    */
    int  available_units;     /* ... of which AVAILABLE                 */
    int  reachable_units;     /* ... of which reachable by road         */
} DispatchSelection;

/* Chooses the best unit for inc (which must be PENDING). Changes nothing. */
DispatchOutcome dispatch_select_resource(const AppState *app, const Incident *inc,
                                         DispatchSelection *sel);
void dispatch_selection_free(DispatchSelection *sel);

/* ---------- unit-choice rules (added in Phase 10) ----------
 * The engine always uses UNIT_NEAREST. UNIT_FIRST_FIT exists only so that
 * evaluation.c can measure a simple baseline: same three filters
 * (compatible, available, reachable), but the FIRST such unit in
 * registration order is sent, however far away it is. */
typedef enum {
    UNIT_NEAREST = 0,
    UNIT_FIRST_FIT
} UnitRule;

/* dispatch_select_resource with a chosen rule (it calls this with UNIT_NEAREST). */
DispatchOutcome dispatch_select_resource_rule(const AppState *app, const Incident *inc,
                                              UnitRule rule, DispatchSelection *sel);

/* Applies a successful selection (sel->outcome == DISPATCH_OK) to a PENDING
 * incident: history append FIRST (the only step that can fail), then unit
 * and incident become DISPATCHED, clock + 1. On success the route moves
 * into the history and *out points to the new allocation. Does NOT touch the
 * heap: a caller that did not extract inc from the heap leaves a stale
 * entry there, which lazy deletion removes later (that is what the FIFO
 * baseline in evaluation.c does). DC_ERR_INVALID / DC_ERR_STATE /
 * DC_ERR_NOMEM; nothing changes on failure. (Was the private "commit"
 * function in Phase 6.) */
DcStatus dispatch_assign(AppState *app, Incident *inc, DispatchSelection *sel,
                         const Allocation **out);

/* Summary of one "dispatch next" command. */
typedef struct {
    DispatchOutcome outcome;
    int incident_id;          /* dispatched incident, -1 if none */
    int resource_id;          /* unit sent, -1 if none           */
    int distance;             /* -1 if none                      */
    const Allocation *allocation;
    int attempted;            /* pending incidents tried this pass             */
    int kept_pending;         /* tried, could not be served, put back in queue */
    int discarded;            /* cancelled entries removed (lazy deletion)     */
} DispatchResult;

/* Dispatches the highest-priority incident that CAN be served.
 * Bounded pass: incidents are extracted in priority order and each is tried
 * at most once; those that cannot be served are re-inserted afterwards, so
 * an unservable incident never blocks the others and the loop always ends.
 * Messages are written to `log` (may be NULL).
 * Returns DC_OK whenever the pass ran (inspect result->outcome);
 * DC_ERR_INVALID / DC_ERR_NOMEM only for real errors. */
DcStatus dispatch_next(AppState *app, FILE *log, DispatchResult *result);

/* Repeats dispatch_next until nothing more can be dispatched. Returns the
 * number of incidents dispatched, or -1 on error. Terminates because every
 * success permanently removes one incident from the pending set. */
int dispatch_all(AppState *app, FILE *log);

/* The same two commands with a chosen unit rule (Phase 10). dispatch_next
 * and dispatch_all are exactly these with UNIT_NEAREST. */
DcStatus dispatch_next_rule(AppState *app, UnitRule rule, FILE *log, DispatchResult *result);
int dispatch_all_rule(AppState *app, UnitRule rule, FILE *log);

/* DISPATCHED -> COMPLETED; releases the unit at the incident's location and
 * marks the allocation completed. DC_ERR_NOT_FOUND / DC_ERR_STATE on error. */
DcStatus dispatch_complete(AppState *app, int incident_id, FILE *log);

/* PENDING -> CANCELLED. The heap entry is removed lazily (skipped later).
 * DC_ERR_NOT_FOUND / DC_ERR_STATE on error. */
DcStatus dispatch_cancel(AppState *app, int incident_id, FILE *log);

/* Writes "0 Central Hospital > 3 Riverside > 5 Old Town". */
void dispatch_print_route(FILE *out, const Graph *g, const int *route, int route_len);

/* Lists every ACTIVE allocation with names and routes. */
void dispatch_print_active(const AppState *app, FILE *out);

#endif /* DC_DISPATCH_H */
