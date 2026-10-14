#ifndef NIO_GROWABLE_MAPPED_FILE_H
#define NIO_GROWABLE_MAPPED_FILE_H
/* Engine-owned C client seam to the Rust reservation-backed growing mapping.
 * Supply engine src + rust/include paths and link the resident Rust staticlib.
 * R1 owns file residency; the caller excludes external mutation/truncation and
 * competing writers for the owner's whole lifetime, and drops it once.
 * Contract and status codes: relational_engine/growable_mapped_file.h. */
#include "relational_engine/growable_mapped_file.h"
#endif
