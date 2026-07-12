/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include <atomic>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <functional>
#include <chrono>
#include <memory>
#include <shared_mutex>

#include "transaction.h"
#include "watermark.h"
#include "recovery/log_manager.h"
#include "concurrency/lock_manager.h"
#include "system/sm_manager.h"
#include "common/exception.h"

class Context;

/* 系统采用的并发控制算法，当前题目中要求两阶段封锁并发控制算法 */
enum class ConcurrencyMode { TWO_PHASE_LOCKING = 0, BASIC_TO, MVCC };

struct TransactionLatchStats {
    uint64_t operation_requests{0};
    uint64_t operation_wait_us{0};
    uint64_t write_requests{0};
    uint64_t write_wait_us{0};
    uint64_t table_read_requests{0};
    uint64_t table_read_wait_us{0};
    uint64_t table_write_requests{0};
    uint64_t table_write_wait_us{0};
};

/// 版本链中的第一个撤销链接，将表堆元组链接到撤销日志。
struct VersionUndoLink {
    /** 版本链中的下一个版本。 */
    UndoLink prev_;
    bool in_progress_{false};

    friend auto operator==(const VersionUndoLink &a, const VersionUndoLink &b) {
        return a.prev_ == b.prev_ && a.in_progress_ == b.in_progress_;
    }

    friend auto operator!=(const VersionUndoLink &a, const VersionUndoLink &b) { return !(a == b); }

    inline static std::optional<VersionUndoLink> FromOptionalUndoLink(std::optional<UndoLink> undo_link) {
        if (undo_link.has_value()) {
            return VersionUndoLink{*undo_link};
        }
        return std::nullopt;
    }
};

class TransactionManager{
public:
    explicit TransactionManager(LockManager *lock_manager, SmManager *sm_manager,
                             ConcurrencyMode concurrency_mode = ConcurrencyMode::TWO_PHASE_LOCKING) {
        sm_manager_ = sm_manager;
        lock_manager_ = lock_manager;
        concurrency_mode_ = concurrency_mode;
    }
    
    ~TransactionManager() = default;

    Transaction* begin(Transaction* txn, LogManager* log_manager);

    void commit(Transaction* txn, LogManager* log_manager);

    void abort(Transaction* txn, LogManager* log_manager);

    ConcurrencyMode get_concurrency_mode() { return concurrency_mode_; }

    void set_concurrency_mode(ConcurrencyMode concurrency_mode) { concurrency_mode_ = concurrency_mode; }

    LockManager* get_lock_manager() { return lock_manager_; }

    void append_insert_version(const std::string &tab_name, const Rid &rid, const RmRecord &record, Transaction *txn);
    void append_update_version(const std::string &tab_name, const Rid &rid, const RmRecord &old_record,
                               const RmRecord &new_record, Transaction *txn);
    void append_delete_version(const std::string &tab_name, const Rid &rid, const RmRecord &old_record, Transaction *txn);

    std::vector<std::pair<Rid, RmRecord>> get_visible_records(const std::string &tab_name, RmFileHandle *fh,
                                                               Context *context);
    std::vector<std::pair<Rid, RmRecord>> get_visible_version_records(const std::string &tab_name, Context *context);
    std::unique_ptr<RmRecord> get_visible_record(const std::string &tab_name, const Rid &rid, RmFileHandle *fh,
                                                  Context *context,
                                                  std::unique_ptr<RmRecord> physical_record = nullptr);
    void check_write_conflict(const std::string &tab_name, const Rid &rid, Transaction *txn);
    void check_insert_conflict(const std::string &tab_name, const RmRecord &record, Transaction *txn);
    void record_select_predicate(const std::string &tab_name, const std::vector<Condition> &conds, Transaction *txn);
    void record_select_record(const std::string &tab_name, const Rid &rid, Transaction *txn);
    void check_ser_write(const std::string &tab_name, const Rid &rid, const RmRecord *old_record,
                         const RmRecord *new_record, Transaction *txn);
    void cleanup_abort(Transaction *txn);
    void retire_transaction(txn_id_t txn_id);
    void clear_table_versions(const std::string &tab_name);
    std::vector<txn_id_t> active_txn_ids(txn_id_t exclude_txn_id = INVALID_TXN_ID);
    std::vector<CheckpointTxnInfo> active_txn_infos(txn_id_t exclude_txn_id = INVALID_TXN_ID);
    std::unique_lock<std::shared_mutex> acquire_write_latch() {
        auto started = std::chrono::steady_clock::now();
        std::unique_lock<std::shared_mutex> guard(write_latch_);
        write_requests_.fetch_add(1, std::memory_order_relaxed);
        write_wait_us_.fetch_add(elapsed_us(started), std::memory_order_relaxed);
        return guard;
    }
    std::shared_lock<std::shared_mutex> acquire_read_latch() {
        auto started = std::chrono::steady_clock::now();
        std::shared_lock<std::shared_mutex> guard(write_latch_);
        operation_requests_.fetch_add(1, std::memory_order_relaxed);
        operation_wait_us_.fetch_add(elapsed_us(started), std::memory_order_relaxed);
        return guard;
    }
    std::shared_lock<std::shared_mutex> acquire_operation_latch() {
        return acquire_read_latch();
    }
    std::shared_lock<std::shared_mutex> acquire_table_read_latch(const std::string &tab_name) {
        auto latch = table_latch(tab_name);
        auto started = std::chrono::steady_clock::now();
        std::shared_lock<std::shared_mutex> guard(*latch);
        table_read_requests_.fetch_add(1, std::memory_order_relaxed);
        table_read_wait_us_.fetch_add(elapsed_us(started), std::memory_order_relaxed);
        return guard;
    }
    std::unique_lock<std::shared_mutex> acquire_table_write_latch(const std::string &tab_name) {
        auto latch = table_latch(tab_name);
        auto started = std::chrono::steady_clock::now();
        std::unique_lock<std::shared_mutex> guard(*latch);
        table_write_requests_.fetch_add(1, std::memory_order_relaxed);
        table_write_wait_us_.fetch_add(elapsed_us(started), std::memory_order_relaxed);
        return guard;
    }

