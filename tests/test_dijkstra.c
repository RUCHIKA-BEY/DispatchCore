/*
 * test_dijkstra.c — unit tests for Dijkstra's algorithm (Phase 2).
 *
 * Expected values for the reference city were calculated by hand; the
 * step-by-step traces are in docs/PHASE2_DIJKSTRA.md.
 *
 * Build and run:   make test
 */
#include "dijkstra.h"
#include "graph.h"
#include "reference_city.h"
#include "test_util.h"

#include <stdlib.h>

#define INF DC_INF

typedef DcStatus (*DijkstraFn)(const Graph *, int, ShortestPaths *);

/* Compares a whole dist[] or prev[] array; reports the first mismatch. */
static void check_array(const char *label, const int *actual,
                        const int *expected, int n)
{
    int mismatches = 0;
    for (int v = 0; v < n; v++) {
        if (actual[v] != expected[v]) {
            if (mismatches == 0)
                printf("  %s: first mismatch at vertex %d: got %d, expected %d\n",
                       label, v, actual[v], expected[v]);
            mismatches++;
        }
    }
    CHECK_EQ_INT(mismatches, 0);
}

static void test_invalid_arguments(void)
{
    TEST_SECTION("invalid arguments are rejected safely");
    Graph *g = build_reference_city();
    CHECK(g != NULL);
    if (g == NULL)
        return;

    DijkstraFn versions[2] = {dijkstra_run, dijkstra_run_array};
    for (int k = 0; k < 2; k++) {
        ShortestPaths sp;
        CHECK_EQ_INT(versions[k](NULL, 0, &sp), DC_ERR_INVALID);
        CHECK(sp.dist == NULL && sp.prev == NULL);
        CHECK_EQ_INT(versions[k](g, -1, &sp), DC_ERR_INVALID);
        CHECK_EQ_INT(versions[k](g, REF_CITY_VERTICES, &sp), DC_ERR_INVALID);
        CHECK(sp.dist == NULL && sp.prev == NULL);
        CHECK_EQ_INT(versions[k](g, 0, NULL), DC_ERR_INVALID);
        shortest_paths_free(&sp);          /* freeing a failed result is safe */
    }
    shortest_paths_free(NULL);
    graph_destroy(g);
}

static void test_reference_from_hospital(void)
{
    TEST_SECTION("reference city, source 0 (Central Hospital)");
    Graph *g = build_reference_city();
    CHECK(g != NULL);
    if (g == NULL)
        return;

    ShortestPaths sp;
    CHECK_EQ_INT(dijkstra_run(g, 0, &sp), DC_OK);
    if (sp.dist != NULL) {
        const int dist[REF_CITY_VERTICES] = {0, 4, 7, 7, 12, 9, 16, 16, INF, INF};
        const int prev[REF_CITY_VERTICES] = {-1, 0, 1, 0, 2, 3, 3, 4, -1, -1};
        check_array("dist from 0", sp.dist, dist, REF_CITY_VERTICES);
        check_array("prev from 0", sp.prev, prev, REF_CITY_VERTICES);

        int *path = NULL, len = 0;
        CHECK_EQ_INT(dijkstra_build_path(&sp, 7, &path, &len), DC_OK);
        const int expected_path[5] = {0, 1, 2, 4, 7};   /* 4 + 3 + 5 + 4 = 16 */
        CHECK_EQ_INT(len, 5);
        if (path != NULL && len == 5)
            check_array("path 0->7", path, expected_path, 5);
        CHECK(dijkstra_validate_path(g, path, len, 0, 7, 16));
        free(path);
    }
    shortest_paths_free(&sp);
    graph_destroy(g);
}

