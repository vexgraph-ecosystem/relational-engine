#ifndef REFLECTION_CLASS_H
#define REFLECTION_CLASS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "reflection/method.h"
#include "reflection/struct.h"
#include "struct/chunked_list.h"

// reflection/class.h — the Class metadata record (a Struct layout + Methods).
//
// A Class is the top of the reflection hierarchy: a folded name, a constructor,
// a Struct LAYOUT (its fields), and a list of Methods — "class { struct of the
// class; methods }". Methods live in a never-moved ChunkedList; the layout is
// borrowed (owned by its creator). The kind is the header typeId
// (TYPE_REFLECT_CLASS). Block layout:
// [MemoryHeader 16][name 24][construct 8][layout 8][methods 8][methodCount 4][pad 4].

#define REFLECT_CLASS_NAME_BYTES VARIABLE_SLOT_NAME_BYTES
#define REFLECT_CLASS_NAME_MAX VARIABLE_SLOT_NAME_MAX
#define REFLECT_CLASS_METHOD_NONE UINT32_MAX

typedef void *(*ClassConstructFn)(void *);

typedef struct Class {
    char name[REFLECT_CLASS_NAME_BYTES]; // folded name (atom grammar)
    ClassConstructFn construct;          // the constructor
    Struct *layout;                      // the class's field layout (borrowed)
    ChunkedList *methods;                // never-moved Method rows
    uint32_t methodCount;                // live methods (== row count)
    uint32_t pad;                        // explicit padding
} Class;

_Static_assert(sizeof(Class) == 56u, "Class must stay 56 Bytes");

bool Class_init(Class *self, const char *name, Struct *layout, ClassConstructFn construct);
Class *Class_0(void);
Class *Class_1(const char *name);
Class *Class_2(const char *name, Struct *layout);
Class *Class_3(const char *name, Struct *layout, ClassConstructFn construct);
#define Class(...) CONSTRUCTOR_DISPATCH(Class, __VA_ARGS__)
void Class_free(Class *self);

uint64_t Class_kind(const Class *self);
bool Class_check(const Class *self, uint64_t typeId);
void *Class_construct(Class *self, void *arg);

// The class's field layout (borrowed; may be null).
Struct *Class_getLayout(const Class *self);
void Class_setLayout(Class *self, Struct *layout);
ClassConstructFn Class_getConstruct(const Class *self);
void Class_setConstruct(Class *self, ClassConstructFn construct);

// Append a copy of a method. Returns its index, or REFLECT_CLASS_METHOD_NONE.
uint32_t Class_addMethod(Class *self, const Method *method);
Method *Class_getMethod(const Class *self, uint32_t index);
uint32_t Class_methodCount(const Class *self);
typedef void (*ClassMethodVisitFn)(Method *method, uint32_t index, void *userdata);
void Class_forEachMethod(Class *self, ClassMethodVisitFn fn, void *userdata);

bool Class_setName(Class *self, const char *name);
int Class_getName(char *out, size_t outCap, const Class *self);

void Class_toString(const Class *self, char *dest, size_t cap, bool *outTruncated);
void Class_toStringStruct(const Class *self, char *dest, size_t cap, bool *outTruncated);

#endif
