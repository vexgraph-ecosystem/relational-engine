# C primitive search

Reserved for native C search over explicitly borrowed, length-bounded primitive
spans. Rust owns storage; C never frees it or assumes Rust atomic layout. Search
requires stable input for the call; GPU work completes before CPU access.

`name_search.{c,h}` compares fixed 24-byte keys at the start of each borrowed row.
`re_name_search` returns 0/found, 1/absent, or 2/invalid with one THROW diagnostic;
output changes only on a match. Keys are raw bytes; Rust owns name grammar/folding.
Non-null spans must be valid/live; arbitrary-address rejection is not promised.
Existing `src/relational/` and
other imported C directories remain comparison material, not the new runtime.
Owner tests mirror this directory under `../../../../../tests/relational-engine/search/primitives`.
