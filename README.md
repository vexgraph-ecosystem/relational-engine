# relational-engine
R2 memory/storage owner with Rust-owned spans and native C search.

**Ecosystem repository:** https://github.com/vexgraph-ecosystem/relational-engine
The local checkout is ``; its physical location
does not change its R2 rank. This is a partial backend, not a finished engine.

## Current State

**Role:** R2 memory/storage owner. A partial backend, not a finished engine.

**Implemented and proven (macOS arm64):**
- Migrated production native C IO/NIO: `Memory`/`MemoryArena`/`Transient` (32-byte
  header arena, 32-byte payload floor) and `File`/`Cache`/`Log`/`VexHome`/`ProcessSpawn` plus a bounded
  caller-fed `FrameInbox` (not a WebSocket client) and
  the macOS clipboard adapter.
- Rust `primitives` byte/string with atomic byte and retained immutable string
  snapshots over a standalone C ABI; stable `Chunk`/`ChunkedList`,
  `TypedChunk`/`TypedPool`, and a 32-byte `repr(C)` `VariableSlot` +
  `VariableRegistry` with native C `re_name_search`.
- Opt-in Rust aligned byte-row storage usable from C via `nio/relational_rows.h`:
  RowPool copies/reads/writes/borrows rows, grows without moving survivors, and
  rejects zero, stale and wrong-owner handles. Typed handles no longer wrap or
  reset after backing release. Registered Rust/C clients cover this slice only.
- The shared **type algebra** (`src/type/type.{h,c}`): the 64-bit id encoding,
  `PROJ_*`/`ARCH_*`, `Type_make`/`Type_arch`, and the parent-chain resolver
  (`Type_registerParents`, `Type_registerBareParents`, `Type_getParentClass`,
  bounded `Type_isA`).

**Implemented mapping slice:** Rust `nio::fixed_mapped_file::FixedMappedFile` retains an
existing regular file and fixed read-only/shared writable byte view until close/drop.
Checked offset/count access, explicit writeback/sync and unsafe external-file
exclusion have passing macOS arm64 debug/release owners and intended compiler
rejections through `../../tests/relational-engine/rust/run.py`. Mapping is not a database
transaction or hot arena; the proof gaps below still apply.

**Physical preallocation slice:** Rust `io::PreallocatedFile` creates a new private
file, reserves its requested disk extent and checks allocated OS blocks. No sparse
fallback. `FixedMappedFile::from_preallocated` consumes the original handle for writable
offset access. Real macOS arm64 owners cover decimal 1 GB reservation, persisted
bit changes and failure cleanup/retry. Linux source exists but is unproven;
Windows explicitly returns Unsupported.

**Stubbed, draft, or planned:** mapping resize/C ABI; buffered readers/writers and
FFF-style gathering/indexing in `io/`; `compress`/`virtual` modules;
manifest-backed persistence (discussion, not implemented durable snapshots or
concurrent writes); live Hotcwap reload integration.

**Platforms proven:** macOS arm64 only; Windows is unproven. Rust sanitizer
instrumentation is not claimed. `src/relational` and `src/reflection` remain
imported C comparison material, not production.

**Evidence:** `tests/relational-engine/{native_run.py,rust/run.py}` and
`../../gist/test-checklist.md`. The registry C API is
`re_variables_new/drop/add/find/slot/set_pointer`.

## R2 responsibility split

R2 has two cooperating repositories. Vexspoke owns CPU computation, math,
algorithms, synchronization and behavior APIs. Relational Engine owns memory
allocation/storage, stable row chunks, variable bindings and native C search
over Rust-owned spans. Production engine code includes no consumer/host headers;
imported C comparison files do not prove standalone runtime dependency closure.

Production IO/NIO ownership has migrated: the default allocator and file/cache/
log/transport implementations now live in this engine's `src/nio` and `src/io`.
Vexspoke contains no copies. Canonical includes and the default workspace build
resolve this engine-owned C ABI. Native allocation semantics and 32-byte headers
are preserved, not rewritten into Rust. Payloads are 32-byte aligned on every
supported platform (the Universal Alignment Law). Broader Rust/container migration remains
staged. R3 may borrow either R2 public contract. R1 owns
lifetimes/residency and excludes active users before destruction. GPU shaders,
dispatch, capabilities and synchronization remain Graphvex R3. No C/Rust
atomic-layout compatibility or automatic record-schema migration is assumed.

## Why C and Rust coexist

This is a boundary decision, not a taste one — and it is the question every reader
asks, so it is answered here once.

