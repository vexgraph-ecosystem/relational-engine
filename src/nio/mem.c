#include "nio/mem.h"
// Production R2 storage owner: Relational Engine. Memory_* ABI is preserved;
// this native allocator is not the separate Rust byte/string learning API.
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/intention.h"
#include "atomic/spin.h"
#include "exception/throw.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: ForeignMemory
 * ============================================================================
 * Pre-allocated Master Arena and Size-Class Slab Allocator fulfilling the
 * Vex Paradigm: zero steady-state malloc, cache-hot slot recycling, and
 * 32-byte negative pointer math. The 32-byte MemoryHeader (id with sugar,
 * hash, length, pointer) is read backwards from the user pointer; the hash is
 * the recomputed veto over the stated fields and the stored pointer is the
 * block's own payload address, so cleared headers always fail verification
 * with no special case. Phase-4 instancing makes the globals the default
 * MemoryArena while secondaries register for address-range free routing.
 * Teardown is Memory_freeAll last, after every dependent subsystem has released
 * its blocks.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: ForeignMemory (nio/mem)
 * ============================================================================
 * Pre-allocated Master Arena and Size-Class Slab Allocator fulfilling the
 * Vex Paradigm: zero steady-state malloc, cache-hot slot recycling, and
 * 32-byte negative pointer math.
 *
 * Phase-4 instancing: globals are the DEFAULT MemoryArena; secondaries
 * register for address-range free routing. Header layout untouched.
 *
 * STRUCT FIELDS (Mirroring nio/mem.h + local to this file):
 * ----------------------------------------------------------------------------
 *   MemoryHeader {
 *     uint64_t typeId;  // triple-sugar self-describe id (BE6C sugar inside)
 *     uint64_t hash;    // recomputed veto over (typeId, length, pointer)
 *     uint64_t length;  // payload byte count
 *     uint64_t pointer; // redirect: the object's address, or a primitive's value
 *   }
 *   Zones: identity (id+length+pointer, stated), provenance (hash veto +
 *   arena-range gate, no magic cookie), allocator (size class is a pure
 *   function of length — no stored index), future (none in-band; struct
 *   evolution is detected per object via length + refused at the manifest
 *   gate). The header is frozen at 32 Bytes, read backwards.
 *   SlabClass {
 *     uint32_t slot_size;        // Bytes per slot in this size class
 *     uint32_t capacity;         // total slots carved from the arena
 *     uint32_t count;            // live (checked-out) slots
 *     uint8_t *arena;            // backing store for this class
 *     FreeNode *free_head;       // lock-free recycled slot stack
 *     SpinLock lock;             // serializes alloc/free on this class
 *   }
 *   MemoryArena {
 *     SlabClass slabs[SLAB_COUNT]; // size-class slab table (64B..4K)
 *     uint8_t *masterArena;      // pre-allocated master backing store
 *     size_t masterCapacity;     // master arena byte capacity
 *     uint8_t *bumpArena;        // large/allocation bump region
 *     size_t bumpCapacity;       // bump region capacity
 *     size_t bumpOffset;         // bump cursor (monotonic)
 *     SpinLock bumpLock;         // serializes bump allocation
 *     SpinLock initLock;         // serializes lazy arena init
 *     bool live;                 // arena ready flag
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Core Functions:
 *   - Memory_init(totalBytes)
 *   - Memory_alloc(typeId, numBytes)
 *   - Memory_defaultArena(void)
 *   - Memory_realloc(userPtr, newBytes)
 *   - Memory_free(userPtr)
 *   - Memory_freeAll(void)
 *   - Memory_length(userPtr)
 *   - Memory_type(userPtr)
 *   - Memory_similar(a, b)
 *   - Memory_findAll(typeId, outArray, maxCount)
 *
 * Arena Functions (Phase-4):
 *   - MemoryArena_create(totalBytes)
 *   - MemoryArena_destroy(a)
 *   - MemoryArena_alloc(a, typeId, numBytes)
 *   - MemoryArena_realloc(a, userPtr, newBytes)
 *   - MemoryArena_free(a, userPtr)
 *   - MemoryArena_freeAll(a)
 *   - MemoryArena_findAll(a, typeId, outArray, maxCount)
 *   - MemoryArena_activeBytes(a)
 *   - MemoryArena_capacity(a)
 *
 * Transient Functions (Dynamic Lifetime Verifier):
 *   Scratch payload lengths fit the 64-bit header length; a request whose
 *   header-plus-alignment arithmetic overflows, or that exceeds capacity, is
 *   refused LOUDLY once per epoch (THROW) and counted thereafter, without
 *   changing headers, generation or bump cursor. Initialization capacity must
 *   round to 16-byte alignment without size_t overflow.
 *   - Memory_initTransient(capacity)
 *   - Transient_alloc(typeId, numBytes)
 *   - Transient_reset(void)
 *   - Transient_contains(ptr)
 *   - Transient_getGeneration(void)
 *   - Transient_getBuffer(void)
 *   - Memory_getLifetime(ptr)
 *
 * CAPACITY / OWNERSHIP:
 *   Construction owns all backing; alloc never escapes into libc on exhaustion.
 *   Exhaustion returns nullptr and is reported once per epoch (THROW) with the
 *   request size, then counted (Memory_exhaustionCount / MemoryArena_exhaustionCount)
 *   without changing existing blocks. Slab slots recycle individually; bump blocks
 *   invalidate individually and reclaim space only at freeAll. Enumeration includes
 *   both storage regions. Registry mutation/reset/destroy/metadata enumeration
 *   require exclusion; initialized disjoint alloc/free may contend on class/bump
 *   locks. Raw pointers have no generations: reuse can alias an old address. Not
 *   stale-handle safety.
 * ============================================================================
 */

#define SLAB_COUNT 7
;;INTENTION("fixed SLAB_COUNT: allocator slab classes are a design constant, not a workload ceiling")
#define ANTI_ARENA_DEFAULT_SIZE (64 * 1024 * 1024) // 64 MB master arena

// Hash-clarification veto over the stated identity, fed little-endian (never
// serialized, but explicit anyway). LSB forced so a stored hash is never 0:
// cleared (zeroed) headers always fail verification with no special case.
/** Compute the nonzero header veto word from type id, length and payload address. */
static uint64_t header_hash(uint64_t typeId, uint64_t length, uint64_t pointer) {
    uint64_t hash = 1469598103934665603ull;
    const uint64_t words[3] = { typeId, length, pointer };
    for (int w = 0; w < 3; w++) {
        for (int i = 0; i < 8; i++) {
            hash ^= (words[w] >> (i * 8)) & 0xFFull;
            hash *= 1099511628211ull;
        }
    }
    return hash | 1ull;
}

/** Stamp a header: identity, length, the block's redirect, and the veto. */
static void header_stamp(MemoryHeader *h, uint64_t typeId, size_t numBytes) {
    (*h).typeId = typeId;
    (*h).length = (uint64_t) numBytes;
    // The redirect is the object's own address here; a primitive form redirects
    // to its value cell instead, which the id's form bits decide (Type_isPrimitive).
    (*h).pointer = (uint64_t) (uintptr_t) ((uint8_t*) h + sizeof(MemoryHeader));
    (*h).hash = header_hash((*h).typeId, (*h).length, (*h).pointer);
}

// Triple sugar: BE6C inside the id word, the hash over the stated fields, and
// the redirect the block carries. The hash vetoes the redirect's value without
// constraining what it addresses, so an object and a primitive block are both
// admissible and the id tells a reader which it is.
/** Validate a header against its own stated fields. */
static bool header_valid(const MemoryHeader *h) {
    if (!h)
        return false;
    return (*h).hash == header_hash((*h).typeId, (*h).length, (*h).pointer);
}

typedef struct FreeNode {
    struct FreeNode *next;
} FreeNode;

typedef struct SlabClass {
    uint32_t slot_size;
    uint32_t capacity;
    uint32_t count;
    uint8_t *arena;
    FreeNode *free_head;
    SpinLock lock;
} SlabClass;

struct MemoryArena {
    SlabClass slabs[SLAB_COUNT];
    uint8_t *masterArena;
    size_t masterCapacity;
    uint8_t *bumpArena;
    size_t bumpCapacity;
    size_t bumpOffset;
    SpinLock bumpLock;
    SpinLock initLock;
    bool live;
    // Exhaustion is LOUD but bounded: the first rejection in an epoch reports
    // through THROW and later ones only count, so a failing frame loop cannot
    // flood stderr. Both reset on init/freeAll (a fresh epoch).
    uint64_t exhaustionCount;   // rejected requests since the last reset
    bool exhaustionReported;    // the epoch's one diagnostic was emitted
};

static uint32_t s_slabSizes[SLAB_COUNT] = { 64, 128, 256, 512, 1024, 2048, 4096 };
static uint32_t s_slabCaps[SLAB_COUNT] = { 32768, 32768, 16384, 8192, 4096, 2048, 2048 };

static MemoryArena s_default = {0};

// Growable arena registry (the No Hardcoding Law):
// slot 0 is always the default arena once Memory_init runs; created arenas
// append on demand. The pointer table is heap-backed (realloc), like the
// calloc'd MemoryArena shims — never arena-backed, so allocator bookkeeping
// can never recurse into itself. Writes happen at init/create/destroy
// (pre-threads); walks are lock-free reads.
static MemoryArena **s_registry = nullptr;
static size_t s_registryCount = 0;
static size_t s_registryCap = 0;
static SpinLock s_registryLock = SPIN_LOCK_INIT;

// Append an arena pointer, growing the table exponentially. OOM returns false
// and leaves the registry untouched.
/** Append an arena to the global free-routing registry, growing the table on demand. */
static bool registry_push(MemoryArena *a) {
    if (s_registryCount == s_registryCap) {
        size_t newCap = (s_registryCap == 0) ? 8 : s_registryCap * 2;
        MemoryArena **nb = (MemoryArena**) realloc(
            s_registry, newCap * sizeof(MemoryArena*));
        if (!nb) return false;
        s_registry = nb;
        s_registryCap = newCap;
    }
    s_registry[s_registryCount++] = a;
    return true;
}

// The default arena occupies slot 0 so free-routing walks always see it.
/** Ensure the default arena occupies the registry's first slot. */
static bool registry_ensureDefault(void) {
    if (s_registryCount == 0) return registry_push(&s_default);
    return true;
}

#define ANTI_TRANSIENT_DEFAULT_SIZE (64 * 1024 * 1024) // 64 MB

typedef struct TransientArena {
    uint8_t *buffer;
    size_t capacity;
    size_t bumpOffset;
    uint32_t generation;
    SpinLock lock;
    bool live;
    // Same bounded loudness as the permanent arenas: report the first scratch
    // rejection of an epoch, count the rest.
    uint64_t exhaustionCount;
    bool exhaustionReported;
} TransientArena;

static TransientArena s_transient = {
    .buffer = nullptr,
    .capacity = 0,
    .bumpOffset = 0,
    .generation = 1,
    .lock = { 0 },
    .live = false,
};


/** Select the smallest slab class that can hold a header and payload, or -1 for bump storage. */
static inline int find_slab(size_t payload_bytes) {
    size_t needed = payload_bytes + sizeof(MemoryHeader);
    if (needed <= 64)   return 0;
    if (needed <= 128)  return 1;
    if (needed <= 256)  return 2;
    if (needed <= 512)  return 3;
    if (needed <= 1024) return 4;
    if (needed <= 2048) return 5;
    if (needed <= 4096) return 6;
    return -1;
}

/** Initialize a slab descriptor from its fixed class-size and capacity tables. */
static void slab_template(SlabClass *slab, uint32_t idx) {
    (*slab).slot_size = s_slabSizes[idx];
    (*slab).capacity = s_slabCaps[idx];
    (*slab).count = 0;
    (*slab).arena = nullptr;
    (*slab).free_head = nullptr;
    (*slab).lock = SPIN_LOCK_INIT;
}

/** Allocate and partition an arena into initialized slab freelists and a bump region. */
static bool arena_init(MemoryArena *a, size_t totalBytes) {
    if (!a)
        return false;
    for (size_t s = 0; s < SLAB_COUNT; s++)
        slab_template(&(*a).slabs[s], (uint32_t) s);
    (*a).initLock = SPIN_LOCK_INIT;
    (*a).bumpLock = SPIN_LOCK_INIT;
    (*a).live = false;

    size_t cap = totalBytes > 0 ? totalBytes : ANTI_ARENA_DEFAULT_SIZE;
    uint8_t *master = (uint8_t*) malloc(cap);
    if (!master)
        return false;

    uint8_t *cur = master;
    size_t left = cap;
    for (size_t s = 0; s < SLAB_COUNT; s++) {
        SlabClass *slab = &(*a).slabs[s];
        size_t need = (size_t)(*slab).capacity * (*slab).slot_size;
        if (need > left) {
            free(master);
            return false;
        }
        (*slab).arena = cur;
        (*slab).free_head = nullptr;
        (*slab).count = 0;
        uint32_t sz = (*slab).slot_size;
        for (size_t i = (*slab).capacity; i > 0; i--) {
            uint8_t *slot_ptr = (*slab).arena + (i - 1) * sz;
            MemoryHeader *h = (MemoryHeader*) slot_ptr;
            memset(h, 0, sizeof(MemoryHeader));
            FreeNode *node = (FreeNode*) (slot_ptr + sizeof(MemoryHeader));
            (*node).next = (*slab).free_head;
            (*slab).free_head = node;
        }
        cur += need;
        left -= need;
    }

    (*a).masterArena = master;
    (*a).masterCapacity = cap;
    (*a).bumpArena = cur;
    (*a).bumpCapacity = left;
    (*a).bumpOffset = 0;
    (*a).live = true;
    (*a).exhaustionCount = 0;
    (*a).exhaustionReported = false;
    return true;
}

/** Allocate from owner storage only; exhaustion is reported loudly, once per epoch. */
;;INTENTION("exhaustion is a cold, terminal detection reached from a hot allocator: it reports once per epoch (never per call) and counts the rest, so the Cold-Strict hot-minimal contract holds for the success path while a failure can never be silent -- the Exhaustion Loudness Law's hot-path clause under the Conflict Triage Law")
static void *arena_alloc(MemoryArena *a, uint64_t typeId, size_t numBytes) {
    if (!a || !(*a).live)
        return nullptr;
    if (numBytes > SIZE_MAX - sizeof(MemoryHeader) - 15u) {
        // A size whose header-plus-alignment arithmetic overflows is a refusal,
        // not a silent no-op.
        (*a).exhaustionCount++;
        if (!(*a).exhaustionReported) {
            (*a).exhaustionReported = true;
            THROW("memory: request %zu overflows the block size", numBytes);
        }
        return nullptr;
    }

    int s_idx = find_slab(numBytes);
    if (s_idx >= 0) {
        SlabClass *slab = &(*a).slabs[s_idx];
        SpinLock_lock(&(*slab).lock);
        FreeNode *node = (*slab).free_head;
        if (node) {
            (*slab).free_head = (*node).next;
            (*slab).count++;
        }
        SpinLock_unlock(&(*slab).lock);

        if (node) {
            MemoryHeader *h = (MemoryHeader*) ((uint8_t*) node - sizeof(MemoryHeader));
            header_stamp(h, typeId, numBytes);
            return (void*) node;
        }
    }

    size_t aligned_len = (numBytes + 15) & ~15ull;
    size_t total = sizeof(MemoryHeader) + aligned_len;

    SpinLock_lock(&(*a).bumpLock);
    if ((*a).bumpOffset + total <= (*a).bumpCapacity) {
        uint8_t *slot_ptr = (*a).bumpArena + (*a).bumpOffset;
        (*a).bumpOffset += total;
        SpinLock_unlock(&(*a).bumpLock);

        MemoryHeader *h = (MemoryHeader*) slot_ptr;
        header_stamp(h, typeId, numBytes);
        return (void*) (slot_ptr + sizeof(MemoryHeader));
    }
    SpinLock_unlock(&(*a).bumpLock);

    // The arena is exhausted: no slab slot and no bump room. Report once with
    // the quantity needed, then count every further rejection.
    (*a).exhaustionCount++;
    if (!(*a).exhaustionReported) {
        (*a).exhaustionReported = true;
        THROW("memory: arena exhausted, request %zu bytes (bump used %zu/%zu)",
              numBytes, (*a).bumpOffset, (*a).bumpCapacity);
    }
    return nullptr;
}

/** Validate an in-arena payload header and recycle its slab slot when applicable. */
static void arena_free(MemoryArena *a, void *userPtr) {
    if (!a || !userPtr)
        return;

    uintptr_t u = (uintptr_t) userPtr;
    if (u < sizeof(MemoryHeader) || (u & 15) != 0)
        return;

    // Validate the pointer is within this arena's known address range before
    // performing the negative-offset header read.  MemoryArena_free calls us
    // directly (bypassing safe_header / arena_for), so a foreign pointer that
    // happens to be aligned would otherwise blindly dereference p-16, which
    // can SIGSEGV when those Bytes are in an unmapped page.
    uint8_t *p = (uint8_t*) userPtr;
    bool in_range = false;
    if ((*a).masterArena && p >= (*a).masterArena + sizeof(MemoryHeader) && p < (*a).masterArena + (*a).masterCapacity)
        in_range = true;
    if (!in_range) {
        // Foreign storage is not ours to inspect or release.
        return;
    }

    MemoryHeader *h = (MemoryHeader*) ((uint8_t*) userPtr - sizeof(MemoryHeader));
    if (!header_valid(h))
        return;

    // Provenance is physical: small spill blocks belong to the bump region,
    // never a slab list. Recycling them into a slab corrupts counts/enum/reset.
    SlabClass *slab = nullptr;
    for (size_t s = 0; s < SLAB_COUNT; s++) {
        SlabClass *candidate = &(*a).slabs[s];
        uintptr_t base = (uintptr_t) (*candidate).arena;
        size_t extent = (size_t) (*candidate).capacity * (*candidate).slot_size;
        if (u >= base + sizeof(MemoryHeader) && u < base + extent) {
            if ((u - base) % (*candidate).slot_size != sizeof(MemoryHeader)
                || (*h).length > (*candidate).slot_size - sizeof(MemoryHeader))
                return;
            slab = candidate;
            break;
        }
    }
    memset(h, 0, sizeof(MemoryHeader));
    if (!slab)
        return; // Bump storage is reclaimed wholesale only.
    FreeNode *node = (FreeNode*) userPtr;

    SpinLock_lock(&(*slab).lock);
    (*node).next = (*slab).free_head;
    (*slab).free_head = node;
    if ((*slab).count > 0)
        (*slab).count--;
    SpinLock_unlock(&(*slab).lock);
}

/** Reset slab freelists and the bump cursor, invalidating all arena payloads. */
static void arena_freeAll(MemoryArena *a) {
    if (!a || !(*a).live)
        return;

    for (size_t s = 0; s < SLAB_COUNT; s++) {
        SlabClass *slab = &(*a).slabs[s];
        SpinLock_lock(&(*slab).lock);
        (*slab).free_head = nullptr;
        (*slab).count = 0;

        uint32_t sz = (*slab).slot_size;
        for (size_t i = (*slab).capacity; i > 0; i--) {
            uint8_t *slot_ptr = (*slab).arena + (i - 1) * sz;
            MemoryHeader *h = (MemoryHeader*) slot_ptr;
            memset(h, 0, sizeof(MemoryHeader));
            FreeNode *node = (FreeNode*) (slot_ptr + sizeof(MemoryHeader));
            (*node).next = (*slab).free_head;
            (*slab).free_head = node;
        }
        SpinLock_unlock(&(*slab).lock);
    }

    SpinLock_lock(&(*a).bumpLock);
    // Clear used headers too: rewinding alone leaves old bump pointers valid.
    memset((*a).bumpArena, 0, (*a).bumpOffset);
    (*a).bumpOffset = 0;
    SpinLock_unlock(&(*a).bumpLock);
    // A reset starts a fresh exhaustion epoch: the next rejection reports again.
    (*a).exhaustionCount = 0;
    (*a).exhaustionReported = false;
}

// Free-routing: headers carry no arena tag (ABI-stable by design), so the
// owner is whoever's master range contains the header. Registry writes happen at
// create/destroy (pre-threads); reads are lock-free.
/** Find the live registered arena whose address range contains a valid payload. */
static MemoryArena *arena_for(void *userPtr) {
    if (!userPtr)
        return nullptr;
    uintptr_t u = (uintptr_t) userPtr;
    if (u < sizeof(MemoryHeader) || (u & 15) != 0)
        return nullptr;

    uint8_t *p = (uint8_t*) userPtr;

    for (size_t i = 0; i < s_registryCount; i++) {
        MemoryArena *a = s_registry[i];
        if (a && (*a).live && (*a).masterArena) {
            uint8_t *base = (*a).masterArena;
            if (p >= base + sizeof(MemoryHeader) && p < base + (*a).masterCapacity) {
                MemoryHeader *h = (MemoryHeader*) (p - sizeof(MemoryHeader));
                if (header_valid(h))
                    return a;
            }
        }
    }
    return nullptr;
}

/** Resolve a validated header only after range-checking transient and registered arenas. */
static const MemoryHeader *safe_header(const void *userPtr) {
    if (!userPtr)
        return nullptr;
    uintptr_t u = (uintptr_t) userPtr;
    if (u < sizeof(MemoryHeader) || (u & 15) != 0)
        return nullptr;

    uint8_t *p = (uint8_t*) userPtr;

    if (s_transient.live && s_transient.buffer) {
        if (p >= s_transient.buffer + sizeof(MemoryHeader) && p < s_transient.buffer + s_transient.bumpOffset) {
            const MemoryHeader *h = (const MemoryHeader*) (p - sizeof(MemoryHeader));
            if (header_valid(h))
                return h;
        }
    }

    for (size_t i = 0; i < s_registryCount; i++) {
        MemoryArena *a = s_registry[i];
        if (a && (*a).live && (*a).masterArena) {
            uint8_t *base = (*a).masterArena;
            if (p >= base + sizeof(MemoryHeader) && p < base + (*a).masterCapacity) {
                const MemoryHeader *h = (const MemoryHeader*) (p - sizeof(MemoryHeader));
                if (header_valid(h))
                    return h;
            }
        }
    }
    return nullptr;
}

/** Lazily initialize the default arena with its named default size. */
static inline void ensure_initialized(void) {
    if (!s_default.live)
        Memory_init(ANTI_ARENA_DEFAULT_SIZE);
}

/** Initialize and register the default arena; repeated calls succeed without reinitializing it. */
bool Memory_init(size_t totalBytes) {
    SpinLock_lock(&s_default.initLock);
    if (s_default.live) {
        SpinLock_unlock(&s_default.initLock);
        return true;
    }
    SpinLock_unlock(&s_default.initLock);
    if (!registry_ensureDefault()) return false;   // slot 0 must exist first
    return arena_init(&s_default, totalBytes);
}

/** Allocate a default-arena payload with a 32-byte self-describing header. */
void *Memory_alloc(uint64_t typeId, size_t numBytes) {
    ensure_initialized();
    return arena_alloc(&s_default, typeId, numBytes);
}

/** Return the process-wide default arena, initializing it on first access. */
MemoryArena *Memory_defaultArena(void) {
    ensure_initialized();
    return &s_default;
}

/** Allocate a replacement in the default arena, copy the shorter length, and free the old arena block. */
void *Memory_realloc(void *userPtr, size_t newBytes) {
    if (!userPtr)
        return Memory_alloc(0, newBytes);
    if (!safe_header(userPtr))
        return nullptr;

    uint64_t typeId = Memory_type(userPtr);
    size_t oldLen = Memory_length(userPtr);
    void *next = Memory_alloc(typeId, newBytes);
    if (!next)
        return nullptr;

    memcpy(next, userPtr, oldLen < newBytes ? oldLen : newBytes);
    Memory_free(userPtr);
    return next;
}

/** Route a validated permanent block to its owning arena; transient or invalid pointers are ignored. */
void Memory_free(void *userPtr) {
    if (!userPtr)
        return;
    if (Transient_contains(userPtr))
        return;

    const MemoryHeader *h = safe_header(userPtr);
    if (!h)
        return;

    // Only registered owner storage can reach this free-routing branch.
    MemoryArena *a = arena_for(userPtr);
    if (!a)
        return;
    arena_free(a, userPtr);
}

/** Reset all allocations in the default arena; dependent users must already be stopped. */
void Memory_freeAll(void) {
    arena_freeAll(&s_default);
}

/** Return a validated block's payload length, or zero when the pointer is rejected. */
size_t Memory_length(void *userPtr) {
    const MemoryHeader *h = safe_header(userPtr);
    if (h)
        return (size_t) (*h).length;
    return 0;
}

/** Return a validated block's type id, or zero when the pointer is rejected. */
uint64_t Memory_type(void *userPtr) {
    const MemoryHeader *h = safe_header(userPtr);
    if (h)
        return (*h).typeId;
    return 0;
}

/** Return whether both validated blocks carry the same type id. */
bool Memory_similar(const void *a, const void *b) {
    if (!a || !b)
        return false;
    const MemoryHeader *ha = safe_header(a);
    if (!ha)
        return false;
    const MemoryHeader *hb = safe_header(b);
    if (!hb)
        return false;
    return (*ha).typeId == (*hb).typeId;
}

/** Initialize the transient bump arena once, rounding capacity up to 16-byte alignment. */
bool Memory_initTransient(size_t capacity) {
    if (capacity > SIZE_MAX - 15u)
        return false;
    SpinLock_lock(&s_transient.lock);
    if (s_transient.live) {
        SpinLock_unlock(&s_transient.lock);
        return true;
    }
    if (capacity == 0)
        capacity = ANTI_TRANSIENT_DEFAULT_SIZE;

    size_t aligned_cap = (capacity + 15) & ~15ull;
    s_transient.buffer = (uint8_t*) malloc(aligned_cap);
    if (!s_transient.buffer) {
        SpinLock_unlock(&s_transient.lock);
        return false;
    }

    s_transient.capacity = aligned_cap;
    s_transient.bumpOffset = 0;
    s_transient.generation = 1;
    s_transient.live = true;
    s_transient.exhaustionCount = 0;
    s_transient.exhaustionReported = false;
    SpinLock_unlock(&s_transient.lock);
    return true;
}

/** Lazily initialize the transient arena using its named default capacity. */
static inline void ensure_transient_initialized(void) {
    if (!s_transient.live)
        Memory_initTransient(ANTI_TRANSIENT_DEFAULT_SIZE);
}

/** Allocate an aligned transient payload, rejecting length, arithmetic, and capacity overflow. */
void *Transient_alloc(uint64_t typeId, size_t numBytes) {
    // The header length is 64-bit; only arithmetic overflow is refused. An
    // oversized request is a refusal and is reported (once per epoch) rather
    // than dropped; the guard stays cheap.
    if (numBytes > SIZE_MAX - sizeof(MemoryHeader) - 15u) {
        s_transient.exhaustionCount++;
        if (!s_transient.exhaustionReported) {
            s_transient.exhaustionReported = true;
            THROW("transient: request %zu overflows the scratch block size", numBytes);
        }
        return nullptr;
    }
    ensure_transient_initialized();
    if (!s_transient.live)
        return nullptr;

    size_t aligned_len = (numBytes + 15) & ~15ull;
    size_t total = sizeof(MemoryHeader) + aligned_len;

    SpinLock_lock(&s_transient.lock);
    if (total > s_transient.capacity - s_transient.bumpOffset) {
        size_t used = s_transient.bumpOffset;
        size_t capacity = s_transient.capacity;
        SpinLock_unlock(&s_transient.lock);
        s_transient.exhaustionCount++;
        if (!s_transient.exhaustionReported) {
            s_transient.exhaustionReported = true;
            THROW("transient: scratch exhausted, request %zu bytes (used %zu/%zu)",
                  numBytes, used, capacity);
        }
        return nullptr;
    }

    uint8_t *slot = s_transient.buffer + s_transient.bumpOffset;
    s_transient.bumpOffset += total;
    SpinLock_unlock(&s_transient.lock);

    MemoryHeader *h = (MemoryHeader*) slot;
    header_stamp(h, typeId, numBytes);

    return (void*) (slot + sizeof(MemoryHeader));
}

/** Rewind the transient bump cursor and advance its generation, invalidating prior borrows. */
void Transient_reset(void) {
    if (!s_transient.live)
        return;

    SpinLock_lock(&s_transient.lock);
#if defined(DEBUG_BORROW_CHECK)
    size_t used = s_transient.bumpOffset;
#endif
    s_transient.bumpOffset = 0;
    s_transient.generation++;
    s_transient.exhaustionCount = 0;
    s_transient.exhaustionReported = false;
    SpinLock_unlock(&s_transient.lock);

#if defined(DEBUG_BORROW_CHECK)
    if (used > 0 && s_transient.buffer) {
        memset(s_transient.buffer, 0xDD, used);
    }
#endif
}

/** Return whether ptr lies in the currently used payload range of the transient arena. */
bool Transient_contains(const void *ptr) {
    if (!ptr || !s_transient.live || !s_transient.buffer)
        return false;
    uint8_t *p = (uint8_t*) ptr;
    return (p >= s_transient.buffer + sizeof(MemoryHeader) && p < s_transient.buffer + s_transient.bumpOffset);
}

/** Return the current transient allocation generation. */
uint32_t Transient_getGeneration(void) {
    return s_transient.generation;
}

/** Return the number of rejected scratch requests since the last reset. */
uint64_t Transient_exhaustionCount(void) {
    return s_transient.exhaustionCount;
}

#if defined(DEBUG_BORROW_CHECK)
;;DEBUG
/** Return the transient arena backing buffer for debug-only borrow diagnostics. */
const uint8_t *Transient_getBuffer(void) {
    return s_transient.buffer;
}
#endif

/** Classify a pointer as transient, permanent, or unknown using registered address ranges. */
MemoryLifetime Memory_getLifetime(const void *ptr) {
    if (!ptr)
        return MEMORY_LIFETIME_UNKNOWN;
    if (Transient_contains(ptr))
        return MEMORY_LIFETIME_TRANSIENT;
    if (arena_for((void*) ptr) != nullptr)
        return MEMORY_LIFETIME_PERMANENT;
    return MEMORY_LIFETIME_UNKNOWN;
}


/** Enumerate default-arena live slab blocks matching typeId, returning total matches found. */
size_t Memory_findAll(uint64_t typeId, void **outArray, size_t maxCount) {
    return MemoryArena_findAll(&s_default, typeId, outArray, maxCount);
}

/** Allocate, initialize, and register an independent arena with the requested capacity. */
MemoryArena *MemoryArena_create(size_t totalBytes) {
    MemoryArena *a = (MemoryArena*) calloc(1, sizeof(MemoryArena));
    if (!a)
        return nullptr;
    if (!arena_init(a, totalBytes)) {
        free(a);
        return nullptr;
    }
    SpinLock_lock(&s_registryLock);
    bool placed = registry_push(a);
    SpinLock_unlock(&s_registryLock);
    if (!placed) {
        free((*a).masterArena);
        free(a);
        return nullptr;
    }
    return a;
}

/** Unregister and destroy a non-default arena and its backing storage. */
void MemoryArena_destroy(MemoryArena *a) {
    if (!a || a == &s_default)
        return;
    SpinLock_lock(&s_registryLock);
    for (size_t i = 0; i < s_registryCount; i++) {
        if (s_registry[i] == a) {
            s_registry[i] = s_registry[--s_registryCount];
            break;
        }
    }
    SpinLock_unlock(&s_registryLock);
    (*a).live = false;
    free((*a).masterArena);
    (*a).masterArena = nullptr;
    free(a);
}

/** Allocate a typed payload from the specified arena. */
void *MemoryArena_alloc(MemoryArena *a, uint64_t typeId, size_t numBytes) {
    if (!a)
        return nullptr;
    return arena_alloc(a, typeId, numBytes);
}

/** Allocate replacement storage in a, copy bytes from userPtr, and route old-block release by owner. */
void *MemoryArena_realloc(MemoryArena *a, void *userPtr, size_t newBytes) {
    if (!a)
        return nullptr;
    if (!userPtr)
        return arena_alloc(a, 0, newBytes);
    if (!safe_header(userPtr))
        return nullptr;

    uint64_t typeId = Memory_type(userPtr);
    size_t oldLen = Memory_length(userPtr);
    void *next = arena_alloc(a, typeId, newBytes);
    if (!next)
        return nullptr;

    memcpy(next, userPtr, oldLen < newBytes ? oldLen : newBytes);
    Memory_free(userPtr);
    return next;
}

/** Attempt to recycle a validated payload within the specified arena. */
void MemoryArena_free(MemoryArena *a, void *userPtr) {
    if (!a)
        return;
    arena_free(a, userPtr);
}

/** Reset every slab and bump allocation in the specified arena. */
void MemoryArena_freeAll(MemoryArena *a) {
    if (!a)
        return;
    arena_freeAll(a);
}

/** Enumerate live slab and bump blocks; total matches may exceed output capacity. */
size_t MemoryArena_findAll(MemoryArena *a, uint64_t typeId, void **outArray, size_t maxCount) {
    size_t count = 0;
    if (!a || !(*a).live)
        return 0;

    for (size_t s = 0; s < SLAB_COUNT; s++) {
        SlabClass *slab = &(*a).slabs[s];
        SpinLock_lock(&(*slab).lock);
        uint32_t sz = (*slab).slot_size;
        for (size_t i = 0; i < (*slab).capacity; i++) {
            uint8_t *slot_ptr = (*slab).arena + i * sz;
            MemoryHeader *h = (MemoryHeader*) slot_ptr;
            if (header_valid(h)) {
                if (typeId == 0 || (*h).typeId == typeId) {
                    if (outArray && count < maxCount) {
                        outArray[count] = (void*) (slot_ptr + sizeof(MemoryHeader));
                    }
                    count++;
                }
            }
        }
        SpinLock_unlock(&(*slab).lock);
    }
    SpinLock_lock(&(*a).bumpLock);
    size_t offset = 0;
    while (offset < (*a).bumpOffset) {
        size_t remaining = (*a).bumpOffset - offset;
        MemoryHeader *h = (MemoryHeader*) ((*a).bumpArena + offset);
        if (remaining < sizeof(MemoryHeader))
            break;
        size_t length = (*h).length;
        size_t total = sizeof(MemoryHeader) + ((length + 15u) & ~15ull);
        if (total > remaining)
            break;
        if (header_valid(h) && (typeId == 0 || (*h).typeId == typeId)) {
            if (outArray && count < maxCount)
                outArray[count] = (uint8_t*) h + sizeof(MemoryHeader);
            count++;
        }
        offset += total;
    }
    SpinLock_unlock(&(*a).bumpLock);
    return count;
}

/** Return occupied slab bytes plus the arena's current bump offset. */
size_t MemoryArena_activeBytes(MemoryArena *a) {
    size_t total = 0;
    if (!a || !(*a).live)
        return 0;
    for (size_t s = 0; s < SLAB_COUNT; s++) {
        SlabClass *slab = &(*a).slabs[s];
        SpinLock_lock(&(*slab).lock);
        total += (size_t)(*slab).count * (*slab).slot_size;
        SpinLock_unlock(&(*slab).lock);
    }
    SpinLock_lock(&(*a).bumpLock);
    total += (*a).bumpOffset;
    SpinLock_unlock(&(*a).bumpLock);
    return total;
}

/** Return the backing master-arena capacity, or zero for a null arena. */
size_t MemoryArena_capacity(MemoryArena *a) {
    if (!a)
        return 0;
    return (*a).masterCapacity;
}

/** Return the number of rejected requests since the arena's last init/freeAll. */
uint64_t MemoryArena_exhaustionCount(const MemoryArena *a) {
    return a ? (*a).exhaustionCount : 0;
}

/** Return the default arena's rejected-request count since the last freeAll. */
uint64_t Memory_exhaustionCount(void) {
    return s_default.live ? s_default.exhaustionCount : 0;
}
