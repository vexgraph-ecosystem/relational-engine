#ifndef NIO_MEM_H
#define NIO_MEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "annotation/debug.h"

// Relational Engine owns this production ForeignMemory API and its implementation.
// Canonical nio/mem.h includes preserve the existing C ABI for every consumer.
//
// 32-BYTE ARENA DOCTRINE (Vex Paradigm):
// In standard C, programs malloc/free constantly. In vex, the engine carves
// out one massive master arena at startup. memory is partitioned into
// size-class slabs for O(1) slot claiming and cache-hot recycling.
//
// Every allocation returns a payload pointer whose 32-byte prefix holds four
// 64-bit words:
//
//     [ id with sugar ][ hash ][ length ][ pointer ]
//
// The id is the triple-sugar self-describe word — form, project, modifier,
// wrapper one, wrapper two, the BE6C sugar and the class number — so any
// pointer names its own project and class without a registry lookup. `hash` is
// the recomputed veto over (id, length, pointer); `length` is the payload's
// byte count; `pointer` is the block's own payload address. A header therefore
// validates three ways and fails closed on any of them: the BE6C sugar inside
// the id, the hash over the stated fields, and the stored pointer equal to this
// header's own payload address. Negative pointer math recovers the header in 1
// subtraction; Memory_type() and Memory_length() are free O(1) reads with zero
// locking. The header is frozen: no growth room lives here (struct evolution is
// detected per object via length + refused at the manifest gate, never
// accommodated in-band).
//
// The stored self-pointer is what makes blocks immovable: a block must never be
// relocated after allocation or its header goes stale and fails closed. The
// master arena mallocs its backing once and never moves a live block.

#define MEMORY_HEADER_SIZE 32

typedef struct MemoryHeader {
    uint64_t typeId;   // triple-sugar self-describe id (BE6C sugar inside)
    uint64_t hash;     // recomputed veto over (typeId, length, pointer)
    uint64_t length;   // payload byte count
    uint64_t pointer;  // this block's own payload address
} MemoryHeader;
_Static_assert(sizeof(MemoryHeader) == 32, "MemoryHeader must stay 32 Bytes");
// 16-byte payload alignment holds by construction, not by attribute (this
// toolchain's _Alignas rejects typedef application): master/slab arenas come
// from malloc (16-aligned on arm64 macOS), every slot size and bump total is a
// multiple of 16, so every payload pointer satisfies (u & 15) == 0 as mem.c
// guards.

// Initialize the master arena with a specific total capacity (e.g. 64MB).
// If not called explicitly, Memory_alloc initializes a 64MB arena lazily.
bool Memory_init(size_t totalBytes);

// Allocate nbytes with the given type id stamped in the 32-byte header.
// Returns the aligned payload pointer, or nullptr on failure/exhaustion. Backing
// capacity is selected at construction, a physical resource bound, not a dynamic
// entity ceiling. No untracked heap fallback; choose a larger owner when needed.
// Exhaustion is LOUD but bounded (the Exhaustion Loudness Law): the FIRST
// rejection of an epoch emits its THROW diagnostic naming the request and the
// arena's used/available bytes; later rejections only increment a counter, so a
// failing frame loop cannot flood stderr. The return contract is unchanged
// (nullptr) and existing blocks and the allocation cursor survive.
void *Memory_alloc(uint64_t typeId, size_t numBytes);

// Grow/shrink a block, preserving contents and type. Returns the new payload
// pointer (the old one is freed). nullptr on failure leaves the original intact.
void *Memory_realloc(void *userPtr, size_t newBytes);

// Free a block back to its slab pool in O(1) cache-hot time.
void Memory_free(void *userPtr);

// Reset slab lists and clear used bump storage, invalidating existing headers.
// Work scales with slab capacity plus used bump bytes; excludes active users.
// Also starts a fresh exhaustion epoch (counter and one-report latch reset).
void Memory_freeAll(void);

// Metadata accessors: cost a single pointer subtract in O(1) without locks.
size_t Memory_length(void *userPtr);
uint64_t Memory_type(void *userPtr);

