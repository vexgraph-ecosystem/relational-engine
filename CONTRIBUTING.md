# Contributing

Read the complete [workspace preferences.md constitution](https://gist.github.com/vex-graph/4132a6c45cb6d3797c3e8eff2e94035a)
before work. In the integrated workspace it is the real workspace-root
`../../../metadata/gists/preferences/gist.md`; read relevant laws with `./tools/prefs`.

The Git Workflow Law, Feature Implementation and Adversarial Proof Law,
Living Documentation Law and Test Segregation Law apply. Preserve imported
reference code until its replacement has an explicit contract and proof.

Imported C comparison files and the partial Rust/native-search implementation
are not two complete production engines.
Use b/native compiler tooling for actual work; do not compile out real diagnostics.

Read [relational-engine-preferences.md](relational-engine-preferences.md) for
the resident storage, atomic boundary and one-type-per-file contracts.
Public scratchpad code is not battle-tested readiness. Owner tests live in the
workspace `../../../tests/relational-engine` tree; generated binaries,
Cargo target output and IDE state stay ignored.

This repository belongs to [vexgraph-ecosystem](https://github.com/vexgraph-ecosystem/relational-engine)
at R2 with workspace path ``. Vexspoke
owns CPU computation/behavior; this engine owns memory/storage and native C
search. Production native IO/NIO and default Memory implementation have migrated
here from Vexspoke with compatible C semantics; Rust/container migration remains
staged. No duplicate IO/NIO implementation is permitted in Vexspoke. R1 owns
residency/lifetimes; GPU shaders/dispatch remain Graphvex R3. See README.md for
implemented scope and gaps, not a blanket readiness claim.
