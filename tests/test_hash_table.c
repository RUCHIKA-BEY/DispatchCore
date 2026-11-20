/*
 * test_hash_table.c — unit tests for the separate-chaining hash table
 * (Phase 5), with deliberately forced collisions.
 *
 * Build and run:   make test
 */
#include "hash_table.h"
#include "test_util.h"

#include <stdlib.h>

/* Values in these tests are addresses of local ints: the table only
 * borrows them, so nothing here must ever be freed by the table. */

static int chain_length(const HashTable *t, int bucket)
{
    int n = 0;
    for (const HashNode *node = t->buckets[bucket]; node != NULL; node = node->next)
        n++;
    return n;
}

static void test_create_and_hash_function(void)
{
    TEST_SECTION("creation and the hash function");
    CHECK(hash_table_create(0) == NULL);
    CHECK(hash_table_create(-3) == NULL);
    CHECK(hash_table_create(DC_MAX_BUCKETS + 1) == NULL);
    hash_table_destroy(NULL);

    CHECK_EQ_INT(hash_index(1001, 101), 1001 % 101);   /* 1001 = 9*101 + 92 */
    CHECK_EQ_INT(hash_index(1001, 101), 92);
    CHECK_EQ_INT(hash_index(0, 7), 0);
    CHECK_EQ_INT(hash_index(7, 7), 0);
    CHECK_EQ_INT(hash_index(5, 1), 0);                  /* one bucket: everything in 0 */

    /* negative keys must still give an index inside the table */
    int out_of_range = 0;
    for (int key = -1000; key <= 1000; key++) {
        int i = hash_index(key, 13);
        if (i < 0 || i >= 13)
            out_of_range++;
    }
    CHECK_EQ_INT(out_of_range, 0);
    CHECK(hash_index(-7, 5) >= 0);
}

static void test_basic_operations(void)
{
    TEST_SECTION("insert, find, duplicate, remove");
    HashTable *t = hash_table_create(101);
    CHECK(t != NULL);
    if (t == NULL)
        return;

    int a = 1, b = 2;
    CHECK_EQ_INT(hash_table_insert(t, 1001, &a), DC_OK);
    CHECK_EQ_INT(hash_table_insert(t, 1002, &b), DC_OK);
    CHECK_EQ_INT(hash_table_count(t), 2);
    CHECK(hash_table_find(t, 1001) == &a);
    CHECK(hash_table_find(t, 1002) == &b);
    CHECK(hash_table_find(t, 1003) == NULL);

    CHECK_EQ_INT(hash_table_insert(t, 1001, &b), DC_ERR_DUPLICATE);
    CHECK(hash_table_find(t, 1001) == &a);        /* original value kept */
    CHECK_EQ_INT(hash_table_count(t), 2);

    CHECK_EQ_INT(hash_table_insert(t, 1004, NULL), DC_ERR_INVALID);
    CHECK_EQ_INT(hash_table_insert(NULL, 1004, &a), DC_ERR_INVALID);

    CHECK_EQ_INT(hash_table_remove(t, 1001), DC_OK);
    CHECK(hash_table_find(t, 1001) == NULL);
    CHECK_EQ_INT(hash_table_remove(t, 1001), DC_ERR_NOT_FOUND);
    CHECK_EQ_INT(hash_table_count(t), 1);
    CHECK_EQ_INT(a, 1);                           /* the value itself untouched */

    /* a removed key can be inserted again */
    CHECK_EQ_INT(hash_table_insert(t, 1001, &a), DC_OK);
    CHECK(hash_table_find(NULL, 1001) == NULL);
    hash_table_destroy(t);
}

static void test_forced_collisions(void)
{
    TEST_SECTION("forced collisions: keys 1, 6, 11, 16 in a 5-bucket table");
    HashTable *t = hash_table_create(5);
    CHECK(t != NULL);
    if (t == NULL)
        return;

    int values[4] = {10, 60, 110, 160};
    const int keys[4] = {1, 6, 11, 16};     /* all are 1 mod 5 -> bucket 1 */
    for (int i = 0; i < 4; i++)
        CHECK_EQ_INT(hash_table_insert(t, keys[i], &values[i]), DC_OK);

    CHECK_EQ_INT(chain_length(t, 1), 4);
    CHECK_EQ_INT(chain_length(t, 0), 0);
    /* head insertion: the chain reads 16 -> 11 -> 6 -> 1 */
    const HashNode *n = t->buckets[1];
    CHECK(n != NULL && n->key == 16);
    CHECK(n != NULL && n->next != NULL && n->next->key == 11);

    for (int i = 0; i < 4; i++)
        CHECK(hash_table_find(t, keys[i]) == &values[i]);
    CHECK(hash_table_find(t, 21) == NULL);  /* same bucket, absent key */

    printf("\n");
    hash_table_print(t, stdout);

    /* remove from the middle, then the head, then the tail, then the last one */
    CHECK_EQ_INT(hash_table_remove(t, 11), DC_OK);   /* 16 -> 6 -> 1 */
    CHECK_EQ_INT(chain_length(t, 1), 3);
    CHECK(hash_table_find(t, 6) == &values[1]);
    CHECK_EQ_INT(hash_table_remove(t, 16), DC_OK);   /* head: 6 -> 1 */
    CHECK(t->buckets[1] != NULL && t->buckets[1]->key == 6);
    CHECK_EQ_INT(hash_table_remove(t, 1), DC_OK);    /* tail: 6 */
    CHECK_EQ_INT(chain_length(t, 1), 1);
    CHECK_EQ_INT(hash_table_remove(t, 21), DC_ERR_NOT_FOUND);
    CHECK_EQ_INT(hash_table_remove(t, 6), DC_OK);    /* bucket now empty */
    CHECK(t->buckets[1] == NULL);
    CHECK_EQ_INT(hash_table_count(t), 0);
    hash_table_destroy(t);
}

