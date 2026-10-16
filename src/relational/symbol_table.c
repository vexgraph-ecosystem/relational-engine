// relational/variable.c — SymbolTable registry port (Legacy: variable/SymbolTable.java).
//
// Rows reference shared pool slots by index (never inline names); lookup
// goes pool-first, then one sparse hop (slot -> var id). Typed queries
// filter rows by classId — one structure, no segregated lists.

#include "relational/symbol_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "nio/mem.h"
#include "relational/variable_pool.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: SymbolTable
 * ============================================================================
 * The relational symbol registry: dense SymbolRow records (varId == index,
 * append-only) reference shared StringPool slots by index — never inline
 * names — and a sparse bySlot hop maps pool slot back to varId. Names are
 * validated cold (ASCII alnum/underscore/dot, folded lowercase, max
 * STRING_POOL_NAME_MAX) before interning; typed queries filter rows by
 * classId over one structure with no segregated lists. Rows and the bySlot
 * table grow on demand via malloc/realloc; shutdown frees both and marks the
 * registry inactive.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: SymbolTable (relational/variable.c)
 * ============================================================================
 * the relational symbol registry (Legacy: variable/SymbolTable.java).
 *
 * STRUCT FIELDS (Mirroring relational/symbol_table.h):
 * ----------------------------------------------------------------------------
 *   SymbolTable {
 *     bool active; // runtime-active flag
 *     SymbolRow *rows; // dense rows, varId == index (append-only)
 *     uint32_t count; // live rows
 *     uint32_t capacity; // allocated rows
 *     int32_t *bySlot; // pool slot -> varId, -1 empty (sparse, grown)
 *     uint32_t bySlotCap; // bySlot slots allocated
 *   }
 *
 * SLOT RECORD (owned by SymbolTable, behaviorless):
 * ----------------------------------------------------------------------------
 *   SymbolRow slot; // int32_t + pool slot index (name lives in pool)
 *   SymbolRow classId; // uint32_t + value class, pinned at creation
 *   SymbolRow pointer; // uintptr_t + the value
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Constructors:
 *   - SymbolTable_init(v)
 *   - SymbolTable_shutdown(v)
 *
 * Core Functions:
 *   - SymbolTable_instant(v, name, classId, targetPointer)
 *   - SymbolTable_rename(v, oldName, newName)
 *   - SymbolTable_findByClass(v, classId, outIds, cap)
 *   - SymbolTable_compareAndSetPointer(v, varId, expected, newPointer)
 *
 * Setters:
 *   - SymbolTable_setPointer(v, varId, targetPointer)
 *
 * Getters:
 *   - SymbolTable_getId(v, name)
 *   - SymbolTable_getPointer(v, varId)
 *   - SymbolTable_getClassId(v, varId)
 *   - SymbolTable_getName(v, varId)
 *   - SymbolTable_getActiveCount(v)
 * ============================================================================
 */

// Validate + fold to lowercase for pool space. Dots ride along for dotted
// paths (character.position.x); everything outside ASCII alnum/underscore/
// dot — including multibyte UTF-8/16 garbage — is rejected cold.
static bool clean_name(const char *name, char *lowered) {
    if (!name || name[0] == '\0')
        return false;
    size_t len = strlen(name);
    if (len > STRING_POOL_NAME_MAX)
        return false;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char) name[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '.';
        if (!ok)
            return false;
        lowered[i] = (c >= 'A' && c <= 'Z') ? (char) (c + 32) : (char) c;
    }
    lowered[len] = '\0';
    return true;
}

static SymbolRow *row_at(SymbolTable *v, int32_t varId) {
    if (!v || !(*v).active || !(*v).rows || varId < 0 || (uint32_t) varId >= (*v).count)
        return nullptr;
    SymbolRow *rows = (*v).rows;
    return &rows[varId];
}

// Pool slot -> var id through the sparse hop. -1 when unmapped (a pool slot
// may exist globally without belonging to this scope).
static int32_t slot_var(SymbolTable *v, int32_t slot) {
    if (slot < 0 || (uint32_t) slot >= (*v).bySlotCap)
        return -1;
    int32_t *bySlot = (*v).bySlot;
    return bySlot[slot];
}

