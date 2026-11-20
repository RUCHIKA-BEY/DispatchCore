/*
 * menu.c — the interactive menu (see menu.h).
 *
 * Layout: small helpers first (saving, asking for a location / type / ID),
 * then one static function per menu option, then the dispatcher
 * menu_handle() and the loop menu_run().
 *
 * Every helper that asks a question returns InputResult; if the input ends
 * in the middle of an action, MENU_EOF travels back up to menu_run, which
 * then exits cleanly.
 */
#include "menu.h"

#include "compatibility.h"
#include "dijkstra.h"
#include "dispatch.h"
#include "evaluation.h"
#include "file_manager.h"
#include "report.h"

#include <stdlib.h>

/* Largest location list printed before every location question. Bigger
 * cities would flood the screen; option 15 lists them instead. */
#define MENU_LIST_LOCATIONS_MAX 30

/* ---------- saving ---------- */

/* Saves the three record files now. Returns 1 on success. */
static int save_now(MenuSession *s)
{
    FILE *out = s->con->out;
    if (fm_save_records(s->app, s->data_dir, out) == DC_OK) {
        s->dirty = 0;
        if (out != NULL)
            fprintf(out, "  (saved to %s)\n", s->data_dir);
        return 1;
    }
    s->save_failures++;
    if (out != NULL)
        fprintf(out, "  WARNING: saving to %s failed. Your data is still in memory;\n"
                     "  check the folder and use option 22 to try again.\n", s->data_dir);
    return 0;
}

/* Called after every action that changed records. */
static void after_change(MenuSession *s)
{
    s->dirty = 1;
    if (s->autosave)
        save_now(s);
}

/* ---------- asking questions ---------- */

static void print_locations(const MenuSession *s)
{
    FILE *out = s->con->out;
    const Graph *g = s->app->graph;
    if (out == NULL)
        return;
    if (g->num_vertices > MENU_LIST_LOCATIONS_MAX) {
        fprintf(out, "  (%d locations; option 15 lists them)\n", g->num_vertices);
        return;
    }
    fprintf(out, "  Locations:");
    for (int v = 0; v < g->num_vertices; v++)
        fprintf(out, "%s %d %s", v == 0 ? "" : " |", v, graph_location_name(g, v));
    fprintf(out, "\n");
}

static InputResult ask_location(MenuSession *s, const char *prompt, int *location)
{
    print_locations(s);
    return input_read_int(s->con, prompt, 0, s->app->graph->num_vertices - 1, location);
}

static InputResult ask_id(MenuSession *s, const char *prompt, int *id)
{
    return input_read_int(s->con, prompt, 1, FM_MAX_ID, id);
}

static InputResult ask_incident_type(MenuSession *s, IncidentType *type)
{
    FILE *out = s->con->out;
    if (out != NULL) {
        for (int t = 0; t < INC_TYPE_COUNT; t++) {
            fprintf(out, "  %d %-18s needs: ", t + 1, incident_type_name((IncidentType)t));
            compat_print_options(out, (IncidentType)t);
            fprintf(out, "\n");
        }
    }
    int n = 0;
    InputResult r = input_read_int(s->con, "Incident type", 1, INC_TYPE_COUNT, &n);
    if (r == INPUT_OK)
        *type = (IncidentType)(n - 1);    /* the menu counts from 1, the enum from 0 */
    return r;
}

static InputResult ask_resource_type(MenuSession *s, ResourceType *type)
{
    FILE *out = s->con->out;
    if (out != NULL)
        for (int t = 0; t < RES_TYPE_COUNT; t++)
            fprintf(out, "  %d %s\n", t + 1, resource_type_name((ResourceType)t));
    int n = 0;
    InputResult r = input_read_int(s->con, "Unit type", 1, RES_TYPE_COUNT, &n);
    if (r == INPUT_OK)
        *type = (ResourceType)(n - 1);
    return r;
}

