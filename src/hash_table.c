/*
 * hash_table.c — separate-chaining hash table (see hash_table.h).
 */
#include "hash_table.h"

#include <stdlib.h>

HashTable *hash_table_create(int num_buckets)
{
    if (num_buckets < 1 || num_buckets > DC_MAX_BUCKETS)
        return NULL;

    HashTable *t = malloc(sizeof *t);
    if (t == NULL)
        return NULL;

    /* calloc: every chain head starts as NULL (empty bucket). */
    t->buckets = calloc((size_t)num_buckets, sizeof *t->buckets);
    if (t->buckets == NULL) {
        free(t);
        return NULL;
    }
    t->num_buckets = num_buckets;
    t->count = 0;
    return t;
}

void hash_table_destroy(HashTable *t)
{
    if (t == NULL)
        return;
    for (int i = 0; i < t->num_buckets; i++) {
        HashNode *node = t->buckets[i];
        while (node != NULL) {
            HashNode *next = node->next;   /* save before free */
            free(node);                    /* node only: the value is borrowed */
            node = next;
        }
    }
    free(t->buckets);
    free(t);
}

int hash_index(int key, int num_buckets)
{
    /* Converting int -> unsigned is well defined even for negative keys
     * (it wraps modulo 2^32), and unsigned % never yields a negative result.
     * With plain int, -7 % 5 == -2, which would be an invalid array index. */
    return (int)((unsigned)key % (unsigned)num_buckets);
}

/* Returns the node holding key in its chain, or NULL. */
static HashNode *find_node(const HashTable *t, int key)
{
    HashNode *node = t->buckets[hash_index(key, t->num_buckets)];
    while (node != NULL && node->key != key)
        node = node->next;
    return node;
}

DcStatus hash_table_insert(HashTable *t, int key, void *value)
{
    if (t == NULL || value == NULL)
        return DC_ERR_INVALID;   /* NULL is reserved to mean "not found" */
    if (find_node(t, key) != NULL)
        return DC_ERR_DUPLICATE;

    HashNode *node = malloc(sizeof *node);
    if (node == NULL)
        return DC_ERR_NOMEM;

    int i = hash_index(key, t->num_buckets);
    node->key = key;
    node->value = value;
    node->next = t->buckets[i];   /* insert at the head of the chain: O(1) */
    t->buckets[i] = node;
    t->count++;
    return DC_OK;
}

void *hash_table_find(const HashTable *t, int key)
{
    if (t == NULL)
        return NULL;
    HashNode *node = find_node(t, key);
    return node != NULL ? node->value : NULL;
}

DcStatus hash_table_remove(HashTable *t, int key)
{
    if (t == NULL)
        return DC_ERR_INVALID;

    /* `link` points at the POINTER that points to the current node: first
     * the bucket head, then some node's `next` field. Overwriting *link
     * unlinks the node, and the same code works for the head, the middle
     * and the tail of the chain — no special cases. */
    HashNode **link = &t->buckets[hash_index(key, t->num_buckets)];
    while (*link != NULL) {
        HashNode *node = *link;
        if (node->key == key) {
            *link = node->next;   /* bypass the node */
            free(node);
            t->count--;
            return DC_OK;
        }
        link = &node->next;
    }
    return DC_ERR_NOT_FOUND;
}

int hash_table_count(const HashTable *t)
{
    return t == NULL ? 0 : t->count;
}

void hash_table_stats(const HashTable *t, HashStats *out)
{
    if (out == NULL)
        return;
    out->count = 0;
    out->num_buckets = 0;
    out->used_buckets = 0;
    out->longest_chain = 0;
    out->load_factor = 0.0;
    if (t == NULL)
        return;

    out->count = t->count;
    out->num_buckets = t->num_buckets;
    out->load_factor = (double)t->count / (double)t->num_buckets;
    for (int i = 0; i < t->num_buckets; i++) {
        int length = 0;
        for (const HashNode *node = t->buckets[i]; node != NULL; node = node->next)
            length++;
        if (length > 0)
            out->used_buckets++;
        if (length > out->longest_chain)
            out->longest_chain = length;
    }
}

void hash_table_print(const HashTable *t, FILE *out)
{
    if (out == NULL)
        return;
    if (t == NULL) {
        fprintf(out, "(no table)\n");
        return;
    }
    for (int i = 0; i < t->num_buckets; i++) {
        if (t->buckets[i] == NULL)
            continue;                         /* empty buckets are skipped */
        fprintf(out, "  bucket %3d:", i);
        for (const HashNode *node = t->buckets[i]; node != NULL; node = node->next)
            fprintf(out, " -> %d", node->key);
        fprintf(out, "\n");
    }
    HashStats s;
    hash_table_stats(t, &s);
    fprintf(out, "  %d keys in %d buckets (%d used), load factor %.2f, longest chain %d\n",
            s.count, s.num_buckets, s.used_buckets, s.load_factor, s.longest_chain);
}
