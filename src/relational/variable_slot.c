// relational/variable_slot.c — the 32-byte name box.
//
// Fold + validate the name once (cold, quiet), store it zero-padded, keep the
// value pointer opaque. No allocation on the read path; the arity constructors
// are the only allocators.

#include "relational/variable_slot.h"

#include <stdio.h>
#include <string.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "nio/mem.h"
#include "oop/type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: VariableSlot
 * ============================================================================
 * The reflection atom of the Relational Engine: a fixed 32-byte name box. The
 * name is folded to lowercase and validated against the 39-character charset
 * [a-z0-9_$-] (1..23 characters; the dot is the path splitter, never a name
 * character), stored NUL-terminated in a 24-byte buffer so two slots pack into
 * one 64-byte cache line with zero padding waste (the 24-Byte Variable Slot
 * Law). The pointer is the value cell's address on the global shelf; the slot
 * itself holds no value.
 *
 * Lifetime: a slot is either inline (a pool row owned by its table) or
 * arena-allocated through the arity constructors (TYPE_VARIABLE_SLOT). The
 * inline form is the steady-state shape; the arena form is the convenience.
 *
 * Path temperature: cold. A name box is read by the reflective walk — name and
 * dotted-path resolution — which is a cold rendezvous, never a per-frame path
 * (the Cold-Only Reflection Law). Validation is exhaustive at the cold seam
 * (the Cold-Strict, Hot-Minimal Validation Law): a bad name is rejected, never
 * silently truncated.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: VariableSlot (relational/variable_slot.c)
 * ============================================================================
 * the 32-byte name box: a folded name plus a value-cell pointer.
 *
 * STRUCT FIELDS (Mirroring relational/variable_slot.h):
 * ----------------------------------------------------------------------------
 *   VariableSlot {
 *     char name[24];      // folded name, NUL-terminated, zero-padded
 *     uintptr_t pointer;  // the value cell address (the destination)
 *   }
 *
 * PRIVATE HELPERS (kept file-local, pure logic, no behavior of their own):
 * ----------------------------------------------------------------------------
 *   (none — the fold/validate helpers are static functions below, not records)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - VariableSlot_0()                         : arena-allocated empty slot
 *   - VariableSlot_1(name)                     : arena-allocated named slot
 *   - VariableSlot_2(name, pointer)            : arena-allocated named + bound
 *   - VariableSlot_init(self, name, pointer)   : inline init (validate + fold)
 *   - VariableSlot_free(self)                  : release an arena slot
 *
 * Public Core Functions: (.h)
 *   - VariableSlot_foldName(name, out)         : fold + validate the charset
 *   - VariableSlot_bucketOf(c)                 : first-char bucket 0..38
 *
 * Private Core Functions: (.c static)
 *   - storeName(self, folded)                  : zero-pad copy into the slot
 *
 * Public Setters: (.h)
 *   - VariableSlot_setName(self, name)
 *   - VariableSlot_setPointer(self, pointer)
 *
 * Public Getters: (.h)
 *   - VariableSlot_getName(self)
 *   - VariableSlot_getPointer(self)
 *   - VariableSlot_isEmpty(self)
 *   - VariableSlot_nameEquals(self, name)
 *
 * Public String Projections: (.h)
 *   - VariableSlot_toString(self, dest, cap, outTruncated)
 *   - VariableSlot_toStringStruct(self, dest, cap, outTruncated)
 * ============================================================================
 */

// Fold a name to lowercase and validate the segment grammar. A name is one or
// more '.'-separated segments, each 1..23 characters of [a-z0-9_$-]; the whole
// name is 1..23 characters (dots included). Case folds to lowercase. The dot is
// the search splitter — stored between segments, never a segment of its own,
// and never the first character (the hash bucket reads the first character).
// Empty segments (leading, trailing, or doubled dots) are rejected. Writes
// NUL-terminated folded Bytes into out, which must hold VARIABLE_SLOT_NAME_BYTES.
bool VariableSlot_foldName(const char *name, char *out) {
    if (name == nullptr || name[0] == '\0')
        return false;
    size_t len = 0u;
    size_t segLen = 0u;
    for (const char *p = name; ; p++) {
        char c = *p;
        if (c == '\0' || c == '.') {
            if (segLen == 0u)
                return false; // leading, trailing, or doubled dot
            if (c == '\0') {
                out[len] = '\0';
                return true;
            }
            if (len >= VARIABLE_SLOT_NAME_MAX)
                return false;
            out[len++] = '.';
            segLen = 0u;
            continue;
        }
        unsigned char u = (unsigned char) c;
        bool ok = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') ||
                  (u >= '0' && u <= '9') || u == '_' || u == '$' || u == '-';
        if (!ok)
            return false;
        if (len >= VARIABLE_SLOT_NAME_MAX)
            return false;
        out[len++] = (char) ((u >= 'A' && u <= 'Z') ? (u + 32) : u);
        segLen++;
    }
}