static InputResult ask_severity(MenuSession *s, int *severity)
{
    FILE *out = s->con->out;
    if (out != NULL) {
        fprintf(out, " ");
        for (int v = INCIDENT_SEVERITY_MIN; v <= INCIDENT_SEVERITY_MAX; v++)
            fprintf(out, " %d %s", v, incident_severity_label(v));
        fprintf(out, "\n");
    }
    return input_read_int(s->con, "Severity", INCIDENT_SEVERITY_MIN, INCIDENT_SEVERITY_MAX,
                          severity);
}

/* ---------- INCIDENTS ---------- */

static MenuAction opt_register_incident(MenuSession *s)
{
    IncidentType type;
    int location = 0, severity = 0;
    if (ask_incident_type(s, &type) != INPUT_OK ||
        ask_location(s, "Location", &location) != INPUT_OK ||
        ask_severity(s, &severity) != INPUT_OK)
        return MENU_EOF;

    Incident *inc = NULL;
    DcStatus st = app_register_incident(s->app, type, location, severity, &inc);
    FILE *out = s->con->out;
    if (st != DC_OK || inc == NULL) {
        if (out != NULL)
            fprintf(out, "Could not register the incident: %s.\n", dc_status_str(st));
        return MENU_CONTINUE;
    }
    if (out != NULL)
        fprintf(out, "Registered incident %d: %s, severity %d (%s), at %d %s. Status PENDING.\n",
                inc->id, incident_type_name(inc->type), inc->severity,
                incident_severity_label(inc->severity), inc->location_id,
                graph_location_name(s->app->graph, inc->location_id));
    after_change(s);
    return MENU_CONTINUE;
}

static MenuAction opt_view_pending(MenuSession *s)
{
    FILE *out = s->con->out;
    if (out == NULL)
        return MENU_CONTINUE;
    fprintf(out, "Pending incidents, highest priority first:\n");
    if (incident_heap_print_priority_order(&s->app->pending, out, 1) < 0)
        fprintf(out, "Out of memory while copying the queue.\n");
    return MENU_CONTINUE;
}

static MenuAction opt_view_incidents(MenuSession *s)
{
    if (s->con->out != NULL)
        incident_store_print(&s->app->incidents, s->con->out, -1);
    return MENU_CONTINUE;
}

static MenuAction opt_search_incident(MenuSession *s)
{
    int id = 0;
    if (ask_id(s, "Incident ID", &id) != INPUT_OK)
        return MENU_EOF;
    FILE *out = s->con->out;
    if (out == NULL)
        return MENU_CONTINUE;

    const Incident *inc = app_find_incident(s->app, id);   /* O(1) average: hash index */
    if (inc == NULL) {
        fprintf(out, "No incident with ID %d.\n", id);
        return MENU_CONTINUE;
    }
    incident_print_header(out);
    incident_print_row(out, inc, graph_location_name(s->app->graph, inc->location_id));
    if (inc->status == INC_DISPATCHED || inc->status == INC_COMPLETED) {
        HistoryFilter f;
        report_filter_all(&f);
        f.incident_id = id;
        report_print_history(s->app, &f, out);
    }
    return MENU_CONTINUE;
}

static MenuAction opt_cancel_incident(MenuSession *s)
{
    int id = 0;
    if (ask_id(s, "Incident ID to cancel", &id) != INPUT_OK)
        return MENU_EOF;
    FILE *out = s->con->out;
    DcStatus st = dispatch_cancel(s->app, id, out);
    if (st == DC_OK) {
        after_change(s);
    } else if (out != NULL) {
        const Incident *inc = app_find_incident(s->app, id);
        if (inc == NULL)
            fprintf(out, "No incident with ID %d.\n", id);
        else
            fprintf(out, "Incident %d is %s; only PENDING incidents can be cancelled.\n",
                    id, incident_status_name(inc->status));
    }
    return MENU_CONTINUE;
}

/* ---------- RESOURCES ---------- */

