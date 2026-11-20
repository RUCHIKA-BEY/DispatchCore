/*
 * compatibility.h — which resource types may serve which incident types.
 *
 * Simplified PROJECT RULES (not real emergency protocols):
 *
 *                       AMBULANCE  FIRE_UNIT  RESCUE_UNIT
 *   MEDICAL                 yes        -          -
 *   ROAD_ACCIDENT           yes        -          yes
 *   FIRE                     -        yes         -
 *   STRUCTURAL_RESCUE        -         -          yes
 *
 * The rules live in ONE read-only table in compatibility.c; changing a rule
 * means changing one cell. This module exists separately because it needs
 * both incident.h and resource.h — putting it inside either of them would
 * make that module depend on the other.
 *
 * Depends on: incident.h, resource.h.
 */
#ifndef DC_COMPATIBILITY_H
#define DC_COMPATIBILITY_H

#include <stdio.h>
#include "incident.h"
#include "resource.h"

/* 1 if resource type rt may serve incident type it; 0 otherwise, including
 * for out-of-range values. O(1). */
int compat_is_compatible(IncidentType it, ResourceType rt);

/* 1 if r may be sent to an incident of type it RIGHT NOW:
 * compatible AND available. This is the filter applied before any distance
 * is considered (blueprint: compatibility before distance). */
int compat_is_eligible(const Resource *r, IncidentType it);

/* Number of resource types that can serve incident type it (0 if invalid). */
int compat_count_options(IncidentType it);

/* Sanity check of the table itself: 1 if every incident type can be served
 * by at least one resource type, i.e. no incident is unservable by design. */
int compat_table_is_complete(void);

/* Writes e.g. "AMBULANCE or RESCUE_UNIT" for ROAD_ACCIDENT. */
void compat_print_options(FILE *out, IncidentType it);

/* Prints the whole matrix as a table. */
void compat_print_matrix(FILE *out);

#endif /* DC_COMPATIBILITY_H */
