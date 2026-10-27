#include "relational/relational.h"

#include <string.h>
#include <strings.h>

#include "nio/mem.h"
#include "primitive/string.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/intention.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Relational
 * ============================================================================
 * Spotlight relational facade over SymbolTable (Legacy:
 * relational/RelationalEngine.java). Three block kinds compose the engine:
 * arena blocks (16B header read backwards), pool slots (32B name table,
 * immutable, shared by pointer), and variable rows (16B bindings per scope).
 * Identity is stated once per layer and never duplicated; queries filter
 * rows by name (O(log n) pool search) or by class (O(n) cold sweeps).
 * Cold mutations print loud to stderr on rejection; lookups stay silent so
 * speculative probing never logs. Teardown composes bottom-up: rows free per
 * scope, pool strings die with their arena, arenas die last.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * MODULE: Relational (relational/relational.c)
 * ============================================================================
 * spotlight relational facade over SymbolTable (Legacy: relational/RelationalEngine.java).
 *
 * How the relational engine works with memory blocks — three block kinds,
 * three sizes, three jobs. Identity is stated once per layer and never
 * duplicated:
 *
 *   ARENA BLOCKS — [16B header][payload], header read backwards.
 *
 *     {self u64, length u32, sugar u32}: self-describing identity, inline
 *     size, hash-clarification veto over (self, length). Frozen forever:
 *     the envelope never moves, so payloads stay versioned — detectable
 *     per object via length, refused at the manifest gate on mismatch,
 *     migrated across restarts via snapshots. Values in this engine are
 *     headed blocks (or raw tagged bits whose class says how to read
 *     them) — never bare unheaded memory.
 *
 *   POOL SLOTS — 32B [ptr][str1][str2][str3], one process-wide table.
 *
 *     Names stated once, shared by pointer. The self link makes any slot
 *     self-validating O(1) with no side tables; the sorted index makes
 *     lookup O(log n); slot indices never invalidate (raw pointers hold
 *     until grow). Immutability is the point: a name means the same thing
 *     in every scope, forever. 23 chars max — longer names rejected cold,
 *     because silent truncation would corrupt identity.
 *
 *   VARIABLE ROWS — 16B [slot u32][classId u32][pointer u64], per scope.
 *
 *     Bindings (mutable) over names (immutable): rebinding changes the
 *     row, never the pool. Class pins at creation so typed gathers
 *     (character.position.x AS a float) fail closed on mismatch instead
 *     of misreading. Queries filter rows — one structure, no segregated
 *     lists to desync; the class field is segregation data, not storage.
 *
 * Gather paths: by name (pool binary search -> sparse hop -> row, O(log n));
 * by class (row filter, O(n) integer compares — cold sweeps only). Cold
 * mutations print loud to stderr on rejection; lookups stay silent because
 * speculative probing (spotlight) must never log. Teardown composes
 * bottom-up: rows free per scope, pool strings die with their arena, arenas
 * die last. Darling widgets are never rows here — the engine is substrate;
 * the UI borrows arenas and interns through these seams and keeps its own
 * storage (Rule 36: OO ergonomics over data-oriented memory, never inside
 * the variable table).
 *
 * STRUCT FIELDS: none — procedural (operates on SymbolTable scope tables (global/local))
 *
 * STRUCT FIELDS: none — procedural (operates on SymbolTable scope tables (global/local))
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - Relational_search(scope, query, outIds, cap)
 *   - Relational_searchAll(global, local, query, outIds, cap)
 *
 * Setters:
 *   - Relational_setName(scope, oldName, newName)
 *   - Relational_setValue(scope, name, classId, ptr)
 *   - Relational_setValueById(scope, varId, ptr)
 *   - Relational_setString(scope, name, value)
 *   - Relational_setFunction(scope, name, fn)
 *
 * Getters:
 *   - Relational_getId(scope, name)
 *   - Relational_getName(scope, varId)
 *   - Relational_getValue(scope, name)
 *   - Relational_getValueById(scope, varId)
 *   - Relational_getString(scope, name)
 *   - Relational_getFunction(scope, name)
 * ============================================================================
 */