static MenuAction opt_register_resource(MenuSession *s)
{
    ResourceType type;
    int location = 0;
    if (ask_resource_type(s, &type) != INPUT_OK ||
        ask_location(s, "Base location", &location) != INPUT_OK)
        return MENU_EOF;

    Resource *r = NULL;
    DcStatus st = app_register_resource(s->app, type, location, &r);
    FILE *out = s->con->out;
    if (st != DC_OK || r == NULL) {
        if (out != NULL)
            fprintf(out, "Could not register the unit: %s.\n", dc_status_str(st));
        return MENU_CONTINUE;
    }
    if (out != NULL)
        fprintf(out, "Registered unit %d: %s at %d %s. Status AVAILABLE.\n",
                r->id, resource_type_name(r->type), r->location_id,
                graph_location_name(s->app->graph, r->location_id));
    after_change(s);
    return MENU_CONTINUE;
}

static MenuAction opt_view_resources(MenuSession *s, int status_filter)
{
    if (s->con->out != NULL)
        resource_store_print(&s->app->resources, s->con->out, status_filter);
    return MENU_CONTINUE;
}

static MenuAction opt_search_resource(MenuSession *s)
{
    int id = 0;
    if (ask_id(s, "Unit ID", &id) != INPUT_OK)
        return MENU_EOF;
    FILE *out = s->con->out;
    if (out == NULL)
        return MENU_CONTINUE;

    const Resource *r = app_find_resource(s->app, id);
    if (r == NULL) {
        fprintf(out, "No unit with ID %d.\n", id);
        return MENU_CONTINUE;
    }
    resource_print_header(out);
    resource_print_row(out, r, graph_location_name(s->app->graph, r->location_id));
    fprintf(out, "History of unit %d:\n", id);
    HistoryFilter f;
    report_filter_all(&f);
    f.resource_id = id;
    report_print_history(s->app, &f, out);
    return MENU_CONTINUE;
}

static MenuAction opt_change_service(MenuSession *s)
{
    int id = 0;
    if (ask_id(s, "Unit ID", &id) != INPUT_OK)
        return MENU_EOF;
    FILE *out = s->con->out;
    Resource *r = app_find_resource(s->app, id);
    if (r == NULL) {
        if (out != NULL)
            fprintf(out, "No unit with ID %d.\n", id);
        return MENU_CONTINUE;
    }
    if (r->status == RES_DISPATCHED) {
        if (out != NULL)
            fprintf(out, "Unit %d is DISPATCHED to incident %d. Complete that incident first.\n",
                    id, r->assigned_incident_id);
        return MENU_CONTINUE;
    }

    int taking_out = (r->status == RES_AVAILABLE);
    if (out != NULL)
        fprintf(out, "Unit %d is %s.\n", id, resource_status_name(r->status));
    int yes = 0;
    if (input_read_yes_no(s->con, taking_out ? "Take it out of service?"
                                             : "Return it to service?", &yes) != INPUT_OK)
        return MENU_EOF;
    if (!yes) {
        if (out != NULL)
            fprintf(out, "No change.\n");
        return MENU_CONTINUE;
    }

    DcStatus st = taking_out ? resource_set_out_of_service(r) : resource_return_to_service(r);
    if (st != DC_OK) {
        if (out != NULL)
            fprintf(out, "Could not change the status: %s.\n", dc_status_str(st));
        return MENU_CONTINUE;
    }
    if (out != NULL)
        fprintf(out, "Unit %d is now %s.\n", id, resource_status_name(r->status));
    after_change(s);
    return MENU_CONTINUE;
}

/* ---------- DISPATCH ---------- */

static MenuAction opt_dispatch_next(MenuSession *s)
{
    DispatchResult result;
    DcStatus st = dispatch_next(s->app, s->con->out, &result);
    if (st != DC_OK) {
        if (s->con->out != NULL)
            fprintf(s->con->out, "Dispatch failed: %s.\n", dc_status_str(st));
        return MENU_CONTINUE;
    }
    /* A dispatch changes records; so does a failed attempt (its counter). */
    if (result.attempted > 0)
        after_change(s);
    return MENU_CONTINUE;
}

