// commit.c — commit object creation and history traversal

#include "pes.h"
#include "commit.h"
#include "tree.h"
#include "index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_COMMIT_SIZE 4096


// ─────────────────────────────────────────
// Get current HEAD commit (if exists)
// ─────────────────────────────────────────
static int get_head_commit(ObjectID *id)
{
    FILE *f = fopen(".pes/refs/heads/main", "r");
    if (!f) return -1;

    char hex[HASH_HEX_SIZE + 1];

    if (!fgets(hex, sizeof(hex), f)) {
        fclose(f);
        return -1;
    }

    fclose(f);

    hex_to_hash(hex, id);
    return 0;
}


// ─────────────────────────────────────────
// Update HEAD to point to new commit
// ─────────────────────────────────────────
static int update_head(const ObjectID *id)
{
    FILE *f = fopen(".pes/refs/heads/main", "w");
    if (!f) return -1;

    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);

    fprintf(f, "%s\n", hex);
    fclose(f);

    return 0;
}


// ─────────────────────────────────────────
// Create a commit object
// ─────────────────────────────────────────
int commit_create(const char *message, ObjectID *commit_id)
{
    ObjectID tree_id;
    ObjectID parent_id;
    int has_parent = 0;

    /* Build tree from index */
    if (tree_from_index(&tree_id) != 0) {
        fprintf(stderr, "error: failed to build tree\n");
        return -1;
    }

    /* Check if HEAD commit exists */
    if (get_head_commit(&parent_id) == 0)
        has_parent = 1;

    char tree_hex[HASH_HEX_SIZE + 1];
    char parent_hex[HASH_HEX_SIZE + 1];

    hash_to_hex(&tree_id, tree_hex);

    if (has_parent)
        hash_to_hex(&parent_id, parent_hex);

    /* Timestamp */
    uint64_t timestamp = (uint64_t)time(NULL);

    /* Build commit content */
    char buffer[MAX_COMMIT_SIZE];

    if (has_parent) {
        snprintf(buffer, sizeof(buffer),
            "tree %s\n"
            "parent %s\n"
            "author PES User <pes@localhost>\n"
            "time %llu\n"
            "\n"
            "%s\n",
            tree_hex,
            parent_hex,
            (unsigned long long)timestamp,
            message);
    }
    else {
        snprintf(buffer, sizeof(buffer),
            "tree %s\n"
            "author PES User <pes@localhost>\n"
            "time %llu\n"
            "\n"
            "%s\n",
            tree_hex,
            (unsigned long long)timestamp,
            message);
    }

    /* Store commit object */
    if (object_write(OBJ_COMMIT, buffer, strlen(buffer), commit_id) != 0) {
        fprintf(stderr, "error: failed to write commit\n");
        return -1;
    }

    /* Update HEAD */
    if (update_head(commit_id) != 0) {
        fprintf(stderr, "error: failed to update HEAD\n");
        return -1;
    }

    return 0;
}


// ─────────────────────────────────────────
// Parse commit object
// ─────────────────────────────────────────
static int parse_commit(const char *data, Commit *commit)
{
    memset(commit, 0, sizeof(Commit));

    const char *ptr = data;

    while (*ptr) {

        if (strncmp(ptr, "tree ", 5) == 0) {
            ptr += 5;
            hex_to_hash(ptr, &commit->tree);
        }

        else if (strncmp(ptr, "parent ", 7) == 0) {
            ptr += 7;
            hex_to_hash(ptr, &commit->parent);
            commit->has_parent = 1;
        }

        else if (strncmp(ptr, "author ", 7) == 0) {
            ptr += 7;
            sscanf(ptr, "%255[^\n]", commit->author);
        }

        else if (strncmp(ptr, "time ", 5) == 0) {
            ptr += 5;
            commit->timestamp = strtoull(ptr, NULL, 10);
        }

        else if (*ptr == '\n') {
            ptr++;
            strcpy(commit->message, ptr);
            break;
        }

        while (*ptr && *ptr != '\n')
            ptr++;

        if (*ptr == '\n')
            ptr++;
    }

    return 0;
}


// ─────────────────────────────────────────
// Walk commit history
// ─────────────────────────────────────────
int commit_walk(commit_walk_fn cb, void *ctx)
{
    ObjectID current;

    if (get_head_commit(&current) != 0)
        return -1;

    while (1) {

        void *data;
        size_t size;
        ObjectType type;

        if (object_read(&current, &type, &data, &size) != 0)
            return -1;

        Commit commit;
        parse_commit(data, &commit);

        cb(&current, &commit, ctx);

        free(data);

        if (!commit.has_parent)
            break;

        current = commit.parent;
    }

    return 0;
}
