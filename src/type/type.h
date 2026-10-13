#ifndef TYPE_TYPE_H
#define TYPE_TYPE_H

#include <stdbool.h>
#include <stdint.h>

// type/type.h — the R2 type-id ALGEBRA (Relational Engine owner).
//
// The project-agnostic half of the One Type Registry Law: the 64-bit id
// encoding, its masks, and the pure readers. Each repository keeps its OWN
// class registry (its ID_*/TYPE_* macros) in its own *-type.h; only the
// algebra below is shared. The 16-byte block-header contract lives with the
// allocator in nio/mem.h (MemoryHeader); the old vexspoke TypeHeader was dead
// and is removed. This header must never include a project registry, or the
// registry that includes it would recurse.
//
//     0xF'PRPR'M'W1'W2'BE6C'CCCCCC
// Bits: 4 + 8 + 4 + 4 + 4 + 16 + 24 = 64.
//
//      Field map:
//      0xF PRPR M W1 W2 BE6C CCCCCC
//        | |    | |  |  |    `-------- class      (24 bits, which struct, per-project)
//        | |    | |  |  `------------- sugar      ("векс" / vex)
//        | |    | |  `---------------- wrapper 2  (probable/future/choice)
//        | |    | `------------------- wrapper 1  (proactive/reactive)
//        | |    `--------------------- modifier   (global/locale/transient)
//        | `-------------------------- project    (8 bits, owning repo, 256 projects)
//        `--------------------------- form       (singleton/array/..., struct layouts)
//
// INTENTIONAL(vex): BE6C is the author's stylized Cyrillic "векс" / vex
// signature, stamped as recognizable sugar in every id. It is a fixed format
// marker, not a checksum or a stale-generation check. A [typeId:8]
// [pointer-or-inline-value:8] slot therefore stays 16 Bytes.
//
// Uniform per-project numbering: every project numbers its classes starting at
// 1; the project byte disambiguates identical local numbers across repos. Bare
// ID_* constants carry no project byte and name VEXSPOKE's own class space
// only — cross-project dispatch ships full type ids (PROJ_* | FORM_* | class),
// and Type_arch reads the byte back.
//
// The parent-chain resolver (Type_registerParents / Type_getParentClass /
// Type_isA) is project-agnostic: a project registers its chain once (keyed by
// its project byte) and this file walks it. Vexspoke's own legacy bare-id
// chain is registered through Type_registerBareParents, so this algebra never
// hardcodes a project's class numbers.

#define MASK_FORM       0xF'00'0'0'0'0000'000000ULL
#define MASK_PROJECT    0x0'FF'0'0'0'0000'000000ULL
#define MASK_MODIFIER   0x0'00'F'0'0'0000'000000ULL
#define MASK_WRAPPER_1  0x0'00'0'F'0'0000'000000ULL
#define MASK_WRAPPER_2  0x0'00'0'0'F'0000'000000ULL
#define MASK_SUGAR      0x0'00'0'0'0'FFFF'000000ULL
#define SUGAR_VEX       0x0'00'0'0'0'BE6C'000000ULL // INTENTIONAL(vex) "векс"
#define MASK_CLASS      0x0'00'0'0'0'0000'FFFFFFULL

#define FORM_SINGLETON          0x1'00'0'0'0'0000'000000ULL
#define FORM_ARRAY              0x2'00'0'0'0'0000'000000ULL
#define FORM_POINTER            0x3'00'0'0'0'0000'000000ULL
#define FORM_STRUCT_SINGLETON   0x4'00'0'0'0'0000'000000ULL
#define FORM_STRUCT_ARRAY       0x5'00'0'0'0'0000'000000ULL
#define FORM_STRUCT_POINTER     0x6'00'0'0'0'0000'000000ULL
#define FORM_ARRAY_SOA          0x7'00'0'0'0'0000'000000ULL
#define FORM_ARRAY_AOS          0x8'00'0'0'0'0000'000000ULL
#define FORM_STRUCT_COEXISTENT  0x9'00'0'0'0'0000'000000ULL

