// relational/variable_mini_map.c — the scoped name => pointer mini map.
//
// One bucket level: first folded char -> a lazily-minted ChunkedList of
// VariableSlot rows. Cold path only.

#include "relational/variable_mini_map.h"

#include <stdio.h>
#include <string.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "nio/mem.h"
#include "oop/type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: VariableMiniMap
 * ============================================================================
 * The scoped sibling of VariableHashMap: the same first-character bucketing with
 * a single level, for names that belong to one owner (a class's fields or
 * methods). 39 buckets, each a lazily-minted ChunkedList of VariableSlot rows
 * with 16 rows per leaf — so an empty mini map is 39 pointers and a used one
 * costs one small leaf per active first character. Names fold and validate
 * through the atom, dotted field paths included.
 *
 * Lifetime: an arena object (or embedded); shutdown frees every bucket list. No
 * removal verb (a symbol is rebound, like the big map).
 *
 * Path temperature: cold. Read by the reflective scoped search, never on a frame
 * (the Cold-Only Reflection Law). Getters are null-safe.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: VariableMiniMap (relational/variable_mini_map.c)
 * ============================================================================
 * the scoped name => pointer mini map (39 single-level buckets).
 *
 * STRUCT FIELDS (Mirroring relational/variable_mini_map.h):
 * ----------------------------------------------------------------------------
 *   VariableMiniMap {
 *     bool active;      // runtime-active flag
 *     uint32_t count;   // live entries
 *     ChunkedList *buckets[39]; // first-char -> lazily minted slot list
 *   }
 *
 * PRIVATE HELPERS (kept file-local, pure logic, no behavior of their own): none.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - VariableMiniMap_0()                    : arena-allocated empty map
 *   - VariableMiniMap_init(map)              : embedded init
 *   - VariableMiniMap_free(map)              : shutdown + release an arena map
 *   - VariableMiniMap_shutdown(map)          : free every bucket list
 *
 * Private Core Functions: (.c static)
 *   - ensureBucket(map, bucket)              : mint a bucket list lazily
 *   - findRow(list, name)                    : name compare inside a bucket
 *
 * Public Core Functions: (.h)
 *   - VariableMiniMap_add(map, name, pointer)
 *   - VariableMiniMap_get(map, name): resolve to a returned pointer value
 *   - VariableMiniMap_contains(map, name)
 *   - VariableMiniMap_forEach(map, fn, userdata)
 *
 * Public Getters: (.h)
 *   - VariableMiniMap_count(map)
 *   - VariableMiniMap_isEmpty(map)
 *
 * Public String Projections: (.h)
 *   - VariableMiniMap_toString(map, dest, cap, outTruncated)
 *   - VariableMiniMap_toStringStruct(map, dest, cap, outTruncated)
 * ============================================================================
 */

// Find a row by folded name inside one bucket list. Null on a hole or a miss.
static VariableSlot *findRow(ChunkedList *list, const char *name) {
    uint32_t rows = ChunkedList_size(list);
    for (uint32_t i = 0; i < rows; i++) {
        VariableSlot *row = (VariableSlot*) ChunkedList_slot(list, i);
        if (row && VariableSlot_nameEquals(row, name))
            return row;
    }
    return nullptr;
}

// Mint a bucket's list on first use: one root slot, 16-row leaves.
static ChunkedList *ensureBucket(VariableMiniMap *map, uint32_t bucket) {
    ChunkedList *list = (*map).buckets[bucket];
    if (list)
        return list;
    list = ChunkedList(ID_VARIABLE_SLOT, VARIABLE_SLOT_SIZE, VARIABLE_MINI_ROOT_INIT, VARIABLE_MINI_LEAF_ROWS);
    if (!list)
        return nullptr;
    (*map).buckets[bucket] = list;
    return list;
}

// CONSTRUCTORS

bool VariableMiniMap_init(VariableMiniMap *map) {
    if (!map)
        return false;
    memset(map, 0, sizeof(*map));
    (*map).active = true;
    return true;
}

// Release all allocated bucket lists and mark the mini map inactive.
void VariableMiniMap_shutdown(VariableMiniMap *map) {
    if (!map)
        return;
    for (uint32_t b = 0u; b < VARIABLE_MINI_BUCKETS; b++) {
        ChunkedList *list = (*map).buckets[b];
        if (list)
            ChunkedList_free(list);
        (*map).buckets[b] = nullptr;
    }
    (*map).count = 0u;
    (*map).active = false;
}