**The ABI is already C, and it is wide.** `Memory_*`, `File`, `VexHome`, the 32-byte
block header, the project-scoped type algebra and the legacy per-project type ids are
consumed by *every* tier: R1 `hotcwap`, the R3 drivers, R4 interfaces and R5 apps all
link the C ABI. The constitution is explicit that native allocation semantics and the
existing type ids are **preserved, not rewritten**, and that this migration is *not* a
Rust allocator rewrite. Rewriting the ABI in one step would invalidate the whole
ecosystem at once, in the wrong dependency order.

**Rust is used where the boundary is thinnest and C's cost is highest:**
typed storage (aligned chunks, typed pools, generation-tagged handles), byte/string
values with atomic publication, and file-backed mappings. These are the places with
pointer arithmetic, lifetime reasoning and *unsafe external admission* — a mutable
file another process may truncate. Rust's ownership system turns
use-after-free, aliasing and "remap while borrowed" from a runtime hope into a
compile error, and the owner suite proves that with compile-negative cases, not
prose: `E0502` borrow rejection, storage arity/type rejection, and
mapping move/close/write/drop rejection.

**Why not all Rust?** Four technical reasons, in order of weight — and one of intent:

1. **The ABI break.** Every C consumer above links the current ABI; a Rust rewrite
   of it lands as one simultaneous, unprovable change across the ecosystem.
2. **No runtime forced on the floor.** R1 and the R3 drivers stay lean and
   predictable; a general-purpose allocator, panic machinery or a runtime is not
   something a host or a driver should inherit.
3. **The hot path wants an arena.** The laws require zero steady-state allocation
   and bounded waits. An arena with explicit lifetimes serves that better than a
   general allocator — and the arena implementation stays C.
4. **Progressive proof.** Each step of the migration is proved in *both* languages:
   a real C client compiles against the engine-owned header and runs the
   Rust-owned behavior. A big-bang rewrite has no such intermediate state.
5. Intentional: I dont believe in **RIIR** sorry not sorry!!!!

**Why not all C?** Because the mapping and typed-storage layer is exactly where
`unsafe` lives. C can express the same rules, but only by convention and review;
Rust enforces the view-versus-close/write/drop ordering at compile time and makes the
owner's `Drop` order explicit (`views before file`). That enforcement is the reason
those files are Rust.

**What it costs, honestly.** Two toolchains (`Cargo` and `b`), a hand-written extern
boundary, and two rules that must never be relaxed: C calls the canonical engine C ABI
and **never** casts Rust atomics to C `_Atomic`, and no C/Rust atomic-layout
compatibility is assumed. Both are stated in the lawbook and both are covered by the
C-ABI and extern-handshake rows in the owner suite.

**The rule that keeps it honest: C owns the stable ABI; Rust owns new typed storage
behind it.** Migration is progressive and each step is proved on both sides.

## Starting point: two interpretations

Relational-engine is an **R2 storage backend alongside Vexspoke**, not a host or
graphics driver. Rust modules separate `nio`, `io`, `primitives`, `variable`,
`struct`, `compress`, and `virtual`. Memory/string/FFI, stable chunks, append-only
collections, variable slots/registry and native C name search have implementations.
mmap is implemented in Rust `nio`; buffered readers/writers and FFF-style
gathering/indexing belong in `io` as planned work. Manifest-backed persistent
objects remain discussion, not implemented durable snapshots or concurrent file
writes. Graphics compute (GPU shaders/dispatch) remains Graphvex-owned.
C `src/search` owns the native search boundary; see its README and the current
owner suite for implemented operations. Rust `Chunk<T>`/`ChunkedList<T>` own
stable row allocations; `VariableSlot` is a 32-byte `repr(C)` name/value record
(`[u8; 24]` plus a borrowed value pointer), not Vexspoke StringSlot's intrusive
self-pointer layout. `VariableRegistry` is exclusively mutated and append-only;
native `re_name_search` searches initialized name bytes without reading value
pointers. Proof and limits live in the owner suite. Imported reflection/relational
files remain comparison material; migrated native IO/NIO is production code.

- `src/nio`, `src/io`: migrated production native C allocation, scratch, filesystem,
  cache, logging and bounded transport/job APIs. The macOS clipboard adapter is
  engine-owned too. Vexspoke supplies CPU-only spin/crypto/annotation/type contracts.
- `src/relational`, `src/reflection`: preserved C comparison material, never part
  of the production native target or a source of shadow consumer headers.
- `rust`: Cargo-recognized Rust scratchpad for the future storage manager.
  A learning backend now owns byte blocks and exposes a standalone C copy ABI;
  it is not the Vexspoke allocator or a production storage manager.
