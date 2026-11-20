/*
 * file_manager.h — loading and saving DispatchCore data as CSV files.
 *
 *   locations.csv    INPUT ONLY  location_id,name
 *   roads.csv        INPUT ONLY  from_id,to_id,distance,status
 *   resources.csv    read/write  resource_id,type,location_id,status,
 *                                assigned_incident_id,total_dispatches,total_distance
 *   incidents.csv    read/write  incident_id,type,location_id,severity,reported_seq,
 *                                status,assigned_resource_id,dispatch_attempts
 *   allocations.csv  read/write  allocation_id,incident_id,resource_id,from_location,
 *                                to_location,distance,route,status,dispatch_seq,complete_seq
 *                                (route is written as 0|3|5)
 *
 * The map files are never written: rewriting roads.csv would change the
 * order of the adjacency lists, which could change which equal-length
 * route is reported after a restart.
 *
 * Loading rules: the first line must be the exact header; blank lines are
 * ignored; a malformed row is skipped with a warning (file:line: reason);
 * the whole load is REFUSED if locations.csv is missing or unusable, if a
 * header is wrong, on a read error, or if the loaded records contradict
 * each other (app_check_consistency). Data is never silently repaired.
 *
 * Saving rules: all three record files are first written to *.tmp and
 * checked; only if all three succeeded is each original renamed to *.bak
 * and the *.tmp renamed into place.
 *
 * Depends on: app_state.h (and, in the .c file, dijkstra.h for route checks).
 */
#ifndef DC_FILE_MANAGER_H
#define DC_FILE_MANAGER_H

#include <stdio.h>
#include "app_state.h"

#define FM_PATH_MAX  512     /* longest accepted "<dir>/<file>.bak" path        */
#define FM_LINE_MAX  8192    /* longest accepted CSV line (long routes fit)    */
#define FM_MAX_ID    100000000  /* IDs above this are rejected (no overflow)   */

typedef struct {
    int loaded;     /* rows accepted                       */
    int skipped;    /* malformed rows skipped with a warning */
    int missing;    /* 1 if the file did not exist          */
} FmFileCount;

typedef struct {
    FmFileCount locations, roads, resources, incidents, allocations;
    int consistency_problems;
} FmLoadReport;

/* Loads everything from directory `dir` into *app, which must NOT already
 * be initialised (it is initialised here). Warnings go to `log` (may be NULL).
 * On success returns DC_OK and *app is ready to use.
 * On failure returns DC_ERR_IO (missing/unusable map, bad header, read
 * error), DC_ERR_STATE (inconsistent records), DC_ERR_NOMEM or
 * DC_ERR_INVALID; *app is then empty and safe to app_free. */
DcStatus fm_load(AppState *app, const char *dir, FILE *log, FmLoadReport *report);

/* Saves resources.csv, incidents.csv and allocations.csv into `dir`
 * (see the saving rules above). DC_ERR_IO on any failure; if a *.tmp file
 * could not be written, the original files are left untouched. */
DcStatus fm_save_records(const AppState *app, const char *dir, FILE *log);

void fm_print_report(const FmLoadReport *report, FILE *out);

#endif /* DC_FILE_MANAGER_H */
