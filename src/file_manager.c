/*
 * file_manager.c — CSV loading and saving (see file_manager.h).
 *
 * Structure:
 *   1. small helpers: paths, numbers, lines, fields
 *   2. one loader per file
 *   3. fm_load (the order and the final consistency check)
 *   4. writers and the safe-save procedure
 */
#include "file_manager.h"
#include "dijkstra.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SEQ   2000000000L    /* fits in a 32-bit long on Windows */
#define MAX_DIST  ((long long)DC_MAX_VERTICES * DC_MAX_WEIGHT)

static const char *const HDR_LOCATIONS = "location_id,name";
static const char *const HDR_ROADS = "from_id,to_id,distance,status";
static const char *const HDR_RESOURCES =
    "resource_id,type,location_id,status,assigned_incident_id,total_dispatches,total_distance";
static const char *const HDR_INCIDENTS =
    "incident_id,type,location_id,severity,reported_seq,status,assigned_resource_id,dispatch_attempts";
static const char *const HDR_ALLOCATIONS =
    "allocation_id,incident_id,resource_id,from_location,to_location,distance,route,status,"
    "dispatch_seq,complete_seq";

/* =====================================================================
 * 1. Helpers
 * ===================================================================== */

/* Builds "<dir>/<name><suffix>". Returns 0 if it would not fit. */
static int make_path(char *buf, size_t size, const char *dir, const char *name,
                     const char *suffix)
{
    int n = snprintf(buf, size, "%s/%s%s", dir, name, suffix);
    return n > 0 && (size_t)n < size;
}

/* Strict number parser: the WHOLE field must be a decimal integer within
 * [min, max]. Rejects "", " 4", "+4", "4x", "4.0" and out-of-range values. */
static int parse_ll(const char *s, long long min, long long max, long long *out)
{
    if (s == NULL || !(*s == '-' || (*s >= '0' && *s <= '9')))
        return 0;
    errno = 0;
    char *end = NULL;
    long long v = strtoll(s, &end, 10);
    if (errno == ERANGE || end == s || *end != '\0' || v < min || v > max)
        return 0;
    *out = v;
    return 1;
}

static int parse_int(const char *s, int min, int max, int *out)
{
    long long v;
    if (!parse_ll(s, min, max, &v))
        return 0;
    *out = (int)v;     /* safe: v is within [min, max] */
    return 1;
}

static int parse_long(const char *s, long min, long max, long *out)
{
    long long v;
    if (!parse_ll(s, min, max, &v))
        return 0;
    *out = (long)v;
    return 1;
}

/* Splits a line at commas, in place. Returns the number of fields, or
 * max_fields + 1 if there are more than max_fields. */
static int split_fields(char *line, char **fields, int max_fields)
{
    int n = 0;
    char *p = line;
    for (;;) {
        if (n == max_fields)
            return max_fields + 1;
        fields[n++] = p;
        char *comma = strchr(p, ',');
        if (comma == NULL)
            return n;
        *comma = '\0';
        p = comma + 1;
    }
}

/* State of one file being read. */
typedef struct {
    FILE *f;
    char path[FM_PATH_MAX];
    int line_no;
    FILE *log;
    FmFileCount *count;
} Reader;

static void warn_skip(Reader *rd, const char *reason)
{
    rd->count->skipped++;
    if (rd->log != NULL)
        fprintf(rd->log, "  %s:%d: %s - row skipped\n", rd->path, rd->line_no, reason);
}

static void fatal(FILE *log, const char *path, int line_no, const char *reason)
{
    if (log == NULL)
        return;
    if (line_no > 0)
        fprintf(log, "  %s:%d: %s\n", path, line_no, reason);
    else
        fprintf(log, "  %s: %s\n", path, reason);
}

/* Reads one line into buf without its line ending.
 * Returns 1 if a line was read, 0 at end of file. *too_long is set when the
 * line did not fit; the rest of that line is consumed and discarded. */
