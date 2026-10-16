// reflection/method.c — the Method metadata record.

#include "reflection/method.h"

#include <stdio.h>
#include <string.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "nio/mem.h"
#include "oop/type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Method
 * ============================================================================
 * A 40-byte record holding a folded name (the atom's grammar), a callable, and a
 * target. A Class owns a list of these. The kind is the header typeId
 * (TYPE_REFLECT_METHOD). Records of a kind are stored segregated (one
 * homogeneous array per kind) per the BitPool Slot Segregation Law.
 *
 * Lifetime: arena-allocated (TYPE_REFLECT_METHOD), embedded, or a row inside a
 * Class's method list (no header there). Cold path only (the Cold-Only
 * Reflection Law); getters are null-safe.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Method (reflection/method.c)
 * ============================================================================
 * the Method metadata record (name + callable + target).
 *
 * STRUCT FIELDS (Mirroring reflection/method.h):
 * ----------------------------------------------------------------------------
 *   Method {
 *     char name[24];  // folded name (atom grammar)
 *     MethodFn invoke; // the callable
 *     void *target;   // the owner / default receiver
 *   }
 *
 * PRIVATE HELPERS (kept file-local, pure logic, no behavior of their own): none.
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h) Method_0() .. _3(name, invoke, target), _init, _free
 * Public Core Functions: (.h) Method_kind, _check, _call
 * Public Setters: (.h) Method_setName / _setInvoke / _setTarget
 * Public Getters: (.h) Method_getName(out, cap, self) / _getInvoke / _getTarget
 * Public String Projections: (.h) Method_toString / _toStringStruct
 * ============================================================================
 */

// CONSTRUCTORS

bool Method_init(Method *self, const char *name, MethodFn invoke, void *target) {
    if (self == nullptr)
        return false;
    char folded[REFLECT_METHOD_NAME_BYTES];
    if (!VariableSlot_foldName(name, folded))
        return false;
    memset(self, 0, sizeof(*self));
    memcpy((*self).name, folded, strlen(folded) + 1);
    (*self).invoke = invoke;
    (*self).target = target;
    return true;
}

static Method *instant(void) {
    Method *self = (Method*) Memory_alloc(TYPE_REFLECT_METHOD, sizeof(Method));
    if (self == nullptr)
        return nullptr;
    memset(self, 0, sizeof(*self));
    return self;
}

Method *Method_0(void) {
    return instant();
}

Method *Method_1(const char *name) {
    return Method_3(name, nullptr, nullptr);
}

Method *Method_2(const char *name, MethodFn invoke) {
    return Method_3(name, invoke, nullptr);
}

Method *Method_3(const char *name, MethodFn invoke, void *target) {
    Method *self = instant();
    if (self == nullptr)
        return nullptr;
    if (!Method_init(self, name, invoke, target)) {
        Memory_free(self);
        return nullptr;
    }
    return self;
}

// Release an arena-allocated Method record.
void Method_free(Method *self) {
    if (self != nullptr)
        Memory_free(self);
}

// CORE FUNCTIONS

uint64_t Method_kind(const Method *self) {
    if (self == nullptr)
        return 0u;
    return Memory_type((void*) self);
}

// Compare the allocation's recorded type with the requested reflection kind.
bool Method_check(const Method *self, uint64_t typeId) {
    if (self == nullptr)
        return false;
    return Memory_type((void*) self) == typeId;
}

void *Method_call(Method *self, void *arg) {
    if (self == nullptr || (*self).invoke == nullptr)
        return nullptr;
    return (*self).invoke(arg);
}

// SETTERS

bool Method_setName(Method *self, const char *name) {
    if (self == nullptr)
        return false;
    char folded[REFLECT_METHOD_NAME_BYTES];
    if (!VariableSlot_foldName(name, folded))
        return false;
    memset((*self).name, 0, sizeof((*self).name));
    memcpy((*self).name, folded, strlen(folded) + 1);
    return true;
}

// Replace the callable stored by this Method.
void Method_setInvoke(Method *self, MethodFn invoke) {
    if (self == nullptr)
        return;
    (*self).invoke = invoke;
}

// Replace the default receiver/context stored by this Method.
void Method_setTarget(Method *self, void *target) {
    if (self == nullptr)
        return;
    (*self).target = target;
}

// GETTERS

int Method_getName(char *out, size_t outCap, const Method *self) {
    if (self == nullptr || out == nullptr || outCap == 0u)
        return -1;
    const char *src = (*self).name;
    size_t len = strlen(src);
    if (len + 1u > outCap)
        return -1;
    memcpy(out, src, len + 1u);
    return (int) len;
}

// Return the callable, or nullptr for a null Method.
MethodFn Method_getInvoke(const Method *self) {
    if (self == nullptr)
        return nullptr;
    return (*self).invoke;
}

void *Method_getTarget(const Method *self) {
    if (self == nullptr)
        return nullptr;
    return (*self).target;
}

// STRING PROJECTIONS (the toString Law)

void Method_toString(const Method *self, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (dest == nullptr || cap == 0u)
        return;
    if (self == nullptr) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    int written = snprintf(dest, cap, "Method(%s)", (*self).name[0] == '\0' ? "?" : (*self).name);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

// Format the Method's own fields into a bounded structure projection.
void Method_toStringStruct(const Method *self, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (dest == nullptr || cap == 0u)
        return;
    if (self == nullptr) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    int written = snprintf(dest, cap, "Method { name=\"%s\", invoke=0x%llx, target=0x%llx }",
                           (*self).name,
                           (unsigned long long) (uintptr_t) (*self).invoke,
                           (unsigned long long) (uintptr_t) (*self).target);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}
