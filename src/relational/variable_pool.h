#ifndef RELATIONAL_VARIABLE_POOL_H
#define RELATIONAL_VARIABLE_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nio/mem.h"

// relational/variable_pool.h — process-wide interned string pool.
//
// Names are stated once, shared by pointer, never copied: each distinct name
// lives in exactly one 32-byte slot, and variable rows (or any consumer)
// reference the slot instead of duplicating Bytes. One table per process —
// name identity is inherently global (two arenas interning "label" must
// share, not duplicate). Backing comes from the init-time arena; pool
// strings are immutable and freed wholesale at arena teardown, never per
// string. All entries serialize on an internal spinlock.
//
// SLOT RECORD (owned by the pool service, behaviorless):
//   [ptr][str1][str2][str3] — 4x uint64, 32 Bytes total:
//     self  : intrusive validity, must equal the slot's own address
//     name  : NUL-terminated Bytes, zero-padded (exact Bytes, no case fold)
// Lookup goes through a sorted u32 index over the slots (binary search with
// full 24-byte compares). Slot indices stay valid forever; raw slot pointers
// hold until the next grow — re-resolve via find/slot, or gate use on isSlot.

#define STRING_POOL_NAME_MAX 23u
#define STRING_POOL_SLOT_SIZE 32u
#define STRING_POOL_DEFAULT_CAPACITY 64u

typedef struct StringSlot {
    uint64_t self;      // intrusive validity: must equal own address
    char name[24];      // NUL-terminated, zero-padded, exact Bytes
} StringSlot;

// One-time setup on an arena (allocates the initial slots + index).
// Idempotent success like Memory_init: live already returns true (shared
// bring-up must not fail). False on null arena or OOM.
bool StringPool_init(MemoryArena *arena);

// Release slots and index back to the arena. Safe to call twice; safe
// before any init. The arena itself is never destroyed here.
void StringPool_shutdown(void);

// Intern a name: returns the slot index, or -1 on null/empty/overlong input
// or OOM. Interning an existing name returns its index (dedup, no copy).
int32_t StringPool_intern(const char *str);

// Resolve a name to its slot index. Returns -1 when absent (or bad input).
int32_t StringPool_find(const char *str);

// Slot accessors. Out-of-range index yields nullptr (never crashes).
const StringSlot *StringPool_slot(uint32_t index);
const char *StringPool_name(uint32_t index);

// Live slot count.
uint32_t StringPool_count(void);

// Validity: self link intact AND inside the current slot range. Stale
// pointers from a grown (moved) array or a shut-down pool fail closed.
bool StringPool_isSlot(const void *ptr);

#endif