- `rust/src/nio/handle.rs`: the generation-tagged `Handle` identity for reusable
  typed slots — `TypedChunk`/`TypedPool` `add_handle`/`get_handle`/`remove_handle`
  reject a reused slot as stale.
- `docs/relational-engine-primer.md`: a plain-English primer on the engine, the
  C/Rust ABI and the data model.
- `rust/annotations` + `rust/src/annotation.rs`: a zero-dependency proc-macro
  crate providing the Rust form of the C `;;` annotation markers (`#[overview]`,
  `#[intention("...")]`, `#[what("T")]`, `#[checker]`, `#[hotcode]`, `#[debug]`, …);
  `annotation.rs` re-exports them so engine files use `crate::annotation::*`.
  Attributes are compile-time passthroughs (the Two-Semicolon Annotation Style Law).
- Owner and tooling tests live in the independent workspace `../../tests/relational-engine`.

The imported C reference retains Vexspoke's Boost Software License in
`src/LICENSE`; the repository's original MIT license remains at the root.

Rust owns allocation/chunk lifetime. Byte operations use copy-in/copy-out; the
native C name search now reads explicitly borrowed Rust-owned row spans. That
limited borrowing is not a complete zero-copy production engine, shared mutation,
whole-record atomicity or Vexspoke container migration.
See `rust/README.md` and run `python3 tests/relational-engine/rust/run.py`
from the workspace root for the owner/ABI suite.

## Build

