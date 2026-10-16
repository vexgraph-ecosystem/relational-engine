#ifndef REFLECTION_STRUCT_H
#define REFLECTION_STRUCT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "reflection/field.h"
#include "struct/chunked_list.h"

// reflection/struct.h — the Struct metadata record (a list of Fields).
//
// A Struct is a named, ordered list of Fields — "struct { field }". Rows live
// in a never-moved ChunkedList (data-oriented, growable, stable addresses), so a
// Field's address is stable for the Struct's whole life. The kind is the header
// typeId (TYPE_REFLECT_STRUCT). Block layout:
// [MemoryHeader 16][name 24][count 4][pad 4][fields 8] = 56 Bytes.

#define REFLECT_STRUCT_NAME_BYTES VARIABLE_SLOT_NAME_BYTES
#define REFLECT_STRUCT_NAME_MAX VARIABLE_SLOT_NAME_MAX
#define REFLECT_STRUCT_INDEX_NONE UINT32_MAX

typedef struct Struct {
    char name[REFLECT_STRUCT_NAME_BYTES]; // folded name (atom grammar)
    uint32_t count;                       // live fields (== row count)
    uint32_t pad;                         // explicit padding
    ChunkedList *fields;                  // never-moved Field rows
} Struct;

_Static_assert(sizeof(Struct) == 40u, "Struct must stay 40 Bytes");

bool Struct_init(Struct *self, const char *name);
Struct *Struct_0(void);
Struct *Struct_1(const char *name);
#define Struct(...) CONSTRUCTOR_DISPATCH(Struct, __VA_ARGS__)
void Struct_free(Struct *self);

uint64_t Struct_kind(const Struct *self);
bool Struct_check(const Struct *self, uint64_t typeId);
// Append a copy of a field. Returns its index, or REFLECT_STRUCT_INDEX_NONE.
uint32_t Struct_add(Struct *self, const Field *field);
// The field row at `index` (mutable, stable), or null.
Field *Struct_get(const Struct *self, uint32_t index);
// Visit every field row in order (cold). Must not mutate the Struct.
typedef void (*StructVisitFn)(Field *field, uint32_t index, void *userdata);
void Struct_forEach(Struct *self, StructVisitFn fn, void *userdata);

bool Struct_setName(Struct *self, const char *name);
int Struct_getName(char *out, size_t outCap, const Struct *self);
uint32_t Struct_count(const Struct *self);
bool Struct_isEmpty(const Struct *self);

void Struct_toString(const Struct *self, char *dest, size_t cap, bool *outTruncated);
void Struct_toStringStruct(const Struct *self, char *dest, size_t cap, bool *outTruncated);

#endif
