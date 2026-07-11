# LRU Replacer Hot-Path Optimization

## Optimization point

The `index-multi` gprof profile showed a large number of calls to
`LRUReplacer::pin`, `LRUReplacer::unpin`, and the standard-library hash/list
operations they invoked. Every record and index page access can make an
unpinned page temporarily non-evictable and then evictable again.

## Implementation

Buffer-pool frame identifiers are dense integers in `[0, pool_size)`. The LRU
replacer now stores its doubly-linked LRU order in preallocated `prev`, `next`,
and membership arrays rather than allocating `std::list` nodes and updating an
`unordered_map` on every transition.

The policy is unchanged:

- `unpin` inserts an evictable frame at the most-recent end;
- `pin` removes it from the evictable set;
- `victim` removes the least-recent frame.

## Why it helps

The common pin/unpin path is now bounded O(1) array work with no per-access
heap allocation or hash-table rehashing. This targets both repeated scans and
index traversals without changing SQL planning, MVCC, WAL, or page eviction
semantics.

## Verification

- `LRUReplacerTest.SampleTest`: passed; confirms the historical victim order.
- Index range scan and index-update maintenance probes: passed.
- Phase 2 load/rollback smoke: passed.
- Phase 3 official-shape matrix and 16-worker kill -9 recovery stress: passed.
