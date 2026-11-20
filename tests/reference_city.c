/*
 * reference_city.c — test fixture: the reference city from blueprint §11.
 * The same data is in data/locations.csv and data/roads.csv.
 */
#include "reference_city.h"

#include <stddef.h>

static const char *const REF_NAMES[REF_CITY_VERTICES] = {
    "Central Hospital", "City Square", "Fire Station North", "Riverside",
    "Industrial Zone",  "Old Town",    "Airport Road",       "University",
    "Hill Village",     "Lake Camp"
};

/* {u, v, distance} — order matters: it fixes the adjacency-list order. */
static const int REF_ROADS[REF_CITY_ROADS][3] = {
    {0, 1, 4}, {0, 3, 7}, {1, 2, 3}, {1, 5, 6}, {2, 4, 5}, {3, 5, 2},
    {3, 6, 9}, {4, 7, 4}, {5, 7, 8}, {6, 7, 3}, {8, 9, 5}
};

Graph *build_reference_city(void)
{
    Graph *g = graph_create(REF_CITY_VERTICES);
    if (g == NULL)
        return NULL;

    for (int i = 0; i < REF_CITY_VERTICES; i++) {
        if (graph_set_location_name(g, i, REF_NAMES[i]) != DC_OK) {
            graph_destroy(g);
            return NULL;
        }
    }
    for (int i = 0; i < REF_CITY_ROADS; i++) {
        if (graph_add_road(g, REF_ROADS[i][0], REF_ROADS[i][1], REF_ROADS[i][2]) != DC_OK) {
            graph_destroy(g);
            return NULL;
        }
    }
    return g;
}
