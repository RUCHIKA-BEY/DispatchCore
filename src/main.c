/*
 * main.c — program entry point (Phase 9).
 *
 *   dispatchcore [--data DIR] [--no-autosave] [--echo] [--help]
 *
 *   --data DIR      folder with the CSV files (default: data)
 *   --no-autosave   never write files unless "Save data now" is chosen
 *                   (used by the end-to-end tests so fixtures stay unchanged)
 *   --echo          print every answer read, so that a run driven by a
 *                   script file (program < script.in) is readable
 *   --benchmark     (Phase 10) print the timing measurements and exit;
 *                   no data folder is read or written
 *
 * Start-up rule (Phase 7 doc, question 20): if loading is refused, print
 * the reasons and stop WITHOUT saving anything, so that damaged files are
 * never overwritten by an empty state.
 *
 * Exit status: 0 normal; 1 data could not be loaded, or the final save
 * failed; 2 bad command-line arguments.
 */
#include "evaluation.h"
#include "file_manager.h"
#include "input.h"
#include "menu.h"

#include <stdio.h>
#include <string.h>

static void print_usage(FILE *out)
{
    fprintf(out, "Usage: dispatchcore [--data DIR] [--no-autosave] [--echo] [--benchmark] [--help]\n");
}

int main(int argc, char *argv[])
{
    const char *data_dir = "data";
    int autosave = 1;
    int echo = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--data") == 0 && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (strcmp(argv[i], "--no-autosave") == 0) {
            autosave = 0;
        } else if (strcmp(argv[i], "--echo") == 0) {
            echo = 1;
        } else if (strcmp(argv[i], "--benchmark") == 0) {
            return eval_benchmark(stdout) == DC_OK ? 0 : 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(stdout);
            return 0;
        } else {
            fprintf(stderr, "Unknown or incomplete argument: %s\n", argv[i]);
            print_usage(stderr);
            return 2;
        }
    }

    printf("DispatchCore - emergency dispatch simulation (academic project;\n"
           "not for real emergency use).\n\n");
    printf("Loading data from %s ...\n", data_dir);

    AppState app;
    FmLoadReport report;
    DcStatus st = fm_load(&app, data_dir, stdout, &report);
    fm_print_report(&report, stdout);
    if (st != DC_OK) {
        printf("Could not load the data (%s). Nothing was saved.\n"
               "Fix the files listed above and start the program again.\n",
               dc_status_str(st));
        app_free(&app);           /* fm_load leaves app empty and safe to free */
        return 1;
    }
    printf("Ready: %d locations, %d units, %d incidents, %d allocations.%s\n",
           app_num_locations(&app), app.resources.count, app.incidents.count,
           app.history.count, autosave ? "" : " Autosave is OFF.");

    Console con = {stdin, stdout, echo};
    MenuSession session;
    menu_session_init(&session, &app, &con, data_dir, autosave);
    int result = menu_run(&session);

    app_free(&app);
    return result;
}
