/*
 * test_file_manager.c — tests for CSV loading and saving (Phase 7).
 *
 * Uses fixture folders under tests/data/ (see docs/PHASE7_PERSISTENCE.md)
 * and writes into build/test_data/roundtrip, which `make test` creates.
 * Run from the project folder.
 *
 * Build and run:   make test
 */
#include "dispatch.h"
#include "file_manager.h"
#include "test_util.h"

#include <stdlib.h>

#define OUT_DIR "build/test_data/roundtrip"

/* ---------- helpers ---------- */

static int copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    if (in == NULL)
        return 0;
    FILE *out = fopen(to, "wb");
    if (out == NULL) {
        fclose(in);
        return 0;
    }
    int ok = 1, c;
    while ((c = fgetc(in)) != EOF)
        if (fputc(c, out) == EOF)
            ok = 0;
    if (ferror(in))
        ok = 0;
    fclose(in);
    if (fclose(out) != 0)
        ok = 0;
    return ok;
}

/* The map files are input-only, so a test output folder needs a copy. */
static int prepare_out_dir(void)
{
    return copy_file("data/locations.csv", OUT_DIR "/locations.csv") &&
           copy_file("data/roads.csv", OUT_DIR "/roads.csv");
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return 0;
    fclose(f);
    return 1;
}

static void check_counts(const char *label, const FmFileCount *c, int loaded, int skipped, int missing)
{
    int ok = c->loaded == loaded && c->skipped == skipped && c->missing == missing;
    if (!ok)
        printf("  %s: loaded %d skipped %d missing %d, expected %d/%d/%d\n",
               label, c->loaded, c->skipped, c->missing, loaded, skipped, missing);
    CHECK(ok);
}

/* Compares everything that is saved. Returns the number of differences. */
static int compare_states(const AppState *a, const AppState *b)
{
    int diff = 0;
    if (a->incidents.count != b->incidents.count || a->resources.count != b->resources.count ||
        a->history.count != b->history.count)
        return 1000;
    for (int i = 0; i < a->incidents.count; i++) {
        const Incident *x = a->incidents.items[i], *y = b->incidents.items[i];
        if (x->id != y->id || x->type != y->type || x->location_id != y->location_id ||
            x->severity != y->severity || x->reported_seq != y->reported_seq ||
            x->status != y->status || x->assigned_resource_id != y->assigned_resource_id ||
            x->dispatch_attempts != y->dispatch_attempts)
            diff++;
    }
    for (int i = 0; i < a->resources.count; i++) {
        const Resource *x = a->resources.items[i], *y = b->resources.items[i];
        if (x->id != y->id || x->type != y->type || x->location_id != y->location_id ||
            x->status != y->status || x->assigned_incident_id != y->assigned_incident_id ||
            x->total_dispatches != y->total_dispatches || x->total_distance != y->total_distance)
            diff++;
    }
    const Allocation *p = a->history.head, *q = b->history.head;
    for (; p != NULL && q != NULL; p = p->next, q = q->next) {
        if (p->id != q->id || p->incident_id != q->incident_id || p->resource_id != q->resource_id ||
            p->from_location != q->from_location || p->to_location != q->to_location ||
            p->distance != q->distance || p->status != q->status ||
            p->dispatch_seq != q->dispatch_seq || p->complete_seq != q->complete_seq ||
            p->route_len != q->route_len) {
            diff++;
            continue;
        }
        for (int k = 0; k < p->route_len; k++)
            if (p->route[k] != q->route[k])
                diff++;
    }
    if (a->next_incident_id != b->next_incident_id || a->next_resource_id != b->next_resource_id ||
        a->next_allocation_id != b->next_allocation_id)
        diff++;

    /* Same service order for pending incidents (cancelled heap entries,
     * which are not saved, are skipped). */
    IncidentHeap ca, cb;
    if (incident_heap_copy(&a->pending, &ca) != DC_OK)
        return diff + 1;
    if (incident_heap_copy(&b->pending, &cb) != DC_OK) {
        incident_heap_free(&ca);
        return diff + 1;
    }
    Incident *x = NULL, *y = NULL;
    for (;;) {
        do { x = NULL; } while (incident_heap_extract_max(&ca, &x) == DC_OK && x->status != INC_PENDING);
        do { y = NULL; } while (incident_heap_extract_max(&cb, &y) == DC_OK && y->status != INC_PENDING);
        if (x == NULL && y == NULL)
            break;
        if (x == NULL || y == NULL || x->id != y->id) {
            diff++;
            break;
        }
    }
    incident_heap_free(&ca);
    incident_heap_free(&cb);
    return diff;
}