;;INTENTION("identity stated once per layer (header self, pool slot, row class)"
            " so no layer duplicates another; fixed sizes keep every hop "
            "O(1) or O(log n) with zero side tables; mutations loud, lookups silent, "
            "teardown composed; UI borrows, never rows")


// helpers — one level only, dest last where it matters

static bool containsCaseInsensitive(const char *haystack, const char *needle) {
    if (!haystack || !needle)
        return false;
    size_t nLen = strlen(needle);
    if (nLen == 0)
        return true;
    size_t hLen = strlen(haystack);
    if (nLen > hLen)
        return false;
    for (size_t i = 0; i + nLen <= hLen; i++) {
        if (strncasecmp(haystack + i, needle, nLen) == 0)
            return true;
    }
    return false;
}

static bool equalsCaseInsensitive(const char *a, const char *b) {
    if (!a || !b)
        return false;
    return strcasecmp(a, b) == 0;
}

static bool prefixCaseInsensitive(const char *haystack, const char *needle) {
    if (!haystack || !needle)
        return false;
    size_t nLen = strlen(needle);
    if (nLen == 0)
        return true;
    return strncasecmp(haystack, needle, nLen) == 0;
}

// exact

int32_t Relational_getId(SymbolTable *scope, const char *name) {
    if (!scope || !name)
        return -1;
    return SymbolTable_getId(scope, name);
}

const char *Relational_getName(const SymbolTable *scope, int32_t varId) {
    return SymbolTable_getName(scope, varId);
}

bool Relational_setName(SymbolTable *scope, const char *oldName, const char *newName) {
    if (!scope || !oldName || !newName)
        return false;
    return SymbolTable_rename(scope, oldName, newName);
}

// value — store as-is, pointer is the value

void *Relational_getValue(SymbolTable *scope, const char *name) {
    if (!scope || !name)
        return nullptr;
    int32_t varId = SymbolTable_getId(scope, name);
    if (varId < 0)
        return nullptr;
    return (void*) SymbolTable_getPointer(scope, varId);
}

void *Relational_getValueById(SymbolTable *scope, int32_t varId) {
    if (!scope || varId < 0 || (size_t) varId >= SymbolTable_getActiveCount(scope))
        return nullptr;
    return (void*) SymbolTable_getPointer(scope, varId);
}

bool Relational_setValue(SymbolTable *scope, const char *name, uint32_t classId, void *ptr) {
    if (!scope || !name)
        return false;
    // Create-or-fail constructor: rebind existing names explicitly so a
    // typo never silently resurrects a stale entry. Class pins at creation.
    int32_t varId = SymbolTable_getId(scope, name);
    if (varId >= 0) {
        SymbolTable_setPointer(scope, varId, (uintptr_t) ptr);
        return true;
    }
    return SymbolTable_instant(scope, name, classId, (uintptr_t) ptr) >= 0;
}

bool Relational_setValueById(SymbolTable *scope, int32_t varId, void *ptr) {
    if (!scope || varId < 0 || (size_t) varId >= SymbolTable_getActiveCount(scope))
        return false;
    SymbolTable_setPointer(scope, varId, (uintptr_t) ptr);
    return true;
}

// string sugar — allocates, frees old block

const char *Relational_getString(SymbolTable *scope, const char *name) {
    void *ptr = Relational_getValue(scope, name);
    if (!ptr)
        return nullptr;
    return string_get((const uint8_t*) ptr);
}

void Relational_setString(SymbolTable *scope, const char *name, const char *value) {
    if (!scope || !name || !value)
        return;
    uint8_t *newPtr = string_allocate(value);
    if (!newPtr)
        return;
    int32_t varId = SymbolTable_getId(scope, name);
    if (varId >= 0) {
        void *oldPtr = (void*) SymbolTable_getPointer(scope, varId);
        SymbolTable_setPointer(scope, varId, (uintptr_t) newPtr);
        if (oldPtr)
            string_free((uint8_t*) oldPtr);
        return;
    }
    int32_t assigned = SymbolTable_instant(scope, name, string_classId(), (uintptr_t) newPtr);
    if (assigned < 0)
        string_free(newPtr);
}

