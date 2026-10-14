#ifndef RE_GROWABLE_MAPPED_FILE_H
#define RE_GROWABLE_MAPPED_FILE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Reservation-backed, file-backed byte view owned by Rust, reached from C through
 * this ONE thin extern-C surface. There is no second mapping implementation in C.
 *
 * Why this exists beside the fixed mapping: a fixed view cannot grow, and on macOS
 * there is no mremap, so re-mapping a grown file may move the base address and
 * invalidate every live pointer. Here the RESERVATION is mapped once and the
 * committed length grows inside it, so a pointer taken before a growth step is the
 * same pointer after it. That stability is the whole contract.
 *
 * The reservation is VIRTUAL address space, not memory: it may far exceed physical
 * RAM and still work, because pages are backed by the file and paged in on demand.
 * Resident bytes are bounded by RAM plus the file's allocated blocks.
 *
 * Status: 0 OK, 1 invalid argument, 2 IO/OS failure, 3 bounds (past the committed
 * length, past the reservation, or backwards), 4 read-only, 5 closed owner,
 * 6 length unrepresentable, 7 destination too small (dest untouched),
 * 8 this host has no reservation backend. Code 8 is unique to this surface: a
 * fixed mapping needs no reservation, so it never returns it.
 *
 * UNSAFE ADMISSION (the caller's precondition, not checked for you): the caller
 * must exclude every other writer and any aliasing view of the file for the owner's
 * whole lifetime. Rust borrowing cannot constrain another process.
 *
 * LIFETIME: re_gmap_drop is the only release; close is not drop. re_gmap_borrow
 * returns a pointer INTO the mapping, valid while the owner is open and no write to
 * an overlapping range occurs — and, unlike a fixed view, it SURVIVES GROWTH. It
 * dies with the owner. Allocation is real: blocks for each step are reserved and
 * observed before the committed length is published, so a failed grow leaves the
 * length unchanged and every byte preserved.
 *
 * Not offered: shrink, truncate, a growing reservation, an escaped descriptor,
 * typed reinterpretation, implicit flush/sync, a file format, transactions or
 * concurrent writers. Page faults, open, flush, sync and cleanup have no wall-time
 * bound and stay off realtime and bounded-join teardown paths. */

typedef struct ReGrowableMappedFile ReGrowableMappedFile;

/* Create a NEW file (an existing path is refused, never clobbered) and map the whole
 * reservation once. `committed <= reserved`, and both must be nonzero for reserved.
 * `path` is a NUL-terminated UTF-8 path. Blocks for `committed` are reserved before
 * the mapping is published. */
uint32_t re_gmap_create(const char *path, size_t reserved, size_t committed,
                        ReGrowableMappedFile **out_owner);
/* Open an EXISTING reserved file — the counterpart of create, for reopening a store
 * rather than making one — and map the whole reservation once. Never creates,
 * resizes or truncates: a missing file is an IO/OS failure and a file shorter than
 * `reserved` is refused, because the reservation was fixed when the file was made.
 * `committed` is the caller's starting extent and must already be backed by
 * allocated blocks; raise it with re_gmap_grow_to, which is free when the blocks
 * are already there. Returns 3 for a file that cannot hold the reservation. */
uint32_t re_gmap_open(const char *path, size_t reserved, size_t committed,
                      ReGrowableMappedFile **out_owner);
/* Release the owner: unmaps before releasing the file. Null is a quiet no-op. */
void re_gmap_drop(ReGrowableMappedFile *owner);

/* The committed length: the only extent this API ever exposes. */
uint32_t re_gmap_len(const ReGrowableMappedFile *owner, size_t *out_len);
/* The fixed reservation chosen at create; it never grows. */
uint32_t re_gmap_reserved(const ReGrowableMappedFile *owner, size_t *out_reserved);
uint32_t re_gmap_is_open(const ReGrowableMappedFile *owner, bool *out_open);
/* Bytes the filesystem has physically allocated, observed rather than assumed. */
uint32_t re_gmap_allocated_bytes(const ReGrowableMappedFile *owner, uint64_t *out_allocated);

/* Copy a checked range out, bounded by the COMMITTED length. A destination smaller
 * than `length` refuses with status 7 and leaves `dest` untouched. */
uint32_t re_gmap_read(const ReGrowableMappedFile *owner, size_t offset, size_t length,
                      uint8_t *dest, size_t capacity, bool *out_truncated);
/* Copy `length` bytes in from `source`; must lie inside the committed length. */
uint32_t re_gmap_write(ReGrowableMappedFile *owner, size_t offset,
                       const uint8_t *source, size_t length);
/* Borrow the committed bytes: readonly, no copy, and valid across later growth. */
uint32_t re_gmap_borrow(const ReGrowableMappedFile *owner,
                        const uint8_t **out_bytes, size_t *out_len);

/* Grow the committed length inside the reservation. Reserve-then-publish: blocks
 * for the step exist before the length changes, and the mapping is never remade.
 * Refuses backwards growth, growth past the reservation, and a closed owner. */
uint32_t re_gmap_grow_to(ReGrowableMappedFile *owner, size_t length);

uint32_t re_gmap_flush(ReGrowableMappedFile *owner);
uint32_t re_gmap_sync(ReGrowableMappedFile *owner);
uint32_t re_gmap_close(ReGrowableMappedFile *owner);
uint32_t re_gmap_to_string(const ReGrowableMappedFile *owner, char *dest, size_t capacity,
                           bool *out_truncated);

#endif