/* ---------- tests ---------- */

static void test_load_valid(void)
{
    TEST_SECTION("load tests/data/valid: every record, counter and status restored");
    AppState app;
    FmLoadReport rep;
    CHECK_EQ_INT(fm_load(&app, "tests/data/valid", stdout, &rep), DC_OK);
    if (app.graph == NULL)
        return;
    check_counts("locations", &rep.locations, 10, 0, 0);
    check_counts("roads", &rep.roads, 11, 0, 0);
    check_counts("resources", &rep.resources, 6, 0, 0);
    check_counts("incidents", &rep.incidents, 4, 0, 0);
    check_counts("allocations", &rep.allocations, 2, 0, 0);
    CHECK_EQ_INT(rep.consistency_problems, 0);

    CHECK_EQ_INT(app.next_incident_id, 1005);
    CHECK_EQ_INT(app.next_resource_id, 7);
    CHECK_EQ_INT(app.next_allocation_id, 3);
    CHECK_EQ_INT(app.clock, 13);                      /* largest saved seq (12) + 1 */
    CHECK_EQ_INT(incident_heap_size(&app.pending), 1);  /* only 1002 is PENDING */

    const Incident *i1001 = app_find_incident(&app, 1001);
    CHECK(i1001 != NULL && i1001->status == INC_DISPATCHED && i1001->assigned_resource_id == 1);
    const Resource *r2 = app_find_resource(&app, 2);
    CHECK(r2 != NULL && r2->location_id == 1 && r2->total_dispatches == 1 && r2->total_distance == 15);
    const Allocation *a2 = history_find(&app.history, 2);
    CHECK(a2 != NULL && a2->status == ALLOC_ACTIVE && a2->route_len == 3 && a2->route[1] == 3);

    /* The loaded state is fully usable: dispatch, complete, register. */
    DispatchResult result;
    CHECK_EQ_INT(dispatch_next(&app, NULL, &result), DC_OK);
    CHECK_EQ_INT(result.outcome, DISPATCH_OK);
    CHECK_EQ_INT(result.incident_id, 1002);
    CHECK_EQ_INT(result.resource_id, 3);               /* fire unit already at 2 */
    CHECK(result.allocation != NULL && result.allocation->id == 3 &&
          result.allocation->dispatch_seq == 13);
    CHECK_EQ_INT(dispatch_complete(&app, 1001, NULL), DC_OK);
    Incident *fresh = NULL;
    CHECK_EQ_INT(app_register_incident(&app, INC_MEDICAL, 3, 2, &fresh), DC_OK);
    CHECK(fresh != NULL && fresh->id == 1005 && fresh->reported_seq > 12);
    CHECK_EQ_INT(app_check_consistency(&app, stdout), 0);
    app_free(&app);
}

static void test_round_trip(void)
{
    TEST_SECTION("save -> reload gives the same state (valid fixture + activity)");
    CHECK(prepare_out_dir());
    AppState original, reloaded;
    FmLoadReport rep;
    CHECK_EQ_INT(fm_load(&original, "tests/data/valid", NULL, &rep), DC_OK);
    if (original.graph == NULL)
        return;

    DispatchResult result;
    dispatch_next(&original, NULL, &result);                     /* 1002 -> R3 */
    dispatch_complete(&original, 1001, NULL);                     /* R1 now at 5 */
    app_register_incident(&original, INC_ROAD_ACCIDENT, 6, 4, NULL);
    app_register_incident(&original, INC_FIRE, 9, 5, NULL);     /* island: unreachable */
    dispatch_all(&original, NULL);

    CHECK_EQ_INT(fm_save_records(&original, OUT_DIR, stdout), DC_OK);
    CHECK_EQ_INT(fm_load(&reloaded, OUT_DIR, stdout, &rep), DC_OK);
    if (reloaded.graph != NULL) {
        CHECK_EQ_INT(compare_states(&original, &reloaded), 0);
        /* Unsaved events (cancellations, registrations of units) also used
         * clock ticks, so the reloaded clock may be smaller - but it is
         * always greater than every saved sequence number. */
        CHECK(reloaded.clock <= original.clock);
        CHECK_EQ_INT(app_check_consistency(&reloaded, stdout), 0);
        app_free(&reloaded);
    }
    app_free(&original);
}