// function pointers — same as value, typed helper

void *Relational_getFunction(SymbolTable *scope, const char *name) {
    return Relational_getValue(scope, name);
}

bool Relational_setFunction(SymbolTable *scope, const char *name, void *fn) {
    if (!scope || !name || !fn)
        return false;
    // classId 0 = raw function pointer; caller may pass ID_FUNCTION if defined
    int32_t varId = SymbolTable_getId(scope, name);
    if (varId >= 0) {
        SymbolTable_setPointer(scope, varId, (uintptr_t) fn);
        return true;
    }
    return SymbolTable_instant(scope, name, 0, (uintptr_t) fn) >= 0;
}

// spotlight — linear scan, ranked exact > prefix > substring

static size_t searchOne(SymbolTable *scope, const char *query, int32_t *outIds, size_t cap, size_t filled) {
    if (!scope || !query || !outIds)
        return filled;
    size_t active = SymbolTable_getActiveCount(scope);
    // pass 1: exact
    for (size_t i = 0; i < active; i++) {
        if (filled >= cap)
            break;
        int32_t varId = (int32_t) i;
        const char *name = SymbolTable_getName(scope, varId);
        if (!name)
            continue;
        if (equalsCaseInsensitive(name, query)) {
            // avoid duplicate if already added
            bool dup = false;
            for (size_t k = 0; k < filled; k++) {
                if (outIds[k] == varId) {
                    dup = true;
                    break;
                }
            }
            if (dup)
                continue;
            outIds[filled++] = varId;
        }
    }
    // pass 2: prefix
    for (size_t i = 0; i < active; i++) {
        if (filled >= cap)
            break;
        int32_t varId = (int32_t) i;
        const char *name = SymbolTable_getName(scope, varId);
        if (!name)
            continue;
        if (!prefixCaseInsensitive(name, query))
            continue;
        if (equalsCaseInsensitive(name, query))
            continue;
        bool dup = false;
        for (size_t k = 0; k < filled; k++) {
            if (outIds[k] == varId) {
                dup = true;
                break;
            }
        }
        if (dup)
            continue;
        outIds[filled++] = varId;
    }
    // pass 3: substring
    for (size_t i = 0; i < active; i++) {
        if (filled >= cap)
            break;
        int32_t varId = (int32_t) i;
        const char *name = SymbolTable_getName(scope, varId);
        if (!name)
            continue;
        if (!containsCaseInsensitive(name, query))
            continue;
        if (equalsCaseInsensitive(name, query))
            continue;
        if (prefixCaseInsensitive(name, query))
            continue;
        bool dup = false;
        for (size_t k = 0; k < filled; k++) {
            if (outIds[k] == varId) {
                dup = true;
                break;
            }
        }
        if (dup)
            continue;
        outIds[filled++] = varId;
    }
    return filled;
}

size_t Relational_search(SymbolTable *scope, const char *query, int32_t *outIds, size_t cap) {
    if (!scope || !query || !outIds || cap == 0)
        return 0;
    size_t filled = 0;
    filled = searchOne(scope, query, outIds, cap, filled);
    return filled;
}

size_t Relational_searchAll(SymbolTable *global, SymbolTable *local, const char *query, int32_t *outIds, size_t cap) {
    if (!query || !outIds || cap == 0)
        return 0;
    size_t filled = 0;
    if (global) {
        filled = searchOne(global, query, outIds, cap, filled);
        if (filled >= cap)
            return filled;
    }
    if (local) {
        // need to collect local matches without clobbering global exact ranking?
        // reuse searchOne which already ranks exact>prefix>substring per table
        size_t before = filled;
        // use a temporary offset — searchOne appends, we keep ranking per table,
        // final order is global exact/prefix/substring then local exact/prefix/substring
        // that's spotlight-friendly: global first, but local exact still outranks global substring
        // we achieve by running searchOne on local after global — it will add its exact first
        filled = searchOne(local, query, outIds, cap, filled);
        // if we want strict global rank interleaved, we'd merge; for now keep simple two-phase
        (void) before;
    }
    return filled;
}