int VariableSlot_bucketOf(char c) {
    if (c >= 'a' && c <= 'z')
        return c - 'a';
    if (c >= '0' && c <= '9')
        return 26 + (c - '0');
    if (c == '_')
        return 36;
    if (c == '$')
        return 37;
    if (c == '-')
        return 38;
    return -1;
}

// Store a validated folded name into the slot, zero-padding the tail so the
// 24-byte buffer has no stale Bytes.
static void storeName(VariableSlot *self, const char *folded) {
    char *dest = (*self).name;
    memset(dest, 0, VARIABLE_SLOT_NAME_BYTES);
    memcpy(dest, folded, strlen(folded) + 1);
}

// CONSTRUCTORS (PUBLIC & PRIVATE)

bool VariableSlot_init(VariableSlot *self, const char *name, uintptr_t pointer) {
    if (self == nullptr)
        return false;
    char folded[VARIABLE_SLOT_NAME_BYTES];
    if (!VariableSlot_foldName(name, folded))
        return false;
    storeName(self, folded);
    (*self).pointer = pointer;
    return true;
}

VariableSlot *VariableSlot_0(void) {
    VariableSlot *self = (VariableSlot*) Memory_alloc(TYPE_VARIABLE_SLOT, sizeof(VariableSlot));
    if (self == nullptr)
        return nullptr;
    memset(self, 0, sizeof(*self));
    return self;
}

VariableSlot *VariableSlot_1(const char *name) {
    return VariableSlot_2(name, 0);
}

VariableSlot *VariableSlot_2(const char *name, uintptr_t pointer) {
    VariableSlot *self = VariableSlot_0();
    if (self == nullptr)
        return nullptr;
    if (!VariableSlot_init(self, name, pointer)) {
        Memory_free(self);
        return nullptr;
    }
    return self;
}

void VariableSlot_free(VariableSlot *self) {
    if (self != nullptr)
        Memory_free(self);
}

// SETTERS (PUBLIC & PRIVATE)

bool VariableSlot_setName(VariableSlot *self, const char *name) {
    if (self == nullptr)
        return false;
    char folded[VARIABLE_SLOT_NAME_BYTES];
    if (!VariableSlot_foldName(name, folded))
        return false;
    storeName(self, folded);
    return true;
}

void VariableSlot_setPointer(VariableSlot *self, uintptr_t pointer) {
    if (self == nullptr)
        return;
    (*self).pointer = pointer;
}

// GETTERS (PUBLIC & PRIVATE)

const char *VariableSlot_getName(const VariableSlot *self) {
    return self ? (*self).name : nullptr;
}

uintptr_t VariableSlot_getPointer(const VariableSlot *self) {
    if (self == nullptr)
        return 0;
    return (*self).pointer;
}

bool VariableSlot_isEmpty(const VariableSlot *self) {
    if (self == nullptr)
        return true;
    return (*self).name[0] == '\0';
}

bool VariableSlot_nameEquals(const VariableSlot *self, const char *name) {
    if (self == nullptr)
        return false;
    char folded[VARIABLE_SLOT_NAME_BYTES];
    if (!VariableSlot_foldName(name, folded))
        return false;
    return strcmp((*self).name, folded) == 0;
}

// STRING PROJECTIONS (the toString Law)

// The name charset cannot contain '"' or '\', so a quoted name needs no escape.
void VariableSlot_toString(const VariableSlot *self, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated != nullptr)
        *outTruncated = false;
    if (dest == nullptr || cap == 0)
        return;
    if (self == nullptr) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    const char *name = (*self).name;
    if (name[0] == '\0')
        name = "?";
    int written = snprintf(dest, cap, "VariableSlot(%s => 0x%llx)", name,
                           (unsigned long long) (*self).pointer);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated != nullptr)
            *outTruncated = true;
    }
}

void VariableSlot_toStringStruct(const VariableSlot *self, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated != nullptr)
        *outTruncated = false;
    if (dest == nullptr || cap == 0)
        return;
    if (self == nullptr) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    int written = snprintf(dest, cap, "VariableSlot { name=\"%s\", pointer=0x%llx }", (*self).name,
                           (unsigned long long) (*self).pointer);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated != nullptr)
            *outTruncated = true;
    }
}