static MenuAction opt_dispatch_all(MenuSession *s)
{
    int queued = incident_heap_size(&s->app->pending);
    int n = dispatch_all(s->app, s->con->out);
    FILE *out = s->con->out;
    if (n < 0) {
        if (out != NULL)
            fprintf(out, "Dispatch stopped because of an error (out of memory).\n");
    } else if (out != NULL) {
        fprintf(out, "%d incident(s) dispatched.\n", n);
    }
    if (queued > 0)
        after_change(s);       /* dispatches and/or attempt counters changed */
    return MENU_CONTINUE;
}

static MenuAction opt_view_active(MenuSession *s)
{
    if (s->con->out != NULL)
        dispatch_print_active(s->app, s->con->out);
    return MENU_CONTINUE;
}

static MenuAction opt_complete(MenuSession *s)
{
    int id = 0;
    if (ask_id(s, "Incident ID to complete", &id) != INPUT_OK)
        return MENU_EOF;
    FILE *out = s->con->out;
    DcStatus st = dispatch_complete(s->app, id, out);
    if (st == DC_OK) {
        after_change(s);
    } else if (out != NULL) {
        const Incident *inc = app_find_incident(s->app, id);
        if (inc == NULL)
            fprintf(out, "No incident with ID %d.\n", id);
        else if (inc->status != INC_DISPATCHED)
            fprintf(out, "Incident %d is %s; only DISPATCHED incidents can be completed.\n",
                    id, incident_status_name(inc->status));
        else
            fprintf(out, "Could not complete incident %d: %s (records disagree).\n",
                    id, dc_status_str(st));
    }
    return MENU_CONTINUE;
}

/* ---------- ROAD NETWORK ---------- */

static MenuAction opt_show_network(MenuSession *s)
{
    FILE *out = s->con->out;
    if (out == NULL)
        return MENU_CONTINUE;
    graph_print(s->app->graph, out);
    int groups = graph_count_components(s->app->graph);
    if (groups > 1)
        fprintf(out, "The city has %d separate groups of locations; no road joins them.\n",
                groups);
    return MENU_CONTINUE;
}

static MenuAction opt_shortest_route(MenuSession *s)
{
    int from = 0, to = 0;
    if (ask_location(s, "From", &from) != INPUT_OK ||
        input_read_int(s->con, "To", 0, s->app->graph->num_vertices - 1, &to) != INPUT_OK)
        return MENU_EOF;
    FILE *out = s->con->out;
    if (out == NULL)
        return MENU_CONTINUE;

    ShortestPaths sp;
    if (dijkstra_run(s->app->graph, from, &sp) != DC_OK) {
        fprintf(out, "Out of memory while computing the route.\n");
        return MENU_CONTINUE;
    }
    int dist = shortest_paths_distance(&sp, to);
    if (dist == DC_INF) {
        fprintf(out, "No road connects %d %s and %d %s.\n",
                from, graph_location_name(s->app->graph, from),
                to, graph_location_name(s->app->graph, to));
    } else {
        int *path = NULL, len = 0;
        if (dijkstra_build_path(&sp, to, &path, &len) == DC_OK) {
            fprintf(out, "Shortest distance %d. Route: ", dist);
            dispatch_print_route(out, s->app->graph, path, len);
            fprintf(out, "\n");
            free(path);
        } else {
            fprintf(out, "Could not build the route.\n");
        }
    }
    shortest_paths_free(&sp);
    return MENU_CONTINUE;
}

/* ---------- REPORTS ---------- */