The build entry is [b](https://github.com/vex-graph/b); with a
standalone b installation:

```sh
b build cargo rust
```

That builds only the Rust learning backend. The integrated production native
entry is `./tools/b build relational_engine` from the workspace root, with the
same engine dependency supplied to ordinary Vexspoke/Graphvex/Darling builds.
Rust editor analysis uses `rust/Cargo.toml`; workspace editor setup lives in
the workspace README.

## Verification and known gaps

### Persistent file views (Rust)

`PreallocatedFile!(path, length)` creates a NEW file, preserving existing
destinations. macOS uses `F_PREALLOCATE` with `F_ALLOCATEALL`; Linux uses kernel
`fallocate`. Reservation/EOF/allocation-query failure removes the new artifact;
cleanup failure reports both errors. Parents must already exist and remain
trusted/stable during creation and cleanup. Unix mode starts at 0600. No automatic
directory creation, Application Support policy or silent sparse fallback exists.

Applications can choose
`~/Library/Application Support/vexgraph/<app>/storage.bin`, reserve 1,000,000,000
bytes (decimal 1 GB), and retain the mapping until shutdown. The file exists in
Finder; mmap does not make the entire GB resident RAM. Checked offset writes and
bit changes use the same fixed extent. `FixedMappedFile::from_preallocated(owner)`
consumes the original descriptor without reopening its path, requiring the same
unsafe external-file exclusion as ordinary mapping admission.

**APFS copy-on-write, snapshots, quotas and device errors can still require more
space or fail on later edits.** Initial physical reservation is not a forever-space,
transaction or crash-durability guarantee. Success followed by mapping failure
leaves the reserved artifact for caller recovery. Real reservation and injected
stage-failure owners live under `../../tests/relational-engine/rust/io`; OS mapping/
sync/cleanup-error injection and other-host runtime proof remain gaps. See
`rust/README.md` for the Application Support example.

`FixedMappedFile!(path)` opens read-only; `FixedMappedFile!(path, true)` opens shared
read/write. Both require an explicit unsafe block: the caller must prevent other
access that mutates/truncates the file or violates writable exclusivity until
close/drop. `FixedMappedFile!()` creates a safe closed owner. Empty files are admitted
without an OS mapping. Admission never creates, truncates or resizes a file.

The application may retain this owner for its lifetime and use
`read(offset, length)` or `write(offset, bytes)`. Borrowed slices cannot outlive
the mapping; bounds/overflow reject before access. Persist **file identity plus
offset**, not a RAM pointer. The consumer validates record types/lengths and format.
Do not cast arbitrary file bytes to Rust structs containing references, bools or
other restricted bit patterns. No mapped C ABI or typed file-pointer API is shipped.

`flush()` requests mapped-page writeback; `sync()` also requests file synchronization.
OS errors propagate and previously written bytes are not rolled back. `close()`
does not sync. These operations and page faults have no hard latency bound, so
mapping is cold storage, never a realtime/hot-arena replacement. Symlinks follow
OS semantics: the API is not path confinement, and read-only mode alone does not
protect against external truncation. Untrusted mutable files need snapshot/copy
or genuinely enforced exclusive ownership before mapping.

Owner proof: `tests/relational-engine/rust/nio/{fixed_mapped_file,mapping_error}_test.rs`
and unsafe/borrow/type/arity negatives in `rust/run.py`. OS mapping/flush/sync
failure injection, huge-file admission, crash durability, Rust sanitizer instrumentation,
Windows and macOS 14 runtime compatibility remain unproved.

The row-pool cycle ran `python3 tests/relational-engine/rust/run.py`: debug/release
owners, intended compile failures, and real C row-pool clients with assertions,
`-Wall -Wextra -Werror` and C-side ASan/UBSan. Rust ASan was attempted separately
but skips: the installed Apple runtime lacks Rust's ASan v8 version symbol.
The installed Rust standard library also reports macOS 27 deployment metadata;
macOS 14 runtime compatibility is unproved despite selecting a 14.0 build target.
No Windows, performance or downstream application migration proof is implied.

### C consumers of Rust storage

Include `nio/relational_rows.h` with both engine `src` and `rust/include` on the
include path; link Cargo's `librelational_engine_scratchpad.a`. The public header
documents row geometry, status codes, borrow lifetime and caller serialization.
R1 owns the pool and drops it only after consumers detach. Byte rows contain no
Rust destructor; embedded C pointers stay caller-owned. Native `Memory_*` remains
unchanged. `./tools/b build relational_engine` still builds the native C target,
not this opt-in Rust library. The real usage example is in the
[primer](docs/relational-engine-primer.md#using-rust-storage-from-c--no-consumer-rewrite).

From the workspace root, run `python3 tests/relational-engine/scaffold_test.py`
for layout, mixed-ignore behavior,
workspace code-model coverage and a warnings-denied Cargo scaffold check.
These are tooling checks, not behavioral proof for the imported C classes.
The separate Rust suite exercises byte ownership, atomic byte/string publication
and a separate engine extern handshake through the standalone C ABI. The stable
row slice has registered per-class Rust owner targets in each
debug/release run, real C row-pool/registry/native-search clients, C-client ASan/UBSan,
exact invalid-span diagnostics and intended arity/type/borrow compile failures.
See the current checklist for executed commands and hashes, not a remembered
green. The registry C API exports `re_variables_new/drop/add/find/slot/set_pointer`
(each with the `re_variables_` prefix), with read-only borrowed slot views and
caller exclusion around rebinding/destruction. It owns labels, never their values.
C comparison dependency closure, Rust/native allocator equivalence, general zero-copy storage,
complete concurrency/fault coverage, performance, automatic record schema
migration, live Hotcwap reload integration and Windows execution remain unproved.

Universal architecture and proof follow the linked constitution in
`CONTRIBUTING.md` and `relational-engine-preferences.md`. The engine is the
production native IO/NIO owner; its Rust typed pools are a separate API. Engine
code/storage remain resident across consumer reloads; value replacement is not
automatic record schema migration.

Run `python3 tests/relational-engine/native_run.py` for strict optimized native
owners, release-like builds without scratch poisoning, and ASan/UBSan with
assertions active. Twelve owners execute per configuration; the clipboard
mutation owner explicitly skips without permission. The native allocator now
rejects physical backing exhaustion instead of returning untracked heap blocks.
Its exhaustion supplement covers all seven slab classes, bump spill, copied-byte
preservation, enumeration/reset, construction/registry allocation failures and
seeded mixed-size histories. Focused TSan uses `--owner mem_exhaustion_test
--mode thread`; its scope is initialized disjoint alloc/free, not concurrent
registry mutation or reset. See [native memory proof](docs/native-memory-proof.md)
and the checklist for actual execution and remaining gaps. HotFileSys remains a
draft no-op. No Windows, performance or live reload claim is implied.

## Scope and Limitations

**Scope:** R2 memory and storage — allocation, file/cache/log/transport IO,
stable typed rows, named bindings, native C search over borrowed spans, and the
shared type-id algebra. It is a backend, not a host or a driver.

**Deliberately not covered:**
- No GPU work: shaders, dispatch, capabilities and synchronization are Graphvex R3.
- No host/supervisor role: R1 `hotcwap` owns lifetimes/residency and excludes
  active users before destruction.
- Imported `src/relational` and `src/reflection` are comparison material, never
  part of the production native target.

**Known limits and gaps:**
- Fixed-extent Rust mmap bytes only; no resize/C mapping ABI, per-page checksums,
  hostile-file exclusion enforcement, transactions or crash durability.
- No automatic record-schema migration; whole-value replacement is not schema
  migration, and no C/Rust atomic-layout compatibility is assumed.
- Rust typed pools are a separate API from the native C allocator; the native ABI
  semantics are preserved, not rewritten in Rust.
- No Windows execution, general zero-copy storage, complete concurrency/fault
  coverage, or performance claim.