    TransactionLatchStats latch_stats() const {
        return {operation_requests_.load(std::memory_order_relaxed), operation_wait_us_.load(std::memory_order_relaxed),
                write_requests_.load(std::memory_order_relaxed), write_wait_us_.load(std::memory_order_relaxed),
                table_read_requests_.load(std::memory_order_relaxed),
                table_read_wait_us_.load(std::memory_order_relaxed),
                table_write_requests_.load(std::memory_order_relaxed), table_write_wait_us_.load(std::memory_order_relaxed)};
    }

    /**
     * @description: 获取事务ID为txn_id的事务对象
     * @return {Transaction*} 事务对象的指针
     * @param {txn_id_t} txn_id 事务ID
     */    
    Transaction* get_transaction(txn_id_t txn_id) {
        if(txn_id == INVALID_TXN_ID) return nullptr;

        std::unique_lock<std::mutex> lock(latch_);
        auto txn_it = TransactionManager::txn_map.find(txn_id);
        if (txn_it == TransactionManager::txn_map.end()) {
            return nullptr;
        }
        auto *res = txn_it->second;
        lock.unlock();
        assert(res != nullptr);
        assert(res->get_thread_id() == std::this_thread::get_id());

        return res;
    }

    static std::unordered_map<txn_id_t, Transaction *> txn_map;     // 全局事务表，存放事务ID与事务对象的映射关系
    std::shared_mutex txn_map_mutex_;
    /** ------------------------以下函数仅可能在MVCC当中使用------------------------------------------*/

    /**
    * @brief 更新一个撤销链接，该链接将表堆元组与第一个撤销日志连接起来。
    * 在更新之前，将调用 `check` 函数以确保有效性。
    */
    bool UpdateUndoLink(Rid rid, std::optional<UndoLink> prev_link,
                        std::function<bool(std::optional<UndoLink>)> &&check = nullptr);

    /**
     * @brief 更新一个撤销链接，该链接将表堆元组与第一个撤销日志连接起来。
     * 在更新之前，将调用 `check` 函数以确保有效性。
     */
    bool UpdateVersionLink(Rid rid, std::optional<VersionUndoLink> prev_version,
                           std::function<bool(std::optional<VersionUndoLink>)> &&check = nullptr);

    /** @brief 获取表堆元组的第一个撤销日志。 */
    std::optional<UndoLink> GetUndoLink(Rid rid);

    /** @brief 获取表堆元组的第一个撤销日志。*/
    std::optional<VersionUndoLink> GetVersionLink(Rid rid);

    /** @brief 访问事务撤销日志缓冲区并获取撤销日志。如果事务不存在，返回 nullopt。
     * 如果索引超出范围仍然会抛出异常。 */
    std::optional<UndoLog> GetUndoLogOptional(UndoLink link);

    /** @brief 访问事务撤销日志缓冲区并获取撤销日志。除非访问当前事务缓冲区，
     * 否则应该始终调用此函数以获取撤销日志，而不是手动检索事务 shared_ptr 并访问缓冲区。 */
    UndoLog GetUndoLog(UndoLink link);

    /** @brief 获取系统中的最低读时间戳。 */
    timestamp_t GetWatermark();

    /** @brief 垃圾回收。仅在所有事务都未访问时调用。 */
    void GarbageCollection();

    struct PageVersionInfo {
        std::shared_mutex mutex_;
        /** 存储所有槽的先前版本信息。注意：不要使用 `[x]` 来访问它，因为
         * 即使不存在也会创建新元素。请使用 `find` 来代替。
         */
        std::unordered_map<slot_offset_t, VersionUndoLink> prev_version_;
    };

    /** 保护版本信息 */
    std::shared_mutex version_info_mutex_;
    /** 存储表堆中每个元组的先前版本。 */
    std::unordered_map<page_id_t, std::shared_ptr<PageVersionInfo>> version_info_;


private:
    struct VersionKey {
        std::string tab_name;
        Rid rid{};

        bool operator==(const VersionKey &other) const {
            return tab_name == other.tab_name && rid == other.rid;
        }
    };

