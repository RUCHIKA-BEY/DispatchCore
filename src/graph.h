/*
 * graph.h — the city road network as a weighted, undirected graph stored
 * with adjacency lists.
 *
 *   vertices = locations, numbered 0 .. num_vertices-1 (ID == array index)
 *   edges    = roads, each with a positive integer length (weight)
 *
 * Every road u-v is stored twice: v in u's list and u in v's list.
 * Depends on: common.h only.
 */
#ifndef DC_GRAPH_H
#define DC_GRAPH_H

#include <stdio.h>
#include "common.h"

/* One entry in an adjacency list: "there is a road from the owning vertex
 * to `dest` of length `weight`". */
typedef struct AdjNode {
    int dest;               /* neighbouring vertex index          */
    int weight;             /* road length, 1 .. DC_MAX_WEIGHT    */
    struct AdjNode *next;   /* next road leaving the same vertex  */
} AdjNode;

typedef struct {
    int  id;                 /* always equal to its index in the array */
    char name[DC_NAME_MAX];
} Location;

typedef struct {
    int       num_vertices;  /* fixed when the graph is created          */
    int       num_edges;     /* number of undirected roads               */
    Location *locations;     /* array of num_vertices locations          */
    AdjNode **adj;           /* array of num_vertices list heads (NULL = no roads) */
} Graph;

/* Creates a graph with num_vertices locations and no roads.
 * Locations get default names "Location 0", "Location 1", ...
 * Returns NULL if num_vertices is outside 1..DC_MAX_VERTICES or memory runs out. */
Graph *graph_create(int num_vertices);

/* Frees every list node, both arrays and the graph. Safe to call with NULL. */
void graph_destroy(Graph *g);

/* 1 if v is a valid vertex index of g, otherwise 0. */
int graph_is_valid_vertex(const Graph *g, int v);

/* Renames a location. The name must be 1..DC_NAME_MAX-1 characters and must
 * not contain ',' '\n' or '\r' (it will later be stored in a CSV file). */
DcStatus graph_set_location_name(Graph *g, int id, const char *name);

/* Name of location v, or NULL if v is invalid. */
const char *graph_location_name(const Graph *g, int v);

/* Adds an undirected road u-v of length weight.
 * Errors: DC_ERR_INVALID  (NULL graph, bad vertex, u == v, weight outside 1..DC_MAX_WEIGHT)
 *         DC_ERR_DUPLICATE (road u-v already exists)
 *         DC_ERR_NOMEM     (graph is left unchanged) */
DcStatus graph_add_road(Graph *g, int u, int v, int weight);

/* Length of the road u-v, or -1 if there is no such road or a vertex is invalid. */
int graph_road_weight(const Graph *g, int u, int v);

/* Number of roads touching v, or -1 if v is invalid. */
int graph_degree(const Graph *g, int v);

/* Number of connected components (groups of mutually reachable locations),
 * found with breadth-first search. Returns -1 on invalid graph or out of memory. */
int graph_count_components(const Graph *g);

/* Writes a readable listing of every location and its roads to `out`. */
void graph_print(const Graph *g, FILE *out);

#endif /* DC_GRAPH_H */
