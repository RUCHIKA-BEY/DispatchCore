/*
 * test_graph.c — unit tests for the graph module (Phase 1).
 *
 * Build and run:   make test
 * Exit status:     0 if every check passed, 1 otherwise.
 */
#include "graph.h"
#include "reference_city.h"
#include "test_util.h"

#include <stddef.h>

static void test_create_rejects_bad_sizes(void)
{
    TEST_SECTION("graph_create rejects invalid sizes");
    CHECK(graph_create(0) == NULL);
    CHECK(graph_create(-5) == NULL);
    CHECK(graph_create(DC_MAX_VERTICES + 1) == NULL);
}

static void test_create_empty_graph(void)
{
    TEST_SECTION("graph_create builds an empty graph");
    Graph *g = graph_create(4);
    CHECK(g != NULL);
    if (g == NULL)
        return;

    CHECK_EQ_INT(g->num_vertices, 4);
    CHECK_EQ_INT(g->num_edges, 0);
    CHECK_STR_EQ(graph_location_name(g, 0), "Location 0");
    CHECK_STR_EQ(graph_location_name(g, 3), "Location 3");
    for (int v = 0; v < 4; v++)
        CHECK_EQ_INT(graph_degree(g, v), 0);
    /* no roads: every location is its own component */
    CHECK_EQ_INT(graph_count_components(g), 4);
    graph_destroy(g);
}

static void test_vertex_validation(void)
{
    TEST_SECTION("vertex validation");
    Graph *g = graph_create(3);
    CHECK(g != NULL);
    if (g == NULL)
        return;

    CHECK(graph_is_valid_vertex(g, 0));
    CHECK(graph_is_valid_vertex(g, 2));
    CHECK(!graph_is_valid_vertex(g, 3));
    CHECK(!graph_is_valid_vertex(g, -1));
    CHECK(!graph_is_valid_vertex(NULL, 0));
    CHECK(graph_location_name(g, 3) == NULL);
    CHECK_EQ_INT(graph_degree(g, -1), -1);
    graph_destroy(g);
}

static void test_location_names(void)
{
    TEST_SECTION("location names");
    Graph *g = graph_create(2);
    CHECK(g != NULL);
    if (g == NULL)
        return;

    CHECK_EQ_INT(graph_set_location_name(g, 0, "Old Town"), DC_OK);
    CHECK_STR_EQ(graph_location_name(g, 0), "Old Town");

    /* 31 characters is the longest that fits in a 32-byte buffer */
    CHECK_EQ_INT(graph_set_location_name(g, 1, "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234"), DC_OK);
    CHECK_EQ_INT(graph_set_location_name(g, 1, "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345"), DC_ERR_INVALID);

    CHECK_EQ_INT(graph_set_location_name(g, 0, ""), DC_ERR_INVALID);
    CHECK_EQ_INT(graph_set_location_name(g, 0, NULL), DC_ERR_INVALID);
    CHECK_EQ_INT(graph_set_location_name(g, 0, "Old,Town"), DC_ERR_INVALID);
    CHECK_EQ_INT(graph_set_location_name(g, 0, "Old\nTown"), DC_ERR_INVALID);
    CHECK_EQ_INT(graph_set_location_name(g, 5, "Nowhere"), DC_ERR_INVALID);
    /* a rejected rename must leave the old name untouched */
    CHECK_STR_EQ(graph_location_name(g, 0), "Old Town");
    graph_destroy(g);
}

static void test_add_road_both_directions(void)
{
    TEST_SECTION("graph_add_road stores both directions");
    Graph *g = graph_create(3);
    CHECK(g != NULL);
    if (g == NULL)
        return;

    CHECK_EQ_INT(graph_add_road(g, 0, 1, 4), DC_OK);
    CHECK_EQ_INT(graph_road_weight(g, 0, 1), 4);
    CHECK_EQ_INT(graph_road_weight(g, 1, 0), 4);
    CHECK_EQ_INT(graph_road_weight(g, 0, 2), -1);
    CHECK_EQ_INT(g->num_edges, 1);
    CHECK_EQ_INT(graph_degree(g, 0), 1);
    CHECK_EQ_INT(graph_degree(g, 1), 1);
    CHECK_EQ_INT(graph_degree(g, 2), 0);

    /* head insertion: the newest road is first in the list */
    CHECK_EQ_INT(graph_add_road(g, 0, 2, 7), DC_OK);
    CHECK(g->adj[0] != NULL && g->adj[0]->dest == 2);
    CHECK(g->adj[0] != NULL && g->adj[0]->next != NULL && g->adj[0]->next->dest == 1);
    graph_destroy(g);
}

