// relational/cell.c — the 32-byte identity cell.
//
// Allocate an arena block whose 32-byte MemoryHeader is the identity; the
// payload is one Cell (value + pad). No behavior beyond the header reads and the
// value slot.

#include "relational/cell.h"

#include <stdio.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "nio/mem.h"

_Static_assert(CELL_BLOCK_BYTES == sizeof(MemoryHeader) + CELL_PAYLOAD_BYTES,
               "Cell block is the 32-byte identity header plus the 16-byte payload");

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Cell
 * ============================================================================
 * The reflection engine's identity atom: one 48-byte arena block made of the
 * standard 32-byte MemoryHeader (id, hash, length, pointer — the identity) plus
 * a 16-byte payload (a value/pointer slot and padding). A cell is therefore
 * self-describing: Cell_check(cell, typeId) answers its kind straight from the
 * header, with no side table and no extra Bytes.
 *
 * Lifetime: arena-allocated (TYPE from the caller's typeId) and released with
 * Cell_free, or wholesale at arena teardown. The identity typeId is the
 * caller's — a cell is a shape, not a fixed class.
 *
 * Path temperature: cold. Cells are read by the reflective rendezvous, never on
 * a frame (the Cold-Only Reflection Law). All getters are null-safe.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Cell (relational/cell.c)
 * ============================================================================
 * the identity cell (32-byte MemoryHeader identity + a 16-byte value slot).
 *
 * STRUCT FIELDS (Mirroring relational/cell.h — the payload; the identity is the
 * block header before it):
 * ----------------------------------------------------------------------------
 *   Cell {
 *     uintptr_t value; // thin pointer to the value, or the value inline
 *     uintptr_t pad;   // tail padding so the block is 32 Bytes
 *   }
 *
 * PRIVATE HELPERS (kept file-local, pure data, no behavior): none.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Cell_0()                    : anonymous cell (typeId 0)
 *   - Cell_1(typeId)              : identity cell, zero value
 *   - Cell_2(typeId, value)       : identity cell, bound value
 *   - Cell_free(cell)             : release an arena cell
 *
 * Public Core Functions: (.h)
 *   - Cell_typeId(cell)           : the identity typeId
 *   - Cell_check(cell, typeId)    : identity compare (null-safe)
 *
 * Public Setters: (.h)
 *   - Cell_setValue(cell, value)
 *
 * Public Getters: (.h)
 *   - Cell_getValue(cell)
 *
 * Public String Projections: (.h)
 *   - Cell_toString(cell, dest, cap, outTruncated)
 *   - Cell_toStringStruct(cell, dest, cap, outTruncated)
 * ============================================================================
 */

// CONSTRUCTORS

static Cell *instant(uint64_t typeId, uintptr_t value) {
    Cell *cell = (Cell*) Memory_alloc(typeId, CELL_PAYLOAD_BYTES);
    if (!cell)
        return nullptr;
    (*cell).value = value;
    (*cell).pad = 0u;
    return cell;
}

Cell *Cell_0(void) {
    return instant(0u, 0u);
}

Cell *Cell_1(uint64_t typeId) {
    return instant(typeId, 0u);
}

Cell *Cell_2(uint64_t typeId, uintptr_t value) {
    return instant(typeId, value);
}

void Cell_free(Cell *cell) {
    if (cell)
        Memory_free(cell);
}

// CORE FUNCTIONS

uint64_t Cell_typeId(const Cell *cell) {
    if (!cell)
        return 0u;
    return Memory_type((void*) cell);
}

bool Cell_check(const Cell *cell, uint64_t typeId) {
    if (!cell)
        return false;
    return Memory_type((void*) cell) == typeId;
}

// SETTERS

void Cell_setValue(Cell *cell, uintptr_t value) {
    if (!cell)
        return;
    (*cell).value = value;
}

// GETTERS

uintptr_t Cell_getValue(const Cell *cell) {
    if (!cell)
        return 0u;
    return (*cell).value;
}

// STRING PROJECTIONS (the toString Law)

void Cell_toString(const Cell *cell, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!cell) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    int written = snprintf(dest, cap, "Cell(typeId=0x%llx, value=0x%llx)",
                           (unsigned long long) Cell_typeId(cell),
                           (unsigned long long) (*cell).value);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

void Cell_toStringStruct(const Cell *cell, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (!dest || cap == 0u)
        return;
    if (!cell) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    int written = snprintf(dest, cap, "Cell { value=0x%llx, pad=0x%llx }",
                           (unsigned long long) (*cell).value,
                           (unsigned long long) (*cell).pad);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}
