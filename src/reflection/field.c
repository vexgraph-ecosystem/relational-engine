// reflection/field.c — the Field metadata record ("field { variable }").

#include "reflection/field.h"

#include <stdio.h>
#include <string.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "nio/mem.h"
#include "oop/type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Field
 * ============================================================================
 * A Field EMBEDS a Variable and adds a setter: the name, reader and target all
 * live in the embedded Variable, so "field { variable }" is literal. The kind is
 * the header typeId (TYPE_REFLECT_FIELD) when arena-allocated; a Field stored as
 * a row inside a Struct carries no header (identity is its index in the Struct).
 *
 * Lifetime: arena-allocated (TYPE_REFLECT_FIELD), embedded, or a Struct row.
 * Cold path only (the Cold-Only Reflection Law); getters are null-safe.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Field (reflection/field.c)
 * ============================================================================
 * the Field metadata record (an embedded Variable + setter).
 *
 * STRUCT FIELDS (Mirroring reflection/field.h):
 * ----------------------------------------------------------------------------
 *   Field {
 *     Variable variable; // the embedded name + reader + target
 *     FieldSetFn set;    // the setter
 *   }
 *
 * PRIVATE HELPERS (kept file-local, pure logic, no behavior of their own): none.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h) Field_0() .. _4(name, read, set, target), _init, _free
 * Public Core Functions: (.h) Field_kind, _check, _getVariable, _read, _write
 * Public Setters: (.h) Field_setName / _setRead / _setSet / _setTarget
 * Public Getters: (.h) Field_getName(out, cap, self) / _getRead / _getSet / _getTarget
 * Public String Projections: (.h) Field_toString / _toStringStruct
 * ============================================================================
 */

// CONSTRUCTORS

bool Field_init(Field *self, const char *name, VariableReadFn read, FieldSetFn set, void *target) {
    if (self == nullptr)
        return false;
    Variable *variable = &(*self).variable;
    if (!Variable_init(variable, name, read, target))
        return false;
    (*self).set = set;
    return true;
}

static Field *instant(void) {
    Field *self = (Field*) Memory_alloc(TYPE_REFLECT_FIELD, sizeof(Field));
    if (self == nullptr)
        return nullptr;
    memset(self, 0, sizeof(*self));
    return self;
}

Field *Field_0(void) {
    return instant();
}

Field *Field_1(const char *name) {
    return Field_4(name, nullptr, nullptr, nullptr);
}

Field *Field_2(const char *name, VariableReadFn read) {
    return Field_4(name, read, nullptr, nullptr);
}

Field *Field_3(const char *name, VariableReadFn read, FieldSetFn set) {
    return Field_4(name, read, set, nullptr);
}

Field *Field_4(const char *name, VariableReadFn read, FieldSetFn set, void *target) {
    Field *self = instant();
    if (self == nullptr)
        return nullptr;
    if (!Field_init(self, name, read, set, target)) {
        Memory_free(self);
        return nullptr;
    }
    return self;
}

// Release an arena-allocated Field record; embedded fields are not freed here.
void Field_free(Field *self) {
    if (self != nullptr)
        Memory_free(self);
}

// CORE FUNCTIONS

uint64_t Field_kind(const Field *self) {
    if (self == nullptr)
        return 0u;
    return Memory_type((void*) self);
}

// Compare the allocation's recorded type with the requested reflection kind.
bool Field_check(const Field *self, uint64_t typeId) {
    if (self == nullptr)
        return false;
    return Memory_type((void*) self) == typeId;
}

Variable *Field_getVariable(Field *self) {
    if (self == nullptr)
        return nullptr;
    return &(*self).variable;
}

void *Field_read(Field *self, void *receiver) {
    if (self == nullptr)
        return nullptr;
    Variable *variable = &(*self).variable;
    return Variable_read(variable, receiver);
}

// Invoke the setter for receiver and value when this Field has one.
void Field_write(Field *self, void *receiver, void *value) {
    if (self == nullptr || (*self).set == nullptr)
        return;
    (*self).set(receiver, value);
}

// SETTERS

bool Field_setName(Field *self, const char *name) {
    if (self == nullptr)
        return false;
    Variable *variable = &(*self).variable;
    return Variable_setName(variable, name);
}

// Replace the embedded Variable's reader callback.
void Field_setRead(Field *self, VariableReadFn read) {
    if (self == nullptr)
        return;
    Variable *variable = &(*self).variable;
    Variable_setRead(variable, read);
}

// Replace this Field's setter callback.
void Field_setSet(Field *self, FieldSetFn set) {
    if (self == nullptr)
        return;
    (*self).set = set;
}

// Replace the target stored by the embedded Variable.
void Field_setTarget(Field *self, void *target) {
    if (self == nullptr)
        return;
    Variable *variable = &(*self).variable;
    Variable_setTarget(variable, target);
}

// GETTERS

int Field_getName(char *out, size_t outCap, const Field *self) {
    if (self == nullptr)
        return -1;
    const Variable *variable = &(*self).variable;
    return Variable_getName(out, outCap, variable);
}

// Return the embedded reader callback, or nullptr for a null Field.
VariableReadFn Field_getRead(const Field *self) {
    if (self == nullptr)
        return nullptr;
    const Variable *variable = &(*self).variable;
    return Variable_getRead(variable);
}

// Return the setter callback, or nullptr for a null Field.
FieldSetFn Field_getSet(const Field *self) {
    if (self == nullptr)
        return nullptr;
    return (*self).set;
}

void *Field_getTarget(const Field *self) {
    if (self == nullptr)
        return nullptr;
    const Variable *variable = &(*self).variable;
    return Variable_getTarget(variable);
}

// STRING PROJECTIONS (the toString Law)

void Field_toString(const Field *self, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (dest == nullptr || cap == 0u)
        return;
    if (self == nullptr) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    const Variable *variable = &(*self).variable;
    int written = snprintf(dest, cap, "Field(%s)", (*variable).name[0] == '\0' ? "?" : (*variable).name);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

// Format this Field and its one-level Variable value into a bounded projection.
void Field_toStringStruct(const Field *self, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (dest == nullptr || cap == 0u)
        return;
    if (self == nullptr) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    const Variable *variable = &(*self).variable;
    char inner[96];
    bool innerTruncated = false;
    Variable_toString(variable, inner, sizeof(inner), &innerTruncated);
    int written = snprintf(dest, cap, "Field { variable=%s, set=0x%llx }", inner,
                           (unsigned long long) (uintptr_t) (*self).set);
    if (written < 0 || (size_t) written >= cap || innerTruncated) {
        if (outTruncated)
            *outTruncated = true;
    }
}