VariableMiniMap *VariableMiniMap_0(void) {
    VariableMiniMap *map = (VariableMiniMap*) Memory_alloc(TYPE_VARIABLE_MINI_MAP, sizeof(VariableMiniMap));
    if (!map)
        return nullptr;
    if (!VariableMiniMap_init(map)) {
        Memory_free(map);
        return nullptr;
    }
    return map;
}

// Shut down and release an arena-allocated mini map.
void VariableMiniMap_free(VariableMiniMap *map) {
    if (!map)
        return;
    VariableMiniMap_shutdown(map);
    Memory_free(map);
}

// CORE FUNCTIONS

bool VariableMiniMap_add(VariableMiniMap *map, const char *name, uintptr_t pointer) {
    if (!map || !(*map).active)
        return false;
    char folded[VARIABLE_SLOT_NAME_BYTES];
    if (!VariableSlot_foldName(name, folded))
        return false;
    int bucket = VariableSlot_bucketOf(folded[0]);
    if (bucket < 0)
        return false;
    ChunkedList *list = ensureBucket(map, (uint32_t) bucket);
    if (!list)
        return false;

    VariableSlot *row = findRow(list, name);
    if (row) {
        VariableSlot_setPointer(row, pointer);
        return true;
    }
    VariableSlot *fresh = (VariableSlot*) ChunkedList_addSlot(list);
    if (!fresh)
        return false;
    if (!VariableSlot_init(fresh, name, pointer))
        return false;
    (*map).count++;
    return true;
}

// Find name and return its pointer value, or zero on miss.
uintptr_t VariableMiniMap_get(const VariableMiniMap *map, const char *name) {
    if (!map || !(*map).active)
        return 0u;
    char folded[VARIABLE_SLOT_NAME_BYTES];
    if (!VariableSlot_foldName(name, folded))
        return 0u;
    int bucket = VariableSlot_bucketOf(folded[0]);
    if (bucket < 0)
        return 0u;
    ChunkedList *list = (*map).buckets[bucket];
    if (!list)
        return 0u;
    VariableSlot *row = findRow(list, name);
    if (!row)
        return 0u;
    return VariableSlot_getPointer(row);
}

// Report whether name resolves to an entry in the mini map.
bool VariableMiniMap_contains(const VariableMiniMap *map, const char *name) {
    if (!map || !(*map).active)
        return false;
    char folded[VARIABLE_SLOT_NAME_BYTES];
    if (!VariableSlot_foldName(name, folded))
        return false;
    int bucket = VariableSlot_bucketOf(folded[0]);
    if (bucket < 0)
        return false;
    ChunkedList *list = (*map).buckets[bucket];
    return list && findRow(list, name) != nullptr;
}

// Visit each stored slot with the caller's context.
void VariableMiniMap_forEach(VariableMiniMap *map, VariableMiniMapVisitFn fn, void *userdata) {
    if (!map || !(*map).active || !fn)
        return;
    for (uint32_t b = 0u; b < VARIABLE_MINI_BUCKETS; b++) {
        ChunkedList *list = (*map).buckets[b];
        if (!list)
            continue;
        uint32_t rows = ChunkedList_size(list);
        for (uint32_t i = 0u; i < rows; i++) {
            VariableSlot *row = (VariableSlot*) ChunkedList_slot(list, i);
            if (row)
                fn(row, userdata);
        }
    }
}

// GETTERS

uint32_t VariableMiniMap_count(const VariableMiniMap *map) {
    return map ? (*map).count : 0u;
}

// Report whether the mini map has no entries; a null map is treated as empty.
bool VariableMiniMap_isEmpty(const VariableMiniMap *map) {
    if (!map)
        return true;
    return (*map).count == 0u;
}

// STRING PROJECTIONS (the toString Law)

void VariableMiniMap_toString(const VariableMiniMap *map, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!map) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    int written = snprintf(dest, cap, "VariableMiniMap(count=%u)", (*map).count);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

// Format the mini map's own fields into a bounded structure projection.
void VariableMiniMap_toStringStruct(const VariableMiniMap *map, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!map) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    int written = snprintf(dest, cap, "VariableMiniMap { active=%s, count=%u, buckets=<array[%u]> }",
                           (*map).active ? "true" : "false", (*map).count, VARIABLE_MINI_BUCKETS);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}