static void test_reference_from_old_town(void)
{
    TEST_SECTION("reference city, source 5 (Old Town) - the blueprint dispatch example");
    Graph *g = build_reference_city();
    CHECK(g != NULL);
    if (g == NULL)
        return;

    ShortestPaths sp;
    CHECK_EQ_INT(dijkstra_run(g, 5, &sp), DC_OK);
    if (sp.dist != NULL) {
        const int dist[REF_CITY_VERTICES] = {9, 6, 9, 2, 12, 0, 11, 8, INF, INF};
        const int prev[REF_CITY_VERTICES] = {3, 5, 1, 5, 7, -1, 3, 5, -1, -1};
        check_array("dist from 5", sp.dist, dist, REF_CITY_VERTICES);
        check_array("prev from 5", sp.prev, prev, REF_CITY_VERTICES);

        /* Ambulance R1 at 0: distance 9 via 5 -> 3 -> 0
         * (dispatch will reverse it to 0 -> 3 -> 5). */
        CHECK_EQ_INT(shortest_paths_distance(&sp, 0), 9);
        int *path = NULL, len = 0;
        CHECK_EQ_INT(dijkstra_build_path(&sp, 0, &path, &len), DC_OK);
        const int expected_path[3] = {5, 3, 0};
        CHECK_EQ_INT(len, 3);
        if (path != NULL && len == 3)
            check_array("path 5->0", path, expected_path, 3);
        free(path);

        /* Two equally short routes to 6 exist (5-3-6 and 5-7-6, both 11).
         * The first one found (via 3) is kept. */
        CHECK_EQ_INT(sp.dist[6], 11);
        CHECK_EQ_INT(sp.prev[6], 3);
    }
    shortest_paths_free(&sp);
    graph_destroy(g);
}

static void test_island_and_unreachable(void)
{
    TEST_SECTION("disconnected island and unreachable destinations");
    Graph *g = build_reference_city();
    CHECK(g != NULL);
    if (g == NULL)
        return;

    ShortestPaths sp;
    CHECK_EQ_INT(dijkstra_run(g, 8, &sp), DC_OK);
    if (sp.dist != NULL) {
        const int dist[REF_CITY_VERTICES] = {INF, INF, INF, INF, INF, INF, INF, INF, 0, 5};
        check_array("dist from 8", sp.dist, dist, REF_CITY_VERTICES);

        int dummy = 0;
        int *path = &dummy;       /* deliberately non-NULL: must be reset to NULL */
        int len = 99;
        CHECK_EQ_INT(dijkstra_build_path(&sp, 0, &path, &len), DC_ERR_NOT_FOUND);
        CHECK(path == NULL);
        CHECK_EQ_INT(len, 0);
        CHECK_EQ_INT(shortest_paths_distance(&sp, 0), DC_INF);
        CHECK_EQ_INT(shortest_paths_distance(&sp, 9), 5);
    }
    shortest_paths_free(&sp);
    graph_destroy(g);
}

static void test_path_to_self_and_bad_destinations(void)
{
    TEST_SECTION("path to self and invalid destinations");
    Graph *g = build_reference_city();
    CHECK(g != NULL);
    if (g == NULL)
        return;

    ShortestPaths sp;
    CHECK_EQ_INT(dijkstra_run(g, 3, &sp), DC_OK);
    int *path = NULL, len = 0;
    CHECK_EQ_INT(dijkstra_build_path(&sp, 3, &path, &len), DC_OK);
    CHECK_EQ_INT(len, 1);
    CHECK(path != NULL && path[0] == 3);
    CHECK(dijkstra_validate_path(g, path, len, 3, 3, 0));
    free(path);

    CHECK_EQ_INT(dijkstra_build_path(&sp, -1, &path, &len), DC_ERR_INVALID);
    CHECK_EQ_INT(dijkstra_build_path(&sp, REF_CITY_VERTICES, &path, &len), DC_ERR_INVALID);
    CHECK_EQ_INT(dijkstra_build_path(&sp, 2, NULL, &len), DC_ERR_INVALID);
    CHECK_EQ_INT(dijkstra_build_path(NULL, 2, &path, &len), DC_ERR_INVALID);
    CHECK_EQ_INT(shortest_paths_distance(&sp, 42), DC_INF);
    shortest_paths_free(&sp);

    /* after freeing, the result must be unusable rather than dangerous */
    CHECK_EQ_INT(dijkstra_build_path(&sp, 2, &path, &len), DC_ERR_INVALID);
    graph_destroy(g);
}