static MenuAction opt_history(MenuSession *s)
{
    FILE *out = s->con->out;
    if (out != NULL)
        fprintf(out, "  1 All  2 Active only  3 Completed only  4 One unit  5 One incident\n");
    int which = 0;
    if (input_read_int(s->con, "Show", 1, 5, &which) != INPUT_OK)
        return MENU_EOF;

    HistoryFilter f;
    report_filter_all(&f);
    if (which == 2)
        f.status = ALLOC_ACTIVE;
    else if (which == 3)
        f.status = ALLOC_COMPLETED;
    else if (which == 4 && ask_id(s, "Unit ID", &f.resource_id) != INPUT_OK)
        return MENU_EOF;
    else if (which == 5 && ask_id(s, "Incident ID", &f.incident_id) != INPUT_OK)
        return MENU_EOF;

    if (out != NULL) {
        int n = report_print_history(s->app, &f, out);
        fprintf(out, "%d allocation(s) shown.\n", n);
    }
    return MENU_CONTINUE;
}

static MenuAction opt_statistics(MenuSession *s)
{
    FILE *out = s->con->out;
    if (out == NULL)
        return MENU_CONTINUE;
    SystemStats st;
    if (report_compute_stats(s->app, &st) != DC_OK)
        return MENU_CONTINUE;
    report_print_stats(&st, out);
    int problems = report_cross_check(&st, out);
    fprintf(out, "Cross-check: %s\n", problems == 0 ? "all counters agree" : "PROBLEMS FOUND");
    fprintf(out, "\nUnit utilisation (busiest first):\n");
    if (report_print_utilisation(s->app, out) < 0)
        fprintf(out, "Out of memory while ranking units.\n");
    return MENU_CONTINUE;
}

static MenuAction opt_debug_view(MenuSession *s)
{
    FILE *out = s->con->out;
    if (out == NULL)
        return MENU_CONTINUE;
    fprintf(out, "Incident index (hash table, ID -> record):\n");
    hash_table_print(s->app->incident_index, out);
    fprintf(out, "\nUnit index (hash table, ID -> record):\n");
    hash_table_print(s->app->resource_index, out);
    fprintf(out, "\nPending queue as stored (binary max-heap array, NOT sorted):\n");
    incident_heap_print_array(&s->app->pending, out);
    return MENU_CONTINUE;
}

/* ---------- SYSTEM ---------- */

static MenuAction opt_write_report(MenuSession *s)
{
    FILE *out = s->con->out;
    char path[FM_PATH_MAX];
    int n = snprintf(path, sizeof path, "%s/report.txt", s->data_dir);
    if (n < 0 || (size_t)n >= sizeof path) {          /* would not fit: refuse */
        if (out != NULL)
            fprintf(out, "The data folder path is too long.\n");
        return MENU_CONTINUE;
    }
    DcStatus st = report_write_file(s->app, path);
    if (out != NULL) {
        if (st == DC_OK)
            fprintf(out, "Report written to %s\n", path);
        else
            fprintf(out, "Could not write %s: %s.\n", path, dc_status_str(st));
    }
    return MENU_CONTINUE;
}

/* ---------- menu ---------- */

void menu_session_init(MenuSession *s, AppState *app, Console *con,
                       const char *data_dir, int autosave)
{
    if (s == NULL)
        return;
    s->app = app;
    s->con = con;
    s->data_dir = data_dir;
    s->autosave = autosave;
    s->dirty = 0;
    s->save_failures = 0;
}

