/*
 * reference_city.h — builds the 10-location reference city used in all
 * DispatchCore examples and tests (blueprint §11).
 *
 *   0 Central Hospital   5 Old Town
 *   1 City Square        6 Airport Road
 *   2 Fire Station North 7 University
 *   3 Riverside          8 Hill Village   (8 and 9 form a separate island)
 *   4 Industrial Zone    9 Lake Camp
 */
#ifndef DC_REFERENCE_CITY_H
#define DC_REFERENCE_CITY_H

#include "graph.h"

#define REF_CITY_VERTICES 10
#define REF_CITY_ROADS    11

/* Returns a new graph (caller must graph_destroy it), or NULL on failure. */
Graph *build_reference_city(void);

#endif /* DC_REFERENCE_CITY_H */
