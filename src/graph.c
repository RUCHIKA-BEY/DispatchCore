/*
 * graph.c — adjacency-list road network (see graph.h for the interface).
 *
 * Memory layout for a graph with 3 vertices and roads 0-1 (4) and 0-2 (7):
 *
 *   g->adj[0] -> [dest 2, w 7] -> [dest 1, w 4] -> NULL
 *   g->adj[1] -> [dest 0, w 4] -> NULL
 *   g->adj[2] -> [dest 0, w 7] -> NULL
 *
 * New roads are inserted at the HEAD of each list (O(1)), so lists appear in
 * reverse order of insertion.
 */
#include "graph.h"

#include <stdlib.h>
#include <string.h>

/* ---------- creation and destruction ---------- */

Graph *graph_create(int num_vertices)
{
    if (num_vertices < 1 || num_vertices > DC_MAX_VERTICES)
        return NULL;

    Graph *g = malloc(sizeof *g);
    if (g == NULL)
        return NULL;

    /* calloc zero-fills, so every list head starts as NULL (no roads). */
    g->locations = calloc((size_t)num_vertices, sizeof *g->locations);
    g->adj       = calloc((size_t)num_vertices, sizeof *g->adj);
    if (g->locations == NULL || g->adj == NULL) {
        free(g->locations);   /* free(NULL) is allowed and does nothing */
        free(g->adj);
        free(g);
        return NULL;
    }

    g->num_vertices = num_vertices;
    g->num_edges    = 0;
    for (int i = 0; i < num_vertices; i++) {
        g->locations[i].id = i;
        snprintf(g->locations[i].name, DC_NAME_MAX, "Location %d", i);
    }
    return g;
}

void graph_destroy(Graph *g)
{
    if (g == NULL)
        return;

    for (int i = 0; i < g->num_vertices; i++) {
        AdjNode *node = g->adj[i];
        while (node != NULL) {
            AdjNode *next = node->next; /* save before free: node is invalid afterwards */
            free(node);
            node = next;
        }
    }
    free(g->adj);
    free(g->locations);
    free(g);
}

/* ---------- locations ---------- */

int graph_is_valid_vertex(const Graph *g, int v)
{
    return g != NULL && v >= 0 && v < g->num_vertices;
}

/* A name is valid if it is non-empty, fits in the buffer and contains no
 * characters that would break a CSV line. */
static int is_valid_location_name(const char *name)
{
    if (name == NULL || name[0] == '\0')
        return 0;
    size_t len = strlen(name);
    if (len >= DC_NAME_MAX)
        return 0;
    return strpbrk(name, ",\r\n") == NULL;
}

DcStatus graph_set_location_name(Graph *g, int id, const char *name)
{
    if (!graph_is_valid_vertex(g, id) || !is_valid_location_name(name))
        return DC_ERR_INVALID;

    /* Length was checked above, so the copy (including '\0') always fits. */
    snprintf(g->locations[id].name, DC_NAME_MAX, "%s", name);
    return DC_OK;
}

const char *graph_location_name(const Graph *g, int v)
{
    return graph_is_valid_vertex(g, v) ? g->locations[v].name : NULL;
}

/* ---------- roads ---------- */

int graph_road_weight(const Graph *g, int u, int v)
{
    if (!graph_is_valid_vertex(g, u) || !graph_is_valid_vertex(g, v))
        return -1;

    for (const AdjNode *node = g->adj[u]; node != NULL; node = node->next) {
        if (node->dest == v)
            return node->weight;
    }
    return -1;
}

DcStatus graph_add_road(Graph *g, int u, int v, int weight)
{
    if (!graph_is_valid_vertex(g, u) || !graph_is_valid_vertex(g, v))
        return DC_ERR_INVALID;
    if (u == v)                                   /* no self-loops */
        return DC_ERR_INVALID;
    if (weight < 1 || weight > DC_MAX_WEIGHT)     /* Dijkstra needs positive weights */
        return DC_ERR_INVALID;
    if (graph_road_weight(g, u, v) != -1)         /* road already exists */
        return DC_ERR_DUPLICATE;

    /* Allocate BOTH nodes before changing anything. If the second malloc
     * fails we undo the first, so the graph never holds a one-way road. */
    AdjNode *to_v = malloc(sizeof *to_v);
    AdjNode *to_u = malloc(sizeof *to_u);
    if (to_v == NULL || to_u == NULL) {
        free(to_v);
        free(to_u);
        return DC_ERR_NOMEM;
    }

    to_v->dest   = v;
    to_v->weight = weight;
    to_v->next   = g->adj[u];   /* insert at head of u's list */
    g->adj[u]    = to_v;

    to_u->dest   = u;
    to_u->weight = weight;
    to_u->next   = g->adj[v];   /* insert at head of v's list */
    g->adj[v]    = to_u;

    g->num_edges++;
    return DC_OK;
}

int graph_degree(const Graph *g, int v)
{
    if (!graph_is_valid_vertex(g, v))
        return -1;

    int count = 0;
    for (const AdjNode *node = g->adj[v]; node != NULL; node = node->next)
        count++;
    return count;
}

/* ---------- connectivity ---------- */

int graph_count_components(const Graph *g)
{
    if (g == NULL)
        return -1;

    int n = g->num_vertices;
    char *visited = calloc((size_t)n, sizeof *visited); /* 0 = not yet seen */
    int  *queue   = malloc((size_t)n * sizeof *queue);  /* each vertex enters once */
    if (visited == NULL || queue == NULL) {
        free(visited);
        free(queue);
        return -1;
    }

    int components = 0;
    for (int start = 0; start < n; start++) {
        if (visited[start])
            continue;

        /* New component found: breadth-first search marks everything
         * reachable from `start`. */
        components++;
        int head = 0, tail = 0;
        queue[tail++] = start;
        visited[start] = 1;

        while (head < tail) {
            int u = queue[head++];
            for (const AdjNode *node = g->adj[u]; node != NULL; node = node->next) {
                if (!visited[node->dest]) {
                    visited[node->dest] = 1;      /* mark when enqueued, not when dequeued */
                    queue[tail++] = node->dest;
                }
            }
        }
    }

    free(visited);
    free(queue);
    return components;
}

/* ---------- display ---------- */

void graph_print(const Graph *g, FILE *out)
{
    if (out == NULL)
        return;
    if (g == NULL) {
        fprintf(out, "(no road network loaded)\n");
        return;
    }

    fprintf(out, "Road network: %d locations, %d roads\n",
            g->num_vertices, g->num_edges);

    for (int i = 0; i < g->num_vertices; i++) {
        fprintf(out, "  [%2d] %-20s:", i, g->locations[i].name);
        if (g->adj[i] == NULL) {
            fprintf(out, " (no roads)\n");
            continue;
        }
        for (const AdjNode *node = g->adj[i]; node != NULL; node = node->next) {
            fprintf(out, " -> %d %s (%d)",
                    node->dest, g->locations[node->dest].name, node->weight);
        }
        fprintf(out, "\n");
    }
}
