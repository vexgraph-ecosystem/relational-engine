#ifndef NIO_FIXED_MAPPED_FILE_H
#define NIO_FIXED_MAPPED_FILE_H
/* Engine-owned C client seam to the Rust fixed-extent file mapping, separate from
 * Memory_* and from the RowPool ABI. Supply engine src + rust/include paths and
 * link the resident Rust staticlib.
 * R1 owns file residency; the caller excludes external mutation/truncation and
 * competing writable views for the owner's whole lifetime.
 * Contract and status codes: relational_engine/fixed_mapped_file.h. */
#include "relational_engine/fixed_mapped_file.h"
#endif