// Project byte: which repo owns the class. 256 projects fit the same stack.
#define PROJ_GENERIC             0x0'00'0'0'0'0000'000000ULL  // zero lol
#define PROJ_VEXSPOKE            0x0'01'0'0'0'0000'000000ULL
#define PROJ_GRAPHVEX            0x0'02'0'0'0'0000'000000ULL
#define PROJ_HOTCWAP             0x0'03'0'0'0'0000'000000ULL
#define PROJ_DARLING             0x0'04'0'0'0'0000'000000ULL
#define PROJ_API_HAVEN           0x0'05'0'0'0'0000'000000ULL
#define PROJ_DARKBASE            0x0'06'0'0'0'0000'000000ULL
#define PROJ_RELATIONAL_ENGINE   0x0'07'0'0'0'0000'000000ULL
#define PROJ_SAMPLERATE          0x0'08'0'0'0'0000'000000ULL

#define MOD_GLOBAL     0x0'00'1'0'0'0000'000000ULL
#define MOD_LOCALE     0x0'00'2'0'0'0000'000000ULL
#define MOD_TRANSIENT  0x0'00'3'0'0'0000'000000ULL

#define WRAP_PROACTIVE  0x0'00'0'1'0'0000'000000ULL
#define WRAP_REACTIVE   0x0'00'0'2'0'0000'000000ULL

#define WRAP2_PROBABLE          0x0'00'0'0'1'0000'000000ULL
#define WRAP2_PROBABLE_OBJECTS  0x0'00'0'0'2'0000'000000ULL
#define WRAP2_FUTURE            0x0'00'0'0'3'0000'000000ULL
#define WRAP2_CHOICE            0x0'00'0'0'4'0000'000000ULL

// Architecture layer owning an id (ARCH_* — one per project byte). Bare ids
// report ARCH_VEXSPOKE, since bare numbers always mean vexspoke's class space.
#define ARCH_VEXSPOKE  1u
#define ARCH_HOTCWAP   2u
#define ARCH_DARLING   3u
#define ARCH_GRAPHVEX  4u
#define ARCH_APIHAVEN  5u
#define ARCH_DARKBASE  6u
#define ARCH_SAMPLERATE 7u

// Compose a full type id from project + form + class id. Every id carries the
// reserved BE6C "векс" sugar (the standard encoding).
static inline uint64_t Type_make(uint64_t proj, uint64_t form, uint32_t classId) {
    return (proj & MASK_PROJECT) | (form & MASK_FORM) | SUGAR_VEX |
           (classId & MASK_CLASS);
}

static inline uint64_t Type_class(uint64_t typeId) {
    return typeId & MASK_CLASS;
}

static inline uint64_t Type_form(uint64_t typeId) {
    return typeId & MASK_FORM;
}

static inline uint64_t Type_project(uint64_t typeId) {
    return typeId & MASK_PROJECT;
}

static inline int Type_isStruct(uint64_t form) {
    return form == FORM_STRUCT_SINGLETON || form == FORM_STRUCT_ARRAY
        || form == FORM_STRUCT_POINTER;
}

static inline int Type_isSingleton(uint64_t typeId) {
    return (typeId & MASK_FORM) == FORM_SINGLETON;
}

static inline int Type_isArray(uint64_t typeId) {
    uint64_t form = typeId & MASK_FORM;
    return form == FORM_ARRAY || form == FORM_ARRAY_SOA || form == FORM_ARRAY_AOS || form == FORM_STRUCT_COEXISTENT;
}

static inline int Type_isPointer(uint64_t typeId) {
    return (typeId & MASK_FORM) == FORM_POINTER;
}

static inline int Type_isStructSingleton(uint64_t typeId) {
    return (typeId & MASK_FORM) == FORM_STRUCT_SINGLETON;
}

static inline int Type_isStructArray(uint64_t typeId) {
    uint64_t form = typeId & MASK_FORM;
    return form == FORM_STRUCT_ARRAY || form == FORM_ARRAY_SOA || form == FORM_ARRAY_AOS || form == FORM_STRUCT_COEXISTENT;
}

static inline int Type_isStructSOA(uint64_t typeId) {
    return (typeId & MASK_FORM) == FORM_ARRAY_SOA;
}

static inline int Type_isStructAOS(uint64_t typeId) {
    return (typeId & MASK_FORM) == FORM_ARRAY_AOS;
}

