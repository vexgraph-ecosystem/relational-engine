#ifndef RELATIONAL_SHELF_H
#define RELATIONAL_SHELF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c23/constructor.h"
#include "struct/chunked_list.h"

// relational/shelf.h — the global shelf: a node pool with u32 graph edges.
//
// The shelf is the Relational Engine's "linked list inside an arraylist": nodes
// live in a never-moved ChunkedList (so a node's address is stable for life),
// and the graph edges are 32-bit INDICES into that list — data-oriented and
// relocatable, not invisible pointer chases. Each node references one identity
// Cell, allocated separately so it carries its own 32-byte header; the shelf is
// the indexable pool the reflective walk traverses.
//
// COLD PATH ONLY (the Cold-Only Reflection Law): the shelf is the rendezvous
// walked by search/debug/script/hot-swap, never a per-frame structure. See the
// `;;INTENTION` marker on the walk in the resolver.

#define SHELF_INDEX_NONE UINT32_MAX

// SLOT RECORD (owned by Shelf, behaviorless): one shelf node.
typedef struct ShelfNode {
    uintptr_t cell; // the identity cell this node references (0 when bare)
    uint32_t next;  // graph edge: u32 index of the next node (SHELF_INDEX_NONE = end)
    uint32_t pad;   // explicit padding (keeps the row 16 Bytes)
} ShelfNode;

typedef struct Shelf {
    bool active;        // runtime-active flag
    uint32_t count;     // live nodes (== row count)
    uint32_t head;      // first node index (SHELF_INDEX_NONE when empty)
    ChunkedList *nodes; // never-moved ShelfNode rows (u32 edges)
} Shelf;

// --- Constructors ---
// Set up an embedded shelf (mints its node list). False on a null receiver/OOM.
bool Shelf_init(Shelf *shelf);
// Release every referenced cell + the node list. Safe twice; never frees the
// receiver (embeddable).
void Shelf_shutdown(Shelf *shelf);

// Arena-allocated convenience (the Arity and Constructive Convenience Law).
Shelf *Shelf_0(void);
#define Shelf(...) CONSTRUCTOR_DISPATCH(Shelf, __VA_ARGS__)
// Shut down and release an arena-allocated shelf (a constructor result).
void Shelf_free(Shelf *shelf);

// --- Core functions ---
// Append a node referencing a FRESH identity cell of `typeId` (the shelf owns
// it). Returns the node index, or SHELF_INDEX_NONE on failure.
uint32_t Shelf_addCell(Shelf *shelf, uint64_t typeId, uintptr_t value);
// Append a node adopting an existing cell pointer (ownership transfers: the
// shelf frees it on shutdown). Returns the node index.
uint32_t Shelf_addNode(Shelf *shelf, uintptr_t cell);

// The identity cell a node references (0 on a bad index).
uintptr_t Shelf_getCell(const Shelf *shelf, uint32_t index);
// The node's outgoing graph edge (SHELF_INDEX_NONE at the end).
uint32_t Shelf_getNext(const Shelf *shelf, uint32_t index);
// Set the node's outgoing edge. Bad index false; a bad target is stored as-is
// (the graph is the caller's to keep well-formed).
bool Shelf_setNext(Shelf *shelf, uint32_t index, uint32_t next);
// Shorthand for setNext(from, to).
bool Shelf_link(Shelf *shelf, uint32_t from, uint32_t to);

// --- Setters ---
// The list entry point (SHELF_INDEX_NONE clears it). Bad index false.
bool Shelf_setHead(Shelf *shelf, uint32_t index);

// --- Getters (null-safe) ---
uint32_t Shelf_getHead(const Shelf *shelf);
uint32_t Shelf_count(const Shelf *shelf);
bool Shelf_isEmpty(const Shelf *shelf);

// --- String projections (the toString Law) ---
void Shelf_toString(const Shelf *shelf, char *dest, size_t cap, bool *outTruncated);
void Shelf_toStringStruct(const Shelf *shelf, char *dest, size_t cap, bool *outTruncated);

#endif