static void test_add_road_rejects_bad_input(void)
{
    TEST_SECTION("graph_add_road rejects invalid roads");
    Graph *g = graph_create(3);
    CHECK(g != NULL);
    if (g == NULL)
        return;

    CHECK_EQ_INT(graph_add_road(g, 0, 1, 4), DC_OK);

    CHECK_EQ_INT(graph_add_road(NULL, 0, 1, 4), DC_ERR_INVALID);
    CHECK_EQ_INT(graph_add_road(g, 1, 1, 4), DC_ERR_INVALID);   /* self-loop       */
    CHECK_EQ_INT(graph_add_road(g, 0, 3, 4), DC_ERR_INVALID);   /* vertex too big  */
    CHECK_EQ_INT(graph_add_road(g, -1, 2, 4), DC_ERR_INVALID);  /* negative vertex */
    CHECK_EQ_INT(graph_add_road(g, 0, 2, 0), DC_ERR_INVALID);   /* zero weight     */
    CHECK_EQ_INT(graph_add_road(g, 0, 2, -3), DC_ERR_INVALID);  /* negative weight */
    CHECK_EQ_INT(graph_add_road(g, 0, 2, DC_MAX_WEIGHT + 1), DC_ERR_INVALID);
    CHECK_EQ_INT(graph_add_road(g, 0, 1, 9), DC_ERR_DUPLICATE); /* same road again */
    CHECK_EQ_INT(graph_add_road(g, 1, 0, 9), DC_ERR_DUPLICATE); /* reversed order  */

    /* none of the failures may change the graph */
    CHECK_EQ_INT(g->num_edges, 1);
    CHECK_EQ_INT(graph_road_weight(g, 0, 1), 4);
    CHECK_EQ_INT(graph_degree(g, 0), 1);
    CHECK_EQ_INT(graph_degree(g, 2), 0);

    /* boundary weights are accepted */
    CHECK_EQ_INT(graph_add_road(g, 0, 2, 1), DC_OK);
    CHECK_EQ_INT(graph_add_road(g, 1, 2, DC_MAX_WEIGHT), DC_OK);
    CHECK_EQ_INT(g->num_edges, 3);
    graph_destroy(g);
}

static void test_reference_city(void)
{
    TEST_SECTION("reference city structure");
    Graph *g = build_reference_city();
    CHECK(g != NULL);
    if (g == NULL)
        return;

    CHECK_EQ_INT(g->num_vertices, REF_CITY_VERTICES);
    CHECK_EQ_INT(g->num_edges, REF_CITY_ROADS);
    CHECK_STR_EQ(graph_location_name(g, 5), "Old Town");

    const int expected_degree[REF_CITY_VERTICES] = {2, 3, 2, 3, 2, 3, 2, 3, 1, 1};
    int degree_sum = 0;
    for (int v = 0; v < REF_CITY_VERTICES; v++) {
        CHECK_EQ_INT(graph_degree(g, v), expected_degree[v]);
        degree_sum += graph_degree(g, v);
    }
    /* handshake lemma: sum of degrees = 2 x number of roads */
    CHECK_EQ_INT(degree_sum, 2 * REF_CITY_ROADS);

    CHECK_EQ_INT(graph_road_weight(g, 3, 5), 2);
    CHECK_EQ_INT(graph_road_weight(g, 7, 6), 3);
    CHECK_EQ_INT(graph_road_weight(g, 0, 5), -1);   /* no direct road */
    CHECK_EQ_INT(graph_road_weight(g, 7, 8), -1);   /* different islands */

    /* main city {0..7} and the island {8, 9} */
    CHECK_EQ_INT(graph_count_components(g), 2);

    printf("\n");
    graph_print(g, stdout);
    printf("\n");
    graph_destroy(g);
}

static void test_components(void)
{
    TEST_SECTION("connected components");
    Graph *g = graph_create(6);
    CHECK(g != NULL);
    if (g == NULL)
        return;

    CHECK_EQ_INT(graph_count_components(g), 6);
    graph_add_road(g, 0, 1, 1);
    graph_add_road(g, 2, 3, 1);
    CHECK_EQ_INT(graph_count_components(g), 4);     /* {0,1} {2,3} {4} {5} */
    graph_add_road(g, 1, 2, 1);
    CHECK_EQ_INT(graph_count_components(g), 3);     /* {0,1,2,3} {4} {5} */
    graph_add_road(g, 4, 5, 1);
    graph_add_road(g, 3, 4, 1);
    CHECK_EQ_INT(graph_count_components(g), 1);
    CHECK_EQ_INT(graph_count_components(NULL), -1);
    graph_destroy(g);
}

static void test_large_chain(void)
{
    TEST_SECTION("largest allowed graph (chain of DC_MAX_VERTICES)");
    Graph *g = graph_create(DC_MAX_VERTICES);
    CHECK(g != NULL);
    if (g == NULL)
        return;

    int failures = 0;
    for (int v = 0; v + 1 < DC_MAX_VERTICES; v++) {
        if (graph_add_road(g, v, v + 1, DC_MAX_WEIGHT) != DC_OK)
            failures++;
    }
    CHECK_EQ_INT(failures, 0);
    CHECK_EQ_INT(g->num_edges, DC_MAX_VERTICES - 1);
    CHECK_EQ_INT(graph_degree(g, 0), 1);
    CHECK_EQ_INT(graph_degree(g, 500), 2);
    CHECK_EQ_INT(graph_count_components(g), 1);
    graph_destroy(g);
}

static void test_destroy_null_is_safe(void)
{
    TEST_SECTION("graph_destroy(NULL) and graph_print(NULL) are safe");
    graph_destroy(NULL);
    graph_print(NULL, stdout);
    CHECK(1); /* reaching this line means nothing crashed */
}

int main(void)
{
    test_create_rejects_bad_sizes();
    test_create_empty_graph();
    test_vertex_validation();
    test_location_names();
    test_add_road_both_directions();
    test_add_road_rejects_bad_input();
    test_reference_city();
    test_components();
    test_large_chain();
    test_destroy_null_is_safe();
    return TEST_SUMMARY();
}
