/*
 * hash_table.h — integer key -> pointer map using SEPARATE CHAINING.
 *
 *   buckets[0] -> NULL
 *   buckets[1] -> [key 1012 | value] -> [key 1001 | value] -> NULL   (collision chain)
 *   buckets[2] -> [key 1002 | value] -> NULL
 *   ...
 *
 * Hash function (division method):  h(key) = (unsigned)key mod num_buckets.
 * Use a PRIME number of buckets so that keys sharing a common factor
 * (e.g. all multiples of 10) still spread across every bucket.
 *
 * Values are BORROWED pointers (to Incident or Resource records owned by
 * their stores). The table never frees a value; destroying the table frees
 * only its own nodes and bucket array.
 *
 * The bucket count is fixed at creation (blueprint decision D10). Average
 * search cost is O(1 + load factor); rehashing is a possible extension.
 *
 * Depends on: common.h only.
 */
#ifndef DC_HASH_TABLE_H
#define DC_HASH_TABLE_H

#include <stdio.h>
#include "common.h"

typedef struct HashNode {
    int key;                  /* incident or resource ID           */
    void *value;              /* borrowed pointer to the record    */
    struct HashNode *next;    /* next node in the same bucket      */
} HashNode;

typedef struct {
    HashNode **buckets;       /* array of num_buckets chain heads  */
    int num_buckets;
    int count;                /* number of stored keys             */
} HashTable;

/* Shape of the table, for the debug view and tests. */
typedef struct {
    int    count;
    int    num_buckets;
    int    used_buckets;      /* buckets with at least one node    */
    int    longest_chain;
    double load_factor;       /* count / num_buckets               */
} HashStats;

/* Creates an empty table with num_buckets chains (1 .. DC_MAX_BUCKETS).
 * Returns NULL on invalid size or out of memory. */
#define DC_MAX_BUCKETS 1000003
HashTable *hash_table_create(int num_buckets);

/* Frees all nodes and the bucket array — NOT the values. Safe on NULL. */
void hash_table_destroy(HashTable *t);

/* Bucket index for key: 0 .. num_buckets-1. Defined for negative keys too. */
int hash_index(int key, int num_buckets);

/* Adds key -> value. Errors: DC_ERR_INVALID (NULL table or value),
 * DC_ERR_DUPLICATE (key already present; table unchanged), DC_ERR_NOMEM. */
DcStatus hash_table_insert(HashTable *t, int key, void *value);

/* Value stored for key, or NULL if absent. */
void *hash_table_find(const HashTable *t, int key);

/* Removes key (the value itself is not freed). DC_ERR_NOT_FOUND if absent. */
DcStatus hash_table_remove(HashTable *t, int key);

int hash_table_count(const HashTable *t);

/* Fills *out with the table's shape. O(num_buckets + count). */
void hash_table_stats(const HashTable *t, HashStats *out);

/* Debug view: one line per non-empty bucket, keys in chain order, followed
 * by the statistics. */
void hash_table_print(const HashTable *t, FILE *out);

#endif /* DC_HASH_TABLE_H */
