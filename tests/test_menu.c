/*
 * test_menu.c — drives the real menu (menu_run) from scripted input files.
 *
 * This is an end-to-end test of everything except main()'s argument
 * parsing: the menu reads answers through input.c, calls the engine and
 * prints messages, exactly as in an interactive run. Expected messages
 * are hand-derived in docs/PHASE9_MENU.md §E.
 *
 * Data:  tests/e2e/data         reference city map only (never written:
 *                               the first session runs with autosave OFF)
 *        build/test_data/menu   a fresh copy made by `make test`, used by
 *                               the autosave session
 *
 * Build and run:   make test
 */
#include "file_manager.h"
#include "menu.h"
#include "test_util.h"

#include <stdlib.h>
#include <string.h>

#define E2E_DATA   "tests/e2e/data"
#define SAVE_DIR   "build/test_data/menu"
#define SCRIPT     "build/test_data/menu_script.in"
#define OUTPUT     "build/test_data/menu_output.txt"
#define OUTPUT_MAX (256 * 1024)

/* ---------- helpers ---------- */

static int write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return 0;
    int ok = fputs(text, f) >= 0;
    if (fclose(f) != 0)
        ok = 0;
    return ok;
}

static char *read_all(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return NULL;
    char *buf = malloc(OUTPUT_MAX + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, OUTPUT_MAX, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

static int contains(const char *text, const char *piece)
{
    return text != NULL && strstr(text, piece) != NULL;
}

/* Runs the menu on `script` with a freshly loaded state from data_dir.
 * The state is left in *app for further checks (caller frees it).
 * Returns menu_run's result, or -1 if the run could not be set up.
 * *text receives the whole output (caller frees it). */
static int run_script(AppState *app, const char *data_dir, int autosave,
                      const char *script, char **text)
{
    *text = NULL;
    FmLoadReport report;
    if (fm_load(app, data_dir, NULL, &report) != DC_OK)
        return -1;
    if (!write_text(SCRIPT, script))
        return -1;
    FILE *in = fopen(SCRIPT, "rb");
    FILE *out = fopen(OUTPUT, "wb");
    if (in == NULL || out == NULL) {
        if (in != NULL)
            fclose(in);
        if (out != NULL)
            fclose(out);
        return -1;
    }
    Console con = {in, out, 1};                 /* echo on: readable transcript */
    MenuSession s;
    menu_session_init(&s, app, &con, data_dir, autosave);
    int result = menu_run(&s);
    fclose(in);
    fclose(out);
    *text = read_all(OUTPUT);
    return result;
}

/* ---------- tests ---------- */

/* One answer per line; the comments are NOT part of the script. */
static const char SCRIPT_DAY[] =
    "6\n1\n0\n"         /* unit 1: AMBULANCE at 0                         */
    "6\n2\n2\n"         /* unit 2: FIRE_UNIT at 2                         */
    "1\n1\n5\n4\n"      /* incident 1001: MEDICAL at 5, severity 4        */
    "1\n3\n4\n5\n"      /* incident 1002: FIRE at 4, severity 5           */
    "2\n"               /* view pending                                   */
    "11\n11\n11\n"      /* dispatch 1002, then 1001, then nothing left    */
    "14\n1002\n"        /* complete 1002                                  */
    "5\n1001\n"         /* cancel 1001: refused, it is DISPATCHED         */
    "10\n2\ny\n"        /* unit 2 out of service                          */
    "7\n"               /* available units: none                          */
    "4\n1001\n"         /* search incident 1001                           */
    "16\n0\n9\n"        /* route to the island: none                      */
    "16\n0\n7\n"        /* route 0 -> 7                                   */
    "19\n"              /* statistics                                     */
    "abc\n99\n"         /* bad menu choices                               */
    "17\n20\n"          /* not implemented / strategy comparison (Ph. 10) */
    "0\n";              /* exit                                           */

static void test_normal_day(void)
{
    TEST_SECTION("scripted day: register, dispatch, complete, reports (autosave off)");
    AppState app;
    char *text = NULL;
    int result = run_script(&app, E2E_DATA, 0, SCRIPT_DAY, &text);
    CHECK_EQ_INT(result, 0);
    CHECK(text != NULL);

    CHECK(contains(text, "Registered unit 1: AMBULANCE at 0 Central Hospital. Status AVAILABLE.\n"));
    CHECK(contains(text, "Registered unit 2: FIRE_UNIT at 2 Fire Station North. Status AVAILABLE.\n"));
    CHECK(contains(text, "Registered incident 1001: MEDICAL, severity 4 (Severe), at 5 Old Town. "
                         "Status PENDING.\n"));
    CHECK(contains(text, "Registered incident 1002: FIRE, severity 5 (Critical), at 4 Industrial Zone. "
                         "Status PENDING.\n"));
    CHECK(contains(text, "Dispatched unit 2 (FIRE_UNIT) to incident 1002 (FIRE, severity 5 Critical) "
                         "at 4 Industrial Zone\n  distance 5, route: 2 Fire Station North > "
                         "4 Industrial Zone\n"));
    CHECK(contains(text, "Dispatched unit 1 (AMBULANCE) to incident 1001 (MEDICAL, severity 4 Severe) "
                         "at 5 Old Town\n  distance 9, route: 0 Central Hospital > 3 Riverside > "
                         "5 Old Town\n"));
    CHECK(contains(text, "No pending incidents.\n"));
    CHECK(contains(text, "Incident 1002 completed. Unit 2 is available again at 4 Industrial Zone.\n"));
    CHECK(contains(text, "Incident 1001 is DISPATCHED; only PENDING incidents can be cancelled.\n"));
    CHECK(contains(text, "Unit 2 is now OUT_OF_SERVICE.\n"));
    CHECK(contains(text, "(no resources)\n"));
    CHECK(contains(text, "#2 ACTIVE: incident 1001 MEDICAL (severity 4) at 5 Old Town\n"));
    CHECK(contains(text, "No road connects 0 Central Hospital and 9 Lake Camp.\n"));
    CHECK(contains(text, "Shortest distance 16. Route: 0 Central Hospital > 1 City Square > "
                         "2 Fire Station North > 4 Industrial Zone > 7 University\n"));
    CHECK(contains(text, "Allocations: 2 (active 1, completed 1)\n"));
    CHECK(contains(text, "Cross-check: all counters agree\n"));
    CHECK(contains(text, "\"abc\" is not a whole number.\n"));
    CHECK(contains(text, "Please type a number from 0 to 23.\n"));
    CHECK(contains(text, "Road closures are an optional feature that has not been implemented.\n"));
    /* Option 20 (Phase 10): the comparison runs on copies; the live state
     * checked below is unchanged by it. */
    CHECK(contains(text, "STRATEGY COMPARISON (academic simulation; times in simulated steps)\n"));
    CHECK(contains(text, "=== 10 x 10 grid, heavy load: 100 locations, 15 units, 400 incidents"));
    CHECK(!contains(text, "run failed"));
    CHECK(!contains(text, " NO\n"));                 /* every run's VALID column is "yes" */
    CHECK(contains(text, "Autosave is off: unsaved changes are discarded.\n"));
    CHECK(contains(text, "Goodbye.\n"));
    CHECK(!contains(text, "(saved to"));            /* autosave off: no file written */
    CHECK(!contains(text, "End of input."));        /* the script ended with 0 */

    /* The state the menu left behind. */
    if (result >= 0) {
        const Incident *i1001 = app_find_incident(&app, 1001);
        const Incident *i1002 = app_find_incident(&app, 1002);
        const Resource *u2 = app_find_resource(&app, 2);
        CHECK(i1001 != NULL && i1001->status == INC_DISPATCHED);
        CHECK(i1002 != NULL && i1002->status == INC_COMPLETED);
        CHECK(u2 != NULL && u2->status == RES_OUT_OF_SERVICE && u2->location_id == 4);
        CHECK_EQ_INT(app.history.count, 2);
        CHECK_EQ_INT(app_check_consistency(&app, stdout), 0);
    }
    free(text);
    app_free(&app);
}

static void test_autosave(void)
{
    TEST_SECTION("autosave writes the record files after a change");
    AppState app;
    char *text = NULL;
    /* register unit 1: RESCUE_UNIT at 4, then exit */
    int result = run_script(&app, SAVE_DIR, 1, "6\n3\n4\n0\n", &text);
    CHECK_EQ_INT(result, 0);
    CHECK(contains(text, "Registered unit 1: RESCUE_UNIT at 4 Industrial Zone. Status AVAILABLE.\n"));
    CHECK(contains(text, "  (saved to " SAVE_DIR ")\n"));
    CHECK(contains(text, "0. Exit (saves first)"));
    free(text);
    app_free(&app);

    /* The file is written in text mode, so on Windows each line ends in
     * "\r\n" and elsewhere in "\n" (the loader accepts both, Phase 7).
     * Fixed on 2026-11-19 after the first real run showed "\r\n". */
    char *csv = read_all(SAVE_DIR "/resources.csv");
    CHECK(contains(csv, "\n1,RESCUE_UNIT,4,AVAILABLE,-1,0,0\n") ||
          contains(csv, "\n1,RESCUE_UNIT,4,AVAILABLE,-1,0,0\r\n"));
    free(csv);

    /* Loading the saved folder again gives the same unit (persistence). */
    FmLoadReport report;
    CHECK_EQ_INT(fm_load(&app, SAVE_DIR, NULL, &report), DC_OK);
    CHECK_EQ_INT(app.resources.count, 1);
    const Resource *r = app_find_resource(&app, 1);
    CHECK(r != NULL && r->type == RES_RESCUE_UNIT && r->location_id == 4);
    CHECK_EQ_INT(app.next_resource_id, 2);
    app_free(&app);
}

static void test_end_of_input(void)
{
    TEST_SECTION("input ending in the middle of an action exits cleanly");
    AppState app;
    char *text = NULL;
    /* register incident, type FIRE, then the input ends at the location question */
    int result = run_script(&app, E2E_DATA, 0, "1\n3\n", &text);
    CHECK_EQ_INT(result, 0);
    CHECK(contains(text, "End of input.\n"));
    CHECK(contains(text, "Goodbye.\n"));
    CHECK(!contains(text, "Autosave is off"));      /* nothing changed: nothing to discard */
    CHECK_EQ_INT(app.incidents.count, 0);           /* half-typed incident not created */
    free(text);
    app_free(&app);

    /* Empty input: the very first question meets end of input. */
    result = run_script(&app, E2E_DATA, 0, "", &text);
    CHECK_EQ_INT(result, 0);
    CHECK(contains(text, "End of input.\n"));
    free(text);
    app_free(&app);
}

static void test_session_null(void)
{
    TEST_SECTION("NULL session arguments");
    CHECK_EQ_INT(menu_run(NULL), 1);
    CHECK_EQ_INT(menu_handle(NULL, 1), MENU_EXIT);
    menu_print(NULL);                                /* must not crash */
    menu_session_init(NULL, NULL, NULL, NULL, 0);
}

int main(void)
{
    test_normal_day();
    test_autosave();
    test_end_of_input();
    test_session_null();
    return TEST_SUMMARY();
}
