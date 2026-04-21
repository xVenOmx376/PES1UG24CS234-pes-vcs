#include "index.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// forward declaration
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

// ─── PROVIDED ─────────────────────────────────────────────

IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    }
    return NULL;
}

int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            int remaining = index->count - i - 1;
            if (remaining > 0)
                memmove(&index->entries[i], &index->entries[i + 1],
                        remaining * sizeof(IndexEntry));
            index->count--;
            return index_save(index);
        }
    }
    fprintf(stderr, "error: '%s' is not in the index\n", path);
    return -1;
}

int index_status(const Index *index) {
    printf("Staged changes:\n");
    if (index->count == 0) printf("  (nothing to show)\n");

    for (int i = 0; i < index->count; i++) {
        printf("  staged: %s\n", index->entries[i].path);
    }

    return 0;
}

// ─── SORT HELPER ─────────────────────────────────────────

static int cmp_entries(const void *a, const void *b) {
    const IndexEntry *ea = (const IndexEntry *)a;
    const IndexEntry *eb = (const IndexEntry *)b;
    return strcmp(ea->path, eb->path);
}

// ─── LOAD INDEX (SAFE LINE PARSING) ──────────────────────

int index_load(Index *index) {
    FILE *fp = fopen(".pes/index", "r");

    index->count = 0;

    if (!fp) return 0;

    char line[1024];

    while (fgets(line, sizeof(line), fp)) {
        IndexEntry e;
        memset(&e, 0, sizeof(IndexEntry));

        char hex[HASH_HEX_SIZE + 1];

        int ret = sscanf(line, "%o %64s %lu %u %511[^\n]",
                         &e.mode,
                         hex,
                         &e.mtime_sec,
                         &e.size,
                         e.path);

        if (ret != 5) continue;

        if (hex_to_hash(hex, &e.hash) != 0)
            continue;

        if (index->count < MAX_INDEX_ENTRIES)
            index->entries[index->count++] = e;
    }

    fclose(fp);
    return 0;
}

// ─── SAVE INDEX (FIXED CRASH) ────────────────────────────

int index_save(const Index *index) {
    FILE *fp = fopen(".pes/index.tmp", "w");
    if (!fp) return -1;

    // 🔥 DO NOT copy entire struct
    // Instead sort a small array of pointers

    IndexEntry *ptrs[MAX_INDEX_ENTRIES];

    int count = index->count;

    if (count < 0 || count > MAX_INDEX_ENTRIES)
        count = 0;

    for (int i = 0; i < count; i++) {
        ptrs[i] = (IndexEntry *)&index->entries[i];
    }

    // comparator for pointers
    int cmp_ptrs(const void *a, const void *b) {
        IndexEntry *ea = *(IndexEntry **)a;
        IndexEntry *eb = *(IndexEntry **)b;
        return strcmp(ea->path, eb->path);
    }

    qsort(ptrs, count, sizeof(IndexEntry *), cmp_ptrs);

    char hex[HASH_HEX_SIZE + 1];

    for (int i = 0; i < count; i++) {
        hash_to_hex(&ptrs[i]->hash, hex);

        fprintf(fp, "%o %s %lu %u %s\n",
                ptrs[i]->mode,
                hex,
                ptrs[i]->mtime_sec,
                ptrs[i]->size,
                ptrs[i]->path);
    }

    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);

    rename(".pes/index.tmp", ".pes/index");
    return 0;
}

// ─── ADD FILE (CLEAN + CONSISTENT) ───────────────────────

int index_add(Index *index, const char *path) {

    struct stat st;

    if (stat(path, &st) != 0) {
        fprintf(stderr, "error: file not found\n");
        return -1;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    void *data = malloc(st.st_size ? st.st_size : 1);
    if (!data) {
        fclose(fp);
        return -1;
    }

    size_t read_bytes = fread(data, 1, st.st_size, fp);
    fclose(fp);

    if (read_bytes != (size_t)st.st_size) {
        free(data);
        return -1;
    }

    ObjectID id;

    if (object_write(OBJ_BLOB, data, st.st_size, &id) != 0) {
        free(data);
        return -1;
    }

    free(data);

    IndexEntry *e = index_find(index, path);

    if (!e) {
        if (index->count >= MAX_INDEX_ENTRIES)
            return -1;

        e = &index->entries[index->count++];
        strcpy(e->path, path);
    }

    e->hash = id;
    e->mode = st.st_mode;
    e->mtime_sec = st.st_mtime;
    e->size = st.st_size;

    return index_save(index);
}
