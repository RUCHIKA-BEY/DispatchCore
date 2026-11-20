/*
 * app_state.h — AppState: the single object that holds ALL live data.
 *
 * It is created once (in main() from Phase 9, in tests now) and passed by
 * pointer to every function that needs it. There are no global variables.
 *
 *   graph           OWNED   road network
 *   incidents       OWNS    every Incident record
 *   resources       OWNS    every Resource record
 *   pending         borrows max-heap of PENDING incidents (lazy deletion)
 *   incident_index  borrows hash table: incident ID -> Incident *
 *   resource_index  borrows hash table: resource ID -> Resource *
 *   history         OWNS    linked list of Allocation records (Phase 6)
 *
 * Registration functions keep the store, the index and the heap in step:
 * a record is either in ALL the structures it belongs to, or in none.
 *
 * Depends on: graph.h, incident.h, incident_heap.h, resource.h, hash_table.h,
 *             history.h.
 */
#ifndef DC_APP_STATE_H
#define DC_APP_STATE_H

#include "common.h"
#include "graph.h"
#include "hash_table.h"
#include "history.h"
#include "incident.h"
#include "incident_heap.h"
#include "resource.h"

#define APP_INDEX_BUCKETS      101    /* prime; see hash_table.h        */
#define APP_FIRST_INCIDENT_ID  1001
#define APP_FIRST_RESOURCE_ID  1
#define APP_FIRST_ALLOCATION_ID 1

typedef struct {
    Graph         *graph;
    IncidentStore  incidents;
    ResourceStore  resources;
    IncidentHeap   pending;
    HashTable     *incident_index;
    HashTable     *resource_index;
    AllocationList history;          /* added in Phase 6 */
    int            next_incident_id;
    int            next_resource_id;
    int            next_allocation_id;  /* added in Phase 6 */
    long           clock;            /* simulated time: +1 per event */
} AppState;

/* Initialises every structure. On success AppState takes OWNERSHIP of
 * graph (app_free will destroy it). On failure nothing is kept, the graph
 * is NOT freed (the caller still owns it) and app is safe to app_free. */
DcStatus app_init(AppState *app, Graph *graph);

/* Frees everything: borrowers first (heap, indexes), then owners. Safe to
 * call twice or after a failed app_init. */
void app_free(AppState *app);

int app_num_locations(const AppState *app);

/* Registers a new PENDING incident: assigns the next ID and the current
 * clock value as reported_seq, then adds it to the store, the index and
 * the heap. If any step fails, the earlier steps are undone and the
 * counters are unchanged. *out (optional, may be NULL) gets the record. */
DcStatus app_register_incident(AppState *app, IncidentType type, int location_id,
                               int severity, Incident **out);

/* Registers a new AVAILABLE resource (next resource ID) in the store and
 * the index, with the same all-or-nothing guarantee. */
DcStatus app_register_resource(AppState *app, ResourceType type, int location_id,
                               Resource **out);

/* ---------- loading from files (added in Phase 7) ----------
 * These add records that ALREADY have IDs, statuses and timestamps.
 * They do not advance any counter; call app_finish_loading once at the end.
 * Each is all-or-nothing; on failure the caller keeps ownership. */

/* Takes ownership of inc on success (store + index, and heap if PENDING). */
DcStatus app_add_loaded_incident(AppState *app, Incident *inc);

/* Takes ownership of r on success (store + index). */
DcStatus app_add_loaded_resource(AppState *app, Resource *r);

/* Appends an allocation with its saved status; takes ownership of route on
 * success. complete_seq must be -1 for ACTIVE and >= dispatch_seq for
 * COMPLETED. DC_ERR_DUPLICATE if the ID already exists. */
DcStatus app_add_loaded_allocation(AppState *app, int id, int incident_id, int resource_id,
                                   int from_location, int to_location, int distance,
                                   int *route, int route_len, AllocationStatus status,
                                   long dispatch_seq, long complete_seq);

/* Sets every next_*_id to (largest loaded ID + 1) and the clock to
 * (largest loaded sequence number + 1), so new records never collide with
 * loaded ones. */
void app_finish_loading(AppState *app);

/* O(1) average lookups through the hash tables; NULL if absent. */
Incident *app_find_incident(const AppState *app, int id);
Resource *app_find_resource(const AppState *app, int id);

/* Cross-checks every structure against the others and returns the number
 * of problems found (0 = consistent). If out is not NULL, each problem is
 * described there. Used by tests now and after loading files in Phase 7.
 * O(N * n) in the worst case — a checking tool, not for normal operation. */
int app_check_consistency(const AppState *app, FILE *out);

#endif /* DC_APP_STATE_H */
