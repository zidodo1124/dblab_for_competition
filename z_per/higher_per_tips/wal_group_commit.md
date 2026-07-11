# WAL 组提交与后台刷盘

本文记录日志管理器的提交路径优化。

## 优化点

旧实现中，每一个有写入的事务在 Commit 或 Abort 时都会直接持有日志互斥锁并调用日志文件写入。并发客户端即使几乎同时提交，也会形成一笔事务一次物理 WAL 写入的串行路径。

## 优化方法

- `LogManager` 启动单独的后台刷盘线程。
- Commit 和 Abort 先追加自己的日志记录，再按该记录 LSN 等待后台线程完成写入。
- 首个请求触发后，后台线程保留 1 ms 的短聚合窗口，将这一窗口内追加的 WAL 合并为一次连续写入。
- 日志缓冲区空间不足时，追加者请求后台刷盘并等待可用空间，不再由业务线程直接写日志文件。
- 缓冲池在写入脏页前仍通过 `flush_log_to_lsn(page_lsn)` 等待对应 WAL；检查点与析构路径仍使用同步等待接口。

## 正确性原因

- 每个提交者只会在 `persist_lsn` 已达到其 Commit 日志 LSN 后返回，因此提交确认不早于其 WAL 写入完成。
- 后台线程只有一个，批次顺序与 LSN 顺序一致；复制待写缓冲区后才允许下一批记录进入新缓冲区。
- 没有把 `write()` 替换为更弱的持久化操作，也没有改变现有系统对 OS 缓存与磁盘同步的语义。
- Abort 同样等待 Abort 日志写入，避免回滚路径因异步化而改变日志顺序。

## 验证

- `cmake --build build --target rmdb -j2` 已通过。
- `python3 z_per/perf_tests/wal_group_commit_probe.py` 已通过：8 个连接各执行 30 笔显式 Insert 事务，240 笔提交在强制终止、重启后全部恢复，耗时 0.212 秒。
- `python3 z_per/phase2_tests/phase2_load_smoke.py --port 18862` 已通过：TPC-C 形状数据加载、索引构建、显式回滚与失败加载原子性均正常。
- `python3 tools/phase3_consistency_stress.py --port 18863 --workers 16 --txns-per-worker 40` 已通过：实时一致性校验的 NewOrder=221、Payment=146、Delivery=130，耗时 1.866 秒；`kill -9` 重启恢复校验也通过。
- `python3 z_loctest/run_phase3_official_shape_matrix.py` 已通过：33 个显式回滚阶段和自动异常回滚均正常；官方形状并发校验完成 960 笔事务，耗时 17.638 秒。

## 后续方向

当前实现的目标是合并日志写入，同时维持提交等待语义。最终 tpmC 测试需要与原始版本对照，确认写入负载下的吞吐提升以及 P95/P99 延迟变化。