static void test_round_trip_after_random_simulation(void)
{
    TEST_SECTION("save -> reload after 600 random simulation steps");
    CHECK(prepare_out_dir());
    AppState app, reloaded;
    FmLoadReport rep;
    CHECK_EQ_INT(fm_load(&app, "tests/data/minimal", NULL, &rep), DC_OK);
    if (app.graph == NULL)
        return;

    uint32_t seed = 77u;
    for (int i = 0; i < 10; i++)
        app_register_resource(&app, (ResourceType)test_random(&seed, RES_TYPE_COUNT),
                              test_random(&seed, 10), NULL);
    for (int step = 0; step < 600; step++) {
        int op = test_random(&seed, 10);
        DispatchResult result;
        if (op < 4)
            app_register_incident(&app, (IncidentType)test_random(&seed, INC_TYPE_COUNT),
                                  test_random(&seed, 10), 1 + test_random(&seed, 5), NULL);
        else if (op < 7)
            dispatch_next(&app, NULL, &result);
        else if (app.incidents.count > 0) {
            Incident *inc = app.incidents.items[test_random(&seed, app.incidents.count)];
            if (op < 9)
                dispatch_complete(&app, inc->id, NULL);   /* may be refused */
            else
                dispatch_cancel(&app, inc->id, NULL);     /* may be refused */
        }
    }
    CHECK_EQ_INT(fm_save_records(&app, OUT_DIR, NULL), DC_OK);
    CHECK_EQ_INT(fm_load(&reloaded, OUT_DIR, stdout, &rep), DC_OK);
    if (reloaded.graph != NULL) {
        CHECK_EQ_INT(compare_states(&app, &reloaded), 0);
        app_free(&reloaded);
    }
    printf("  %d incidents, %d resources, %d allocations saved and reloaded\n",
           app.incidents.count, app.resources.count, app.history.count);
    app_free(&app);
}

static void test_malformed_rows_are_skipped(void)
{
    TEST_SECTION("malformed rows are skipped with warnings; good rows still load");
    AppState app;
    FmLoadReport rep;
    printf("\n");
    CHECK_EQ_INT(fm_load(&app, "tests/data/malformed", stdout, &rep), DC_OK);
    printf("\n");
    fm_print_report(&rep, stdout);
    check_counts("locations", &rep.locations, 10, 3, 0);
    check_counts("roads", &rep.roads, 11, 7, 0);
    check_counts("resources", &rep.resources, 2, 6, 0);
    check_counts("incidents", &rep.incidents, 2, 9, 0);
    check_counts("allocations", &rep.allocations, 0, 5, 0);
    if (app.graph != NULL) {
        CHECK_EQ_INT(app.graph->num_vertices, 10);
        CHECK_EQ_INT(app.graph->num_edges, 11);
        CHECK_STR_EQ(graph_location_name(app.graph, 5), "Old Town");   /* first one kept */
        CHECK(app_find_resource(&app, 7) != NULL);
        CHECK(app_find_incident(&app, 1008) != NULL);
        CHECK(app_find_incident(&app, 1005) == NULL);
        CHECK_EQ_INT(app.next_resource_id, 8);
        app_free(&app);
    }
}

