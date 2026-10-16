#ifndef RELATIONAL_SYMBOL_TABLE_H
#define RELATIONAL_SYMBOL_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// relational/symbol_table.h — the relational symbol registry (Legacy: variable/SymbolTable.java).
//
// Maps a name to a (classId, targetPointer) payload. Every registered symbol
// is a row: the name lives in the shared string pool (referenced by slot,
// never copied), the pointer is the value. This is the heart of "everything
// is a pointer" — a name/pointer table whose values are themselves addresses
// of other headed blocks.
//
// Row layout (16 Bytes): [slot u32][classId u32][pointer u64]. Class is
// pinned at creation (no setter by design — rebind the value, not the kind).
// Lookup by name goes pool-first (binary search, shared globally), then one
// sparse hop (slot -> var id). Typed queries filter rows by classId
// (SymbolTable_findByClass) — one structure, no segregated lists to desync.
//
// Name policy (enforced cold, printed loud): ASCII alnum plus underscore
// and dot ([A-Za-z0-9_.], folded to lowercase), 1..23 chars. Anything else
// — empty, overlong, multibyte/UTF-16 garbage — is rejected. Mutations
// (instant, rename) print to stderr on rejection; lookups fail silent (-1),
// because speculative probing (spotlight) must never log.
//
// SLOT RECORD (owned by SymbolTable, behaviorless):
//   SymbolRow slot;     // int32_t + pool slot index (name lives in pool)
//   SymbolRow classId;  // uint32_t + value class, pinned at creation
//   SymbolRow pointer;  // uintptr_t + the value (everything is a pointer)

#define SYMBOL_TABLE_DEFAULT_CAPACITY 1024

typedef struct SymbolRow {
    int32_t slot;       // pool slot index (name lives in the pool)
    uint32_t classId;   // value class, pinned at creation
    uintptr_t pointer;  // the value (everything is a pointer)
} SymbolRow;

typedef struct SymbolTable {
    bool active;            // runtime-active flag
    SymbolRow *rows;      // dense rows, varId == index (append-only)
    uint32_t count;         // live rows
    uint32_t capacity;      // allocated rows
    int32_t *bySlot;        // pool slot -> varId, -1 empty (sparse, grown)
    uint32_t bySlotCap;     // bySlot slots allocated
} SymbolTable;

// Set up the registry (brings the shared pool up on the default arena,
// allocates rows). Returns false on OOM.
bool SymbolTable_init(SymbolTable *v);

// Release rows and index. Safe to call twice. Never touches the pool.
void SymbolTable_shutdown(SymbolTable *v);

// Register name => (classId, targetPointer). Create-or-FAIL: an existing
// name prints an error and yields -1 (never updates — rebind via
// setPointer, rename via rename). Empty/overlong/illegal names print and
// yield -1. Returns the assigned var id on success.
int32_t SymbolTable_instant(SymbolTable *v, const char *name, uint32_t classId, uintptr_t targetPointer);

// Rename an existing symbol (class and pointer follow the name). New-name
// collisions, unknown olds, and bad names print and yield false.
bool SymbolTable_rename(SymbolTable *v, const char *oldName, const char *newName);

// Resolve a name to its var id. Returns -1 if absent (silent —
// speculative probing must never log).
int32_t SymbolTable_getId(SymbolTable *v, const char *name);

// Collect var ids holding a class (insertion order). Fills at most cap;
// returns the total match count even when truncated (never silent).
size_t SymbolTable_findByClass(SymbolTable *v, uint32_t classId, int32_t *outIds, size_t cap);

// Payload accessors. varId must be a valid registered id.
uintptr_t SymbolTable_getPointer(SymbolTable *v, int32_t varId);
void SymbolTable_setPointer(SymbolTable *v, int32_t varId, uintptr_t targetPointer);
bool SymbolTable_compareAndSetPointer(SymbolTable *v, int32_t varId, uintptr_t expected, uintptr_t newPointer);
uint32_t SymbolTable_getClassId(SymbolTable *v, int32_t varId);

// Copy the registered name into _out (nul-terminated, at most outCap Bytes).
// Returns the string length, or -1 on bad varId / short buffer.
const char *SymbolTable_getName(const SymbolTable *v, int32_t varId);

// Number of registered symbols.
size_t SymbolTable_getActiveCount(SymbolTable *v);

#endif
