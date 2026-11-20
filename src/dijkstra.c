/*
 * dijkstra.c — Dijkstra's shortest-path algorithm (see dijkstra.h).
 *
 * Core idea: keep a tentative distance for every vertex. Repeatedly take
 * the unfinished vertex with the smallest tentative distance, declare it
 * final, and try to improve ("relax") the distances of its neighbours.
 * Because all road lengths are positive, once a vertex is the smallest
 * unfinished one, no later path can be shorter, so finalising it is safe.
 */
#include "dijkstra.h"
#include "minheap.h"

#include <stdlib.h>

/* ---------- shared helpers ---------- */

static void reset_result(ShortestPaths *out)
{
    out->source = -1;
    out->num_vertices = 0;
    out->dist = NULL;
    out->prev = NULL;
}

/* Validates arguments and allocates dist[] (all DC_INF) and prev[] (all -1),
 * with dist[source] = 0. Used by both implementations. */
static DcStatus prepare_result(const Graph *g, int source, ShortestPaths *out)
{
    if (out == NULL)
        return DC_ERR_INVALID;
    reset_result(out);
    if (!graph_is_valid_vertex(g, source))
        return DC_ERR_INVALID;

    int n = g->num_vertices;
    out->dist = malloc((size_t)n * sizeof *out->dist);
    out->prev = malloc((size_t)n * sizeof *out->prev);
    if (out->dist == NULL || out->prev == NULL) {
        shortest_paths_free(out);
        return DC_ERR_NOMEM;
    }

    for (int v = 0; v < n; v++) {
        out->dist[v] = DC_INF;
        out->prev[v] = -1;
    }
    out->source = source;
    out->num_vertices = n;
    out->dist[source] = 0;
    return DC_OK;
}

/* Relaxation step shared by both versions: tries to reach v through u.
 * Returns 1 if dist[v] improved. dist[u] is known to be finite here.
 * Overflow is impossible: dist[u] < DC_MAX_VERTICES * DC_MAX_WEIGHT (1e7). */
static int relax(ShortestPaths *sp, int u, int v, int weight)
{
    int candidate = sp->dist[u] + weight;
    if (sp->dist[v] == DC_INF || candidate < sp->dist[v]) {
        sp->dist[v] = candidate;
        sp->prev[v] = u;
        return 1;
    }
    return 0;   /* equal or longer: keep the path found first (deterministic) */
}

/* ---------- binary min-heap version ---------- */

DcStatus dijkstra_run(const Graph *g, int source, ShortestPaths *out)
{
    DcStatus st = prepare_result(g, source, out);
    if (st != DC_OK)
        return st;

    int n = g->num_vertices;
    char *finalised = calloc((size_t)n, sizeof *finalised);
    MinHeap heap;
    st = minheap_init(&heap, n);        /* grows automatically if needed */
    if (finalised == NULL || st != DC_OK) {
        free(finalised);
        minheap_free(&heap);
        shortest_paths_free(out);
        return DC_ERR_NOMEM;
    }

    st = minheap_push(&heap, source, 0);

    MinHeapEntry entry;
    while (st == DC_OK && minheap_pop(&heap, &entry) == DC_OK) {
        int u = entry.vertex;

        /* "Lazy deletion": a vertex can be in the heap several times, once
         * for every improvement of its distance. Only the first (smallest)
         * copy to come out matters; later copies are stale and skipped. */
        if (finalised[u])
            continue;
        finalised[u] = 1;

        for (const AdjNode *road = g->adj[u]; road != NULL; road = road->next) {
            int v = road->dest;
            if (finalised[v])
                continue;                       /* already optimal */
            if (relax(out, u, v, road->weight)) {
                st = minheap_push(&heap, v, out->dist[v]);
                if (st != DC_OK)
                    break;                      /* out of memory */
            }
        }
    }

    free(finalised);
    minheap_free(&heap);
    if (st != DC_OK) {
        shortest_paths_free(out);               /* never return half a result */
        return st;
    }
    return DC_OK;
}

/* ---------- O(V^2) reference version ---------- */

DcStatus dijkstra_run_array(const Graph *g, int source, ShortestPaths *out)
{
    DcStatus st = prepare_result(g, source, out);
    if (st != DC_OK)
        return st;

    int n = g->num_vertices;
    char *finalised = calloc((size_t)n, sizeof *finalised);
    if (finalised == NULL) {
        shortest_paths_free(out);
        return DC_ERR_NOMEM;
    }

    for (int round = 0; round < n; round++) {
        /* Linear scan: the unfinalised vertex with the smallest finite
         * distance; ties go to the lower index (scan order). */
        int u = -1;
        for (int v = 0; v < n; v++) {
            if (finalised[v] || out->dist[v] == DC_INF)
                continue;
            if (u == -1 || out->dist[v] < out->dist[u])
                u = v;
        }
        if (u == -1)
            break;      /* everything left is unreachable */

        finalised[u] = 1;
        for (const AdjNode *road = g->adj[u]; road != NULL; road = road->next) {
            if (!finalised[road->dest])
                relax(out, u, road->dest, road->weight);
        }
    }

    free(finalised);
    return DC_OK;
}

/* ---------- results ---------- */

void shortest_paths_free(ShortestPaths *sp)
{
    if (sp == NULL)
        return;
    free(sp->dist);
    free(sp->prev);
    reset_result(sp);
}

int shortest_paths_distance(const ShortestPaths *sp, int dest)
{
    if (sp == NULL || sp->dist == NULL || dest < 0 || dest >= sp->num_vertices)
        return DC_INF;
    return sp->dist[dest];
}

DcStatus dijkstra_build_path(const ShortestPaths *sp, int dest,
                             int **path_out, int *len_out)
{
    if (path_out == NULL || len_out == NULL)
        return DC_ERR_INVALID;
    *path_out = NULL;
    *len_out = 0;

    if (sp == NULL || sp->dist == NULL || sp->prev == NULL ||
        dest < 0 || dest >= sp->num_vertices)
        return DC_ERR_INVALID;
    if (sp->dist[dest] == DC_INF)
        return DC_ERR_NOT_FOUND;

    /* Pass 1: count the vertices on the path. A valid path has at most
     * num_vertices vertices; more than that means prev[] contains a cycle,
     * so the bound also protects against an infinite loop. */
    int len = 0;
    for (int v = dest; v != -1; v = sp->prev[v]) {
        if (v < 0 || v >= sp->num_vertices || len == sp->num_vertices)
            return DC_ERR_INVALID;
        len++;
    }

    int *path = malloc((size_t)len * sizeof *path);
    if (path == NULL)
        return DC_ERR_NOMEM;

    /* Pass 2: prev[] walks dest -> source, so fill the array from the end. */
    int i = len - 1;
    for (int v = dest; v != -1; v = sp->prev[v])
        path[i--] = v;

    if (path[0] != sp->source) {        /* chain did not end at the source */
        free(path);
        return DC_ERR_INVALID;
    }

    *path_out = path;
    *len_out = len;
    return DC_OK;
}

int dijkstra_validate_path(const Graph *g, const int *path, int len,
                           int source, int dest, int expected_dist)
{
    if (g == NULL || path == NULL || len < 1)
        return 0;
    if (path[0] != source || path[len - 1] != dest)
        return 0;

    long total = 0;   /* long: path is untrusted input, avoid int overflow */
    for (int i = 0; i + 1 < len; i++) {
        int w = graph_road_weight(g, path[i], path[i + 1]);
        if (w < 0)
            return 0;   /* no road between consecutive vertices */
        total += w;
    }
    return total == expected_dist;
}
