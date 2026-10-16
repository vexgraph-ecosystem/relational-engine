#ifndef REFLECTION_FIELD_H
#define REFLECTION_FIELD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "reflection/variable.h"

// reflection/field.h — the Field metadata record.
//
// A Field IS a Variable plus a setter: it EMBEDS a Variable (name + reader +
// target) and adds the write behavior. "field { variable }" made literal — the
// name, reader and target all live in the embedded Variable; the field's own
// Bytes are just that Variable plus its setter. The kind is the header typeId
// (TYPE_REFLECT_FIELD). Block layout:
// [MemoryHeader 16][Variable 40][set 8] = 64 Bytes.

typedef void (*FieldSetFn)(void *, void *);

// A Field's name is the embedded Variable's name (same 24-byte grammar).
#define REFLECT_FIELD_NAME_BYTES REFLECT_VARIABLE_NAME_BYTES
#define REFLECT_FIELD_NAME_MAX REFLECT_VARIABLE_NAME_MAX

typedef struct Field {
    Variable variable; // the embedded name + reader + target
    FieldSetFn set;    // the setter
} Field;

_Static_assert(sizeof(Field) == 48u, "Field must stay 48 Bytes");

bool Field_init(Field *self, const char *name, VariableReadFn read, FieldSetFn set, void *target);
Field *Field_0(void);
Field *Field_1(const char *name);
Field *Field_2(const char *name, VariableReadFn read);
Field *Field_3(const char *name, VariableReadFn read, FieldSetFn set);
Field *Field_4(const char *name, VariableReadFn read, FieldSetFn set, void *target);
#define Field(...) CONSTRUCTOR_DISPATCH(Field, __VA_ARGS__)
void Field_free(Field *self);

uint64_t Field_kind(const Field *self);
bool Field_check(const Field *self, uint64_t typeId);
// The embedded Variable (borrowed; lifetime == the field's).
Variable *Field_getVariable(Field *self);
// Read / write through the embedded reader and the setter (null-safe).
void *Field_read(Field *self, void *receiver);
void Field_write(Field *self, void *receiver, void *value);

bool Field_setName(Field *self, const char *name);
int Field_getName(char *out, size_t outCap, const Field *self);
void Field_setRead(Field *self, VariableReadFn read);
VariableReadFn Field_getRead(const Field *self);
void Field_setSet(Field *self, FieldSetFn set);
FieldSetFn Field_getSet(const Field *self);
void Field_setTarget(Field *self, void *target);
void *Field_getTarget(const Field *self);

void Field_toString(const Field *self, char *dest, size_t cap, bool *outTruncated);
void Field_toStringStruct(const Field *self, char *dest, size_t cap, bool *outTruncated);

#endif
