/*
 * report.c — read-only reports over the AppState (see report.h).
 *
 * Every function here takes `const AppState *`: reports only LOOK at the
 * data. All loops are single passes over the stores, the heap array or the
 * history list; the only extra memory is the temporary ranking array used
 * by report_print_utilisation.
 */
#include "report.h"
#include "dispatch.h"   /* dispatch_print_route */

#include <stdlib.h>

/* ---------- small helpers ---------- */

/* Writes "5 Old Town" (ID and name) into buf. 12 characters cover any int
 * and the space; DC_NAME_MAX covers the longest name and the '\0'. */
#define PLACE_BUF (12 + DC_NAME_MAX)

static void format_place(char *buf, size_t size, const Graph *g, int location)
{
    const char *name = graph_location_name(g, location);
    snprintf(buf, size, "%d %s", location, name != NULL ? name : "?");
}

/* Writes an average with 2 decimals, or "-" when there is nothing to
 * average (count 0) — "0.00" would wrongly suggest a measured value. */
#define AVG_BUF 32

static void format_average(char *buf, size_t size, long long sum, int count)
{
    if (count <= 0)
        snprintf(buf, size, "-");
    else
        snprintf(buf, size, "%.2f", report_average(sum, count));
}

static void print_average(FILE *out, long long sum, int count)
{
    char buf[AVG_BUF];
    format_average(buf, sizeof buf, sum, count);
    fputs(buf, out);
}

double report_average(long long sum, int count)
{
    if (count <= 0)
        return 0.0;
    return (double)sum / (double)count;
}

/* ---------- statistics ---------- */

DcStatus report_compute_stats(const AppState *app, SystemStats *st)
{
    if (app == NULL || st == NULL)
        return DC_ERR_INVALID;

    /* Start from all zeros, then set the two "no value" markers. */
    SystemStats zero = {0};
    *st = zero;
    st->distance_min = -1;
    st->distance_max = -1;
    st->clock = app->clock;

    /* 1. Incidents: one pass over the store, O(N). */
    for (int i = 0; i < app->incidents.count; i++) {
        const Incident *inc = app->incidents.items[i];
        st->incidents_total++;
        if ((int)inc->status >= 0 && inc->status < INC_STATUS_COUNT)
            st->incidents_by_status[inc->status]++;
        if ((int)inc->type >= 0 && inc->type < INC_TYPE_COUNT)
            st->incidents_by_type[inc->type]++;
        st->failed_attempts_total += inc->dispatch_attempts;
    }

    /* 2. The heap array: count entries that lazy deletion has not removed
     *    yet (cancelled incidents still sitting in the heap). O(heap size). */
    st->heap_entries = incident_heap_size(&app->pending);
    for (int i = 0; i < st->heap_entries; i++)
        if (app->pending.data[i]->status != INC_PENDING)
            st->heap_stale_entries++;

    /* 3. Resources: one pass over the store, O(R). */
    for (int i = 0; i < app->resources.count; i++) {
        const Resource *r = app->resources.items[i];
        st->resources_total++;
        if ((int)r->status >= 0 && r->status < RES_STATUS_COUNT)
            st->resources_by_status[r->status]++;
        if ((int)r->type >= 0 && r->type < RES_TYPE_COUNT) {
            st->resources_by_type[r->type]++;
            st->dispatches_by_type[r->type] += r->total_dispatches;
        }
        st->resource_dispatch_sum += r->total_dispatches;
        st->resource_distance_sum += r->total_distance;
    }

    /* 4. History: one pass over the linked list, O(H), with one O(1)
     *    average hash lookup per entry to find the incident's report time. */
    for (const Allocation *a = app->history.head; a != NULL; a = a->next) {
        st->allocations_total++;
        if (a->status == ALLOC_COMPLETED) {
            st->allocations_completed++;
            st->service_sum += a->complete_seq - a->dispatch_seq;
            st->service_count++;
        } else {
            st->allocations_active++;
        }

        st->distance_sum += a->distance;
        if (st->distance_min < 0 || a->distance < st->distance_min)
            st->distance_min = a->distance;
        if (a->distance > st->distance_max)
            st->distance_max = a->distance;

        const Incident *inc = app_find_incident(app, a->incident_id);
        if (inc == NULL) {
            st->missing_incident_refs++;   /* cannot compute its waiting time */
            continue;
        }
        long wait = a->dispatch_seq - inc->reported_seq;
        st->wait_sum += wait;
        st->wait_count++;
        if (inc->severity >= INCIDENT_SEVERITY_MIN && inc->severity <= INCIDENT_SEVERITY_MAX) {
            st->wait_sum_by_severity[inc->severity] += wait;
            st->wait_count_by_severity[inc->severity]++;
        }
    }
    return DC_OK;
}

