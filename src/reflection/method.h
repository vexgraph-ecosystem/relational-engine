#ifndef REFLECTION_METHOD_H
#define REFLECTION_METHOD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "relational/variable_slot.h"

// reflection/method.h — the Method metadata record.
//
// A Method is a folded name (the atom's grammar), a callable
// (void* (*)(void*)), and a target (the owner / default receiver). A Class
// owns a list of these. The kind is the header typeId (TYPE_REFLECT_METHOD).
// Block layout: [MemoryHeader 16][name 24][invoke 8][target 8] = 40 Bytes.
//
// COLD PATH ONLY (the Cold-Only Reflection Law).

#define REFLECT_METHOD_NAME_BYTES VARIABLE_SLOT_NAME_BYTES
#define REFLECT_METHOD_NAME_MAX VARIABLE_SLOT_NAME_MAX

typedef void *(*MethodFn)(void *);

typedef struct Method {
    char name[REFLECT_METHOD_NAME_BYTES]; // folded name (atom grammar)
    MethodFn invoke;                      // the callable
    void *target;                         // the owner / default receiver
} Method;

_Static_assert(sizeof(Method) == 40u, "Method must stay 40 Bytes");

bool Method_init(Method *self, const char *name, MethodFn invoke, void *target);
Method *Method_0(void);
Method *Method_1(const char *name);
Method *Method_2(const char *name, MethodFn invoke);
Method *Method_3(const char *name, MethodFn invoke, void *target);
#define Method(...) CONSTRUCTOR_DISPATCH(Method, __VA_ARGS__)
void Method_free(Method *self);

uint64_t Method_kind(const Method *self);
bool Method_check(const Method *self, uint64_t typeId);
void *Method_call(Method *self, void *arg);

bool Method_setName(Method *self, const char *name);
int Method_getName(char *out, size_t outCap, const Method *self);
void Method_setInvoke(Method *self, MethodFn invoke);
MethodFn Method_getInvoke(const Method *self);
void Method_setTarget(Method *self, void *target);
void *Method_getTarget(const Method *self);

void Method_toString(const Method *self, char *dest, size_t cap, bool *outTruncated);
void Method_toStringStruct(const Method *self, char *dest, size_t cap, bool *outTruncated);

#endif