// Grow the pool-slot-to-variable index table to cover the requested slot.
static bool ensure_byslot(SymbolTable *v, uint32_t need) {
    if (need < (*v).bySlotCap)
        return true;
    uint32_t cap = (*v).bySlotCap ? (*v).bySlotCap * 2 : 64;
    while (cap <= need) {
        if (cap > UINT32_MAX / 2)
            return false;
        cap *= 2;
    }
    int32_t *next = (int32_t*) malloc((size_t) cap * sizeof(int32_t));
    if (!next)
        return false;
    for (uint32_t i = 0; i < cap; i++)
        next[i] = -1;
    if ((*v).bySlot) {
        memcpy(next, (*v).bySlot, (size_t)(*v).bySlotCap * sizeof(int32_t));
        free((*v).bySlot);
    }
    (*v).bySlot = next;
    (*v).bySlotCap = cap;
    return true;
}

// Initialize a registry and its shared string pool using the default arena.
bool SymbolTable_init(SymbolTable *v) {
    if (!v)
        return false;
    memset(v, 0, sizeof(*v));
    MemoryArena *arena = Memory_defaultArena();
    if (!arena)
        return false;
    if (!StringPool_init(arena))
        return false;
    (*v).rows = (SymbolRow*) malloc(SYMBOL_TABLE_DEFAULT_CAPACITY * sizeof(SymbolRow));
    if (!(*v).rows)
        return false;
    (*v).capacity = SYMBOL_TABLE_DEFAULT_CAPACITY;
    (*v).active = true;
    return true;
}

// Release registry-owned arrays and mark the registry inactive.
void SymbolTable_shutdown(SymbolTable *v) {
    if (!v || !(*v).active)
        return;
    free((*v).rows);
    free((*v).bySlot);
    (*v).rows = nullptr;
    (*v).bySlot = nullptr;
    (*v).count = 0;
    (*v).capacity = 0;
    (*v).bySlotCap = 0;
    (*v).active = false;
}

// Intern and register a validated name, returning its append-only row id.
int32_t SymbolTable_instant(SymbolTable *v, const char *name, uint32_t classId, uintptr_t targetPointer) {
    if (!v || !(*v).active) {
        fprintf(stderr, "[variable] instant: inactive registry\n");
        return -1;
    }
    if (!name || name[0] == '\0') {
        fprintf(stderr, "[variable] instant: empty name rejected\n");
        return -1;
    }
    char lowered[24];
    size_t rawLen = strlen(name);
    if (rawLen > STRING_POOL_NAME_MAX) {
        fprintf(stderr, "[variable] instant: name too long ('%.32s', max 23)\n", name);
        return -1;
    }
    if (!clean_name(name, lowered)) {
        fprintf(stderr, "[variable] instant: illegal name ('%.32s'): ascii alnum, underscore, dot only\n", name);
        return -1;
    }
    int32_t slot = StringPool_find(lowered);
    if (slot >= 0 && slot_var(v, slot) >= 0) {
        fprintf(stderr, "[variable] instant: name already registered ('%s')\n", lowered);
        return -1;
    }
    if (!ensure_byslot(v, StringPool_count())) {
        fprintf(stderr, "[variable] instant: out of memory ('%s')\n", lowered);
        return -1;
    }
    slot = StringPool_intern(lowered);
    if (slot < 0) {
        fprintf(stderr, "[variable] instant: intern failed ('%s')\n", lowered);
        return -1;
    }
    if ((*v).count >= (*v).capacity) {
        size_t newCap = (size_t)(*v).capacity + SYMBOL_TABLE_DEFAULT_CAPACITY;
        SymbolRow *next = (SymbolRow*) realloc((*v).rows, newCap * sizeof(SymbolRow));
        if (!next) {
            fprintf(stderr, "[variable] instant: out of memory ('%s')\n", lowered);
            return -1;
        }
        (*v).rows = next;
        (*v).capacity = (uint32_t) newCap;
    }
    uint32_t id = (*v).count;
    SymbolRow *rows = (*v).rows;
    rows[id].slot = slot;
    rows[id].classId = classId;
    rows[id].pointer = targetPointer;
    int32_t *bySlot = (*v).bySlot;
    bySlot[slot] = (int32_t) id;
    (*v).count++;
    return (int32_t) id;
}

// Resolve a normalized name to its registered row id, or -1 when absent.
int32_t SymbolTable_getId(SymbolTable *v, const char *name) {
    if (!v || !(*v).active || !name)
        return -1;
    char lowered[24];
    if (!clean_name(name, lowered))
        return -1;
    int32_t slot = StringPool_find(lowered);
    if (slot < 0)
        return -1;
    return slot_var(v, slot);
}