// Standard comparison: same kind (provenance-checked self Bytes equal).
// Answers identity, never content: two ID_STRING blocks of different
// lengths are similar. Foreign, corrupted, or freed pointers fail closed.
bool Memory_similar(const void *a, const void *b);

// Search slabs AND bump blocks. Return total live matches; typeId zero is wildcard.
// If outArray is not nullptr, fill up to maxCount payload pointers, leaving the
// tail unchanged. Enumeration excludes mutation; corrupted headers are not a
// trusted serialization format and a forged checksum is not an identity proof.
size_t Memory_findAll(uint64_t typeId, void **outArray, size_t maxCount);

// Phase-4 instancing (Arena-B): the process-global allocator above is the
// DEFAULT arena. Secondary arenas are fully isolated slab sets carved from
// their own malloc — allocate in B, verify, free B, default untouched.
// Headers are unchanged (32B, no arena tag), so this is ABI-stable: free
// routes by address-range lookup across a growable registry.
// Registration happens pre-threads; the hot path takes no extra locks.
// Initialized disjoint alloc/free use per-region locks. Init/create/destroy,
// reset/enumeration and shared-block access require caller exclusion. Freed raw
// addresses have no generations: reuse may alias them; not a stale-handle API.
// Slab blocks recycle individually. Bump blocks (including small overflow spill)
// invalidate on free but reclaim capacity only on freeAll, never individually.
typedef struct MemoryArena MemoryArena;

// The process-global (default) arena, lazily initialized like Memory_alloc.
// Borrowed — never destroy it (destroy guards the default). Exists so
// leaf-global services (the string pool) can anchor backing without
// threading arena handles through every caller.
MemoryArena *Memory_defaultArena(void);

MemoryArena *MemoryArena_create(size_t totalBytes);
void MemoryArena_destroy(MemoryArena *a);
void *MemoryArena_alloc(MemoryArena *a, uint64_t typeId, size_t numBytes);
void *MemoryArena_realloc(MemoryArena *a, void *userPtr, size_t newBytes);
void MemoryArena_free(MemoryArena *a, void *userPtr);
void MemoryArena_freeAll(MemoryArena *a);
size_t MemoryArena_findAll(MemoryArena *a, uint64_t typeId, void **outArray, size_t maxCount);
size_t MemoryArena_activeBytes(MemoryArena *a);
size_t MemoryArena_capacity(MemoryArena *a);

// --- Exhaustion observability (the Exhaustion Loudness Law) ---
// Every rejection counted since the arena's last init/freeAll; the diagnostic
// itself fires once per epoch. A test/tool asserts the count deterministically
// instead of parsing stderr.
uint64_t Memory_exhaustionCount(void);
uint64_t MemoryArena_exhaustionCount(const MemoryArena *a);

// Lifetime verification classification
typedef enum MemoryLifetime {
    MEMORY_LIFETIME_UNKNOWN = 0,
    MEMORY_LIFETIME_PERMANENT,
    MEMORY_LIFETIME_TRANSIENT,
} MemoryLifetime;

// Transient (Frame / Scratchpad) bump-only arena lifecycle
// Overflow/layout/capacity rejection is LOUD but bounded: the first rejection
// of an epoch reports via THROW, later ones only count (Transient_exhaustionCount).
// Transient payload lengths must fit the 64-bit header length; a request whose
// header-plus-alignment arithmetic overflows size_t is refused. Initialization
// capacity must round to 16-byte alignment without overflowing size_t. Reset
// excludes active users and starts a fresh exhaustion epoch.
bool Memory_initTransient(size_t capacity);
void *Transient_alloc(uint64_t typeId, size_t numBytes);
void Transient_reset(void);
bool Transient_contains(const void *ptr);
uint32_t Transient_getGeneration(void);
uint64_t Transient_exhaustionCount(void);
#if defined(DEBUG_BORROW_CHECK)
;;DEBUG
const uint8_t *Transient_getBuffer(void);
#endif
MemoryLifetime Memory_getLifetime(const void *ptr);

#endif
