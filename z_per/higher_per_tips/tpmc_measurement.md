# 本地 tpmC 测量记录

本文记录当前全部性能优化合并后的本地 TPC-C 吞吐测量，不属于单一优化点。

## 测量方法

- 驱动：`z_per/perf_tests/tpmc_official_window.py`。
- 数据：TPCC-Tester 生成的 scale=1 完整 TPC-C 兼容数据；时间列使用 RMDB 支持的 `char(19)` schema。
- 负载：16 个 TPCC-Tester worker，默认五类事务配比，读写比 0.92。
- 每轮：预热 30 秒，正式测量 360 秒；共 3 轮。
- 计数：在正式窗口起止分别读取 `orders` 行数，以增量作为成功提交的 NewOrder 数，按 `delta * 60 / 360` 计算 tpmC。
- 每轮结束后独立校验 District、Orders、NewOrders 和 OrderLine 的 TPC-C 不变量。

## 结果

2026-07-11 本地测量结果：

| 轮次 | 成功 NewOrder | tpmC |
| --- | ---: | ---: |
| 1 | 875 | 145.83 |
| 2 | 871 | 145.17 |
| 3 | 814 | 135.67 |

三轮中位数为 **145.17 tpmC**。

## 验证状态

- 每轮负载后的 TPC-C 不变量检查通过。
- 测量前已通过 Phase 2 加载、Phase 3 并发一致性、`kill -9` 恢复和官方形状事务矩阵。
- 计时驱动还完成了单线程纯 NewOrder 与 16 并发纯 NewOrder、混合事务短窗口预检。

## 适用边界

该结果用于本机回归和优化对比。正式评测使用其自身机器配置、仓库规模和测试器实现，不能把这个本地数值直接视作排行榜最终分数。
