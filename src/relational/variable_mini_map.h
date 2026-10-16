#ifndef RELATIONAL_VARIABLE_MINI_MAP_H
#define RELATIONAL_VARIABLE_MINI_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "relational/variable_slot.h"
#include "struct/chunked_list.h"

// relational/variable_mini_map.h — the scoped name => pointer mini map.
//
// The big VariableHashMap (39 x 1024) is overkill for names that are scoped to
// ONE owner — a class's field names, a class's method names — where the whole
// key space is small and private. The mini map is the same first-character idea
// with a single level: 39 buckets, each a lazily-minted ChunkedList of
// VariableSlot rows. No 1024 dashcode layer, so an empty mini map is 39
// pointers and a used one costs one small leaf (16 rows) per active first
// character.
//
//     first folded char -> one of 39 buckets -> ChunkedList(VariableSlot)
//
// Names fold and validate through the atom (VariableSlot_foldName), including
// dotted field paths. COLD PATH ONLY: a scoped map is read by the reflective
// search, never on a frame (the Cold-Only Reflection Law).

#define VARIABLE_MINI_BUCKETS 39u
#define VARIABLE_MINI_ROOT_INIT 1u
#define VARIABLE_MINI_LEAF_ROWS 16u

typedef struct VariableMiniMap {
    bool active;                                 // runtime-active flag
    uint32_t count;                              // live entries
    ChunkedList *buckets[VARIABLE_MINI_BUCKETS]; // first-char -> lazily minted slot list
} VariableMiniMap;

// --- Constructors ---
bool VariableMiniMap_init(VariableMiniMap *map);
void VariableMiniMap_shutdown(VariableMiniMap *map);
VariableMiniMap *VariableMiniMap_0(void);
#define VariableMiniMap(...) CONSTRUCTOR_DISPATCH(VariableMiniMap, __VA_ARGS__)
void VariableMiniMap_free(VariableMiniMap *map);

// --- Core functions ---
// Insert or update name => pointer (the additive verb). Folds + validates the
// name; rejected names and OOM yield false and leave the map untouched.
bool VariableMiniMap_add(VariableMiniMap *map, const char *name, uintptr_t pointer);
// Resolve a name and return its value. Zero means absent/invalid; use contains
// when a stored zero value must be distinguished from a miss.
uintptr_t VariableMiniMap_get(const VariableMiniMap *map, const char *name);
// Pure probe (silent on miss).
bool VariableMiniMap_contains(const VariableMiniMap *map, const char *name);

// Visit every live entry (cold). Must not mutate the map.
typedef void (*VariableMiniMapVisitFn)(VariableSlot *slot, void *userdata);
void VariableMiniMap_forEach(VariableMiniMap *map, VariableMiniMapVisitFn fn, void *userdata);

// --- Getters (null-safe) ---
uint32_t VariableMiniMap_count(const VariableMiniMap *map);
bool VariableMiniMap_isEmpty(const VariableMiniMap *map);

// --- String projections (the toString Law) ---
void VariableMiniMap_toString(const VariableMiniMap *map, char *dest, size_t cap, bool *outTruncated);
void VariableMiniMap_toStringStruct(const VariableMiniMap *map, char *dest, size_t cap, bool *outTruncated);

#endif
