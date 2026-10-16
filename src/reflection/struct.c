// reflection/struct.c — the Struct metadata record ("struct { field }").

#include "reflection/struct.h"

#include <stdio.h>
#include <string.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "nio/mem.h"
#include "oop/type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Struct
 * ============================================================================
 * A named, ordered list of Fields held in a never-moved ChunkedList (page-sized
 * leaves) — data-oriented, growable, and address-stable. "struct { field }". The
 * kind is the header typeId (TYPE_REFLECT_STRUCT); the Field rows inside carry no
 * header (a field is identified by its index here).
 *
 * Lifetime: arena-allocated (TYPE_REFLECT_STRUCT); free releases the field list.
 * Cold path only (the Cold-Only Reflection Law); getters are null-safe.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Struct (reflection/struct.c)
 * ============================================================================
 * the Struct metadata record (a named list of Fields).
 *
 * STRUCT FIELDS (Mirroring reflection/struct.h):
 * ----------------------------------------------------------------------------
 *   Struct {
 *     char name[24];     // folded name (atom grammar)
 *     uint32_t count;    // live fields (== row count)
 *     uint32_t pad;      // explicit padding
 *     ChunkedList *fields; // never-moved Field rows
 *   }
 *
 * PRIVATE HELPERS (kept file-local, pure logic, no behavior of their own):
 *   ensureFields(self)  // mint the field list lazily (static)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h) Struct_0(), Struct_1(name), _init, _free
 * Public Core Functions: (.h) Struct_kind, _check, _add, _get, _forEach
 * Public Setters: (.h) Struct_setName
 * Public Getters: (.h) Struct_getName(out, cap, self), _count, _isEmpty
 * Public String Projections: (.h) Struct_toString / _toStringStruct
 * ============================================================================
 */

static bool ensureFields(Struct *self) {
    if ((*self).fields)
        return true;
    ChunkedList *fields = ChunkedList(ID_REFLECT_FIELD, (uint32_t) sizeof(Field), CHUNKED_LIST_TWO_LAYER_DEFAULT);
    if (!fields)
        return false;
    (*self).fields = fields;
    return true;
}

// CONSTRUCTORS

bool Struct_init(Struct *self, const char *name) {
    if (self == nullptr)
        return false;
    char folded[REFLECT_STRUCT_NAME_BYTES];
    if (!VariableSlot_foldName(name, folded))
        return false;
    memset(self, 0, sizeof(*self));
    memcpy((*self).name, folded, strlen(folded) + 1);
    return ensureFields(self);
}

Struct *Struct_0(void) {
    Struct *self = (Struct*) Memory_alloc(TYPE_REFLECT_STRUCT, sizeof(Struct));
    if (self == nullptr)
        return nullptr;
    memset(self, 0, sizeof(*self));
    if (!ensureFields(self)) {
        Memory_free(self);
        return nullptr;
    }
    return self;
}

Struct *Struct_1(const char *name) {
    Struct *self = (Struct*) Memory_alloc(TYPE_REFLECT_STRUCT, sizeof(Struct));
    if (self == nullptr)
        return nullptr;
    if (!Struct_init(self, name)) {
        Memory_free(self);
        return nullptr;
    }
    return self;
}

// Release the owned field-row list and arena-allocated Struct record.
void Struct_free(Struct *self) {
    if (self == nullptr)
        return;
    if ((*self).fields)
        ChunkedList_free((*self).fields);
    Memory_free(self);
}

// CORE FUNCTIONS

uint64_t Struct_kind(const Struct *self) {
    if (self == nullptr)
        return 0u;
    return Memory_type((void*) self);
}

// Compare the allocation's recorded type with the requested reflection kind.
bool Struct_check(const Struct *self, uint64_t typeId) {
    if (self == nullptr)
        return false;
    return Memory_type((void*) self) == typeId;
}

// Copy a Field into the stable row list and return its assigned index.
uint32_t Struct_add(Struct *self, const Field *field) {
    if (self == nullptr || field == nullptr || (*self).fields == nullptr)
        return REFLECT_STRUCT_INDEX_NONE;
    Field *row = (Field*) ChunkedList_addSlot((*self).fields);
    if (row == nullptr)
        return REFLECT_STRUCT_INDEX_NONE;
    memcpy(row, field, sizeof(Field));
    uint32_t index = ChunkedList_size((*self).fields) - 1u;
    (*self).count = index + 1u;
    return index;
}

Field *Struct_get(const Struct *self, uint32_t index) {
    if (self == nullptr || (*self).fields == nullptr)
        return nullptr;
    if (index >= (*self).count)
        return nullptr;
    return (Field*) ChunkedList_slot((*self).fields, index);
}

// Visit field rows in index order, passing each index and caller context.
void Struct_forEach(Struct *self, StructVisitFn fn, void *userdata) {
    if (self == nullptr || fn == nullptr || (*self).fields == nullptr)
        return;
    uint32_t n = (*self).count;
    for (uint32_t i = 0u; i < n; i++) {
        Field *row = (Field*) ChunkedList_slot((*self).fields, i);
        if (row)
            fn(row, i, userdata);
    }
}

// SETTERS

bool Struct_setName(Struct *self, const char *name) {
    if (self == nullptr)
        return false;
    char folded[REFLECT_STRUCT_NAME_BYTES];
    if (!VariableSlot_foldName(name, folded))
        return false;
    memset((*self).name, 0, sizeof((*self).name));
    memcpy((*self).name, folded, strlen(folded) + 1);
    return true;
}

// GETTERS

int Struct_getName(char *out, size_t outCap, const Struct *self) {
    if (self == nullptr || out == nullptr || outCap == 0u)
        return -1;
    const char *src = (*self).name;
    size_t len = strlen(src);
    if (len + 1u > outCap)
        return -1;
    memcpy(out, src, len + 1u);
    return (int) len;
}

// Return the number of stored fields, or zero for a null Struct.
uint32_t Struct_count(const Struct *self) {
    return self ? (*self).count : 0u;
}

// Report whether the Struct has no fields; a null Struct is treated as empty.
bool Struct_isEmpty(const Struct *self) {
    if (self == nullptr)
        return true;
    return (*self).count == 0u;
}

// STRING PROJECTIONS (the toString Law)

void Struct_toString(const Struct *self, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (dest == nullptr || cap == 0u)
        return;
    if (self == nullptr) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    int written = snprintf(dest, cap, "Struct(%s, fields=%u)",
                           (*self).name[0] == '\0' ? "?" : (*self).name, (*self).count);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

// Format the Struct's own fields into a bounded structure projection.
void Struct_toStringStruct(const Struct *self, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (dest == nullptr || cap == 0u)
        return;
    if (self == nullptr) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    int written = snprintf(dest, cap, "Struct { name=\"%s\", count=%u, fields=0x%llx }",
                           (*self).name, (*self).count,
                           (unsigned long long) (uintptr_t) (*self).fields);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}
