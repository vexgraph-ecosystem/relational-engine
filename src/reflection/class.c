// reflection/class.c — the Class metadata record ("class { struct; methods }").

#include "reflection/class.h"

#include <stdio.h>
#include <string.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "nio/mem.h"
#include "oop/type.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Class
 * ============================================================================
 * The top of the reflection hierarchy: a folded name, a constructor, a Struct
 * LAYOUT (its fields, borrowed), and a list of Methods held in a never-moved
 * ChunkedList. "class { struct of the class; methods }". The kind is the header
 * typeId (TYPE_REFLECT_CLASS). Class IS the generic kind — there is no separate
 * Generic record.
 *
 * Lifetime: arena-allocated (TYPE_REFLECT_CLASS); free releases the method list
 * but never the borrowed layout. Cold path only (the Cold-Only Reflection Law);
 * getters are null-safe.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Class (reflection/class.c)
 * ============================================================================
 * the Class metadata record (name + construct + Struct layout + Methods).
 *
 * STRUCT FIELDS (Mirroring reflection/class.h):
 * ----------------------------------------------------------------------------
 *   Class {
 *     char name[24];         // folded name (atom grammar)
 *     ClassConstructFn construct; // the constructor
 *     Struct *layout;        // the class's field layout (borrowed)
 *     ChunkedList *methods;  // never-moved Method rows
 *     uint32_t methodCount;  // live methods (== row count)
 *     uint32_t pad;          // explicit padding
 *   }
 *
 * PRIVATE HELPERS (kept file-local, pure logic, no behavior of their own):
 *   ensureMethods(self)  // mint the method list lazily (static)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h) Class_0() .. _3(name, layout, construct), _init, _free
 * Public Core Functions: (.h) Class_kind, _check, _construct, _addMethod, _getMethod, _forEachMethod
 * Public Setters: (.h) Class_setName / _setLayout / _setConstruct
 * Public Getters: (.h) Class_getName(out, cap, self) / _getLayout / _getConstruct / _methodCount
 * Public String Projections: (.h) Class_toString / _toStringStruct
 * ============================================================================
 */

// Lazily create the stable method-row list owned by this Class.
static bool ensureMethods(Class *self) {
    if ((*self).methods)
        return true;
    ChunkedList *methods = ChunkedList(ID_REFLECT_METHOD, (uint32_t) sizeof(Method), CHUNKED_LIST_TWO_LAYER_DEFAULT);
    if (!methods)
        return false;
    (*self).methods = methods;
    return true;
}

// CONSTRUCTORS

bool Class_init(Class *self, const char *name, Struct *layout, ClassConstructFn construct) {
    if (self == nullptr)
        return false;
    char folded[REFLECT_CLASS_NAME_BYTES];
    if (!VariableSlot_foldName(name, folded))
        return false;
    memset(self, 0, sizeof(*self));
    memcpy((*self).name, folded, strlen(folded) + 1);
    (*self).construct = construct;
    (*self).layout = layout;
    return ensureMethods(self);
}

Class *Class_0(void) {
    Class *self = (Class*) Memory_alloc(TYPE_REFLECT_CLASS, sizeof(Class));
    if (self == nullptr)
        return nullptr;
    memset(self, 0, sizeof(*self));
    if (!ensureMethods(self)) {
        Memory_free(self);
        return nullptr;
    }
    return self;
}

Class *Class_1(const char *name) {
    return Class_3(name, nullptr, nullptr);
}

Class *Class_2(const char *name, Struct *layout) {
    return Class_3(name, layout, nullptr);
}

Class *Class_3(const char *name, Struct *layout, ClassConstructFn construct) {
    Class *self = (Class*) Memory_alloc(TYPE_REFLECT_CLASS, sizeof(Class));
    if (self == nullptr)
        return nullptr;
    if (!Class_init(self, name, layout, construct)) {
        Memory_free(self);
        return nullptr;
    }
    return self;
}

// Release owned method rows and the arena-allocated Class record.
void Class_free(Class *self) {
    if (self == nullptr)
        return;
    if ((*self).methods)
        ChunkedList_free((*self).methods);
    Memory_free(self);
}

// CORE FUNCTIONS

uint64_t Class_kind(const Class *self) {
    if (self == nullptr)
        return 0u;
    return Memory_type((void*) self);
}

// Compare the allocation's recorded type with the requested reflection kind.
bool Class_check(const Class *self, uint64_t typeId) {
    if (self == nullptr)
        return false;
    return Memory_type((void*) self) == typeId;
}

