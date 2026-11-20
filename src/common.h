/*
 * common.h — definitions shared by every DispatchCore module.
 *
 * Contains only status codes and size limits. It must never include another
 * project header, so that any module can include it without creating a
 * circular dependency.
 */
#ifndef DC_COMMON_H
#define DC_COMMON_H

/* Every function that can fail returns one of these instead of printing an
 * error itself. The caller (usually the menu) decides how to report it. */
typedef enum {
    DC_OK = 0,
    DC_ERR_NOMEM,      /* malloc/calloc/realloc returned NULL            */
    DC_ERR_INVALID,    /* bad argument or bad input value                 */
    DC_ERR_NOT_FOUND,  /* the requested ID does not exist                 */
    DC_ERR_DUPLICATE,  /* the ID or road already exists                   */
    DC_ERR_EMPTY,      /* the heap or list is empty                       */
    DC_ERR_STATE,      /* illegal status transition                       */
    DC_ERR_IO          /* a file could not be read or written             */
} DcStatus;

#define DC_NAME_MAX      32     /* location name buffer size, including '\0' */
#define DC_LINE_MAX      512    /* longest accepted CSV/input line            */
#define DC_MAX_WEIGHT    10000  /* longest allowed single road                */
#define DC_MAX_VERTICES  1000   /* largest allowed city                       */

/* "Unreachable" marker for distance arrays (used from Phase 2).
 * We use -1 instead of INT_MAX so no code ever computes INT_MAX + weight,
 * which would be signed overflow (undefined behaviour in C). */
#define DC_INF (-1)

/* Human-readable text for a status code, e.g. "duplicate". */
const char *dc_status_str(DcStatus status);

#endif /* DC_COMMON_H */