/* Inserts 10, 20, ..., 500 and returns the table's statistics. */
static HashStats multiples_of_ten(int num_buckets)
{
    HashStats s = {0, 0, 0, 0, 0.0};
    static int dummy = 0;               /* any non-NULL value will do */
    HashTable *t = hash_table_create(num_buckets);
    if (t == NULL)
        return s;
    for (int k = 1; k <= 50; k++)
        hash_table_insert(t, 10 * k, &dummy);
    hash_table_stats(t, &s);
    hash_table_destroy(t);
    return s;
}

static void test_prime_bucket_count(void)
{
    TEST_SECTION("why the bucket count should be prime");
    /* 10 buckets: every multiple of 10 is 0 mod 10 -> one chain of 50 */
    HashStats bad = multiples_of_ten(10);
    CHECK_EQ_INT(bad.count, 50);
    CHECK_EQ_INT(bad.used_buckets, 1);
    CHECK_EQ_INT(bad.longest_chain, 50);

    /* 11 buckets (prime): 10 and 11 share no factor, so all 11 are used;
     * 50 = 4*11 + 6 -> six chains of 5 and five chains of 4 */
    HashStats good = multiples_of_ten(11);
    CHECK_EQ_INT(good.used_buckets, 11);
    CHECK_EQ_INT(good.longest_chain, 5);
    CHECK(good.load_factor > 4.5 && good.load_factor < 4.6);   /* 50/11 = 4.545... */

    printf("  10 buckets: used %d, longest chain %d\n", bad.used_buckets, bad.longest_chain);
    printf("  11 buckets: used %d, longest chain %d\n", good.used_buckets, good.longest_chain);
}

static void test_large_and_against_oracle(void)
{
    TEST_SECTION("10000 keys; random operations compared with a simple array");
    enum { N = 10000 };
    HashTable *t = hash_table_create(101);
    CHECK(t != NULL);
    if (t == NULL)
        return;

    static int marker[N];            /* static: 40 KB kept off the stack */
    int failures = 0;
    for (int k = 0; k < N; k++)
        if (hash_table_insert(t, k + 1, &marker[k]) != DC_OK)
            failures++;
    for (int k = 0; k < N; k += 2)
        if (hash_table_remove(t, k + 1) != DC_OK)
            failures++;
    CHECK_EQ_INT(failures, 0);
    CHECK_EQ_INT(hash_table_count(t), N / 2);

    int lookup_errors = 0;
    for (int k = 0; k < N; k++) {
        void *expected = (k % 2 == 0) ? NULL : (void *)&marker[k];
        if (hash_table_find(t, k + 1) != expected)
            lookup_errors++;
    }
    CHECK_EQ_INT(lookup_errors, 0);
    hash_table_destroy(t);

    /* Random insert/remove/find on keys 0..199 checked against a plain
     * "present?" array, which is obviously correct. */
    HashTable *r = hash_table_create(7);     /* small: long chains */
    CHECK(r != NULL);
    if (r == NULL)
        return;
    int present[200] = {0};
    int mismatches = 0;
    uint32_t seed = 55u;
    for (int step = 0; step < 20000; step++) {
        int key = test_random(&seed, 200);
        int op = test_random(&seed, 3);
        if (op == 0) {
            DcStatus st = hash_table_insert(r, key, &marker[key]);
            if (st != (present[key] ? DC_ERR_DUPLICATE : DC_OK))
                mismatches++;
            present[key] = 1;
        } else if (op == 1) {
            DcStatus st = hash_table_remove(r, key);
            if (st != (present[key] ? DC_OK : DC_ERR_NOT_FOUND))
                mismatches++;
            present[key] = 0;
        } else {
            void *found = hash_table_find(r, key);
            if ((found != NULL) != present[key])
                mismatches++;
        }
    }
    int expected_count = 0;
    for (int key = 0; key < 200; key++)
        expected_count += present[key];
    CHECK_EQ_INT(mismatches, 0);
    CHECK_EQ_INT(hash_table_count(r), expected_count);
    hash_table_destroy(r);
}

int main(void)
{
    test_create_and_hash_function();
    test_basic_operations();
    test_forced_collisions();
    test_prime_bucket_count();
    test_large_and_against_oracle();
    return TEST_SUMMARY();
}
