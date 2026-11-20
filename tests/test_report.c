/*
 * test_report.c — tests for the Phase 8 reports.
 *
 * The main scenario continues the Phase 6 "seven incidents" example and is
 * hand-traced step by step in docs/PHASE8_REPORTING.md §E. Every expected
 * number below comes from that trace, not from running the program.
 *
 * The full report is written to build/test_data/report.txt (the Makefile's
 * `test` target creates build/test_data). Nothing under data/ is touched.
 *
 * Build and run:   make test
 */
#include "report.h"
#include "dispatch.h"
#include "reference_city.h"
#include "test_util.h"

#include <stdlib.h>
#include <string.h>

#define REPORT_PATH     "build/test_data/report.txt"
#define REPORT_BAD_PATH "build/test_data/no_such_folder/report.txt"

/* ---------- helpers ---------- */

static int setup(AppState *app)
{
    Graph *g = build_reference_city();
    if (g == NULL) {
        app_init(app, NULL);
        return 0;
    }
    if (app_init(app, g) != DC_OK) {
        graph_destroy(g);
        return 0;
    }
    return 1;
}

/* Reads a whole text file into a malloc'd, '\0'-terminated buffer.
 * Returns NULL if the file cannot be read or is larger than max_size.
 * TEXT mode ("r"), because report_write_file writes in text mode: on
 * Windows each line is stored as "\r\n", and reading in text mode turns it
 * back into "\n", so the expected lines below work on every system.
 * (Fixed on 2026-11-19: the first real run read with "rb" and 18 checks
 * failed on "\r\n" although the report text itself was correct.) */
