# relational-engine — Repo-Local Living Preferences

## Constitution Link

The complete [workspace constitution](https://gist.github.com/vex-graph/4132a6c45cb6d3797c3e8eff2e94035a) governs this repository. The binding law list is `gist.md` (`../../../metadata/gists/preferences/gist.md`); `../../../PREFERENCES.md` is its full rationale and is never ingested whole. Read the law first, then this file, the proof lawbook `../../../tests/test-preferences.md` (slice one law with `./tools/prefs test "<title>"`), the evidence ledger `../../../metadata/gists/test-checklist.md`, and the actual implementation.

## Law Index (Binding Matrix)

| Law Title | Scope | Enforcement |
| :--- | :--- | :--- |
| Resident Storage Boundary Law | C/Rust ownership; consumer reload | Owner/ABI/lifetime tests; no unload with live users |
| Cross-Language Atomic Access Law | Shared primitives and strings | Atomic-op, snapshot/concurrency tests |
| One Rust Class Per File Law | Rust declarations | One struct/class or enum per file, private helpers too |
| CamelCase Rust Constructor Macro Law | Construction spelling | CamelCase!() macros; snake_case operations |
| R2 Responsibility Layout Law | Rust storage; C search | Owner checks; explicit unimplemented scope |
| Stable Row and Variable Binding Law | Chunks, bindings, 32-byte ABI | Growth/layout/failure/C-client owner tests |
| Fixed File Mapping Law | Fixed-extent file-backed byte views | Admission, checked ranges, borrow-negative + real owners |
| Growable File Mapping Law | Reservation-backed growing byte views | One C ABI; free-list reuse and platform gaps |
| Physical File Reservation Law | Create-new byte files | Real block proof; no sparse fallback; retained descriptors |

### Physical File Reservation Law

Rust `io/preallocated_file.rs` owns `PreallocatedFile`: caller-sized nonzero, create-new regular byte storage. macOS F_PREALLOCATE/F_ALLOCATEALL and Linux fallocate reserve disk before success; EOF is never reservation and allocated OS blocks must cover the extent. Unsupported hosts/filesystems and allocation failures reject without sparse fallback; Linux unproven, Windows has no backend (rejects Unsupported).

The host states a trusted/stable parent (e.g. private Application Support/vexgraph app storage); no parent or real-user artifact is created implicitly; Unix mode starts at 0600. Existing destinations/final symlinks reject unchanged. Creation-stage failure closes/removes the artifact; cleanup failure reports both errors. Not a hostile-directory sandbox or TOCTOU defence. Drop/close retains successful files.

`FixedMappedFile::from_preallocated` consumes the original descriptor, never reopening the pathname; its unsafe external-file exclusion still applies. Mapping failure retains the artifact for explicit recovery. No resize, format, checksums, transaction or persistence policy is added. APFS COW/snapshots/quotas/device errors may still need space or fail on later edits. Creation can block; keep it off realtime/bounded-join teardown paths.

File/path/length change only through creation/close/transfer (a scoped exception under the Conflict Triage Law and Single Class Per File Law (Java Law)), never setters that forge reservation. Registered real/injected owners under `../../../tests/relational-engine/rust/io` prove physical decimal 1 GB reservation, offset bit changes/reopen, rejection/cleanup/retry and descriptor provenance. OS mapping/sync/cleanup-error injection, Rust sanitizer, Linux/Windows and macOS14 runtime proof remain explicit gaps.

### Fixed File Mapping Law

Rust `nio/fixed_mapped_file.rs` owns `FixedMappedFile`: a fixed-extent existing regular-file mapping through memmap2; the owner retains the file until close/drop and unmaps before release. Read-only and shared read/write modes are explicit; empty files are valid open owners without an OS mapping. No implicit create/truncate/resize, C mapping ABI, internal dirty tracker, database format or typed reinterpretation; the native Memory implementation stays unchanged.

Opening is unsafe admission: the caller excludes external mutation/truncation and alias violations of writable exclusivity for the full mapping lifetime. Rust borrowing cannot constrain another process; advisory locks/permissions make hostile truncation no safer. Paths follow OS open semantics including symlinks — not sandbox confinement; untrusted mutable files need a different snapshot/copy or enforced ownership boundary.

Reads/writes validate bounds and arithmetic before access; rejection preserves bytes. Views borrow the owner; close/write/drop while a live incompatible Rust view is used fail compilation. Durable references are file identity plus offsets, never process addresses; format/type/record validation belongs to the consumer.

Flush requests mapped-page writeback; sync requests file synchronization; both expose OS errors through MappingError/Result, neither claims atomic publication, power-loss transactions or rollback of prior writes, and close does not implicitly sync. memmap2/std Drop does not expose unmap/descriptor-close failures. Mapping/page faults/open/flush/sync/cleanup have no hard latency bound and stay off realtime/bounded-join teardown paths. Geometry/mode/backing change only through admission/access/close (a scoped managed exception under the Conflict Triage Law and Single Class Per File Law (Java Law)), never arbitrary setters.

Registered owners: `fixed_mapped_file_test.rs` and `mapping_error_test.rs` under `../../../tests/relational-engine/rust/nio`, plus intended unsafe/type/arity/borrow compiler rejection cases in the main runner. OS mapping/flush/sync-failure injection, huge-file admission, Rust sanitizer instrumentation, Windows and macOS 14 runtime proof remain explicit gaps.

8. **C reaches the mapping through ONE thin extern-C ABI, never a second implementation.** The mapping is owned in Rust (`ui`/`nio/fixed_mapped_file.rs`); C consumes it through a hand-written `extern "C"` surface and the canonical C header, per the engine's existing Rust/FFI policy (explicit status codes as the failure channel; borrowed byte views read-only and bounded by the committed length; no escaped descriptor; no C/Rust atomic or layout assumptions). A real C client in the owner tests proves the handshake in debug and release; a second C implementation is a defect (the Vertical Integration Law).

### Growable File Mapping Law

#### Definition:

`GrowableMappedFile` (Rust `nio/`) owns a **fixed virtual RESERVATION** over a file it creates or reopens, plus a **committed length** growing inside that reservation; the mapping is made once and never re-made, so pointers taken before a growth step stay valid after it — distinct from `FixedMappedFile` (existing file, unchanging length, may be read-only).

#### The Why:

The store needs one growing file (darkbase's `.vexdb`): macOS lacks `mremap`, so re-mapping could move the base and invalidate live pointers; a reservation is cheap **virtual** address space whose file-backed pages page in on demand.

#### The Rule:

1. **Reservation is fixed at create**, caller-chosen; mapping made once; growing the reservation is not offered. **Opening is create's counterpart**, not a second creation: the caller states the same reservation, a too-short file is refused, a missing file is an OS failure rather than a silent create, and opening never resizes, truncates or allocates.
2. **Sparse growth by step.** Committed length grows in caller-sized steps; only the step's blocks are allocated, never the whole reservation up front.
3. **Allocate before publishing.** Reserve step blocks, then raise the committed length; a failed growth leaves length and every byte unchanged. Growth into blocks the file ALREADY holds reserves nothing, publishing length only — decided by observed allocation, never by the request.
4. **Never expose past committed.** Byte access is bounded by committed length, never the reservation; mapping longer than the file is legal, touching past EOF faults (SIGBUS).
5. **Released ranges are reused.** A free list of emptied page ranges (the caller's, or a sibling owner's) lets committed length plateau instead of ratcheting upward; reuse is explicit, never implicit compaction.
6. **Always read/write.** No read-only form: growth requires write access.
7. **Same exclusions as `FixedMappedFile`:** no file format, transaction, typed reinterpretation or concurrent writers; no implicit sync — flush/sync are explicit caller decisions; page faults are unbounded waits kept off joined-worker teardown paths (the Bounded Wait Law).
8. **Status: implemented, with stated gaps.** `nio/growable_mapped_file.rs` owns reservation and committed length; one `extern "C"` surface (`re_gmap_*`) reaches C; `io/preallocation.rs` gained `reserve_more` because `reserve` checks a whole extent, not an incremental step. Owner tests assert pointer stability across growth steps, byte survival, past-reservation/backwards growth refusal, unchanged length and bytes after failed growth, allocation *observed* through `st_blocks`, never assumed. Reopen proofs (Rust owner + real C client of the ABI) show opening never resizes, too-short reservation and unbackable committed extent refuse, growth back to held blocks reserves nothing. **Still missing:** free-list reuse (no release operation; committed length only ratchets up) and Windows/Linux runtime proof (path exists, rejects `Unsupported`, never run).

### Stable Row and Variable Binding Law

`nio/chunk.rs` owns a fixed-capacity typed row allocation; `struct/chunked_list.rs` grows its directory without moving initialized rows. Geometry is chosen at construction (named default, caller override), validated for size/alignment, immutable thereafter. Rows are append-only, never deleted/reused in this version. Growth and mutation require exclusive access — not a concurrent radix publication implementation. Raw row addresses survive growth but not destruction. Failed admission preserves initialized rows/length; rejected owned input is dropped.

`variable/variable_slot.rs` is a C-layout 32-byte binding: `[name: u8[24]][value pointer: 8 bytes]`, offsets 0/24 checked. Names are 1..23 ASCII bytes plus NUL and zero padding, folded lowercase under the existing segmented-name grammar; invalid input rejects without silent truncation or mutating prior state. This pointer is borrowed VALUE storage, not the legacy `StringSlot.self` link — the records must never be conflated; storing a pointer implies no dereference/ownership/type validation. Zero slots are unnamed/unbound; registry admission requires a valid name; plain binding replacement is exclusive, not atomic.

`VariableRegistry` owns stable slot rows and rejects folded duplicate names. Engine C `search/primitives/name_search` scans fixed 24-byte keys in borrowed leaves: no Vexspoke/host headers, no allocation, invalid spans admitted through engine `exception/throw.h`, output preserved on failure or absence; admission reports explicit Result/status codes, not Vexspoke THROW. The C ABI exposes an opaque registry and read-only borrowed slot views; the caller excludes reads before rebind/drop and retains pointed values. Pointers are live-process addresses, not persistent/SSD/GPU identifiers or portable serialization. Schema migration and a stable indirection cell remain future work; no public free/reuse/generation check exists — stale/non-live pointers are outside the ABI contract, not a claimed safe rejection. The first C adapter stands alone; migrating existing Vexspoke collections is not implied.

### R2 Responsibility Layout Law

`nio/typed_chunk.rs` owns aligned object rows with a packed occupancy bitmap; `struct/typed_pool.rs` owns a growable directory with lazy backing chunks — the reusable typed-storage slice, separate from the append-only classes. Caller-selected default is 1,024 rows per chunk, never a total ceiling. Exclusive removal transfers ownership out and reuses holes without moving live objects. Explicit empty-chunk release retains directory positions and requires excluding raw-pointer borrowers. Failed fallible growth preserves all live state and drops the rejected incoming value. Raw pool-local indices are reusable locations; the generation-tagged `Handle` surface (`add_handle`/`get_handle`/`get_handle_mut`/`remove_handle`) is the identity API and rejects a reused slot as stale. Not claimed: ecosystem type registry, C allocator parity, compaction, concurrent allocation. Optional-name integration, bulk and scratch remain future work.

R2 storage-backend placement, Vexspoke-borrow rules and production IO/NIO/allocator ownership are inherited from the canonical constitution (R2 split of ownership). The universal Relational Memory Law makes this repository's `src/nio/mem.c` the ecosystem's only allocator implementation: every other repository allocates through `Memory_alloc`/`MemoryArena_alloc`/`Transient_alloc`, and libc `malloc`/`free`/`calloc`/`realloc`/`strdup` outside this file is a defect. Stated specifics: engine `src/io`/`src/nio` own the production native C Memory/MemoryArena/Transient and file/cache/log/transport ABI (32-byte headers, legacy project type IDs, C allocation semantics preserved); default builds consume the engine; Rust typed pools stay a distinct API, not a silent allocator substitution.

Rust layout: `nio/` buffers, heap/foreign storage, fixed-extent file-backed mappings (`FixedMappedFile`/mmap); `io/` future buffered readers/writers, gathering, indexing, watching; `primitives/` byte/number/string values; `variable/` named bindings; `struct/` collections and relational records; `compress/` explicit codecs (ZIP/7z archives, ASTC textures), never transparent active-RAM compression; `virtual/` GPU-storage references and transfer contracts (reserved words: `r#struct`, `r#virtual`). Manifest-backed persistence stays proposed; no concurrent file-commit/durability contract is implemented. FFF = the file-search toolkit at https://github.com/dmtrKovalenko/fff (our equivalent remains future work). Graphics compute (GPU shaders, dispatch, capabilities, synchronization) lives in Graphvex; no engine `compute/` duplicates it; CPU atomic publication does not prove GPU completion or shared-memory accessibility. C `src/search/primitives` owns native primitive-span name search over Rust-owned bytes; `src/io`/`src/nio` are promoted migrated production implementations; the duplicated C `src/reflection`/`src/relational` reference copies are deleted — Vexspoke owns that live C layer, and the engine owns Rust storage plus the `nio`/`io`/`type` ABI; other planned Rust areas have no runtime API until implementation and registered owner proof exist. Preserve legacy `text` and root Rust aliases while moving existing string implementation to `primitives/`.

### CamelCase Rust Constructor Macro Law

Constructor macros use CamelCase matching the object concept: `Memory!()` and `Bytes!()`. Ordinary Rust methods and C-facing primitive operations use snake_case, e.g. `get_atomic_byte`. Compatibility lowercase macros may remain for existing clients but never replace the canonical spelling in new examples. A byte-view constructor borrows bytes; it does not imply allocation.

### One Rust Class Per File Law

One file per class: a class is its Rust struct plus its owning `impl` methods; the lowercase snake-case file name matches the class (Memory in `mem.rs` keeps the established memory-module spelling). Each implementation file defines at most one named struct or enum; the owning type's methods stay in its `impl` in that same file, public methods `pub fn`. Each enum, including MemoryError and private Value, has its own file; private helper records too. `mod.rs`/`lib.rs` declare/re-export modules and macros, not mixed behavioral types. Procedural FFI files may declare several operations but no owning class.

### Resident Storage Boundary Law

The opt-in RowPool C API owns runtime-selected aligned byte rows, not native Memory blocks or arbitrary Rust types. C sees an opaque owner and a RowHandle (process-local owner/index/generation/reserved; zero invalid; generation exhaustion retires slots; empty backing release retains identity history); Rust TypedChunk/TypedPool handles stay owner-local. Counts, backing, geometry and owner identity change only through construction/add/remove/release — a scoped managed exception under the Conflict Triage Law and Single Class Per File Law (Java Law) preserving ownership truth, never raw setters forging lifetime or identity. Row FFI reports failure via explicit status codes, not synchronous logging (the engine's Rust/FFI policy). All C calls need live disjoint spans and external serialization; borrowed row bytes are read-only; consumers detach before removal/destruction. Ordinary row allocation is fallible; cold projections keep Rust's abort-on-OOM. Racing owner-ID construction may reject for external retry, never loops. The native allocator stays unchanged; default builds never silently opt in.

R1 residency of engine code/storage is inherited from the canonical constitution (R2 split of ownership). Stated gaps: static linking is the current tested form; actual Hotcwap registration and live two-module reload remain unproved.

Whole-string replacement may change its byte length without invalidating prior Rust snapshot borrows — value replacement, not record-schema migration. Future schema upgrades must validate/prepare a replacement off the hot path, publish only after successful migration and preserve the previous state on failure; the present implementation claims no automatic schema migration.

### Cross-Language Atomic Access Law

Rust owns atomic storage; C calls the canonical engine C ABI and never casts Rust atomics to C `_Atomic` representations. Registration/release/destruction require exclusive access; existing atomic values permit concurrent atomic get/set while the registry stays frozen; non-atomic getters cannot read atomic cells; wrong storage kinds reject with a defined error.

Atomic Bytes use release stores/acquire loads. Atomic strings publish immutable byte snapshots with release/acquire; readers never lock; writers try-lock and return busy instead of waiting. Old snapshots survive until owner destruction. The caller-selected retention budget includes byte lengths and snapshot headers; exhaustion is observable and leaves the current value intact. String mutation is cold and may allocate. No lock-free writer, allocator-OOM recovery, hot-path registry lookup, UTF-8-only storage or whole-record atomicity is claimed.

The teaching block IDs are allocation handles, not ecosystem type IDs; the existing project-scoped type format and self-describing header are preserved contracts for a future backend this prototype does not implement yet.

## Proof

Owner tests live in the independent `../../../tests/relational-engine` checkout; migrated native IO/NIO owners in `../../../tests/relational-engine/io` and `../../../tests/relational-engine/nio`, including the Rust byte/string handshake. Use registered runners and the Timestamped Test Checklist Law; platform, sanitizer and migration gaps remain explicit, not production readiness.

## Readiness Cross-Reference (Living Documentation Law)

- Feature readiness matrix: [relational-engine](https://gist.github.com/vex-graph/6943f92acb931b25dad1073c46da6ce7#file-relational-engine-md).
- Open blockers and deferred decisions: [ecosystem blockers Gist](https://gist.github.com/vex-graph/e921fa188eebbd0c68c4e59646109887).
