# Native memory: exhaustion and adversarial proof

This assessment concerns `src/nio/mem.{c,h}`, not Rust pools, mappings or disk
preallocation. The implementation is a finite preallocated slab/bump owner with
a growable owner directory. It is not a generational handle allocator or a
complete production-readiness certificate.

## Contract and repaired defects

- Exhausted slab storage may spill into the owner's remaining bump region.
  Exhausting both returns `nullptr`, without heap allocation or changing existing
  blocks. Select larger backing or another owner on a cold path when needed.
  The old heap fallback returned pointers outside every registered owner range:
  metadata/free rejected them, leaking storage. The exhaustion probe reproduced
  the unwanted success before this repair.
- Physical slab slots recycle individually. Bump blocks invalidate individually
  but retain reserved capacity until reset. Small bump spills must not be linked
  into a slab freelist: that mixed storage provenance, counts and reset behavior.
- Enumeration includes both live slab and bump blocks, returns the total count,
  and writes only the requested output capacity. Freed bump headers retain length
  for traversal, but their cleared sugar excludes them from live results.
- Reset clears used bump storage as well as resetting slabs. Rewinding alone
  left old bump headers accepted. Reset work is proportional to slab capacity
  plus used bump bytes, not constant time.
- Invalid-source realloc rejects before constructing a replacement. Successful
  grow/shrink copies the shorter length; failed replacement preserves the source.

Allocation/free of disjoint blocks may contend after single-threaded admission.
Init, registry mutation, reset, destroy, enumeration and shared-block mutation
require caller exclusion. Destroy releases backing; it does not preserve live
borrows. Freed pointers have no generations: address reuse can alias a new block.
Sugar detects ordinary corruption, not hostile deliberate checksum forgery.

## Executable scenarios

`../../../tests/relational-engine/nio/mem_test.c` retains the basic public-seam regression
and now checks every copied byte instead of only byte zero.
`../../../tests/relational-engine/nio/mem_exhaustion_test.c` supplements it with:

| Factor | Implemented test |
| :--- | :--- |
| Capacity | Exhaust each of seven slab classes; exhaust slab plus exact bump spill; repeated rejection and free/reuse recovery |
| Ownership | True A-to-B realloc, wrong-owner free preservation, bump spill not recycled as a slab |
| Preservation | Full-payload byte checks after rejection, growth/shrink and cross-owner transfer |
| Metadata | Large/spilled blocks enumerated; output canaries; freed/reset bump headers rejected |
| Construction failure | Inject descriptor, master-backing and owner-directory growth allocation failure; retry with surviving owners |
| Hostile admission | SIZE_MAX/UINT32 limits, misaligned/aligned interior pointers with zero-filled payloads, separate sugar/type/length corruption |
| Size histories | 10,000 seeded operations, 64 live-model slots and 26 payload sizes across every class transition, zero and large sizes |
| Legal contention | Four synchronized workers, 4,000 disjoint allocation/free operations each, post-join counts and reserved bytes |

The native runner substitutes libc calls only when compiling this test's mem.o;
production source has no test hooks. The ordinary umbrella target executes core
scenarios but not the substituted construction-failure/no-heap probes. All
registered native subprocesses have external watchdogs. Seeds are printed.

```sh
python3 tests/relational-engine/native_run.py
python3 tests/relational-engine/native_run.py --owner mem_exhaustion_test --mode thread
./tools/b test mem_exhaustion_test
```

The main runner covers optimized strict, release-like (no DEBUG_BORROW_CHECK)
and ASan/UBSan builds with assertions enabled. Exact commands/results/hashes live
in `../../../gist/test-checklist.md`; a runner's presence is not execution evidence.

## Not yet proved

The Deliberate Exhaustion and Backend Trust Law now requires thousands of
simultaneously live allocations across workers where legal. The current contention
case performs 16,000 operations but retains at most four worker-owned blocks at
once; its large-live-set exhaustion case is single-threaded. Neither proves the
combined concurrent-occupancy requirement. Multiple seeded schedules, explicit
live-address non-overlap under concurrent occupancy, and overlapping pressure
through exhaustion/recovery remain missing. Existing scoped passes are not
retroactively promoted by this law.

Windows/macOS 14 runtime, all legal schedules, long-running downstream loads,
host teardown exclusion, first-use concurrency, adversarial checksum forgery,
every metadata-corruption traversal, OS allocation pressure, generation-safe raw
pointer reuse and full performance/latency distributions remain unproved. macOS
ASan is not leak detection; construction-failure accounting is not an OS leak
certificate. No timing threshold or benchmark is used as a correctness oracle.
This is scoped passing evidence, not blanket battle-tested readiness.