void menu_print(const MenuSession *s)
{
    if (s == NULL || s->con == NULL || s->con->out == NULL)
        return;
    fputs("\n================ DispatchCore ================\n"
          " INCIDENTS\n"
          "   1. Register incident\n"
          "   2. View pending incidents (priority order)\n"
          "   3. View all incidents\n"
          "   4. Search incident by ID\n"
          "   5. Cancel pending incident\n"
          " RESOURCES\n"
          "   6. Register unit\n"
          "   7. View available units\n"
          "   8. View all units\n"
          "   9. Search unit by ID\n"
          "  10. Take unit out of / back into service\n"
          " DISPATCH\n"
          "  11. Dispatch next emergency\n"
          "  12. Dispatch all possible\n"
          "  13. View active dispatches\n"
          "  14. Complete incident\n"
          " ROAD NETWORK\n"
          "  15. Display road network\n"
          "  16. Find shortest route\n"
          "  17. Close / reopen road              [not implemented]\n"
          " REPORTS\n"
          "  18. Allocation history\n"
          "  19. System statistics\n"
          "  20. Compare dispatch strategies (simulation)\n"
          "  21. Hash table and heap debug view\n"
          " SYSTEM\n"
          "  22. Save data now\n"
          "  23. Write full report to a text file\n", s->con->out);
    fprintf(s->con->out, "   0. Exit%s\n", s->autosave ? " (saves first)" : " (autosave is OFF)");
    fputs("==============================================\n", s->con->out);
}

MenuAction menu_handle(MenuSession *s, int choice)
{
    if (s == NULL || s->app == NULL || s->con == NULL)
        return MENU_EXIT;
    FILE *out = s->con->out;

    switch (choice) {
    case 0:  return MENU_EXIT;
    case 1:  return opt_register_incident(s);
    case 2:  return opt_view_pending(s);
    case 3:  return opt_view_incidents(s);
    case 4:  return opt_search_incident(s);
    case 5:  return opt_cancel_incident(s);
    case 6:  return opt_register_resource(s);
    case 7:  return opt_view_resources(s, RES_AVAILABLE);
    case 8:  return opt_view_resources(s, -1);
    case 9:  return opt_search_resource(s);
    case 10: return opt_change_service(s);
    case 11: return opt_dispatch_next(s);
    case 12: return opt_dispatch_all(s);
    case 13: return opt_view_active(s);
    case 14: return opt_complete(s);
    case 15: return opt_show_network(s);
    case 16: return opt_shortest_route(s);
    case 17:
        if (out != NULL)
            fprintf(out, "Road closures are an optional feature that has not been implemented.\n"
                         "Roads are read from roads.csv at start-up and cannot be changed while running.\n");
        return MENU_CONTINUE;
    case 18: return opt_history(s);
    case 19: return opt_statistics(s);
    case 20:
        /* Runs on COPIES of the map with generated fleets and incidents;
         * the live data is not read or changed (except the map, read-only). */
        if (out != NULL && eval_run_suite(s->app->graph, out) != DC_OK)
            fprintf(out, "The comparison could not be completed (out of memory).\n");
        return MENU_CONTINUE;
    case 21: return opt_debug_view(s);
    case 22:
        save_now(s);
        return MENU_CONTINUE;
    case 23: return opt_write_report(s);
    default:
        /* input_read_int already limits the range; this is a safety net. */
        if (out != NULL)
            fprintf(out, "Unknown choice %d.\n", choice);
        return MENU_CONTINUE;
    }
}

int menu_run(MenuSession *s)
{
    if (s == NULL || s->app == NULL || s->con == NULL)
        return 1;
    FILE *out = s->con->out;

    MenuAction action = MENU_CONTINUE;
    while (action == MENU_CONTINUE) {
        menu_print(s);
        int choice = 0;
        if (input_read_int(s->con, "Choice", 0, MENU_MAX_CHOICE, &choice) != INPUT_OK) {
            action = MENU_EOF;
            break;
        }
        action = menu_handle(s, choice);
    }

    if (action == MENU_EOF && out != NULL)
        fprintf(out, "End of input.\n");

    /* Exit part of the saving rule (menu.h). */
    if (s->dirty) {
        if (s->autosave) {
            save_now(s);
        } else if (out != NULL) {
            fprintf(out, "Autosave is off: unsaved changes are discarded.\n");
        }
    }
    if (out != NULL)
        fprintf(out, "Goodbye.\n");
    /* Failure only if autosave was on and the final save did not succeed.
     * (An earlier failure that a later save repaired does not count.) */
    return (s->dirty && s->autosave) ? 1 : 0;
}
