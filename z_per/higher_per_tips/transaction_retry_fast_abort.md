# Transaction Retry Fast Path

## Optimization point

TPC-C with scale factor 1 has unavoidable hot rows: all Payment transactions
touch the single warehouse row, and NewOrder transactions contend for the
district rows. Under snapshot isolation, a transaction that encounters an
uncommitted writer or a newer committed version must still abort and retry.

## Implementation

`TransactionManager::begin` now shares the operation/checkpoint latch rather
than exclusively taking it. A checkpoint still takes the exclusive form of the
same latch, so it either observes the newly registered transaction or runs
before that transaction begins.

`TransactionManager::abort` has a fast path only when both conditions hold:

1. its write set is empty;
2. it has no first WAL LSN.

Those transactions have neither changed a data/index page nor written WAL.
Their abort only removes active/MVCC bookkeeping while holding the shared
operation latch. Transactions with writes retain the former exclusive rollback
path, including physical undo, index repair, WAL abort records, and flush.

## Why it helps

The TPC-C tester log shows most retries fail at Payment Step 1 or NewOrder
Phase 1. Previously every one of those harmless early failures acquired the
global exclusive latch and paused all other statements. The new path allows
other tables and read operations to proceed while the loser is retired.

## Correctness boundary

This does not change write-write conflict detection, snapshot timestamps,
unique-index checks, commit durability, checkpoint exclusion, or any rollback
that has touched persistent state. Phase 2 load/rollback and Phase 3
concurrency/recovery tests remain mandatory regression checks.

## Verification (2026-07-11)

- Phase 2 load smoke: passed.
- Phase 3 16-worker live consistency and kill -9 recovery: passed.
- Phase 3 official-shape matrix: 33 explicit rollback stages, one automatic
  abort case, and 958 committed transactions passed.
- A 16-worker mixed TPC-C short run reported 516 successful transactions,
  2,759 retries, and 663.83 tester-reported tpmC. The retry ratio remains the
  next bottleneck; this change reduces its global-latch cost without changing
  the required abort decision.