// Rename a registered row after validating and interning its replacement name.
bool SymbolTable_rename(SymbolTable *v, const char *oldName, const char *newName) {
    if (!v || !(*v).active) {
        fprintf(stderr, "[variable] rename: inactive registry\n");
        return false;
    }
    char oldLower[24];
    char newLower[24];
    if (!oldName || !clean_name(oldName, oldLower)) {
        fprintf(stderr, "[variable] rename: bad old name\n");
        return false;
    }
    if (!newName || !clean_name(newName, newLower)) {
        fprintf(stderr, "[variable] rename: bad new name\n");
        return false;
    }
    int32_t oldSlot = StringPool_find(oldLower);
    int32_t id = oldSlot >= 0 ? slot_var(v, oldSlot) : -1;
    if (id < 0) {
        fprintf(stderr, "[variable] rename: unknown name ('%s')\n", oldLower);
        return false;
    }
    int32_t newSlot = StringPool_find(newLower);
    if (newSlot >= 0 && slot_var(v, newSlot) >= 0) {
        fprintf(stderr, "[variable] rename: name already registered ('%s')\n", newLower);
        return false;
    }
    if (!ensure_byslot(v, StringPool_count())) {
        fprintf(stderr, "[variable] rename: out of memory ('%s')\n", newLower);
        return false;
    }
    newSlot = StringPool_intern(newLower);
    if (newSlot < 0) {
        fprintf(stderr, "[variable] rename: intern failed ('%s')\n", newLower);
        return false;
    }
    SymbolRow *rows = (*v).rows;
    int32_t *bySlot = (*v).bySlot;
    int32_t oldSlotIdx = rows[id].slot;
    rows[id].slot = newSlot;
    if (oldSlotIdx >= 0 && (uint32_t) oldSlotIdx < (*v).bySlotCap && bySlot[oldSlotIdx] == id)
        bySlot[oldSlotIdx] = -1;
    bySlot[newSlot] = id;
    return true;
}

// Count matching class rows and write up to cap row ids when output is supplied.
size_t SymbolTable_findByClass(SymbolTable *v, uint32_t classId, int32_t *outIds, size_t cap) {
    if (!v || !(*v).active)
        return 0;
    size_t total = 0;
    SymbolRow *rows = (*v).rows;
    for (uint32_t i = 0; i < (*v).count; i++) {
        if (rows[i].classId == classId) {
            if (outIds && total < cap)
                outIds[total] = (int32_t) i;
            total++;
        }
    }
    return total;
}

// Read the stored target pointer, returning zero for an invalid row id.
uintptr_t SymbolTable_getPointer(SymbolTable *v, int32_t varId) {
    SymbolRow *row = row_at(v, varId);
    return row ? (*row).pointer : 0;
}

// Replace a row's target pointer; invalid row ids leave the registry unchanged.
void SymbolTable_setPointer(SymbolTable *v, int32_t varId, uintptr_t targetPointer) {
    SymbolRow *row = row_at(v, varId);
    if (!row)
        return;
    (*row).pointer = targetPointer;
}

// Replace the target pointer only when the row still contains expected.
bool SymbolTable_compareAndSetPointer(SymbolTable *v, int32_t varId, uintptr_t expected, uintptr_t newPointer) {
    SymbolRow *row = row_at(v, varId);
    if (!row)
        return false;
    if ((*row).pointer != expected)
        return false;
    (*row).pointer = newPointer;
    return true;
}

// Read a row's class id, returning zero for an invalid row id.
uint32_t SymbolTable_getClassId(SymbolTable *v, int32_t varId) {
    SymbolRow *row = row_at(v, varId);
    return row ? (*row).classId : 0;
}

// Borrow the registered name from the shared string pool.
const char *SymbolTable_getName(const SymbolTable *v, int32_t varId) {
    const SymbolRow *row = row_at((const SymbolTable*) v, varId);
    if (!row)
        return nullptr;
    const char *name = StringPool_name((uint32_t) (*row).slot);
    return name;
}

// Return the number of registered rows, or zero for an inactive registry.
size_t SymbolTable_getActiveCount(SymbolTable *v) {
    if (!v || !(*v).active)
        return 0;
    return (*v).count;
}
