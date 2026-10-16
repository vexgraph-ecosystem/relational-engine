#ifndef RELATIONAL_RELATIONAL_H
#define RELATIONAL_RELATIONAL_H

#include <stdint.h>

#include "relational/symbol_table.h"

// relational/relational.h — spotlight relational facade over SymbolTable (Legacy: relational/RelationalEngine.java).
//
// RELATIONAL ENGINE PHILOSOPHY: this engine refuses the textbook trilemma —
// textbook OOP vs textbook DOD vs textbook ECS — because none of them, as
// taught, answers the question it exists for: "find the thing called X,
// right now, from anywhere." The argument is with formula-following, not
// the underlying ideas (hot iteration stays DOD where it belongs — see
// below); a book pattern applied without asking what question it answers
// is how systems rot from the inside.
//
//   Textbook OOP binds names at COMPILE time (identifiers vanish into addresses)
//     and hides state behind encapsulation. At runtime nothing is findable
//     except by walking graph you must already hold. Query cost: O(graph).
//
//   Textbook DOD answers "process everything fast" (sweeps over flat
//     arrays). It never answers "find one thing now" — you rebuild that per
//     case, usually as a shadow naming system that drifts. The engine does
//     not compete: hot iteration stays DOD (scene graph, SoA physics);
//     cold rendezvous comes here. Complementary axes.
//
//   Textbook ECS answers "all entities with [A,B,C]" — sets by signature,
//     entities as numbers. It never answers "the thing called
//     character.position.x" without a bolted-on name table, i.e. this engine
//     reinvented badly. ECS shards values for systems slicing; the engine
//     maps names to whole values for authors addressing things. Different
//     questions.
//
// The thesis: OOP names things for the compiler, ECS numbers things for the
// scheduler — the relational engine names things for everyone at runtime
// (authors, debuggers, scripts, search boxes, hot-swap, telemetry), with
// O(log n) name resolution plus O(1) slot->row->value hops and O(n) class
// filters. One primitive (name => value, globally findable) underlies N
// features — spotlight, live inspectors, script binding, save/load walks,
// swap rebinding — instead of N bespoke lookup systems.
//
// CONSTRUCTOR VS SET (the pool-uniqueness law): the pool holds each name
// exactly once, so construction is create-or-FAIL — instant() on a taken
// name prints and yields -1, never updates, never duplicates. Changing a
// value is setPointer()/setValue()'s job, never the constructor's: create
// brings things into being, set mutates what exists, and confusing the two
// is how stale entries resurrect under typos. Rename is its own explicit
// operation with the same collision rule.
//
// Honest costs, paid deliberately:
//   - Runtime names mean runtime typos (the compiler stops checking so the
//     runtime can start finding). Paid with: strict charset, fail-closed
//     lookups, class tags pinned at creation, loud constructor errors.
//   - A global writable namespace invites spooky action. Paid with scopes:
//     search spans global+local, mutation stays scope-local, and the strict
//     constructor refuses silent resurrection.
//   - No overclaim: gather-by-name is O(log n), not O(1) end to end; class
//     filters are O(n) integer scans for cold sweeps only. Hot per-frame
//     typed iteration belongs to scene graph, never here.
//
// Two SymbolTable tables are the scopes: global and local. Every symbol is a row
// name => (classId, targetPointer). The pointer is the value — a string block,
// a typed struct, a Map, or a function address. Search is the spotlight: query
// "health" returns health, health_ui, health_progress_bar, hp_text, etc.

// Exact lookup: name => varId, or -1 if absent.
int32_t Relational_getId(SymbolTable *scope, const char *name);

// Name accessors for search/rename. varId must be valid.
const char *Relational_getName(const SymbolTable *scope, int32_t varId);
bool Relational_setName(SymbolTable *scope, const char *oldName, const char *newName);

// Value accessors — store as-is, no copy. classId describes the pointer type.
void *Relational_getValue(SymbolTable *scope, const char *name);
void *Relational_getValueById(SymbolTable *scope, int32_t varId);
bool Relational_setValue(SymbolTable *scope, const char *name, uint32_t classId, void *ptr);
bool Relational_setValueById(SymbolTable *scope, int32_t varId, void *ptr);

// String sugar — value is a string block (TYPE_STRING_ARRAY). Old block is freed on set.
const char *Relational_getString(SymbolTable *scope, const char *name);
void Relational_setString(SymbolTable *scope, const char *name, const char *value);

// Function pointers — same as value, typed as void (*)(void*).
void *Relational_getFunction(SymbolTable *scope, const char *name);
bool Relational_setFunction(SymbolTable *scope, const char *name, void *fn);

// Spotlight search: query substring (case-insensitive) over all names in scope.
// Fills outIds[cap] with matching varIds ranked exact > prefix > substring.
// Returns the number of matches (may exceed cap; only cap are written).
size_t Relational_search(SymbolTable *scope, const char *query, int32_t *outIds, size_t cap);
size_t Relational_searchAll(SymbolTable *global, SymbolTable *local, const char *query, int32_t *outIds, size_t cap);

#endif
