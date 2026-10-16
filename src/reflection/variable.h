#ifndef REFLECTION_VARIABLE_H
#define REFLECTION_VARIABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "relational/variable_slot.h"

// reflection/variable.h — the Variable metadata record (base of the hierarchy).
//
// The smallest reflection kind: a folded name (the atom's 24-byte grammar), a
// reader (void* (*)(void*)), and a target (the value / owner). A Field embeds
// one; a Struct is a list of Fields; a Class owns a Struct. The kind is the
// header typeId (TYPE_REFLECT_VARIABLE). Block layout:
// [MemoryHeader 16][name 24][read 8][target 8] = 40 Bytes.
//
// COLD PATH ONLY (the Cold-Only Reflection Law).

#define REFLECT_VARIABLE_NAME_BYTES VARIABLE_SLOT_NAME_BYTES
#define REFLECT_VARIABLE_NAME_MAX VARIABLE_SLOT_NAME_MAX

typedef void *(*VariableReadFn)(void *);

typedef struct Variable {
    char name[REFLECT_VARIABLE_NAME_BYTES]; // folded name (atom grammar)
    VariableReadFn read;                    // the reader
    void *target;                           // the value / owner
} Variable;

_Static_assert(sizeof(Variable) == 40u, "Variable must stay 40 Bytes");

bool Variable_init(Variable *self, const char *name, VariableReadFn read, void *target);
Variable *Variable_0(void);
Variable *Variable_1(const char *name);
Variable *Variable_2(const char *name, VariableReadFn read);
Variable *Variable_3(const char *name, VariableReadFn read, void *target);
#define Variable(...) CONSTRUCTOR_DISPATCH(Variable, __VA_ARGS__)
void Variable_free(Variable *self);

uint64_t Variable_kind(const Variable *self);
bool Variable_check(const Variable *self, uint64_t typeId);
void *Variable_read(Variable *self, void *arg);

bool Variable_setName(Variable *self, const char *name);
int Variable_getName(char *out, size_t outCap, const Variable *self);
void Variable_setRead(Variable *self, VariableReadFn read);
VariableReadFn Variable_getRead(const Variable *self);
void Variable_setTarget(Variable *self, void *target);
void *Variable_getTarget(const Variable *self);

void Variable_toString(const Variable *self, char *dest, size_t cap, bool *outTruncated);
void Variable_toStringStruct(const Variable *self, char *dest, size_t cap, bool *outTruncated);

#endif