/* Reports one disagreement between two numbers that should be equal. */
static int disagree(FILE *out, const char *what, long long left, long long right)
{
    if (left == right)
        return 0;
    if (out != NULL)
        fprintf(out, "  CROSS-CHECK FAILED: %s (%lld vs %lld)\n", what, left, right);
    return 1;
}

int report_cross_check(const SystemStats *st, FILE *out)
{
    if (st == NULL)
        return 1;
    int problems = 0;
    /* Each dispatch is counted twice, independently: once in the unit's
     * statistics (resource_assign) and once as a history node. */
    problems += disagree(out, "unit dispatch counters vs allocations",
                         st->resource_dispatch_sum, st->allocations_total);
    problems += disagree(out, "unit distance totals vs allocation distances",
                         st->resource_distance_sum, st->distance_sum);
    /* Every DISPATCHED incident has exactly one ACTIVE allocation and one
     * DISPATCHED unit; every COMPLETED incident one COMPLETED allocation. */
    problems += disagree(out, "DISPATCHED incidents vs active allocations",
                         st->incidents_by_status[INC_DISPATCHED], st->allocations_active);
    problems += disagree(out, "DISPATCHED units vs active allocations",
                         st->resources_by_status[RES_DISPATCHED], st->allocations_active);
    problems += disagree(out, "COMPLETED incidents vs completed allocations",
                         st->incidents_by_status[INC_COMPLETED], st->allocations_completed);
    /* Live heap entries are exactly the PENDING incidents. */
    problems += disagree(out, "live heap entries vs PENDING incidents",
                         st->heap_entries - st->heap_stale_entries,
                         st->incidents_by_status[INC_PENDING]);
    problems += disagree(out, "allocations naming an unknown incident", st->missing_incident_refs, 0);
    return problems;
}

void report_print_stats(const SystemStats *st, FILE *out)
{
    if (st == NULL || out == NULL)
        return;

    fprintf(out, "SYSTEM STATISTICS (simulated clock = %ld)\n", st->clock);

    fprintf(out, "Incidents: %d\n  by status:", st->incidents_total);
    for (int s = 0; s < INC_STATUS_COUNT; s++)
        fprintf(out, " %s %d", incident_status_name((IncidentStatus)s), st->incidents_by_status[s]);
    fprintf(out, "\n  by type:  ");
    for (int t = 0; t < INC_TYPE_COUNT; t++)
        fprintf(out, " %s %d", incident_type_name((IncidentType)t), st->incidents_by_type[t]);
    fprintf(out, "\n  failed dispatch attempts: %d\n", st->failed_attempts_total);

    fprintf(out, "Queue: %d heap entries (%d live, %d cancelled awaiting lazy removal)\n",
            st->heap_entries, st->heap_entries - st->heap_stale_entries, st->heap_stale_entries);

    fprintf(out, "Units: %d\n  by status:", st->resources_total);
    for (int s = 0; s < RES_STATUS_COUNT; s++)
        fprintf(out, " %s %d", resource_status_name((ResourceStatus)s), st->resources_by_status[s]);
    fprintf(out, "\n  by type:  ");
    for (int t = 0; t < RES_TYPE_COUNT; t++)
        fprintf(out, " %s %d", resource_type_name((ResourceType)t), st->resources_by_type[t]);
    fprintf(out, "\n  dispatches by unit type:");
    for (int t = 0; t < RES_TYPE_COUNT; t++)
        fprintf(out, " %s %d", resource_type_name((ResourceType)t), st->dispatches_by_type[t]);
    fprintf(out, "\n");

    fprintf(out, "Allocations: %d (active %d, completed %d)\n",
            st->allocations_total, st->allocations_active, st->allocations_completed);
    fprintf(out, "  distance: total %lld, average ", st->distance_sum);
    print_average(out, st->distance_sum, st->allocations_total);
    if (st->allocations_total > 0)
        fprintf(out, ", min %d, max %d\n", st->distance_min, st->distance_max);
    else
        fprintf(out, ", min -, max -\n");
    fprintf(out, "  wait (reported -> dispatched): average ");
    print_average(out, st->wait_sum, st->wait_count);
    fprintf(out, " ticks over %d\n", st->wait_count);
    fprintf(out, "  service (dispatched -> completed): average ");
    print_average(out, st->service_sum, st->service_count);
    fprintf(out, " ticks over %d\n", st->service_count);

    fprintf(out, "  average wait by severity:\n");
    for (int s = INCIDENT_SEVERITY_MAX; s >= INCIDENT_SEVERITY_MIN; s--) {
        fprintf(out, "    %d %-9s %3d dispatched, average wait ", s,
                incident_severity_label(s), st->wait_count_by_severity[s]);
        print_average(out, st->wait_sum_by_severity[s], st->wait_count_by_severity[s]);
        fprintf(out, "\n");
    }
}

