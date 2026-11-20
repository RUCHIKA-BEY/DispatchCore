/*
 * common.c — implementation of the helpers declared in common.h.
 */
#include "common.h"

const char *dc_status_str(DcStatus status)
{
    switch (status) {
    case DC_OK:            return "ok";
    case DC_ERR_NOMEM:     return "out of memory";
    case DC_ERR_INVALID:   return "invalid value";
    case DC_ERR_NOT_FOUND: return "not found";
    case DC_ERR_DUPLICATE: return "duplicate";
    case DC_ERR_EMPTY:     return "empty";
    case DC_ERR_STATE:     return "illegal state change";
    case DC_ERR_IO:        return "file error";
    }
    return "unknown status"; /* reached only if a corrupted value is passed */
}