static int read_line(Reader *rd, char *buf, size_t size, int *too_long)
{
    *too_long = 0;
    if (fgets(buf, (int)size, rd->f) == NULL)
        return 0;
    rd->line_no++;

    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[--len] = '\0';
    } else {
        int c = fgetc(rd->f);           /* is there more on this line? */
        if (c != EOF && c != '\n') {
            *too_long = 1;
            while ((c = fgetc(rd->f)) != EOF && c != '\n')
                ;                       /* discard the rest of the line */
        }
    }
    if (len > 0 && buf[len - 1] == '\r')  /* Windows line ending */
        buf[--len] = '\0';
    return 1;
}

/* Opens <dir>/<name> and checks its header line.
 * Returns DC_OK (file open), DC_ERR_NOT_FOUND (file does not exist) or
 * DC_ERR_IO (cannot be opened for another reason, or wrong header). */
static DcStatus open_reader(Reader *rd, const char *dir, const char *name,
                            const char *header, FILE *log, FmFileCount *count)
{
    rd->f = NULL;
    rd->line_no = 0;
    rd->log = log;
    rd->count = count;
    if (!make_path(rd->path, sizeof rd->path, dir, name, "")) {
        fatal(log, name, 0, "path too long");
        return DC_ERR_IO;
    }

    errno = 0;
    rd->f = fopen(rd->path, "r");
    if (rd->f == NULL) {
        if (errno == ENOENT) {
            count->missing = 1;
            return DC_ERR_NOT_FOUND;
        }
        /* NOT treated as "missing": an unreadable file must not later be
         * overwritten by an empty save. */
        fatal(log, rd->path, 0, "file exists but cannot be opened");
        return DC_ERR_IO;
    }

    char line[FM_LINE_MAX];
    int too_long = 0;
    if (!read_line(rd, line, sizeof line, &too_long) || too_long) {
        fatal(log, rd->path, 1, "missing or unreadable header line");
        fclose(rd->f);
        rd->f = NULL;
        return DC_ERR_IO;
    }
    const char *text = line;
    if ((unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
        text += 3;                     /* skip a UTF-8 byte-order mark (Excel adds one) */
    if (strcmp(text, header) != 0) {
        fatal(log, rd->path, 1, "unexpected header line (wrong file or wrong format)");
        if (log != NULL)
            fprintf(log, "    expected: %s\n", header);
        fclose(rd->f);
        rd->f = NULL;
        return DC_ERR_IO;
    }
    return DC_OK;
}

/* Closes the reader; DC_ERR_IO if a read error happened at any point. */
static DcStatus close_reader(Reader *rd)
{
    DcStatus st = ferror(rd->f) ? DC_ERR_IO : DC_OK;
    if (st != DC_OK)
        fatal(rd->log, rd->path, 0, "read error");
    fclose(rd->f);
    rd->f = NULL;
    return st;
}

/* =====================================================================
 * 2. Loaders
 * ===================================================================== */

static DcStatus load_locations(const char *dir, FILE *log, FmFileCount *count, Graph **out)
{
    *out = NULL;
    Reader rd;
    DcStatus st = open_reader(&rd, dir, "locations.csv", HDR_LOCATIONS, log, count);
    if (st == DC_ERR_NOT_FOUND) {
        fatal(log, rd.path, 0, "not found - a road network is required to start");
        return DC_ERR_IO;
    }
    if (st != DC_OK)
        return st;

    /* Locations are collected first, because the graph's size (largest
     * ID + 1) is only known after reading the whole file. */
    char (*names)[DC_NAME_MAX] = calloc(DC_MAX_VERTICES, sizeof *names);
    int *seen_at = calloc(DC_MAX_VERTICES, sizeof *seen_at);   /* 0 = not seen */
    if (names == NULL || seen_at == NULL) {
        free(names);
        free(seen_at);
        fclose(rd.f);
        return DC_ERR_NOMEM;
    }

    int max_id = -1;
    char line[FM_LINE_MAX];
    int too_long;
    while (read_line(&rd, line, sizeof line, &too_long)) {
        if (too_long) { warn_skip(&rd, "line too long"); continue; }
        if (line[0] == '\0') continue;
        char *f[3];
        int id;
        if (split_fields(line, f, 2) != 2) { warn_skip(&rd, "expected 2 fields"); continue; }
        if (!parse_int(f[0], 0, DC_MAX_VERTICES - 1, &id)) { warn_skip(&rd, "bad location_id"); continue; }
        if (strlen(f[1]) >= DC_NAME_MAX) { warn_skip(&rd, "name too long"); continue; }
        if (seen_at[id] != 0) { warn_skip(&rd, "duplicate location_id"); continue; }
        seen_at[id] = rd.line_no;
        snprintf(names[id], DC_NAME_MAX, "%s", f[1]);
        if (id > max_id)
            max_id = id;
        count->loaded++;
    }
    st = close_reader(&rd);

    Graph *g = NULL;
    if (st == DC_OK && max_id < 0) {
        fatal(log, rd.path, 0, "contains no valid locations");
        st = DC_ERR_IO;
    }
    for (int id = 0; st == DC_OK && id <= max_id; id++) {
        if (seen_at[id] == 0) {
            if (log != NULL)
                fprintf(log, "  %s: location %d is missing (IDs must be 0..%d with no gaps)\n",
                        rd.path, id, max_id);
            st = DC_ERR_IO;
        }
    }
    if (st == DC_OK) {
        g = graph_create(max_id + 1);
        if (g == NULL)
            st = DC_ERR_NOMEM;
    }
    for (int id = 0; st == DC_OK && id <= max_id; id++) {
        if (graph_set_location_name(g, id, names[id]) != DC_OK) {
            fatal(log, rd.path, seen_at[id], "invalid location name (empty or illegal characters)");
            st = DC_ERR_IO;
        }
    }
    free(names);
    free(seen_at);
    if (st != DC_OK) {
        graph_destroy(g);
        return st;
    }
    *out = g;
    return DC_OK;
}

static DcStatus load_roads(Graph *g, const char *dir, FILE *log, FmFileCount *count)
{
    Reader rd;
    DcStatus st = open_reader(&rd, dir, "roads.csv", HDR_ROADS, log, count);
    if (st == DC_ERR_NOT_FOUND) {
        if (log != NULL)
            fprintf(log, "  %s: not found - the city has no roads\n", rd.path);
        return DC_OK;
    }
    if (st != DC_OK)
        return st;

    char line[FM_LINE_MAX];
    int too_long;
    while (read_line(&rd, line, sizeof line, &too_long)) {
        if (too_long) { warn_skip(&rd, "line too long"); continue; }
        if (line[0] == '\0') continue;
        char *f[5];
        int u, v, w;
        if (split_fields(line, f, 4) != 4) { warn_skip(&rd, "expected 4 fields"); continue; }
        if (!parse_int(f[0], 0, INT_MAX, &u) || !parse_int(f[1], 0, INT_MAX, &v)) {
            warn_skip(&rd, "bad location ID");
            continue;
        }
        if (!parse_int(f[2], 1, DC_MAX_WEIGHT, &w)) { warn_skip(&rd, "bad distance"); continue; }
        if (strcmp(f[3], "CLOSED") == 0) {
            warn_skip(&rd, "road closures are not supported yet");
            continue;
        }
        if (strcmp(f[3], "OPEN") != 0) { warn_skip(&rd, "status must be OPEN"); continue; }
        DcStatus add = graph_add_road(g, u, v, w);
        if (add == DC_ERR_NOMEM) {
            fclose(rd.f);
            return DC_ERR_NOMEM;
        }
        if (add != DC_OK) {
            warn_skip(&rd, add == DC_ERR_DUPLICATE ? "duplicate road"
                                                   : "invalid road (unknown location or self-loop)");
            continue;
        }
        count->loaded++;
    }
    return close_reader(&rd);
}

static DcStatus load_resources(AppState *app, const char *dir, FILE *log, FmFileCount *count)
{
    Reader rd;
    DcStatus st = open_reader(&rd, dir, "resources.csv", HDR_RESOURCES, log, count);
    if (st == DC_ERR_NOT_FOUND)
        return DC_OK;                 /* no resources yet: fine */
    if (st != DC_OK)
        return st;

    char line[FM_LINE_MAX];
    int too_long;
    while (read_line(&rd, line, sizeof line, &too_long)) {
        if (too_long) { warn_skip(&rd, "line too long"); continue; }
        if (line[0] == '\0') continue;
        char *f[8];
        if (split_fields(line, f, 7) != 7) { warn_skip(&rd, "expected 7 fields"); continue; }

        Resource fields;
        long long distance;
        if (!parse_int(f[0], 1, FM_MAX_ID, &fields.id) ||
            resource_type_from_name(f[1], &fields.type) != DC_OK ||
            !parse_int(f[2], 0, INT_MAX, &fields.location_id) ||
            resource_status_from_name(f[3], &fields.status) != DC_OK ||
            !parse_int(f[4], -1, FM_MAX_ID, &fields.assigned_incident_id) ||
            !parse_int(f[5], 0, INT_MAX, &fields.total_dispatches) ||
            !parse_ll(f[6], 0, LLONG_MAX / 2, &distance)) {
            warn_skip(&rd, "bad value (number, type or status)");
            continue;
        }
        fields.total_distance = distance;

        Resource *r = NULL;
        st = resource_restore(&fields, app_num_locations(app), &r);
        if (st == DC_ERR_NOMEM) { fclose(rd.f); return st; }
        if (st != DC_OK) { warn_skip(&rd, "invalid resource (location or status/assignment)"); continue; }
        st = app_add_loaded_resource(app, r);
        if (st != DC_OK) {
            free(r);
            if (st == DC_ERR_NOMEM) { fclose(rd.f); return st; }
            warn_skip(&rd, "duplicate resource_id");
            continue;
        }
        count->loaded++;
    }
    return close_reader(&rd);
}

static DcStatus load_incidents(AppState *app, const char *dir, FILE *log, FmFileCount *count)
{
    Reader rd;
    DcStatus st = open_reader(&rd, dir, "incidents.csv", HDR_INCIDENTS, log, count);
    if (st == DC_ERR_NOT_FOUND)
        return DC_OK;
    if (st != DC_OK)
        return st;

    char line[FM_LINE_MAX];
    int too_long;
    while (read_line(&rd, line, sizeof line, &too_long)) {
        if (too_long) { warn_skip(&rd, "line too long"); continue; }
        if (line[0] == '\0') continue;
        char *f[9];
        if (split_fields(line, f, 8) != 8) { warn_skip(&rd, "expected 8 fields"); continue; }

        Incident fields;
        if (!parse_int(f[0], 1, FM_MAX_ID, &fields.id) ||
            incident_type_from_name(f[1], &fields.type) != DC_OK ||
            !parse_int(f[2], 0, INT_MAX, &fields.location_id) ||
            !parse_int(f[3], INT_MIN, INT_MAX, &fields.severity) ||
            !parse_long(f[4], 0, MAX_SEQ, &fields.reported_seq) ||
            incident_status_from_name(f[5], &fields.status) != DC_OK ||
            !parse_int(f[6], -1, FM_MAX_ID, &fields.assigned_resource_id) ||
            !parse_int(f[7], 0, INT_MAX, &fields.dispatch_attempts)) {
            warn_skip(&rd, "bad value (number, type or status)");
            continue;
        }

        Incident *inc = NULL;
        st = incident_restore(&fields, app_num_locations(app), &inc);
        if (st == DC_ERR_NOMEM) { fclose(rd.f); return st; }
        if (st != DC_OK) {
            warn_skip(&rd, "invalid incident (severity, location or status/assignment)");
            continue;
        }
        st = app_add_loaded_incident(app, inc);
        if (st != DC_OK) {
            free(inc);
            if (st == DC_ERR_NOMEM) { fclose(rd.f); return st; }
            warn_skip(&rd, "duplicate incident_id");
            continue;
        }
        count->loaded++;
    }
    return close_reader(&rd);
}

/* Parses "0|3|5" into a new array. Returns 1 on success. */
static int parse_route(const char *text, int num_vertices, int **route_out, int *len_out)
{
    *route_out = NULL;
    *len_out = 0;
    int len = 1;
    for (const char *p = text; *p != '\0'; p++)
        if (*p == '|')
            len++;
    if (len > num_vertices)
        return 0;                    /* a simple path visits each vertex at most once */

    int *route = malloc((size_t)len * sizeof *route);
    char *copy = malloc(strlen(text) + 1);
    if (route == NULL || copy == NULL) {
        free(route);
        free(copy);
        return 0;
    }
    strcpy(copy, text);              /* safe: copy has exactly strlen + 1 bytes */

    int ok = 1;
    char *p = copy;
    for (int i = 0; i < len && ok; i++) {
        char *bar = strchr(p, '|');
        if (bar != NULL)
            *bar = '\0';
        ok = parse_int(p, 0, num_vertices - 1, &route[i]);
        p = (bar != NULL) ? bar + 1 : p + strlen(p);
    }
    free(copy);
    if (!ok) {
        free(route);
        return 0;
    }
    *route_out = route;
    *len_out = len;
    return 1;
}

static DcStatus load_allocations(AppState *app, const char *dir, FILE *log, FmFileCount *count)
{
    Reader rd;
    DcStatus st = open_reader(&rd, dir, "allocations.csv", HDR_ALLOCATIONS, log, count);
    if (st == DC_ERR_NOT_FOUND)
        return DC_OK;
    if (st != DC_OK)
        return st;

    const int n = app_num_locations(app);
    char line[FM_LINE_MAX];
    int too_long;
    while (read_line(&rd, line, sizeof line, &too_long)) {
        if (too_long) { warn_skip(&rd, "line too long"); continue; }
        if (line[0] == '\0') continue;
        char *f[11];
        if (split_fields(line, f, 10) != 10) { warn_skip(&rd, "expected 10 fields"); continue; }

        int id, incident_id, resource_id, from, to, distance;
        long dispatch_seq, complete_seq;
        AllocationStatus status;
        if (!parse_int(f[0], 1, FM_MAX_ID, &id) ||
            !parse_int(f[1], 1, FM_MAX_ID, &incident_id) ||
            !parse_int(f[2], 1, FM_MAX_ID, &resource_id) ||
            !parse_int(f[3], 0, n - 1, &from) ||
            !parse_int(f[4], 0, n - 1, &to) ||
            !parse_int(f[5], 0, (int)MAX_DIST, &distance) ||
            allocation_status_from_name(f[7], &status) != DC_OK ||
            !parse_long(f[8], 0, MAX_SEQ, &dispatch_seq) ||
            !parse_long(f[9], -1, MAX_SEQ, &complete_seq)) {
            warn_skip(&rd, "bad value (number, location or status)");
            continue;
        }

        int *route = NULL, route_len = 0;
        if (!parse_route(f[6], n, &route, &route_len)) {
            warn_skip(&rd, "bad route");
            continue;
        }
        /* The route must be a real road path of the stated length. */
        if (!dijkstra_validate_path(app->graph, route, route_len, from, to, distance)) {
            free(route);
            warn_skip(&rd, "route does not match the road network or the distance");
            continue;
        }
        st = app_add_loaded_allocation(app, id, incident_id, resource_id, from, to, distance,
                                       route, route_len, status, dispatch_seq, complete_seq);
        if (st != DC_OK) {
            free(route);                 /* not taken: still ours */
            warn_skip(&rd, st == DC_ERR_DUPLICATE ? "duplicate allocation_id"
                                                  : "invalid allocation (times or status)");
            continue;
        }
        count->loaded++;
    }
    return close_reader(&rd);
}

/* =====================================================================
 * 3. fm_load
 * ===================================================================== */

static void clear_report(FmLoadReport *r)
{
    FmFileCount zero = {0, 0, 0};
    r->locations = r->roads = r->resources = r->incidents = r->allocations = zero;
    r->consistency_problems = 0;
}

DcStatus fm_load(AppState *app, const char *dir, FILE *log, FmLoadReport *report)
{
    if (app == NULL || report == NULL)
        return DC_ERR_INVALID;
    clear_report(report);
    app_init(app, NULL);                 /* fails on purpose: leaves *app safely empty */
    if (dir == NULL)
        return DC_ERR_INVALID;

    Graph *g = NULL;
    DcStatus st = load_locations(dir, log, &report->locations, &g);
    if (st != DC_OK)
        return st;
    st = load_roads(g, dir, log, &report->roads);
    if (st != DC_OK) {
        graph_destroy(g);
        return st;
    }
    st = app_init(app, g);
    if (st != DC_OK) {
        graph_destroy(g);                /* not taken over on failure */
        return st;
    }

    /* Order matters for readability of warnings only; cross-file rules are
     * checked once everything is in memory. */
    st = load_resources(app, dir, log, &report->resources);
    if (st == DC_OK)
        st = load_incidents(app, dir, log, &report->incidents);
    if (st == DC_OK)
        st = load_allocations(app, dir, log, &report->allocations);
    if (st != DC_OK) {
        app_free(app);
        return st;
    }

    app_finish_loading(app);
    report->consistency_problems = app_check_consistency(app, log);
    if (report->consistency_problems != 0) {
        if (log != NULL)
            fprintf(log, "  The saved records contradict each other (%d problem(s)); "
                         "refusing to load rather than guess. Files were not changed.\n",
                    report->consistency_problems);
        app_free(app);
        return DC_ERR_STATE;
    }
    return DC_OK;
}

void fm_print_report(const FmLoadReport *r, FILE *out)
{
    if (r == NULL || out == NULL)
        return;
    const char *names[5] = {"locations", "roads", "resources", "incidents", "allocations"};
    const FmFileCount *c[5] = {&r->locations, &r->roads, &r->resources, &r->incidents,
                               &r->allocations};
    for (int i = 0; i < 5; i++) {
        if (c[i]->missing)
            fprintf(out, "  %-12s file not found\n", names[i]);
        else
            fprintf(out, "  %-12s %d loaded, %d skipped\n", names[i], c[i]->loaded, c[i]->skipped);
    }
}

/* =====================================================================
 * 4. Saving
 * ===================================================================== */

/* Each writer returns 1 if every fprintf succeeded. fprintf returns a
 * negative number on error, so the results are combined with &=. */
static int write_resources(FILE *f, const AppState *app)
{
    int ok = fprintf(f, "%s\n", HDR_RESOURCES) >= 0;
    for (int i = 0; i < app->resources.count; i++) {
        const Resource *r = app->resources.items[i];
        ok &= fprintf(f, "%d,%s,%d,%s,%d,%d,%lld\n", r->id, resource_type_name(r->type),
                      r->location_id, resource_status_name(r->status),
                      r->assigned_incident_id, r->total_dispatches, r->total_distance) >= 0;
    }
    return ok;
}

static int write_incidents(FILE *f, const AppState *app)
{
    int ok = fprintf(f, "%s\n", HDR_INCIDENTS) >= 0;
    for (int i = 0; i < app->incidents.count; i++) {
        const Incident *inc = app->incidents.items[i];
        ok &= fprintf(f, "%d,%s,%d,%d,%ld,%s,%d,%d\n", inc->id, incident_type_name(inc->type),
                      inc->location_id, inc->severity, inc->reported_seq,
                      incident_status_name(inc->status), inc->assigned_resource_id,
                      inc->dispatch_attempts) >= 0;
    }
    return ok;
}

static int write_allocations(FILE *f, const AppState *app)
{
    int ok = fprintf(f, "%s\n", HDR_ALLOCATIONS) >= 0;
    for (const Allocation *a = app->history.head; a != NULL; a = a->next) {
        ok &= fprintf(f, "%d,%d,%d,%d,%d,%d,", a->id, a->incident_id, a->resource_id,
                      a->from_location, a->to_location, a->distance) >= 0;
        for (int i = 0; i < a->route_len; i++) {
            if (i > 0)
                ok &= fputc('|', f) != EOF;
            ok &= fprintf(f, "%d", a->route[i]) >= 0;
        }
        ok &= fprintf(f, ",%s,%ld,%ld\n", allocation_status_name(a->status),
                      a->dispatch_seq, a->complete_seq) >= 0;
    }
    return ok;
}

typedef int (*RecordWriter)(FILE *f, const AppState *app);

/* Writes <dir>/<name>.tmp completely. Returns 1 on success; on failure the
 * partial .tmp file is deleted. */
static int write_tmp(const char *dir, const char *name, RecordWriter writer,
                     const AppState *app, FILE *log)
{
    char tmp[FM_PATH_MAX];
    if (!make_path(tmp, sizeof tmp, dir, name, ".tmp"))
        return 0;
    FILE *f = fopen(tmp, "w");
    if (f == NULL) {
        fatal(log, tmp, 0, "cannot create temporary file");
        return 0;
    }
    int ok = writer(f, app);
    if (ferror(f))
        ok = 0;
    if (fclose(f) != 0)          /* buffered data is written here: must be checked */
        ok = 0;
    if (!ok) {
        fatal(log, tmp, 0, "write failed");
        remove(tmp);
    }
    return ok;
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return 0;
    fclose(f);
    return 1;
}

/* Replaces <name> with <name>.tmp, keeping the old version as <name>.bak.
 * rename() on Windows fails if the destination exists, hence the explicit
 * remove/rename steps. */
static int replace_with_tmp(const char *dir, const char *name, FILE *log)
{
    char target[FM_PATH_MAX], tmp[FM_PATH_MAX], bak[FM_PATH_MAX];
    if (!make_path(target, sizeof target, dir, name, "") ||
        !make_path(tmp, sizeof tmp, dir, name, ".tmp") ||
        !make_path(bak, sizeof bak, dir, name, ".bak"))
        return 0;

    remove(bak);                                  /* may not exist: ignore result */
    if (file_exists(target) && rename(target, bak) != 0) {
        fatal(log, target, 0, "cannot move the old file to .bak (is it open elsewhere?)");
        return 0;
    }
    if (rename(tmp, target) != 0) {
        fatal(log, target, 0, "cannot move the new file into place; restoring the old one");
        rename(bak, target);
        return 0;
    }
    return 1;
}

DcStatus fm_save_records(const AppState *app, const char *dir, FILE *log)
{
    if (app == NULL || dir == NULL)
        return DC_ERR_INVALID;

    static const char *const FILES[3] = {"resources.csv", "incidents.csv", "allocations.csv"};
    const RecordWriter writers[3] = {write_resources, write_incidents, write_allocations};

    /* Phase 1: write all three temporary files. */
    int all_written = 1;
    for (int i = 0; i < 3 && all_written; i++)
        all_written = write_tmp(dir, FILES[i], writers[i], app, log);
    if (!all_written) {
        for (int i = 0; i < 3; i++) {             /* originals were never touched */
            char tmp[FM_PATH_MAX];
            if (make_path(tmp, sizeof tmp, dir, FILES[i], ".tmp"))
                remove(tmp);
        }
        return DC_ERR_IO;
    }

    /* Phase 2: swap them into place. */
    for (int i = 0; i < 3; i++) {
        if (!replace_with_tmp(dir, FILES[i], log)) {
            if (log != NULL)
                fprintf(log, "  Save incomplete: files before %s were already replaced.\n",
                        FILES[i]);
            return DC_ERR_IO;
        }
    }
    return DC_OK;
}