static char *read_file(const char *path, size_t max_size)
{
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return NULL;
    char *buf = malloc(max_size + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, max_size + 1, f);
    fclose(f);
    if (n > max_size) {          /* file too large for the test buffer */
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    return buf;
}

static int contains(const char *text, const char *piece)
{
    return text != NULL && strstr(text, piece) != NULL;
}

/* Builds the Phase 8 scenario (docs/PHASE8_REPORTING.md §E1). Returns 1 on
 * success. The clock value after each step is noted on the right. */
static int build_scenario(AppState *app)
{
    if (!setup(app))
        return 0;
    int ok = 1;

    /* Reference fleet R1..R6 (clock 0..5 -> 6). */
    const ResourceType rtypes[6] = {RES_AMBULANCE, RES_AMBULANCE, RES_FIRE_UNIT,
                                    RES_RESCUE_UNIT, RES_FIRE_UNIT, RES_AMBULANCE};
    const int rlocs[6] = {0, 6, 2, 4, 7, 8};
    for (int i = 0; i < 6; i++)
        ok &= app_register_resource(app, rtypes[i], rlocs[i], NULL) == DC_OK;

    /* Incidents 1001..1007, reported_seq 6..12 (clock -> 13). */
    const IncidentType itypes[7] = {INC_MEDICAL, INC_FIRE, INC_ROAD_ACCIDENT,
                                    INC_STRUCTURAL_RESCUE, INC_MEDICAL, INC_FIRE,
                                    INC_ROAD_ACCIDENT};
    const int ilocs[7] = {5, 2, 6, 4, 0, 7, 1};
    const int sev[7] = {3, 5, 3, 4, 5, 1, 4};
    for (int i = 0; i < 7; i++)
        ok &= app_register_incident(app, itypes[i], ilocs[i], sev[i], NULL) == DC_OK;

    ok &= dispatch_all(app, NULL) == 5;                       /* clock 13..17 -> 18 */
    ok &= dispatch_complete(app, 1002, NULL) == DC_OK;        /* done at 18 -> 19   */
    ok &= dispatch_complete(app, 1007, NULL) == DC_OK;        /* done at 19 -> 20   */
    ok &= dispatch_cancel(app, 1003, NULL) == DC_OK;          /* -> 21              */

    Resource *r6 = app_find_resource(app, 6);
    ok &= r6 != NULL && resource_set_out_of_service(r6) == DC_OK;   /* no clock tick */

    DispatchResult result;                                    /* sent at 21 -> 22   */
    ok &= dispatch_next(app, NULL, &result) == DC_OK;
    ok &= result.outcome == DISPATCH_OK && result.incident_id == 1001 &&
          result.resource_id == 2 && result.distance == 6;
    return ok;
}

/* ---------- tests ---------- */

static void test_empty_state(void)
{
    TEST_SECTION("reports on an empty state (no records)");
    AppState app;
    CHECK(setup(&app));
    if (app.graph == NULL)
        return;

    SystemStats st;
    CHECK_EQ_INT(report_compute_stats(&app, &st), DC_OK);
    CHECK_EQ_INT(st.incidents_total, 0);
    CHECK_EQ_INT(st.resources_total, 0);
    CHECK_EQ_INT(st.allocations_total, 0);
    CHECK_EQ_INT(st.distance_min, -1);
    CHECK_EQ_INT(st.distance_max, -1);
    CHECK_EQ_INT(st.wait_count, 0);
    CHECK_EQ_INT(st.heap_entries, 0);
    CHECK_EQ_INT(report_cross_check(&st, stdout), 0);

    printf("\n");
    report_print_stats(&st, stdout);
    CHECK_EQ_INT(report_print_history(&app, NULL, stdout), 0);
    CHECK_EQ_INT(report_print_completed(&app, stdout), 0);
    CHECK_EQ_INT(report_print_utilisation(&app, stdout), 0);
    const Resource *dummy[1];
    CHECK_EQ_INT(report_rank_resources(&app, dummy, 0), 0);   /* 0 units fit in 0 slots */
    app_free(&app);
}

static void test_invalid_arguments(void)
{
    TEST_SECTION("NULL arguments are rejected, never crash");
    SystemStats st;
    const Resource *slot[1];
    CHECK_EQ_INT(report_compute_stats(NULL, &st), DC_ERR_INVALID);
    CHECK_EQ_INT(report_print_history(NULL, NULL, stdout), -1);
    CHECK_EQ_INT(report_print_completed(NULL, stdout), -1);
    CHECK_EQ_INT(report_print_utilisation(NULL, stdout), -1);
    CHECK_EQ_INT(report_rank_resources(NULL, slot, 1), -1);
    CHECK_EQ_INT(report_print_full(NULL, stdout), DC_ERR_INVALID);
    CHECK_EQ_INT(report_write_file(NULL, REPORT_PATH), DC_ERR_INVALID);
    CHECK_EQ_INT(report_cross_check(NULL, NULL), 1);
    CHECK_EQ_INT(report_count_history(NULL, NULL), 0);
    CHECK(!report_allocation_matches(NULL, NULL));
    CHECK(!report_busier(NULL, NULL));

    AppState app;
    CHECK(setup(&app));
    CHECK_EQ_INT(report_compute_stats(&app, NULL), DC_ERR_INVALID);
    CHECK_EQ_INT(report_print_history(&app, NULL, NULL), -1);
    CHECK_EQ_INT(report_write_file(&app, NULL), DC_ERR_INVALID);
    CHECK_EQ_INT(report_write_file(&app, ""), DC_ERR_INVALID);
    CHECK_EQ_INT(report_rank_resources(&app, NULL, 1), -1);
    app_free(&app);
}

static void test_average(void)
{
    TEST_SECTION("report_average");
    CHECK(report_average(0, 0) == 0.0);      /* no division by zero */
    CHECK(report_average(10, -3) == 0.0);
    CHECK(report_average(21, 6) == 3.5);     /* exact in binary floating point */
    double a = report_average(41, 6);        /* 6.8333... */
    CHECK(a > 6.833 && a < 6.834);
}

static void test_scenario_statistics(void)
{
    TEST_SECTION("statistics of the hand-traced scenario (PHASE8 doc §E)");
    AppState app;
    CHECK(build_scenario(&app));
    if (app.graph == NULL)
        return;

    SystemStats st;
    CHECK_EQ_INT(report_compute_stats(&app, &st), DC_OK);
    CHECK_EQ_INT(st.clock, 22);

    /* incidents */
    CHECK_EQ_INT(st.incidents_total, 7);
    CHECK_EQ_INT(st.incidents_by_status[INC_PENDING], 0);
    CHECK_EQ_INT(st.incidents_by_status[INC_DISPATCHED], 4);   /* 1004 1005 1006 1001 */
    CHECK_EQ_INT(st.incidents_by_status[INC_COMPLETED], 2);    /* 1002 1007 */
    CHECK_EQ_INT(st.incidents_by_status[INC_CANCELLED], 1);    /* 1003 */
    CHECK_EQ_INT(st.incidents_by_type[INC_MEDICAL], 2);
    CHECK_EQ_INT(st.incidents_by_type[INC_ROAD_ACCIDENT], 2);
    CHECK_EQ_INT(st.incidents_by_type[INC_FIRE], 2);
    CHECK_EQ_INT(st.incidents_by_type[INC_STRUCTURAL_RESCUE], 1);
    CHECK_EQ_INT(st.failed_attempts_total, 4);                 /* 1001: 2, 1003: 2 */

    /* queue: the cancelled 1003 is still physically in the heap */
    CHECK_EQ_INT(st.heap_entries, 1);
    CHECK_EQ_INT(st.heap_stale_entries, 1);

    /* units */
    CHECK_EQ_INT(st.resources_total, 6);
    CHECK_EQ_INT(st.resources_by_status[RES_AVAILABLE], 1);        /* R3 */
    CHECK_EQ_INT(st.resources_by_status[RES_DISPATCHED], 4);       /* R1 R2 R4 R5 */
    CHECK_EQ_INT(st.resources_by_status[RES_OUT_OF_SERVICE], 1);   /* R6 */
    CHECK_EQ_INT(st.resources_by_type[RES_AMBULANCE], 3);
    CHECK_EQ_INT(st.resources_by_type[RES_FIRE_UNIT], 2);
    CHECK_EQ_INT(st.resources_by_type[RES_RESCUE_UNIT], 1);
    CHECK_EQ_INT(st.dispatches_by_type[RES_AMBULANCE], 3);         /* R1 1 + R2 2 */
    CHECK_EQ_INT(st.dispatches_by_type[RES_FIRE_UNIT], 2);
    CHECK_EQ_INT(st.dispatches_by_type[RES_RESCUE_UNIT], 1);
    CHECK_EQ_INT(st.resource_dispatch_sum, 6);
    CHECK(st.resource_distance_sum == 21);

    /* allocations */
    CHECK_EQ_INT(st.allocations_total, 6);
    CHECK_EQ_INT(st.allocations_active, 4);
    CHECK_EQ_INT(st.allocations_completed, 2);
    CHECK(st.distance_sum == 21);                  /* 0+0+0+15+0+6 */
    CHECK_EQ_INT(st.distance_min, 0);
    CHECK_EQ_INT(st.distance_max, 15);
    CHECK(st.wait_sum == 41);                      /* 6+4+6+4+6+15 */
    CHECK_EQ_INT(st.wait_count, 6);
    CHECK(st.service_sum == 8);                    /* (18-13) + (19-16) */
    CHECK_EQ_INT(st.service_count, 2);
    CHECK_EQ_INT(st.missing_incident_refs, 0);

    /* waiting time per severity: 5 -> {6,4}, 4 -> {6,4}, 3 -> {15}, 1 -> {6} */
    CHECK_EQ_INT(st.wait_count_by_severity[5], 2);
    CHECK(st.wait_sum_by_severity[5] == 10);
    CHECK_EQ_INT(st.wait_count_by_severity[4], 2);
    CHECK(st.wait_sum_by_severity[4] == 10);
    CHECK_EQ_INT(st.wait_count_by_severity[3], 1);
    CHECK(st.wait_sum_by_severity[3] == 15);
    CHECK_EQ_INT(st.wait_count_by_severity[2], 0);
    CHECK_EQ_INT(st.wait_count_by_severity[1], 1);
    CHECK(st.wait_sum_by_severity[1] == 6);

    /* Independent structures agree, and so does the Phase 5 checker. */
    CHECK_EQ_INT(report_cross_check(&st, stdout), 0);
    CHECK_EQ_INT(app_check_consistency(&app, stdout), 0);

    /* The cross-check really detects a disagreement. */
    SystemStats broken = st;
    broken.resource_dispatch_sum++;
    CHECK_EQ_INT(report_cross_check(&broken, stdout), 1);
    broken = st;
    broken.heap_stale_entries = 0;          /* would mean 1 live entry, 0 PENDING */
    broken.missing_incident_refs = 1;
    CHECK_EQ_INT(report_cross_check(&broken, stdout), 2);

    /* Reports are read-only: printing everything and computing again gives
     * the same numbers, and the clock did not move. (Fields are compared
     * one by one: memcmp on a struct would also compare padding bytes,
     * whose values C does not define.) */
    printf("\n");
    report_print_full(&app, stdout);
    SystemStats again;
    report_compute_stats(&app, &again);
    CHECK_EQ_INT(again.clock, 22);
    CHECK_EQ_INT(app.clock, 22);
    CHECK_EQ_INT(again.allocations_total, st.allocations_total);
    CHECK(again.wait_sum == st.wait_sum);
    CHECK_EQ_INT(again.heap_entries, st.heap_entries);
    CHECK_EQ_INT(again.resources_by_status[RES_DISPATCHED], st.resources_by_status[RES_DISPATCHED]);
    app_free(&app);
}

static void test_history_filters(void)
{
    TEST_SECTION("allocation history filters");
    AppState app;
    CHECK(build_scenario(&app));
    if (app.graph == NULL)
        return;

    HistoryFilter f;
    report_filter_all(&f);
    CHECK_EQ_INT(f.status, -1);
    CHECK_EQ_INT(report_count_history(&app.history, &f), 6);
    CHECK_EQ_INT(report_count_history(&app.history, NULL), 6);   /* NULL = everything */

    f.status = ALLOC_ACTIVE;
    CHECK_EQ_INT(report_count_history(&app.history, &f), 4);     /* #2 #3 #5 #6 */
    f.status = ALLOC_COMPLETED;
    CHECK_EQ_INT(report_count_history(&app.history, &f), 2);     /* #1 #4 */

    report_filter_all(&f);
    f.resource_id = 2;                                          /* unit 2 worked twice */
    CHECK_EQ_INT(report_count_history(&app.history, &f), 2);     /* #4 #6 */
    f.status = ALLOC_COMPLETED;
    CHECK_EQ_INT(report_count_history(&app.history, &f), 1);     /* #4 only */
    f.resource_id = 6;
    CHECK_EQ_INT(report_count_history(&app.history, &f), 0);     /* R6 never sent */

    report_filter_all(&f);
    f.incident_id = 1001;
    CHECK_EQ_INT(report_count_history(&app.history, &f), 1);     /* #6 */
    f.incident_id = 1003;
    CHECK_EQ_INT(report_count_history(&app.history, &f), 0);     /* cancelled, never sent */

    printf("\n-- history of unit 2 --\n");
    report_filter_all(&f);
    f.resource_id = 2;
    CHECK_EQ_INT(report_print_history(&app, &f, stdout), 2);
    printf("-- history of incident 1003 --\n");
    report_filter_all(&f);
    f.incident_id = 1003;
    CHECK_EQ_INT(report_print_history(&app, &f, stdout), 0);
    app_free(&app);
}

static void test_completed_report(void)
{
    TEST_SECTION("completed incidents report");
    AppState app;
    CHECK(build_scenario(&app));
    if (app.graph == NULL)
        return;
    printf("\n");
    CHECK_EQ_INT(report_print_completed(&app, stdout), 2);   /* 1002 and 1007 */
    app_free(&app);
}

static void test_busier_rule(void)
{
    TEST_SECTION("report_busier ordering rule");
    Resource a = {0}, b = {0};
    a.id = 1;
    b.id = 2;
    CHECK(report_busier(&a, &b));          /* all equal -> lower ID first */
    CHECK(!report_busier(&b, &a));
    CHECK(!report_busier(&a, &a));         /* strict: never before itself */

    b.total_distance = 5;                  /* equal dispatches: longer distance first */
    CHECK(report_busier(&b, &a));
    CHECK(!report_busier(&a, &b));

    a.total_dispatches = 1;                /* more dispatches beats any distance */
    CHECK(report_busier(&a, &b));
    CHECK(!report_busier(&b, &a));
}

static int ids_equal(const Resource *const *ranking, const int *expected, int n)
{
    for (int i = 0; i < n; i++)
        if (ranking[i] == NULL || ranking[i]->id != expected[i])
            return 0;
    return 1;
}

static void test_ranking(void)
{
    TEST_SECTION("unit ranking (insertion sort)");
    AppState app;
    CHECK(build_scenario(&app));
    if (app.graph == NULL)
        return;

    const Resource *ranking[6];
    CHECK_EQ_INT(report_rank_resources(&app, ranking, 6), 6);
    const int expected[6] = {2, 1, 3, 4, 5, 6};   /* R2: 2 jobs; R1,R3-R5: 1; R6: 0 */
    CHECK(ids_equal(ranking, expected, 6));
    CHECK_EQ_INT(report_rank_resources(&app, ranking, 5), -1);   /* too small */

    printf("\n");
    CHECK_EQ_INT(report_print_utilisation(&app, stdout), 6);
    app_free(&app);

    /* Worst case for insertion sort: the store is in REVERSE ranking order.
     * Statistics are set directly; only the ordering is being tested. */
    CHECK(setup(&app));
    if (app.graph == NULL)
        return;
    for (int i = 0; i < 5; i++) {
        Resource *r = NULL;
        CHECK_EQ_INT(app_register_resource(&app, RES_AMBULANCE, 0, &r), DC_OK);
        if (r != NULL)
            r->total_dispatches = i;     /* unit 1: 0 jobs ... unit 5: 4 jobs */
    }
    const Resource *rank5[5];
    CHECK_EQ_INT(report_rank_resources(&app, rank5, 5), 5);
    const int reversed[5] = {5, 4, 3, 2, 1};
    CHECK(ids_equal(rank5, reversed, 5));

    /* All equal: the ranking keeps ID (= registration) order. */
    for (int i = 0; i < 5; i++)
        app.resources.items[i]->total_dispatches = 0;
    const int in_order[5] = {1, 2, 3, 4, 5};
    CHECK_EQ_INT(report_rank_resources(&app, rank5, 5), 5);
    CHECK(ids_equal(rank5, in_order, 5));

    /* Distance breaks a tie in dispatches. */
    app.resources.items[3]->total_dispatches = 1;    /* unit 4 */
    app.resources.items[3]->total_distance = 5;
    app.resources.items[1]->total_dispatches = 1;    /* unit 2 */
    app.resources.items[1]->total_distance = 9;
    const int tie[5] = {2, 4, 1, 3, 5};
    CHECK_EQ_INT(report_rank_resources(&app, rank5, 5), 5);
    CHECK(ids_equal(rank5, tie, 5));
    app_free(&app);
}

static void test_full_report_file(void)
{
    TEST_SECTION("full report written to " REPORT_PATH);
    AppState app;
    CHECK(build_scenario(&app));
    if (app.graph == NULL)
        return;

    CHECK_EQ_INT(report_write_file(&app, REPORT_PATH), DC_OK);
    char *text = read_file(REPORT_PATH, 64 * 1024);
    CHECK(text != NULL);

    /* Exact lines predicted in PHASE8 doc §E4. */
    CHECK(contains(text, "SYSTEM STATISTICS (simulated clock = 22)\n"));
    CHECK(contains(text, "  by status: PENDING 0 DISPATCHED 4 COMPLETED 2 CANCELLED 1\n"));
    CHECK(contains(text, "  failed dispatch attempts: 4\n"));
    CHECK(contains(text, "Queue: 1 heap entries (0 live, 1 cancelled awaiting lazy removal)\n"));
    CHECK(contains(text, "  by status: AVAILABLE 1 DISPATCHED 4 OUT_OF_SERVICE 1\n"));
    CHECK(contains(text, "Allocations: 6 (active 4, completed 2)\n"));
    CHECK(contains(text, "  distance: total 21, average 3.50, min 0, max 15\n"));
    CHECK(contains(text, "  wait (reported -> dispatched): average 6.83 ticks over 6\n"));
    CHECK(contains(text, "  service (dispatched -> completed): average 4.00 ticks over 2\n"));
    CHECK(contains(text, "    5 Critical    2 dispatched, average wait 5.00\n"));
    CHECK(contains(text, "    3 Serious     1 dispatched, average wait 15.00\n"));
    CHECK(contains(text, "    2 Moderate    0 dispatched, average wait -\n"));
    CHECK(contains(text, "Cross-check: all counters agree\n"));
    CHECK(contains(text, "#1 COMPLETED: incident 1002 FIRE (severity 5) at 2 Fire Station North\n"));
    CHECK(contains(text, "    unit 3 FIRE_UNIT from 2 Fire Station North, distance 0, "
                         "sent at 13, done at 18, waited 6\n"));
    CHECK(contains(text, "    route: 6 Airport Road > 7 University > 4 Industrial Zone > "
                         "2 Fire Station North > 1 City Square\n"));
    CHECK(contains(text, "#6 ACTIVE: incident 1001 MEDICAL (severity 3) at 5 Old Town\n"));
    CHECK(contains(text, "    unit 2 AMBULANCE from 1 City Square, distance 6, sent at 21, waited 15\n"));
    CHECK(!contains(text, "CROSS-CHECK FAILED"));
    free(text);

    /* A folder that does not exist: reported as an I/O error. */
    CHECK_EQ_INT(report_write_file(&app, REPORT_BAD_PATH), DC_ERR_IO);
    app_free(&app);
}

int main(void)
{
    test_empty_state();
    test_invalid_arguments();
    test_average();
    test_scenario_statistics();
    test_history_filters();
    test_completed_report();
    test_busier_rule();
    test_ranking();
    test_full_report_file();
    return TEST_SUMMARY();
}
