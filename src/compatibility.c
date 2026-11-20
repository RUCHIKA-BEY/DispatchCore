/*
 * compatibility.c — the incident-type x resource-type rule table
 * (see compatibility.h).
 */
#include "compatibility.h"

/* COMPAT[incident type][resource type] = 1 if allowed.
 * Rows follow IncidentType order, columns follow ResourceType order.
 * `static const`: private to this file and read-only. */
static const int COMPAT[INC_TYPE_COUNT][RES_TYPE_COUNT] = {
    /*                      AMBULANCE FIRE_UNIT RESCUE_UNIT */
    /* MEDICAL           */ {   1,        0,        0      },
    /* ROAD_ACCIDENT     */ {   1,        0,        1      },
    /* FIRE              */ {   0,        1,        0      },
    /* STRUCTURAL_RESCUE */ {   0,        0,        1      },
};

int compat_is_compatible(IncidentType it, ResourceType rt)
{
    /* Bounds check first: an out-of-range enum would read outside the table. */
    if ((int)it < 0 || (int)it >= INC_TYPE_COUNT)
        return 0;
    if ((int)rt < 0 || (int)rt >= RES_TYPE_COUNT)
        return 0;
    return COMPAT[it][rt];
}

int compat_is_eligible(const Resource *r, IncidentType it)
{
    return r != NULL && compat_is_compatible(it, r->type) && resource_is_available(r);
}

int compat_count_options(IncidentType it)
{
    int n = 0;
    for (int rt = 0; rt < RES_TYPE_COUNT; rt++)
        n += compat_is_compatible(it, (ResourceType)rt);
    return n;
}

int compat_table_is_complete(void)
{
    for (int it = 0; it < INC_TYPE_COUNT; it++)
        if (compat_count_options((IncidentType)it) == 0)
            return 0;
    return 1;
}

void compat_print_options(FILE *out, IncidentType it)
{
    if (out == NULL)
        return;
    int printed = 0;
    for (int rt = 0; rt < RES_TYPE_COUNT; rt++) {
        if (!compat_is_compatible(it, (ResourceType)rt))
            continue;
        fprintf(out, "%s%s", printed > 0 ? " or " : "", resource_type_name((ResourceType)rt));
        printed++;
    }
    if (printed == 0)
        fprintf(out, "(none)");
}

void compat_print_matrix(FILE *out)
{
    if (out == NULL)
        return;
    fprintf(out, "%-19s", "INCIDENT \\ UNIT");
    for (int rt = 0; rt < RES_TYPE_COUNT; rt++)
        fprintf(out, " %-12s", resource_type_name((ResourceType)rt));
    fprintf(out, "\n");

    for (int it = 0; it < INC_TYPE_COUNT; it++) {
        fprintf(out, "%-19s", incident_type_name((IncidentType)it));
        for (int rt = 0; rt < RES_TYPE_COUNT; rt++)
            fprintf(out, " %-12s",
                    compat_is_compatible((IncidentType)it, (ResourceType)rt) ? "yes" : "-");
        fprintf(out, "\n");
    }
}
