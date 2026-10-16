// reflection/variable.c — the Variable metadata record (base of the hierarchy).

#include "reflection/variable.h"

#include <stdio.h>
#include <string.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "nio/mem.h"
#include "oop/type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Variable
 * ============================================================================
 * The base reflection kind: a 40-byte record holding a folded name (the atom's
 * grammar), a reader, and a target. A Field embeds one. The kind is the header
 * typeId (TYPE_REFLECT_VARIABLE). Records of a kind are stored segregated (one
 * homogeneous array per kind) per the BitPool Slot Segregation Law.
 *
 * Lifetime: arena-allocated (TYPE_REFLECT_VARIABLE) or embedded (a Field embeds
 * one with no header). Cold path only (the Cold-Only Reflection Law); getters
 * are null-safe.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Variable (reflection/variable.c)
 * ============================================================================
 * the Variable metadata record (name + reader + target).
 *
 * STRUCT FIELDS (Mirroring reflection/variable.h):
 * ----------------------------------------------------------------------------
 *   Variable {
 *     char name[24];      // folded name (atom grammar)
 *     VariableReadFn read; // the reader
 *     void *target;       // the value / owner
 *   }
 *
 * PRIVATE HELPERS (kept file-local, pure logic, no behavior of their own): none.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h) Variable_0() .. _3(name, read, target), _init, _free
 * Public Core Functions: (.h) Variable_kind, _check, _read
 * Public Setters: (.h) Variable_setName / _setRead / _setTarget
 * Public Getters: (.h) Variable_getName(out, cap, self) / _getRead / _getTarget
 * Public String Projections: (.h) Variable_toString / _toStringStruct
 * ============================================================================
 */

// CONSTRUCTORS

bool Variable_init(Variable *self, const char *name, VariableReadFn read, void *target) {
    if (self == nullptr)
        return false;
    char folded[REFLECT_VARIABLE_NAME_BYTES];
    if (!VariableSlot_foldName(name, folded))
        return false;
    memset(self, 0, sizeof(*self));
    memcpy((*self).name, folded, strlen(folded) + 1);
    (*self).read = read;
    (*self).target = target;
    return true;
}

static Variable *instant(void) {
    Variable *self = (Variable*) Memory_alloc(TYPE_REFLECT_VARIABLE, sizeof(Variable));
    if (self == nullptr)
        return nullptr;
    memset(self, 0, sizeof(*self));
    return self;
}

Variable *Variable_0(void) {
    return instant();
}

Variable *Variable_1(const char *name) {
    return Variable_3(name, nullptr, nullptr);
}

Variable *Variable_2(const char *name, VariableReadFn read) {
    return Variable_3(name, read, nullptr);
}

Variable *Variable_3(const char *name, VariableReadFn read, void *target) {
    Variable *self = instant();
    if (self == nullptr)
        return nullptr;
    if (!Variable_init(self, name, read, target)) {
        Memory_free(self);
        return nullptr;
    }
    return self;
}

// Release an arena-allocated Variable record; embedded values remain borrowed.
void Variable_free(Variable *self) {
    if (self != nullptr)
        Memory_free(self);
}

// CORE FUNCTIONS

uint64_t Variable_kind(const Variable *self) {
    if (self == nullptr)
        return 0u;
    return Memory_type((void*) self);
}

// Compare the allocation's recorded type with the requested reflection kind.
bool Variable_check(const Variable *self, uint64_t typeId) {
    if (self == nullptr)
        return false;
    return Memory_type((void*) self) == typeId;
}

void *Variable_read(Variable *self, void *arg) {
    if (self == nullptr || (*self).read == nullptr)
        return nullptr;
    return (*self).read(arg);
}

// SETTERS

bool Variable_setName(Variable *self, const char *name) {
    if (self == nullptr)
        return false;
    char folded[REFLECT_VARIABLE_NAME_BYTES];
    if (!VariableSlot_foldName(name, folded))
        return false;
    memset((*self).name, 0, sizeof((*self).name));
    memcpy((*self).name, folded, strlen(folded) + 1);
    return true;
}

// Replace the callback used to read this Variable's value.
void Variable_setRead(Variable *self, VariableReadFn read) {
    if (self == nullptr)
        return;
    (*self).read = read;
}

// Replace the callback context or value target stored by this Variable.
void Variable_setTarget(Variable *self, void *target) {
    if (self == nullptr)
        return;
    (*self).target = target;
}

// GETTERS

int Variable_getName(char *out, size_t outCap, const Variable *self) {
    if (self == nullptr || out == nullptr || outCap == 0u)
        return -1;
    const char *src = (*self).name;
    size_t len = strlen(src);
    if (len + 1u > outCap)
        return -1;
    memcpy(out, src, len + 1u);
    return (int) len;
}

// Return the reader callback, or nullptr for a null Variable.
VariableReadFn Variable_getRead(const Variable *self) {
    if (self == nullptr)
        return nullptr;
    return (*self).read;
}

void *Variable_getTarget(const Variable *self) {
    if (self == nullptr)
        return nullptr;
    return (*self).target;
}

// STRING PROJECTIONS (the toString Law)

void Variable_toString(const Variable *self, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (dest == nullptr || cap == 0u)
        return;
    if (self == nullptr) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    int written = snprintf(dest, cap, "Variable(%s)", (*self).name[0] == '\0' ? "?" : (*self).name);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

// Format the Variable's own fields into a bounded structure projection.
void Variable_toStringStruct(const Variable *self, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (dest == nullptr || cap == 0u)
        return;
    if (self == nullptr) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    int written = snprintf(dest, cap, "Variable { name=\"%s\", read=0x%llx, target=0x%llx }",
                           (*self).name,
                           (unsigned long long) (uintptr_t) (*self).read,
                           (unsigned long long) (uintptr_t) (*self).target);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}