static void test_crlf_bom_and_missing_files(void)
{
    TEST_SECTION("Windows line endings + byte-order mark; missing optional files");
    AppState app;
    FmLoadReport rep;
    CHECK_EQ_INT(fm_load(&app, "tests/data/crlf", stdout, &rep), DC_OK);
    check_counts("crlf locations", &rep.locations, 10, 0, 0);
    check_counts("crlf roads", &rep.roads, 11, 0, 0);
    check_counts("crlf resources", &rep.resources, 2, 0, 0);
    CHECK(rep.incidents.missing && rep.allocations.missing);
    if (app.graph != NULL) {
        CHECK_STR_EQ(graph_location_name(app.graph, 9), "Lake Camp");   /* no stray '\r' */
        app_free(&app);
    }

    CHECK_EQ_INT(fm_load(&app, "tests/data/minimal", stdout, &rep), DC_OK);
    CHECK(rep.resources.missing && rep.incidents.missing && rep.allocations.missing);
    CHECK_EQ_INT(app.resources.count + app.incidents.count + app.history.count, 0);
    CHECK_EQ_INT(app.next_incident_id, APP_FIRST_INCIDENT_ID);
    app_free(&app);
}

static void test_refused_loads(void)
{
    TEST_SECTION("loads that must be refused (and leave a safe, empty state)");
    AppState app;
    FmLoadReport rep;

    printf("\n");
    CHECK_EQ_INT(fm_load(&app, "tests/data/bad_header", stdout, &rep), DC_ERR_IO);
    CHECK(app.graph == NULL && app.incidents.count == 0);
    app_free(&app);

    CHECK_EQ_INT(fm_load(&app, "tests/data/inconsistent", stdout, &rep), DC_ERR_STATE);
    CHECK(rep.consistency_problems >= 1);
    CHECK(app.graph == NULL);
    app_free(&app);

    CHECK_EQ_INT(fm_load(&app, "tests/data/gap", stdout, &rep), DC_ERR_IO);
    CHECK(app.graph == NULL);

    CHECK_EQ_INT(fm_load(&app, "tests/data/no_such_folder", stdout, &rep), DC_ERR_IO);
    CHECK(rep.locations.missing);
    CHECK_EQ_INT(fm_load(NULL, "tests/data/valid", NULL, &rep), DC_ERR_INVALID);
    CHECK_EQ_INT(fm_load(&app, NULL, NULL, &rep), DC_ERR_INVALID);
    app_free(&app);
}

static void test_safe_save(void)
{
    TEST_SECTION("safe save: .bak keeps the previous version, no .tmp left behind");
    CHECK(prepare_out_dir());
    AppState app;
    FmLoadReport rep;
    CHECK_EQ_INT(fm_load(&app, "tests/data/valid", NULL, &rep), DC_OK);
    if (app.graph == NULL)
        return;

    CHECK_EQ_INT(fm_save_records(&app, OUT_DIR, stdout), DC_OK);   /* version A */
    app_register_incident(&app, INC_FIRE, 2, 3, NULL);
    CHECK_EQ_INT(fm_save_records(&app, OUT_DIR, stdout), DC_OK);   /* version B */

    CHECK(file_exists(OUT_DIR "/incidents.csv.bak"));
    CHECK(!file_exists(OUT_DIR "/incidents.csv.tmp"));
    CHECK(!file_exists(OUT_DIR "/resources.csv.tmp"));
    CHECK(!file_exists(OUT_DIR "/allocations.csv.tmp"));

    /* the .bak file is version A: 4 incidents (+ header) */
    FILE *f = fopen(OUT_DIR "/incidents.csv.bak", "r");
    int lines = 0, c;
    if (f != NULL) {
        while ((c = fgetc(f)) != EOF)
            if (c == '\n')
                lines++;
        fclose(f);
    }
    CHECK_EQ_INT(lines, 5);

    /* saving into a folder that does not exist fails cleanly */
    CHECK_EQ_INT(fm_save_records(&app, "build/test_data/no_such_folder", NULL), DC_ERR_IO);
    CHECK_EQ_INT(fm_save_records(NULL, OUT_DIR, NULL), DC_ERR_INVALID);
    app_free(&app);
}

int main(void)
{
    test_load_valid();
    test_round_trip();
    test_round_trip_after_random_simulation();
    test_malformed_rows_are_skipped();
    test_crlf_bom_and_missing_files();
    test_refused_loads();
    test_safe_save();
    return TEST_SUMMARY();
}
