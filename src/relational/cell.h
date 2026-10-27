#ifndef RELATIONAL_CELL_H
#define RELATIONAL_CELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c23/constructor.h"

// relational/cell.h — the identity cell.
//
// A cell is one arena block whose 32-byte MemoryHeader IS its identity — the
// "self-describing header" (the Self-Describing memory Block Law) made the unit
// of reflection. The block is:
//
//     [ MemoryHeader 32B: id | hash | length | pointer ][ value 8B ][ pad 8B ] = 48B
//       `----------------- the identity ---------------'  `-- the Cell payload --'
//
// The `value` field is a thin pointer to the cell's value, or the value inline
// for small kinds — one 8-byte slot either way, so the cell is uniform. The pad
// rounds the payload to 16 Bytes; the block is the 32-byte header plus that
// payload, and its payload is 32-byte aligned (the Universal Alignment Law).
//
// Cell_check(cell, typeId) reads the identity header's typeId, so a live
// pointer can answer "are you kind X?" without a side table. This is the block
// reactive objects are built on (the same header, a different payload shape).

#define CELL_PAYLOAD_BYTES 16u
// Whole block: the 32-byte identity header plus the 16-byte payload.
#define CELL_BLOCK_BYTES 48u

typedef struct Cell {
    uintptr_t value; // thin pointer to the value, or the value inline
    uintptr_t pad;   // tail padding so the block is 32 Bytes
} Cell;

_Static_assert(sizeof(Cell) == CELL_PAYLOAD_BYTES, "Cell payload must stay 16 Bytes");

// --- Constructors ---
// Cell_0() is an anonymous cell (typeId 0, PROJ_GENERIC); Cell_1/_2 carry the
// identity typeId and an optional initial value. Arena-allocated.
Cell *Cell_0(void);
Cell *Cell_1(uint64_t typeId);
Cell *Cell_2(uint64_t typeId, uintptr_t value);

#define Cell(...) CONSTRUCTOR_DISPATCH(Cell, __VA_ARGS__)

// Release an arena-allocated cell (a constructor result).
void Cell_free(Cell *cell);

// --- Core functions ---
// The identity typeId stamped in the header (0 on a null cell).
uint64_t Cell_typeId(const Cell *cell);
// True when the cell's identity typeId equals typeId. Null-safe (false).
bool Cell_check(const Cell *cell, uint64_t typeId);

// --- Setters / Getters (the Symmetric Getter/Setter Completeness Law) ---
void Cell_setValue(Cell *cell, uintptr_t value);
uintptr_t Cell_getValue(const Cell *cell);

// --- String projections (the toString Law) ---
void Cell_toString(const Cell *cell, char *dest, size_t cap, bool *outTruncated);
void Cell_toStringStruct(const Cell *cell, char *dest, size_t cap, bool *outTruncated);

#endif
