#ifndef RELATIONAL_VARIABLE_SLOT_H
#define RELATIONAL_VARIABLE_SLOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c23/constructor.h"

// relational/variable_slot.h — the 32-byte name box (the variable slot atom).
//
// The Relational Engine's reflection atom: a 32-byte record holding a folded
// 23-character name (plus NUL, zero-padded to 24) and an 8-byte pointer. It is
// the LABEL on the jar — the name states the symbol, the pointer addresses its
// value cell on the global shelf (the reflection plan, section 3.7). The slot
// carries no value of its own.
//
// Per the 24-Byte Variable Slot Law (The 23+1 Rule): names are 1..23 ASCII
// characters folded to lowercase over the charset [a-z0-9_$-], with '.' as the
// SEARCH SPLITTER between segments (character.position.x); the dot is stored but
// is never a segment of its own. Two slots pack into one 64-byte cache line with
// zero padding waste.
//
// A slot is usable inline (a pool row) or arena-allocated (the arity
// constructors). All behavior takes the slot by pointer; getters are null-safe
// (the Symmetric Getter/Setter Completeness Law). Cold-path only — a name box
// is read by the reflective walk, which is a cold rendezvous (the Cold-Only
// Reflection Law).

#define VARIABLE_SLOT_NAME_MAX 23u
#define VARIABLE_SLOT_NAME_BYTES 24u
#define VARIABLE_SLOT_SIZE 32u

typedef struct VariableSlot {
    char name[VARIABLE_SLOT_NAME_BYTES];  // folded name, NUL-terminated, zero-padded
    uintptr_t pointer;                    // the value cell address (the destination)
} VariableSlot;

_Static_assert(sizeof(VariableSlot) == VARIABLE_SLOT_SIZE, "VariableSlot must stay 32 Bytes");

// --- Constructors ---
// Inline init: validate + fold the name and set the pointer. False on a null
// slot or a null/empty/overlong/illegal name (the slot is left zeroed on
// failure). This is the cold validation seam for the name charset.
bool VariableSlot_init(VariableSlot *self, const char *name, uintptr_t pointer);

// Validate + fold a name into the slot grammar: one or more '.'-separated
// segments of [a-z0-9_$-], 1..23 characters total, folded to lowercase. Writes
// NUL-terminated folded Bytes into out, which must hold VARIABLE_SLOT_NAME_BYTES.
// The dot is the search splitter (stored, never a segment of its own; empty
// segments rejected). The atom owns this policy; the hash map and the segment
// search reuse it. False on null/empty/overlong/illegal.
bool VariableSlot_foldName(const char *name, char *out);

// First-character bucket 0..38 for a folded name character (a-z -> 0..25,
// 0-9 -> 26..35, '_' -> 36, '$' -> 37, '-' -> 38); -1 for any other byte. The
// atom owns the charset, so the hash maps share one bucket mapping.
int VariableSlot_bucketOf(char c);

// Arena-allocated conveniences (the Arity and Constructive Convenience Law).
VariableSlot *VariableSlot_0(void);
VariableSlot *VariableSlot_1(const char *name);
VariableSlot *VariableSlot_2(const char *name, uintptr_t pointer);

#define VariableSlot(...) CONSTRUCTOR_DISPATCH(VariableSlot, __VA_ARGS__)

// Release an arena-allocated slot (a constructor result). Never call on an
// inline slot; never twice.
void VariableSlot_free(VariableSlot *self);

// --- Setters ---
// Replace the name (fold + validate). False on a bad name; the old name stays.
bool VariableSlot_setName(VariableSlot *self, const char *name);
// Set the value cell address (a null slot is a no-op).
void VariableSlot_setPointer(VariableSlot *self, uintptr_t pointer);

// --- Getters (null-safe) ---
// Borrow the folded name from this slot; returns nullptr for a null slot.
const char *VariableSlot_getName(const VariableSlot *self);
uintptr_t VariableSlot_getPointer(const VariableSlot *self);
bool VariableSlot_isEmpty(const VariableSlot *self);
// True when the folded name equals the folded form of name.
bool VariableSlot_nameEquals(const VariableSlot *self, const char *name);

// --- String projections (the toString Law) ---
void VariableSlot_toString(const VariableSlot *self, char *dest, size_t cap, bool *outTruncated);
void VariableSlot_toStringStruct(const VariableSlot *self, char *dest, size_t cap, bool *outTruncated);

#endif