static inline int Type_isStructCoexistent(uint64_t typeId) {
    return (typeId & MASK_FORM) == FORM_STRUCT_COEXISTENT;
}

static inline int Type_isStructPointer(uint64_t typeId) {
    return (typeId & MASK_FORM) == FORM_STRUCT_POINTER;
}

static inline int Type_isPrimitive(uint64_t typeId) {
    uint64_t form = typeId & MASK_FORM;
    return form == FORM_SINGLETON || form == FORM_ARRAY || form == FORM_POINTER;
}

static inline int Type_isGlobal(uint64_t typeId) {
    return (typeId & MASK_MODIFIER) == MOD_GLOBAL;
}

static inline int Type_isLocale(uint64_t typeId) {
    return (typeId & MASK_MODIFIER) == MOD_LOCALE;
}

static inline int Type_isTransient(uint64_t typeId) {
    return (typeId & MASK_MODIFIER) == MOD_TRANSIENT;
}

static inline int Type_isProactive(uint64_t typeId) {
    return (typeId & MASK_WRAPPER_1) == WRAP_PROACTIVE;
}

static inline int Type_isReactive(uint64_t typeId) {
    return (typeId & MASK_WRAPPER_1) == WRAP_REACTIVE;
}

static inline int Type_isProbable(uint64_t typeId) {
    return (typeId & MASK_WRAPPER_2) == WRAP2_PROBABLE;
}

static inline int Type_isProbableObjects(uint64_t typeId) {
    return (typeId & MASK_WRAPPER_2) == WRAP2_PROBABLE_OBJECTS;
}

static inline int Type_isFuture(uint64_t typeId) {
    return (typeId & MASK_WRAPPER_2) == WRAP2_FUTURE;
}

static inline int Type_isChoice(uint64_t typeId) {
    return (typeId & MASK_WRAPPER_2) == WRAP2_CHOICE;
}

// Architecture layer owning an id (reads the project byte).
uint64_t Type_arch(uint64_t classId);

static inline int Type_isVexspoke(uint64_t classId) {
    return Type_arch(classId) == ARCH_VEXSPOKE;
}

static inline int Type_isHotcwap(uint64_t classId) {
    return Type_arch(classId) == ARCH_HOTCWAP;
}

static inline int Type_isDarling(uint64_t classId) {
    return Type_arch(classId) == ARCH_DARLING;
}

static inline int Type_isDarkbase(uint64_t classId) {
    return Type_arch(classId) == ARCH_DARKBASE;
}

// Register a project's parent table: parents[i] = parent class number of class
// number i, 0 = root (the class is its own parent). Row 0 (the entry for class
// 0) is unused. Class number indexes the row; the project byte picks the table,
// so the same number space means different things in every repo. Registration
// is idempotent (re-registering replaces its table). Rejects invalid proj
// (zero, non-project bits, PROJ_VEXSPOKE), a (nullptr, count != 0) pair, an
// out-of-range parent entry, a CYCLIC or self-referential chain, and any
// allocation failure; every rejection preserves the previous table.
bool Type_registerParents(uint64_t proj, const uint32_t *parents, uint32_t count);

// Register the shared BARE-id / PROJ_VEXSPOKE chain. Bare ids name vexspoke's
// own class space; vexspoke grants its legacy chain here once so this algebra
// never hardcodes a project's class numbers. Same validation and atomicity as
// Type_registerParents.
bool Type_registerBareParents(const uint32_t *parents, uint32_t count);

// Parent-class walk. Bare id or PROJ_VEXSPOKE class -> the registered bare
// chain (row 0 = root); a registered downstream project -> that project's
// table; an unregistered project byte (or unlisted class) -> root. Returns the
// parent class id, or the class id itself when the class is a root.
uint64_t Type_getParentClass(uint64_t classId);

// True if classId is ancestorId or any descendant of it. Walks the parent chain
// WITH the project byte intact; the ancestor target is masked to class only, so
// the walk stays project-invariant and full/bare ids mix freely (pinned by the
// owner test). The walk is BOUNDED by the project's chain length, so a cyclic
// table can never spin (the Bounded Wait Law).
int Type_isA(uint64_t classId, uint64_t ancestorId);

#endif
