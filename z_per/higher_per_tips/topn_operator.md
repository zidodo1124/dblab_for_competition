# TopN 算子优化

本文记录新增执行算子的优化。实现保持 SQL 原有语义，不依赖固定表名、字段名或评测 SQL 文本。

## 优化点

`ORDER BY ... LIMIT N` 原先先物化全部匹配行并做完整稳定排序，再由 Limit 丢弃大部分结果。

## 优化方法

- 新增 `TopNPlan` 和 `TopNExecutor`。
- 当查询包含 ORDER BY 且 `0 <= LIMIT <= 128` 时，以 TopN 替换 Sort 加 Limit。
- 执行器按 ORDER BY 的多列与升降序规则维护已排序的至多 N 条候选记录；相同排序键保留输入顺序。
- 没有 LIMIT 或 LIMIT 大于 128 的查询继续使用原 Sort 和 Limit，避免在大 N 情况下退化。

## 优化原因

TPC-C OrderStatus 需要按订单号倒序取得最新订单，通常只需要一行。TopN 将排序阶段的内存从 O(匹配行数) 降为 O(N)，并避免对所有候选行进行完整排序。

## 验证

- `cmake --build build --target rmdb -j2` 已通过。
- 探针脚本：`python3 z_per/perf_tests/topn_order_status_probe.py`。
- 脚本模拟复合索引上的 OrderStatus 访问形状，校验最新订单结果、`TopN(limit=1)` 计划节点，并输出查询耗时。

## 后续验证要求

性能优化收敛后，必须重新执行 Phase 2 加载、Phase 3 一致性和崩溃恢复验证；最终以完整 TPC-C 负载的正确性检查与 tpmC 结果作为是否保留优化的依据。
