/*
 * dijkstra.h — single-source shortest paths on the road graph.
 *
 * Two implementations that must always produce identical distances:
 *   dijkstra_run        binary min-heap ("lazy" version)   O((V + E) log V)
 *   dijkstra_run_array  linear scan for the closest vertex O(V^2)
 * The array version is kept as a correctness reference for tests and for
 * the Phase 10 performance comparison. Dispatch uses dijkstra_run.
 *
 * Depends on: graph.h (read-only), minheap.h, common.h.
 */
#ifndef DC_DIJKSTRA_H
#define DC_DIJKSTRA_H

#include "common.h"
#include "graph.h"

typedef struct {
    int  source;        /* vertex the distances are measured from          */
    int  num_vertices;  /* length of both arrays                           */
    int *dist;          /* dist[v] = shortest distance, or DC_INF (-1)     */
    int *prev;          /* prev[v] = vertex before v on that path, -1 if none */
} ShortestPaths;

/* Computes shortest distances from `source` to every vertex of g.
 * On success the caller owns *out and must call shortest_paths_free(out).
 * On failure *out holds NULL arrays (still safe to free).
 * Errors: DC_ERR_INVALID (NULL pointer, invalid source), DC_ERR_NOMEM. */
DcStatus dijkstra_run(const Graph *g, int source, ShortestPaths *out);

/* Same contract, O(V^2) array-scan implementation. */
DcStatus dijkstra_run_array(const Graph *g, int source, ShortestPaths *out);

/* Frees both arrays and resets the struct. Safe on NULL / already freed. */
void shortest_paths_free(ShortestPaths *sp);

/* Distance from sp->source to dest, or DC_INF if unreachable or dest invalid. */
int shortest_paths_distance(const ShortestPaths *sp, int dest);

/* Builds the route source -> ... -> dest by following prev[] backwards.
 * On success *path_out is a new malloc'd array (caller frees it) holding
 * *len_out vertices; path[0] == source and path[len-1] == dest.
 * Errors: DC_ERR_INVALID (bad args, or corrupted prev[] chain),
 *         DC_ERR_NOT_FOUND (dest unreachable), DC_ERR_NOMEM.
 * On error *path_out is NULL and *len_out is 0. */
DcStatus dijkstra_build_path(const ShortestPaths *sp, int dest,
                             int **path_out, int *len_out);

/* Independent route check: 1 if path has len >= 1 vertices, starts at
 * `source`, ends at `dest`, every consecutive pair is joined by a road in g,
 * and the road lengths add up to expected_dist. Otherwise 0. */
int dijkstra_validate_path(const Graph *g, const int *path, int len,
                           int source, int dest, int expected_dist);

#endif /* DC_DIJKSTRA_H */