/* ---------- allocation history ---------- */

void report_filter_all(HistoryFilter *f)
{
    if (f == NULL)
        return;
    f->status = -1;
    f->incident_id = -1;
    f->resource_id = -1;
}

int report_allocation_matches(const Allocation *a, const HistoryFilter *f)
{
    if (a == NULL)
        return 0;
    if (f == NULL)
        return 1;
    if (f->status >= 0 && (int)a->status != f->status)
        return 0;
    if (f->incident_id >= 0 && a->incident_id != f->incident_id)
        return 0;
    if (f->resource_id >= 0 && a->resource_id != f->resource_id)
        return 0;
    return 1;
}

int report_count_history(const AllocationList *list, const HistoryFilter *f)
{
    if (list == NULL)
        return 0;
    int n = 0;
    for (const Allocation *a = list->head; a != NULL; a = a->next)
        if (report_allocation_matches(a, f))
            n++;
    return n;
}

int report_print_history(const AppState *app, const HistoryFilter *f, FILE *out)
{
    if (app == NULL || out == NULL)
        return -1;
    char to_place[PLACE_BUF], from_place[PLACE_BUF];
    int shown = 0;

    for (const Allocation *a = app->history.head; a != NULL; a = a->next) {
        if (!report_allocation_matches(a, f))
            continue;
        const Incident *inc = app_find_incident(app, a->incident_id);
        const Resource *r = app_find_resource(app, a->resource_id);
        format_place(to_place, sizeof to_place, app->graph, a->to_location);
        format_place(from_place, sizeof from_place, app->graph, a->from_location);

        fprintf(out, "#%d %s: incident %d %s (severity %d) at %s\n",
                a->id, allocation_status_name(a->status), a->incident_id,
                inc != NULL ? incident_type_name(inc->type) : "?",
                inc != NULL ? inc->severity : 0, to_place);
        fprintf(out, "    unit %d %s from %s, distance %d, sent at %ld",
                a->resource_id, r != NULL ? resource_type_name(r->type) : "?",
                from_place, a->distance, a->dispatch_seq);
        if (a->status == ALLOC_COMPLETED)
            fprintf(out, ", done at %ld", a->complete_seq);
        if (inc != NULL)
            fprintf(out, ", waited %ld", a->dispatch_seq - inc->reported_seq);
        fprintf(out, "\n    route: ");
        dispatch_print_route(out, app->graph, a->route, a->route_len);
        fprintf(out, "\n");
        shown++;
    }
    if (shown == 0)
        fprintf(out, "(no matching allocations)\n");
    return shown;
}

/* ---------- completed incidents ---------- */

int report_print_completed(const AppState *app, FILE *out)
{
    if (app == NULL || out == NULL)
        return -1;
    char place[PLACE_BUF];
    int shown = 0;

    fprintf(out, "%-8s %-17s %3s  %-24s %4s %-11s %5s %5s %7s\n",
            "INCIDENT", "TYPE", "SEV", "LOCATION", "UNIT", "UNIT TYPE",
            "DIST", "WAIT", "SERVICE");
    for (const Allocation *a = app->history.head; a != NULL; a = a->next) {
        if (a->status != ALLOC_COMPLETED)
            continue;
        const Incident *inc = app_find_incident(app, a->incident_id);
        const Resource *r = app_find_resource(app, a->resource_id);
        format_place(place, sizeof place, app->graph, a->to_location);

        fprintf(out, "%-8d %-17s %3d  %-24s %4d %-11s %5d ",
                a->incident_id, inc != NULL ? incident_type_name(inc->type) : "?",
                inc != NULL ? inc->severity : 0, place, a->resource_id,
                r != NULL ? resource_type_name(r->type) : "?", a->distance);
        if (inc != NULL)
            fprintf(out, "%5ld ", a->dispatch_seq - inc->reported_seq);
        else
            fprintf(out, "%5s ", "?");
        fprintf(out, "%7ld\n", a->complete_seq - a->dispatch_seq);
        shown++;
    }
    if (shown == 0)
        fprintf(out, "(no completed incidents)\n");
    return shown;
}

