# relational-engine — Repo-Local Living Preferences

## Constitution Link

The complete [workspace constitution](https://gist.github.com/vex-graph/4132a6c45cb6d3797c3e8eff2e94035a)
governs this repository. Local workspace path: `../../gist/PREFERENCES.md`.
Read it first, then this file, `../../tests/test-preferences.md`, current
`../../gist/test-checklist.md`, and the actual implementation.

## Law Index (Binding Matrix)

| Law Title | Scope | Enforcement |
| :--- | :--- | :--- |
| Resident Storage Boundary Law | C/Rust ownership and consumer reload | Owner/ABI/lifetime tests; no engine unload with live users |
| Cross-Language Atomic Access Law | Shared primitive values and strings | Typed atomic operations, snapshot/concurrency tests |
| One Rust Class Per File Law | Rust declarations | One struct/class or enum per implementation file, including private helpers |
| CamelCase Rust Constructor Macro Law | Rust construction spelling | CamelCase!() macros; snake_case ordinary operations |
| R2 Responsibility Layout Law | Rust storage and C search | Module/layout owner checks; explicit unimplemented scope |
| Stable Row and Variable Binding Law | Chunk/registry lifetimes and 32-byte ABI | Growth, layout, failure and C-client owner tests |
| Fixed File Mapping Law | Rust fixed-extent file-backed byte views and lifetime | Unsafe external-file admission, checked ranges, borrow-negative and real file owners |
| Growable File Mapping Law | Rust reservation-backed growing byte views | Implemented with one C ABI; free-list reuse and Windows/Linux proof remain gaps |
| Physical File Reservation Law | Rust create-new byte files | Real allocated-block proof, no sparse fallback, failure preservation and retained-descriptor transfer |

### Physical File Reservation Law

Rust `io/preallocated_file.rs` owns `PreallocatedFile`: caller-sized nonzero,
create-new regular byte storage. macOS F_PREALLOCATE/F_ALLOCATEALL and Linux kernel
fallocate reserve disk space before success; EOF alone is never reservation.
Allocated OS blocks must cover the requested extent. Unsupported hosts/filesystems
and allocation failures reject without sparse fallback. Linux is unproven;
Windows has no backend and rejects Unsupported.

The host selects an existing trusted/stable parent (for example private Application
Support/vexgraph app storage). No parent or implicit real-user artifact is created.
Unix mode starts at 0600. Existing destinations/final symlinks reject unchanged.
Creation-stage failure closes/removes the new artifact; cleanup failure reports
both errors. This stable-parent contract is not a hostile-directory sandbox or
TOCTOU defence; ancestor symlinks, same-user and privileged actors are not excluded.
Drop/close retains successful files.

`FixedMappedFile::from_preallocated` consumes the original descriptor, never reopening
the pathname; its unsafe external-file exclusion still applies. Mapping failure
retains the reserved artifact for explicit recovery. References remain file
identity plus offsets. No resize, format, checksums, transaction or persistence
policy is added. APFS COW/snapshots/quotas/device errors may still require more
space or fail on later edits; initial reservation is not an eternal space promise.
Creation/sync/cleanup can block and stay off realtime/bounded-join teardown paths.

File/path/length change only through creation/close/transfer, a scoped exception
under the Conflict Triage Law and Single Class Per File Law (Java Law), not setters
that forge reservation. Registered real/injected owners under
`../../tests/relational-engine/rust/io` prove physical decimal 1 GB reservation, offset
bit changes/reopen, rejection/cleanup/retry and descriptor provenance. OS mapping/
sync/cleanup-error injection, Rust sanitizer, Linux/Windows and macOS14 runtime
proof remain explicit gaps.

### Fixed File Mapping Law

Rust `nio/fixed_mapped_file.rs` owns `FixedMappedFile`, a fixed-extent existing regular-file
mapping through memmap2. The owner retains the file until close/drop and unmaps
before releasing it. Read-only and shared read/write modes are explicit; empty
files are valid open owners without an OS mapping. No implicit create/truncate/resize,
C mapping ABI, internal dirty tracker, database format or typed reinterpretation
is offered. The native Memory implementation remains unchanged.

Opening a file-backed mapping is unsafe admission: the caller excludes external
mutation/truncation and aliases violating writable exclusivity for the complete
mapping lifetime. Rust borrowing alone cannot constrain another process, and
advisory locks/permissions do not make hostile truncation safe. Paths follow OS
open semantics, including symlinks; this is not sandbox confinement. Untrusted
mutable files require a different snapshot/copy or enforced ownership boundary.

Offset/count reads and writes validate bounds and arithmetic before access;
rejection preserves bytes. Views borrow the owner; close/write/drop while a live
incompatible Rust view is used fail compilation. Application owners may retain
the mapping until shutdown. Durable references use file identity plus offsets,
never process addresses; format/type/record validation belongs to the consumer.

Flush requests mapped-page writeback; sync then requests file synchronization.
Both expose OS errors through MappingError/Result and neither claims atomic
publication, power-loss transactions or rollback of prior writes. Close does not
implicitly sync. memmap2/std Drop does not expose unmap/descriptor-close failures.
Mapping/page faults/open/flush/sync/cleanup have no hard latency bound and remain
off realtime and bounded-join teardown paths. Geometry/mode/backing mutate only
through admission/access/close, not arbitrary setters, a scoped managed exception
under the Conflict Triage Law and Single Class Per File Law (Java Law).

Registered owners are `fixed_mapped_file_test.rs` and `mapping_error_test.rs` under
`../../tests/relational-engine/rust/nio`, plus intended unsafe/type/arity/borrow
compiler rejection cases in the main runner. OS mapping/flush/sync-failure injection,
huge-file admission, Rust sanitizer instrumentation, Windows and macOS 14 runtime proof remain
explicit gaps, not inferred from a successful local round trip.

8. **C reaches the mapping through ONE thin extern-C ABI, never a second implementation.**
The mapping is owned in Rust (`ui`/`nio/fixed_mapped_file.rs`); C consumes it through a
hand-written `extern "C"` surface and the canonical C header, following the engine's
existing Rust/FFI policy: explicit status codes as the observable failure channel,
borrowed byte views read-only and bounded by the committed length, no escaped
descriptor, no C/Rust atomic or layout assumptions. A real C client in the owner tests
proves the handshake in both debug and release; a C mapping implementation of its own is
a defect, not an alternative (the Vertical Integration Law).

### Growable File Mapping Law

#### Definition:
`GrowableMappedFile` (Rust `nio/`) owns a **fixed virtual RESERVATION** over a file it
creates and owns, plus a **committed length** that grows inside that reservation. The
mapping is created once and never re-created, so every pointer handed out before a
growth step remains valid after it. That stability — not the growth itself — is the
entire reason this is a separate type from `FixedMappedFile`, which views an existing
file at a length that cannot change and may be read-only.

#### The Why:
The store needs one file that grows: darkbase's `.vexdb`. On macOS there is no
`mremap`, so re-mapping a grown file may return a different base address and
invalidate every live pointer — the exact failure this type exists to prevent. A
reservation costs nothing on 64-bit because it is **virtual** address space, not RAM:
a mapping may comfortably exceed physical memory and still work, because its pages are
backed by the file, paged in on demand. Reserved virtual space can be enormous;
resident bytes are bounded by RAM plus the file's allocated blocks. That distinction
must be stated wherever this type is described.

#### The Rule:
1. **Reservation is fixed at create**, chosen by the caller, and the mapping is made
   once. Growing the reservation itself is not offered: on macOS it would need a new
   mapping and could move the base.
2. **Sparse growth by step.** The committed length grows in caller-sized steps and
   only the step's blocks are allocated — never the whole reservation up front, so
   create stays cheap and `grow_to` stays meaningful.
3. **Allocate before publishing.** Blocks for the step are reserved first, and only
   then is the committed length raised. A failed growth leaves the length unchanged
   and every byte preserved; there is no half-grown state.
4. **Never expose past committed.** Byte access is bounded by the committed length,
   never by the reservation. Mapping longer than the file is legal; *touching* past
   EOF faults (SIGBUS). This is the type's most important line.
5. **Released ranges are reused.** A free list of emptied page ranges (the caller's
   list, or a sibling owner) lets the committed length plateau instead of ratcheting
   to a permanent high-water mark. Reuse is explicit, never implicit compaction.
6. **Always read/write.** There is no read-only form: growth requires write access.
7. **Same exclusions as `FixedMappedFile`:** no file format, no transaction, no
   implicit sync (flush/sync stay explicit caller decisions), no typed
   reinterpretation, no concurrent writers, and page faults are unbounded waits that
   must stay off joined-worker bounded-teardown paths (the Bounded Wait Law).
8. **Status: implemented, with stated gaps.** `nio/growable_mapped_file.rs` owns the
   reservation and the committed length, one `extern "C"` surface (`re_gmap_*`) reaches
   it from C, and `io/preallocation.rs` gained `reserve_more` because the existing
   `reserve` checks a whole extent and so cannot express an incremental step. The owner
   tests assert the property the type exists for: a pointer taken before a growth step
   is byte-identical after it, bytes survive growth, growth past the reservation and
   backwards growth are refused, and a failed growth leaves both length and bytes
   unchanged. Real allocation is *observed* through `st_blocks`, never assumed.
   **Still missing:** the free-list reuse of released ranges (there is no release
   operation yet, so the committed length only ratchets up), and Windows/Linux runtime
   proof — the code path exists and rejects `Unsupported`, but has never run there.

### Stable Row and Variable Binding Law

`nio/chunk.rs` owns a fixed-capacity typed row allocation; `struct/chunked_list.rs`
grows its directory without moving initialized rows. Geometry is chosen at
construction (named default, caller override), validated for size/alignment and
immutable thereafter. Rows are append-only, never deleted/reused in this version.
Growth and mutation require exclusive access; this is not a concurrent radix
publication implementation. Raw row addresses survive growth but not destruction.
Failed admission preserves initialized rows/length; rejected owned input is dropped.

`variable/variable_slot.rs` is a C-layout 32-byte binding: `[name: u8[24]][value
pointer: 8 bytes]`, with offsets 0/24 checked. Names are 1..23 ASCII bytes plus
NUL and zero padding, folded lowercase under the existing segmented-name grammar.
Invalid input rejects without silent truncation or mutating prior state.
This pointer is borrowed VALUE storage, not the legacy `StringSlot.self` link;
the two records must never be conflated. No dereference/ownership/type validation
is implied by storing a pointer. Zero slots are unnamed/unbound; registry admission
requires a valid name. Plain binding replacement is exclusive, not atomic.

`VariableRegistry` owns stable slot rows and rejects folded duplicate names.
Engine-owned C `search/primitives/name_search` scans fixed 24-byte keys in borrowed
leaves. It includes no Vexspoke/host headers, owns no allocation, reports invalid
span admission through engine `exception/throw.h`, and preserves output on failure
or absence. Rust/FFI use explicit Result/status codes for admission; they do not
depend on Vexspoke THROW. The C ABI exposes an opaque registry and read-only borrowed
slot views; caller excludes reads before rebind/drop and retains pointed values.
Pointers are live-process addresses, not persistent/SSD/GPU identifiers or portable
serialization. Schema migration and a stable indirection cell remain future work.
No public free/reuse/generation check exists; using stale/non-live pointers is
outside the ABI contract, not a claimed safe rejection. The first C adapter can
be used independently; migration of any existing Vexspoke collection is not implied.

### R2 Responsibility Layout Law

The reusable typed-storage slice is separate from the append-only classes:
`nio/typed_chunk.rs` owns aligned object rows and a packed occupancy bitmap;
`struct/typed_pool.rs` owns a growable directory with lazy backing chunks.
The caller-selected default is 1,024 rows per chunk, never a total ceiling.
Exclusive removal transfers ownership out and reuses holes without moving live
objects. Explicit empty-chunk release retains directory positions and requires
excluding raw-pointer borrowers. Failed fallible growth preserves all live state
and drops the rejected incoming value. Raw pool-local indices are reusable
locations; the generation-tagged `Handle` surface (`add_handle` / `get_handle` /
`get_handle_mut` / `remove_handle`) is the identity API and rejects a reused slot
as stale. No ecosystem type registry, C allocator parity, compaction or concurrent
allocation is claimed. Optional-name integration, bulk and scratch remain future work.

Relational-engine is an R2 storage backend alongside Vexspoke, not R1 and not
a GPU driver. Engine `src/io` and `src/nio` now own the production native C
Memory/MemoryArena/Transient and file/cache/log/transport ABI. Vexspoke has no
IO/NIO implementation copies; the production build consumes the engine by
default. C allocation semantics, 16-byte headers and legacy project type IDs
are preserved, not rewritten into Rust. Existing Rust typed pools remain a
distinct API, not a silent allocator substitution. Native code may borrow
Vexspoke CPU-only spin/crypto/type/annotation contracts; never downstream or
host headers, duplicate memory implementations or a recursive target graph.
Rust `nio/` owns buffers, heap/foreign storage and fixed-extent file-backed mappings
(`FixedMappedFile`/mmap); `io/` owns future buffered readers/writers,
gathering, indexing and watching. Manifest-backed persistence remains proposed;
no concurrent file-commit/durability contract is implemented or implied.
FFF means the file-search toolkit at https://github.com/dmtrKovalenko/fff,
not a new file format; our equivalent remains future work.
Rust `primitives/` owns byte/number/string values; `variable/` owns named bindings;
`struct/` owns collections and relational records. `compress/` is explicit codec
work (ZIP/7z archives, ASTC textures), never transparent active-RAM compression.
`virtual/` is reserved for GPU-storage references and transfer contracts.
Rust paths for reserved words use `r#struct` and `r#virtual`.
Graphics compute means GPU shaders and their execution; its implementations live
in Graphvex, including dispatch, capabilities and synchronization. No engine
`compute/` implementation duplicates that owner. CPU atomic publication does not
prove GPU completion or shared-memory accessibility.

C `src/search/primitives` contains native primitive-span name search over
Rust-owned bytes. C `src/io` and `src/nio` are promoted migrated production
implementations; `src/reflection` and `src/relational` remain comparison code.
Other planned Rust areas have no runtime API until
implementation and registered owner proof exist. Preserve legacy `text` and root
Rust aliases while moving existing string implementation to `primitives/`.

### CamelCase Rust Constructor Macro Law

Constructor macros use CamelCase matching the object concept: `Memory!()` and
`Bytes!()`. Ordinary Rust methods and C-facing primitive operations use
snake_case, such as `get_atomic_byte`. Compatibility lowercase macros may remain
for existing clients but do not replace the canonical constructor spelling in
new examples. A byte-view constructor borrows bytes; it does not imply allocation.

### One Rust Class Per File Law

One file per class: a class is its Rust struct plus its owning `impl` methods.
The lowercase snake-case file name matches the class (Memory in `mem.rs` keeps
the established memory-module spelling). Each Rust implementation file defines
one named struct or enum at most. Keep the owning type's methods in its `impl`
in that same file; public methods still use `pub fn` so
clients can call them. Each enum, including MemoryError and private Value, has
its own file. Private helper records also get their own files. `mod.rs` and
`lib.rs` declare/re-export modules and macros, not mixed behavioral types.
Procedural FFI files may declare several operations but no owning class.

### Resident Storage Boundary Law

The opt-in RowPool C API owns runtime-selected aligned byte rows, not native
Memory blocks or arbitrary Rust types. C sees an opaque owner and a RowHandle
with process-local owner/index/generation/reserved fields. Zero is invalid;
generation exhaustion retires slots, and empty backing release retains identity
history. Rust TypedChunk/TypedPool handles remain owner-local. Counts, backing,
geometry and owner identity change through construction/add/remove/release only,
not raw setters that could forge lifetime or identity. This scoped managed
exception under the Conflict Triage Law and Single Class Per File Law preserves
ownership truth. Row FFI uses explicit status codes as its observable failure
channel, matching the engine's existing Rust/FFI policy, not synchronous logging.
All C calls require live disjoint spans and external serialization; borrowed row
bytes are read-only and consumers detach before removal/destruction. Ordinary
row allocation is fallible; cold projections retain Rust's abort-on-OOM behavior.
Racing owner-ID construction may reject for external retry, never loops. The
native allocator remains unchanged and default builds do not silently opt in.

The engine is the production IO/NIO implementation owner for Vexspoke and
database consumers, not their supervisor. Its migrated native IO/NIO borrows
Vexspoke CPU-only contracts under the constitution's explicit R2 seam; it never
includes Darkbase/Hotcwap or other downstream headers. Native migrated code
is compiled and linked by the default build. Other imported comparison code
does not confer standalone runtime dependency closure.

R1 keeps engine code and storage resident while consumer code reloads. Owners
belong to the host lifetime, never unloadable consumer globals. Stop admission
and finish active reads/writes before destruction. Static linking is the current
tested form; actual Hotcwap registration and live two-module reload remain gaps.

Whole-string replacement may change its byte length without invalidating prior
Rust snapshot borrows. This is value replacement, not record-schema migration.
Future schema upgrades must validate/prepare a replacement off the hot path,
publish only after successful migration and preserve the previous state on
failure; no automatic schema migration is claimed by the present implementation.

### Cross-Language Atomic Access Law

Rust owns atomic storage; C calls the canonical engine C ABI and never casts
Rust atomics to C `_Atomic` representations. Registration/release/destruction
require exclusive access. Existing atomic values permit concurrent atomic
get/set while the registry stays frozen. Non-atomic getters cannot read atomic
cells; wrong storage kinds reject with a defined error.

Atomic Bytes use release stores/acquire loads. Atomic strings publish immutable
byte snapshots with release/acquire; readers never lock. Writers use try-lock
and return busy instead of waiting. Old snapshots survive until owner destruction.
The caller-selected retention budget includes byte lengths and snapshot headers;
exhaustion is observable and leaves the current value intact. String mutation is
cold and may allocate. No lock-free writer, allocator-OOM recovery, hot-path
registry lookup, UTF-8-only storage or whole-record atomicity is claimed.

The teaching block IDs are allocation handles, not ecosystem type IDs. The
existing project-scoped type format and self-describing header are preserved
contracts for a future backend; this prototype does not implement them yet.

## Proof

Owner tests live in the independent `../../tests/relational-engine` checkout.
Migrated native IO/NIO owners live in `../../tests/relational-engine/io` and
`../../tests/relational-engine/nio`, including the Rust byte/string handshake.
Use registered runners and the Timestamped Test Checklist Law; current platform,
sanitizer and migration gaps remain explicit, not production readiness.

## Readiness Cross-Reference (Living Documentation Law)

- Feature readiness matrix: [relational-engine](https://gist.github.com/vex-graph/6943f92acb931b25dad1073c46da6ce7#file-relational-engine-md).
- Open blockers and deferred decisions: [ecosystem blockers Gist](https://gist.github.com/vex-graph/e921fa188eebbd0c68c4e59646109887).