static void test_validate_path_rejects_bad_routes(void)
{
    TEST_SECTION("dijkstra_validate_path rejects wrong routes");
    Graph *g = build_reference_city();
    CHECK(g != NULL);
    if (g == NULL)
        return;

    const int good[3]      = {0, 3, 5};    /* 7 + 2 = 9 */
    const int no_road[2]   = {0, 5};       /* 0 and 5 are not adjacent */
    const int off_graph[2] = {0, 42};
    CHECK(dijkstra_validate_path(g, good, 3, 0, 5, 9));
    CHECK(!dijkstra_validate_path(g, good, 3, 0, 5, 10));   /* wrong total */
    CHECK(!dijkstra_validate_path(g, good, 3, 1, 5, 9));    /* wrong start */
    CHECK(!dijkstra_validate_path(g, good, 3, 0, 6, 9));    /* wrong end   */
    CHECK(!dijkstra_validate_path(g, no_road, 2, 0, 5, 9));
    CHECK(!dijkstra_validate_path(g, off_graph, 2, 0, 42, 1));
    CHECK(!dijkstra_validate_path(g, good, 0, 0, 5, 9));    /* empty path  */
    CHECK(!dijkstra_validate_path(g, NULL, 3, 0, 5, 9));
    CHECK(!dijkstra_validate_path(NULL, good, 3, 0, 5, 9));
    graph_destroy(g);
}

static void test_corrupted_prev_chain(void)
{
    TEST_SECTION("a corrupted prev[] cycle cannot cause an infinite loop");
    int dist[3] = {0, 5, 5};
    int prev[3] = {-1, 2, 1};             /* 1 -> 2 -> 1 -> 2 ... never reaches 0 */
    ShortestPaths sp = {0, 3, dist, prev};
    int *path = NULL, len = 0;
    CHECK_EQ_INT(dijkstra_build_path(&sp, 1, &path, &len), DC_ERR_INVALID);
    CHECK(path == NULL);

    int prev_bad_index[3] = {-1, 7, -1};  /* points outside the array */
    sp.prev = prev_bad_index;
    CHECK_EQ_INT(dijkstra_build_path(&sp, 1, &path, &len), DC_ERR_INVALID);
}

static void test_small_graphs(void)
{
    TEST_SECTION("single vertex and graph without roads");
    Graph *one = graph_create(1);
    ShortestPaths sp;
    CHECK(one != NULL);
    if (one != NULL) {
        CHECK_EQ_INT(dijkstra_run(one, 0, &sp), DC_OK);
        CHECK_EQ_INT(shortest_paths_distance(&sp, 0), 0);
        shortest_paths_free(&sp);
        graph_destroy(one);
    }

    Graph *empty = graph_create(4);
    CHECK(empty != NULL);
    if (empty != NULL) {
        CHECK_EQ_INT(dijkstra_run(empty, 2, &sp), DC_OK);
        const int dist[4] = {INF, INF, 0, INF};
        if (sp.dist != NULL)
            check_array("no roads", sp.dist, dist, 4);
        shortest_paths_free(&sp);
        graph_destroy(empty);
    }
}

/* Checks one result against the graph WITHOUT trusting Dijkstra's logic:
 *   - every reachable vertex has a valid route whose length equals dist[v]
 *   - no road can shorten any distance (dist[v] <= dist[u] + w), and a
 *     neighbour of a reachable vertex is reachable.
 * Together these prove the distances are correct shortest distances.
 * Returns the number of problems found. */
static int certify_result(const Graph *g, const ShortestPaths *sp)
{
    int problems = 0;
    for (int v = 0; v < g->num_vertices; v++) {
        if (sp->dist[v] != DC_INF) {
            int *path = NULL, len = 0;
            if (dijkstra_build_path(sp, v, &path, &len) != DC_OK ||
                !dijkstra_validate_path(g, path, len, sp->source, v, sp->dist[v]))
                problems++;
            free(path);
        }
        for (const AdjNode *road = g->adj[v]; road != NULL; road = road->next) {
            int u = road->dest;
            if (sp->dist[v] == DC_INF)
                continue;
            if (sp->dist[u] == DC_INF || sp->dist[u] > sp->dist[v] + road->weight)
                problems++;
        }
    }
    return problems;
}

