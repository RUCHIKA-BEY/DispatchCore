/*
 * report.h — read-only reports over the whole AppState (Phase 8):
 *
 *   1. system statistics   (counts, distances, waiting and service times)
 *   2. allocation history  (filtered by status / incident / unit)
 *   3. completed incidents (what was served, by whom, how long it took)
 *   4. unit utilisation    (units ranked by workload; insertion sort)
 *   5. a full text report written to a file
 *
 * RULE: nothing in this module changes the AppState. Every function takes a
 * `const AppState *`, so the compiler enforces that reports can never
 * corrupt the simulation.
 *
 * Times are SIMULATED CLOCK TICKS (AppState.clock), not seconds:
 *   wait    = allocation.dispatch_seq - incident.reported_seq
 *             (how long the incident sat in the queue)
 *   service = allocation.complete_seq - allocation.dispatch_seq
 *             (how long the unit was busy with it)
 * The clock advances by 1 per event (registration, dispatch, completion,
 * cancellation), so a tick means "one event happened in between".
 *
 * Depends on: app_state.h (and dispatch.h in the .c file, for route printing).
 */
#ifndef DC_REPORT_H
#define DC_REPORT_H

#include <stdio.h>
#include "app_state.h"

/* Index 0 is unused so that slot s holds severity s (1..5). */
#define REPORT_SEVERITY_SLOTS (INCIDENT_SEVERITY_MAX + 1)

/* Every number in the statistics report. Sums are kept as integers (not
 * averages) so that tests can compare them exactly; averages are computed
 * only when printing, with report_average(). */
typedef struct {
    long clock;

    /* incidents (from the incident store) */
    int incidents_total;
    int incidents_by_status[INC_STATUS_COUNT];
    int incidents_by_type[INC_TYPE_COUNT];
    int failed_attempts_total;          /* sum of dispatch_attempts            */

    /* priority queue (lazy deletion makes these two numbers differ) */
    int heap_entries;                   /* entries physically in the heap      */
    int heap_stale_entries;             /* of which no longer PENDING          */

    /* resources (from the resource store) */
    int resources_total;
    int resources_by_status[RES_STATUS_COUNT];
    int resources_by_type[RES_TYPE_COUNT];
    int dispatches_by_type[RES_TYPE_COUNT];   /* sum of total_dispatches per type */
    int resource_dispatch_sum;          /* sum of total_dispatches, all units  */
    long long resource_distance_sum;    /* sum of total_distance, all units    */

    /* allocations (from the history list) */
    int allocations_total;
    int allocations_active;
    int allocations_completed;
    long long distance_sum;
    int distance_min;                   /* -1 when there are no allocations    */
    int distance_max;                   /* -1 when there are no allocations    */
    long long wait_sum;                 /* over allocations whose incident exists */
    int wait_count;
    long long service_sum;              /* over COMPLETED allocations          */
    int service_count;
    long long wait_sum_by_severity[REPORT_SEVERITY_SLOTS];
    int wait_count_by_severity[REPORT_SEVERITY_SLOTS];
    int missing_incident_refs;          /* allocations naming an unknown incident */
} SystemStats;

/* Which allocations to show. -1 in a field means "any". */
typedef struct {
    int status;        /* ALLOC_ACTIVE, ALLOC_COMPLETED or -1 */
    int incident_id;
    int resource_id;
} HistoryFilter;

/* ---------- statistics ---------- */

/* Fills *st in one pass over each structure: O(N + R + H + heap size),
 * where each history entry also does one O(1)-average hash lookup.
 * DC_ERR_INVALID if app or st is NULL. */
DcStatus report_compute_stats(const AppState *app, SystemStats *st);

/* sum / count as a double; 0.0 when count is 0 (avoids division by zero). */
double report_average(long long sum, int count);

/* Checks that numbers computed from DIFFERENT structures agree, e.g. the
 * dispatch counters kept in the units equal the number of allocations.
 * Returns the number of disagreements (0 = all agree); each one is
 * described on out (may be NULL). */
int report_cross_check(const SystemStats *st, FILE *out);

void report_print_stats(const SystemStats *st, FILE *out);

/* ---------- allocation history ---------- */

/* Sets every field to -1 (show everything). */
void report_filter_all(HistoryFilter *f);

/* 1 if a passes every field of f (a NULL filter matches everything). */
int report_allocation_matches(const Allocation *a, const HistoryFilter *f);

/* Number of allocations that match, O(H). */
int report_count_history(const AllocationList *list, const HistoryFilter *f);

/* Prints matching allocations oldest first, with names and routes.
 * Returns the number of rows printed, or -1 if app or out is NULL. */
int report_print_history(const AppState *app, const HistoryFilter *f, FILE *out);

/* ---------- completed incidents ---------- */

/* One row per COMPLETED allocation (in dispatch order): incident type,
 * severity, place, unit, distance, wait and service ticks.
 * Returns rows printed, or -1 if app or out is NULL. */
int report_print_completed(const AppState *app, FILE *out);

/* ---------- unit utilisation ---------- */

/* 1 if unit a ranks before unit b: more dispatches first; equal ->
 * larger total distance first; equal -> lower ID first. Never true for
 * a == b, so the ranking is a strict total order (deterministic). */
int report_busier(const Resource *a, const Resource *b);

/* Fills ranking[0..R-1] with BORROWED pointers to every unit, busiest
 * first (insertion sort, O(R^2)). Returns R, or -1 if capacity < R or an
 * argument is NULL. The caller must not free the pointed-to units. */
int report_rank_resources(const AppState *app, const Resource **ranking, int capacity);

/* Prints the ranking as a table. Returns rows printed, or -1 on error
 * (including out of memory for the temporary ranking array). */
int report_print_utilisation(const AppState *app, FILE *out);

/* ---------- full report ---------- */

/* Writes every section above to out. DC_ERR_INVALID / DC_ERR_NOMEM. */
DcStatus report_print_full(const AppState *app, FILE *out);

/* Writes the full report to a text file (CREATED OR REPLACED). Returns
 * DC_ERR_IO if the file cannot be opened, written or closed. The CSV data
 * files are never touched by this function. */
DcStatus report_write_file(const AppState *app, const char *path);

#endif /* DC_REPORT_H */