/* ---------- unit utilisation ---------- */

int report_busier(const Resource *a, const Resource *b)
{
    if (a == NULL || b == NULL)
        return 0;
    if (a->total_dispatches != b->total_dispatches)
        return a->total_dispatches > b->total_dispatches;
    if (a->total_distance != b->total_distance)
        return a->total_distance > b->total_distance;
    return a->id < b->id;
}

int report_rank_resources(const AppState *app, const Resource **ranking, int capacity)
{
    if (app == NULL || ranking == NULL)
        return -1;
    int n = app->resources.count;
    if (capacity < n)
        return -1;

    /* Insertion sort, building the sorted array as we read the store:
     * ranking[0..i-1] is already sorted; shift every entry that ranks
     * BELOW the new unit one place right, then drop the unit in the gap.
     *   best case  O(R)   (store already in ranking order)
     *   worst case O(R^2) (store in reverse order)
     * Fine for a fleet of tens of units, and simple to verify by hand. */
    for (int i = 0; i < n; i++) {
        const Resource *key = app->resources.items[i];
        int j = i - 1;
        while (j >= 0 && report_busier(key, ranking[j])) {
            ranking[j + 1] = ranking[j];
            j--;
        }
        ranking[j + 1] = key;
    }
    return n;
}

int report_print_utilisation(const AppState *app, FILE *out)
{
    if (app == NULL || out == NULL)
        return -1;
    int n = app->resources.count;
    if (n == 0) {
        fprintf(out, "(no units registered)\n");
        return 0;
    }
    const Resource **ranking = malloc((size_t)n * sizeof *ranking);
    if (ranking == NULL)
        return -1;
    if (report_rank_resources(app, ranking, n) != n) {
        free(ranking);
        return -1;
    }

    char place[PLACE_BUF];
    char avg[AVG_BUF];
    fprintf(out, "%4s %4s %-11s %-14s %-24s %10s %10s %8s\n",
            "RANK", "UNIT", "TYPE", "STATUS", "LOCATION", "DISPATCHES", "TOTAL DIST", "AVG DIST");
    for (int i = 0; i < n; i++) {
        const Resource *r = ranking[i];
        format_place(place, sizeof place, app->graph, r->location_id);
        format_average(avg, sizeof avg, r->total_distance, r->total_dispatches);
        fprintf(out, "%4d %4d %-11s %-14s %-24s %10d %10lld %8s\n",
                i + 1, r->id, resource_type_name(r->type), resource_status_name(r->status),
                place, r->total_dispatches, r->total_distance, avg);
    }
    free(ranking);
    return n;
}

/* ---------- full report ---------- */

DcStatus report_print_full(const AppState *app, FILE *out)
{
    if (app == NULL || out == NULL)
        return DC_ERR_INVALID;
    SystemStats st;
    DcStatus status = report_compute_stats(app, &st);
    if (status != DC_OK)
        return status;

    fprintf(out, "==================== DispatchCore report ====================\n");
    fprintf(out, "(academic simulation; times are simulated clock ticks)\n\n");
    report_print_stats(&st, out);
    int problems = report_cross_check(&st, out);
    fprintf(out, "Cross-check: %s\n", problems == 0 ? "all counters agree" : "PROBLEMS FOUND");

    fprintf(out, "\n---- Unit utilisation (busiest first) ----\n");
    if (report_print_utilisation(app, out) < 0)
        return DC_ERR_NOMEM;   /* the only failure left: the ranking array */

    fprintf(out, "\n---- Completed incidents ----\n");
    report_print_completed(app, out);

    fprintf(out, "\n---- Allocation history (oldest first) ----\n");
    report_print_history(app, NULL, out);
    fprintf(out, "==================== end of report ====================\n");
    return DC_OK;
}

DcStatus report_write_file(const AppState *app, const char *path)
{
    if (app == NULL || path == NULL || path[0] == '\0')
        return DC_ERR_INVALID;
    FILE *f = fopen(path, "w");
    if (f == NULL)
        return DC_ERR_IO;

    DcStatus status = report_print_full(app, f);
    if (status == DC_OK && ferror(f))
        status = DC_ERR_IO;        /* e.g. disk full while writing */
    /* fclose flushes the buffer, so it can fail too; always check it. */
    if (fclose(f) != 0 && status == DC_OK)
        status = DC_ERR_IO;
    return status;
}