static void test_reference_all_sources(void)
{
    TEST_SECTION("reference city: all sources, both versions, symmetry");
    Graph *g = build_reference_city();
    CHECK(g != NULL);
    if (g == NULL)
        return;

    int all[REF_CITY_VERTICES][REF_CITY_VERTICES];
    int version_mismatches = 0, certify_problems = 0, failures = 0;

    for (int s = 0; s < REF_CITY_VERTICES; s++) {
        ShortestPaths heap_sp, array_sp;
        if (dijkstra_run(g, s, &heap_sp) != DC_OK ||
            dijkstra_run_array(g, s, &array_sp) != DC_OK) {
            failures++;
            shortest_paths_free(&heap_sp);
            continue;
        }
        for (int v = 0; v < REF_CITY_VERTICES; v++) {
            if (heap_sp.dist[v] != array_sp.dist[v])
                version_mismatches++;
            all[s][v] = heap_sp.dist[v];
        }
        certify_problems += certify_result(g, &heap_sp);
        certify_problems += certify_result(g, &array_sp);
        shortest_paths_free(&heap_sp);
        shortest_paths_free(&array_sp);
    }
    CHECK_EQ_INT(failures, 0);
    CHECK_EQ_INT(version_mismatches, 0);
    CHECK_EQ_INT(certify_problems, 0);

    /* Undirected roads => dist(u,v) == dist(v,u). The dispatch engine relies
     * on this to run Dijkstra once from the incident (blueprint D4). */
    int asymmetric = 0;
    if (failures == 0) {
        for (int u = 0; u < REF_CITY_VERTICES; u++)
            for (int v = 0; v < REF_CITY_VERTICES; v++)
                if (all[u][v] != all[v][u])
                    asymmetric++;
    }
    CHECK_EQ_INT(asymmetric, 0);
    graph_destroy(g);
}

static void test_random_graphs(void)
{
    TEST_SECTION("60 random graphs: heap version == array version, results certified");
    uint32_t seed = 2026u;
    int failures = 0, version_mismatches = 0, certify_problems = 0;

    for (int trial = 0; trial < 60; trial++) {
        int n = 1 + test_random(&seed, 40);
        Graph *g = graph_create(n);
        if (g == NULL) {
            failures++;
            continue;
        }
        /* Few edges in some trials (many components), many in others.
         * Small weights (1..20) make equal-length routes common. */
        int attempts = test_random(&seed, 3 * n + 1);
        for (int k = 0; k < attempts; k++) {
            int u = test_random(&seed, n);
            int v = test_random(&seed, n);
            int w = 1 + test_random(&seed, 20);
            graph_add_road(g, u, v, w);   /* self-loops/duplicates are rejected; fine */
        }

        for (int s = 0; s < n; s++) {
            ShortestPaths a, b;
            if (dijkstra_run(g, s, &a) != DC_OK || dijkstra_run_array(g, s, &b) != DC_OK) {
                failures++;
                shortest_paths_free(&a);
                continue;
            }
            for (int v = 0; v < n; v++)
                if (a.dist[v] != b.dist[v])
                    version_mismatches++;
            certify_problems += certify_result(g, &a);
            certify_problems += certify_result(g, &b);
            shortest_paths_free(&a);
            shortest_paths_free(&b);
        }
        graph_destroy(g);
    }
    CHECK_EQ_INT(failures, 0);
    CHECK_EQ_INT(version_mismatches, 0);
    CHECK_EQ_INT(certify_problems, 0);
}

int main(void)
{
    test_invalid_arguments();
    test_reference_from_hospital();
    test_reference_from_old_town();
    test_island_and_unreachable();
    test_path_to_self_and_bad_destinations();
    test_validate_path_rejects_bad_routes();
    test_corrupted_prev_chain();
    test_small_graphs();
    test_reference_all_sources();
    test_random_graphs();
    return TEST_SUMMARY();
}