void *Class_construct(Class *self, void *arg) {
    if (self == nullptr || (*self).construct == nullptr)
        return nullptr;
    return (*self).construct(arg);
}

// Copy a method into the stable list and return its assigned index.
uint32_t Class_addMethod(Class *self, const Method *method) {
    if (self == nullptr || method == nullptr || (*self).methods == nullptr)
        return REFLECT_CLASS_METHOD_NONE;
    Method *row = (Method*) ChunkedList_addSlot((*self).methods);
    if (row == nullptr)
        return REFLECT_CLASS_METHOD_NONE;
    memcpy(row, method, sizeof(Method));
    uint32_t index = ChunkedList_size((*self).methods) - 1u;
    (*self).methodCount = index + 1u;
    return index;
}

Method *Class_getMethod(const Class *self, uint32_t index) {
    if (self == nullptr || (*self).methods == nullptr)
        return nullptr;
    if (index >= (*self).methodCount)
        return nullptr;
    return (Method*) ChunkedList_slot((*self).methods, index);
}

// Visit each extant method row in index order with caller context.
void Class_forEachMethod(Class *self, ClassMethodVisitFn fn, void *userdata) {
    if (self == nullptr || fn == nullptr || (*self).methods == nullptr)
        return;
    uint32_t n = (*self).methodCount;
    for (uint32_t i = 0u; i < n; i++) {
        Method *row = (Method*) ChunkedList_slot((*self).methods, i);
        if (row)
            fn(row, i, userdata);
    }
}

// SETTERS

bool Class_setName(Class *self, const char *name) {
    if (self == nullptr)
        return false;
    char folded[REFLECT_CLASS_NAME_BYTES];
    if (!VariableSlot_foldName(name, folded))
        return false;
    memset((*self).name, 0, sizeof((*self).name));
    memcpy((*self).name, folded, strlen(folded) + 1);
    return true;
}

// Replace the borrowed Struct layout associated with this Class.
void Class_setLayout(Class *self, Struct *layout) {
    if (self == nullptr)
        return;
    (*self).layout = layout;
}

// Replace the constructor callback associated with this Class.
void Class_setConstruct(Class *self, ClassConstructFn construct) {
    if (self == nullptr)
        return;
    (*self).construct = construct;
}

// GETTERS

Struct *Class_getLayout(const Class *self) {
    if (self == nullptr)
        return nullptr;
    return (*self).layout;
}

// Return the constructor callback, or nullptr for a null Class.
ClassConstructFn Class_getConstruct(const Class *self) {
    if (self == nullptr)
        return nullptr;
    return (*self).construct;
}

// Copy the folded class name when the complete NUL-terminated value fits.
int Class_getName(char *out, size_t outCap, const Class *self) {
    if (self == nullptr || out == nullptr || outCap == 0u)
        return -1;
    const char *src = (*self).name;
    size_t len = strlen(src);
    if (len + 1u > outCap)
        return -1;
    memcpy(out, src, len + 1u);
    return (int) len;
}

// Return the number of methods currently stored on this Class.
uint32_t Class_methodCount(const Class *self) {
    return self ? (*self).methodCount : 0u;
}

// STRING PROJECTIONS (the toString Law)

void Class_toString(const Class *self, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (dest == nullptr || cap == 0u)
        return;
    if (self == nullptr) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    int written = snprintf(dest, cap, "Class(%s, methods=%u)",
                           (*self).name[0] == '\0' ? "?" : (*self).name, (*self).methodCount);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}

// Format the Class's own fields into a bounded structure projection.
void Class_toStringStruct(const Class *self, char *dest, size_t cap, bool *outTruncated) {
    if (outTruncated)
        *outTruncated = false;
    if (dest == nullptr || cap == 0u)
        return;
    if (self == nullptr) {
        snprintf(dest, cap, "nullptr");
        return;
    }
    int written = snprintf(dest, cap, "Class { name=\"%s\", construct=0x%llx, layout=0x%llx, methods=0x%llx, methodCount=%u }",
                           (*self).name,
                           (unsigned long long) (uintptr_t) (*self).construct,
                           (unsigned long long) (uintptr_t) (*self).layout,
                           (unsigned long long) (uintptr_t) (*self).methods,
                           (*self).methodCount);
    if (written < 0 || (size_t) written >= cap) {
        if (outTruncated)
            *outTruncated = true;
    }
}
