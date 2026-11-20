/*
 * menu.h — the interactive menu (Phase 9).
 *
 * The menu is the only module that talks to the user. It reads every
 * answer through input.c, calls the engine modules (app_state, dispatch,
 * report, file_manager), and decides when to save.
 *
 * Everything the menu needs is in one MenuSession passed by pointer (no
 * global variables). Because input and output are FILE streams inside the
 * Console, the whole menu can be driven by a test program from a file.
 *
 * SAVING RULE
 *   - "dirty" = there are changes that have not been saved yet.
 *   - autosave on  (default): after every action that changes records, the
 *                  three record files are saved at once; on exit, any
 *                  unsaved change is saved again.
 *   - autosave off (--no-autosave, used by tests): nothing is written unless
 *                  the user picks "Save data"; on exit unsaved changes are
 *                  discarded WITH a warning.
 *   - A failed save is reported; the data stays in memory and the program
 *     keeps running, so the user can try again.
 *
 * Depends on: app_state.h, input.h (and dispatch, report, file_manager,
 *             dijkstra, compatibility in the .c file).
 */
#ifndef DC_MENU_H
#define DC_MENU_H

#include "app_state.h"
#include "input.h"

#define MENU_MAX_CHOICE 23

typedef struct {
    AppState   *app;        /* loaded data (borrowed; main owns it)        */
    Console    *con;        /* where to read answers and write messages    */
    const char *data_dir;   /* folder used for saving and the report file  */
    int         autosave;   /* 1 = save after every change (see above)     */
    int         dirty;      /* 1 = changes not yet saved                   */
    int         save_failures;  /* number of failed saves, for the exit code */
} MenuSession;

typedef enum {
    MENU_CONTINUE = 0,  /* show the menu again          */
    MENU_EXIT,          /* the user chose 0             */
    MENU_EOF            /* input ended during an action */
} MenuAction;

/* Fills in a session with dirty = 0 and save_failures = 0. */
void menu_session_init(MenuSession *s, AppState *app, Console *con,
                       const char *data_dir, int autosave);

void menu_print(const MenuSession *s);

/* Performs one menu choice (0..MENU_MAX_CHOICE). */
MenuAction menu_handle(MenuSession *s, int choice);

/* The main loop: show the menu, read a choice, handle it, until the user
 * exits or the input ends. Then applies the exit part of the saving rule.
 * Returns 0 if everything that had to be saved was saved, 1 otherwise. */
int menu_run(MenuSession *s);

#endif /* DC_MENU_H */