    struct VersionKeyHash {
        size_t operator()(const VersionKey &key) const {
            size_t table_hash = std::hash<std::string>{}(key.tab_name);
            size_t rid_hash = std::hash<Rid>{}(key.rid);
            return table_hash ^ (rid_hash + 0x9e3779b9U + (table_hash << 6) + (table_hash >> 2));
        }
    };

    struct Version {
        Rid rid{};
        RmRecord record;
        txn_id_t txn_id{INVALID_TXN_ID};
        timestamp_t commit_ts{INVALID_TS};
        bool committed{false};
        bool aborted{false};
        bool deleted{false};
        bool old_image{false};
    };

    struct PredicateRead {
        std::string tab_name;
        std::vector<Condition> conds;
    };

    struct TxnSerState {
        std::set<std::pair<std::string, int64_t>> read_records;
        std::vector<PredicateRead> predicates;
        bool committed{false};
        bool aborted{false};
        timestamp_t commit_ts{INVALID_TS};
    };

    struct NoIndexWrite {
        RmRecord record;
        txn_id_t txn_id{INVALID_TXN_ID};
        timestamp_t commit_ts{INVALID_TS};
        bool committed{false};
        bool aborted{false};
    };

    VersionKey version_key(const std::string &tab_name, const Rid &rid) const;
    int64_t rid_key(const Rid &rid) const;
    size_t record_hash(const RmRecord &record) const;
    bool same_record(const RmRecord &lhs, const RmRecord &rhs) const;
    void remember_no_index_write(const std::string &tab_name, const RmRecord &record, Transaction *txn);
    void ensure_base_version(const std::string &tab_name, const Rid &rid, RmFileHandle *fh, Context *context);
    Version *latest_version(const std::string &tab_name, const Rid &rid);
    Version *visible_version(const std::string &tab_name, const Rid &rid, Transaction *txn);
    Version *visible_version(std::vector<Version> &versions, Transaction *txn);
    bool version_visible(const Version &version, Transaction *txn) const;
    bool record_matches(const std::string &tab_name, const std::vector<Condition> &conds, const RmRecord &record);
    bool add_rw_edge(txn_id_t reader, txn_id_t writer);
    bool creates_rw_cycle(txn_id_t from, txn_id_t to);
    bool creates_dangerous_structure(txn_id_t reader, txn_id_t writer);
    bool tout_committed_before_tin(txn_id_t tout, txn_id_t tin) const;
    bool txn_relevant_for_ser(txn_id_t txn_id, Transaction *current);
    std::shared_ptr<std::shared_mutex> table_latch(const std::string &tab_name) {
        std::lock_guard<std::mutex> lock(table_latches_mutex_);
        auto [it, inserted] = table_latches_.try_emplace(tab_name, std::make_shared<std::shared_mutex>());
        (void)inserted;
        return it->second;
    }

    ConcurrencyMode concurrency_mode_;      // 事务使用的并发控制算法，目前只需要考虑2PL
    std::atomic<txn_id_t> next_txn_id_{0};  // 用于分发事务ID
    std::atomic<timestamp_t> next_timestamp_{0};    // 用于分发事务时间戳
    std::mutex latch_;  // 用于txn_map的并发
    SmManager *sm_manager_;
    LockManager *lock_manager_;

    std::shared_mutex mvcc_latch_;
    std::shared_mutex write_latch_;
    std::mutex table_latches_mutex_;
    std::unordered_map<std::string, std::shared_ptr<std::shared_mutex>> table_latches_;
    std::unordered_set<txn_id_t> active_txns_;
    std::unordered_map<VersionKey, std::vector<Version>, VersionKeyHash> versions_;
    std::unordered_map<txn_id_t, std::unordered_set<VersionKey, VersionKeyHash>> txn_version_keys_;
    std::unordered_map<std::string, std::unordered_map<size_t, std::vector<NoIndexWrite>>> no_index_writes_;
    std::unordered_map<txn_id_t, std::vector<std::pair<std::string, size_t>>> txn_no_index_write_keys_;
    std::unordered_map<txn_id_t, TxnSerState> ser_states_;
    std::unordered_map<txn_id_t, timestamp_t> committed_ts_;
    std::unordered_map<txn_id_t, std::unordered_set<txn_id_t>> rw_edges_;
    std::atomic<uint64_t> operation_requests_{0};
    std::atomic<uint64_t> operation_wait_us_{0};
    std::atomic<uint64_t> write_requests_{0};
    std::atomic<uint64_t> write_wait_us_{0};
    std::atomic<uint64_t> table_read_requests_{0};
    std::atomic<uint64_t> table_read_wait_us_{0};
    std::atomic<uint64_t> table_write_requests_{0};
    std::atomic<uint64_t> table_write_wait_us_{0};

    std::atomic<timestamp_t> last_commit_ts_{0};    // 最后提交的时间戳,仅用于MVCC
    Watermark running_txns_{0};             // 存储所有正在运行事务的读取时间戳，以便于垃圾回收，仅用于MVCC

    static uint64_t elapsed_us(std::chrono::steady_clock::time_point started) {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count());
    }
};
