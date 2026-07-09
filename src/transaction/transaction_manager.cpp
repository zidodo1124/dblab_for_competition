/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "transaction_manager.h"

#include <algorithm>
#include <cstring>
#include <queue>

#include "index/ix.h"
#include "common/context.h"
#include "record/rm_file_handle.h"
#include "record/rm_scan.h"
#include "system/sm_manager.h"

std::unordered_map<txn_id_t, Transaction *> TransactionManager::txn_map = {};

namespace {
std::vector<char> make_index_key(const RmRecord &rec, const IndexMeta &index) {
    std::vector<char> key(index.col_tot_len);
    int offset = 0;
    for (int i = 0; i < index.col_num; ++i) {
        memcpy(key.data() + offset, rec.data + index.cols[i].offset, index.cols[i].len);
        offset += index.cols[i].len;
    }
    return key;
}

void clear_write_set(Transaction *txn) {
    auto write_set = txn->get_write_set();
    while (!write_set->empty()) {
        delete write_set->front();
        write_set->pop_front();
    }
}
}  // namespace

/**
 * @description: 事务的开始方法
 * @return {Transaction*} 开始事务的指针
 * @param {Transaction*} txn 事务指针，空指针代表需要创建新事务，否则开始已有事务
 * @param {LogManager*} log_manager 日志管理器指针
 */
Transaction * TransactionManager::begin(Transaction* txn, LogManager* log_manager) {
    auto write_guard = acquire_write_latch();
    if (txn == nullptr) {
        txn = new Transaction(next_txn_id_++);
    }
    txn->set_state(TransactionState::GROWING);
    txn->set_start_ts(next_timestamp_++);

    std::unique_lock<std::mutex> lock(latch_);
    TransactionManager::txn_map[txn->get_transaction_id()] = txn;
    {
        std::lock_guard<std::mutex> mvcc_lock(mvcc_latch_);
        ser_states_[txn->get_transaction_id()];
    }
    if (log_manager != nullptr) {
        BeginLogRecord log_record(txn->get_transaction_id());
        log_manager->add_log_record(&log_record, txn);
    }
    return txn;
}

/**
 * @description: 事务的提交方法
 * @param {Transaction*} txn 需要提交的事务
 * @param {LogManager*} log_manager 日志管理器指针
 */
void TransactionManager::commit(Transaction* txn, LogManager* log_manager) {
    auto write_guard = acquire_write_latch();
    if (txn == nullptr) {
        return;
    }
    if (txn->get_state() == TransactionState::ABORTED || txn->get_state() == TransactionState::COMMITTED) {
        return;
    }
    timestamp_t commit_ts = next_timestamp_++;
    {
        std::lock_guard<std::mutex> mvcc_lock(mvcc_latch_);
        txn_id_t txn_id = txn->get_transaction_id();
        auto key_it = txn_version_keys_.find(txn_id);
        if (key_it != txn_version_keys_.end()) {
            for (const auto &key : key_it->second) {
                auto version_it = versions_.find(key);
                if (version_it == versions_.end()) {
                    continue;
                }
                for (auto &version : version_it->second) {
                    if (version.txn_id != txn_id || version.aborted) {
                        continue;
                    }
                    version.committed = true;
                    version.commit_ts = commit_ts;
                }
            }
            txn_version_keys_.erase(key_it);
        }
        committed_ts_[txn_id] = commit_ts;
        auto ser_it = ser_states_.find(txn_id);
        if (ser_it != ser_states_.end()) {
            auto &ser_state = ser_it->second;
            ser_state.committed = true;
            ser_state.commit_ts = commit_ts;
            bool has_edges = rw_edges_.count(txn_id) > 0;
            if (!has_edges) {
                for (auto &entry : rw_edges_) {
                    if (entry.second.count(txn_id) > 0) {
                        has_edges = true;
                        break;
                    }
                }
            }
            if (!has_edges && ser_state.read_records.empty() && ser_state.predicates.empty()) {
                ser_states_.erase(ser_it);
            }
        }
    }
    clear_write_set(txn);
    auto lock_set = txn->get_lock_set();
    while (!lock_set->empty()) {
        auto lock_data_id = *lock_set->begin();
        lock_manager_->unlock(txn, lock_data_id);
    }
    if (log_manager != nullptr) {
        CommitLogRecord log_record(txn->get_transaction_id());
        log_manager->add_log_record(&log_record, txn);
        log_manager->flush_log_to_disk();
    }
    txn->set_state(TransactionState::COMMITTED);
}

/**
 * @description: 事务的终止（回滚）方法
 * @param {Transaction *} txn 需要回滚的事务
 * @param {LogManager} *log_manager 日志管理器指针
 */
void TransactionManager::abort(Transaction * txn, LogManager *log_manager) {
    auto write_guard = acquire_write_latch();
    if (txn == nullptr) {
        return;
    }
    if (txn->get_state() == TransactionState::ABORTED) {
        cleanup_abort(txn);
        return;
    }
    if (txn->get_state() == TransactionState::COMMITTED) {
        return;
    }
    auto write_set = txn->get_write_set();
    while (!write_set->empty()) {
        WriteRecord *write_record = write_set->back();
        write_set->pop_back();

        const std::string tab_name = write_record->GetTableName();
        const Rid rid = write_record->GetRid();
        TabMeta &tab = sm_manager_->db_.get_table(tab_name);
        RmFileHandle *fh = sm_manager_->fhs_.at(tab_name).get();
        Context rollback_context(lock_manager_, log_manager, txn);

        if (write_record->GetWriteType() == WType::INSERT_TUPLE) {
            auto rec = fh->get_record(rid, &rollback_context);
            for (auto &index : tab.indexes) {
                auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name, index.cols)).get();
                auto key = make_index_key(*rec, index);
                ih->delete_entry(key.data(), txn);
            }
            fh->delete_record(rid, &rollback_context);
        } else if (write_record->GetWriteType() == WType::DELETE_TUPLE) {
            RmRecord &old_rec = write_record->GetRecord();
            fh->insert_record(rid, old_rec.data);
            for (auto &index : tab.indexes) {
                auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name, index.cols)).get();
                auto key = make_index_key(old_rec, index);
                ih->insert_entry(key.data(), rid, txn);
            }
        } else if (write_record->GetWriteType() == WType::UPDATE_TUPLE) {
            auto cur_rec = fh->get_record(rid, &rollback_context);
            for (auto &index : tab.indexes) {
                auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name, index.cols)).get();
                auto key = make_index_key(*cur_rec, index);
                ih->delete_entry(key.data(), txn);
            }
            RmRecord &old_rec = write_record->GetRecord();
            fh->update_record(rid, old_rec.data, &rollback_context);
            for (auto &index : tab.indexes) {
                auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name, index.cols)).get();
                auto key = make_index_key(old_rec, index);
                ih->insert_entry(key.data(), rid, txn);
            }
        }
        delete write_record;
    }

    auto lock_set = txn->get_lock_set();
    while (!lock_set->empty()) {
        auto lock_data_id = *lock_set->begin();
        lock_manager_->unlock(txn, lock_data_id);
    }
    if (log_manager != nullptr) {
        AbortLogRecord log_record(txn->get_transaction_id());
        log_manager->add_log_record(&log_record, txn);
        log_manager->flush_log_to_disk();
    }
    txn->set_state(TransactionState::ABORTED);
    cleanup_abort(txn);
}

std::string TransactionManager::version_key(const std::string &tab_name, const Rid &rid) const {
    return tab_name + "#" + std::to_string(rid.page_no) + "#" + std::to_string(rid.slot_no);
}

int64_t TransactionManager::rid_key(const Rid &rid) const {
    return (static_cast<int64_t>(rid.page_no) << 32) | static_cast<unsigned int>(rid.slot_no);
}

void TransactionManager::ensure_base_version(const std::string &tab_name, const Rid &rid, RmFileHandle *fh,
                                             Context *context) {
    std::string key = version_key(tab_name, rid);
    if (versions_.find(key) != versions_.end()) {
        return;
    }
    auto rec = fh->get_record(rid, context);
    Version version;
    version.rid = rid;
    version.record = *rec;
    version.txn_id = INVALID_TXN_ID;
    version.commit_ts = 0;
    version.committed = true;
    versions_[key].push_back(version);
}

TransactionManager::Version *TransactionManager::latest_version(const std::string &tab_name, const Rid &rid) {
    auto it = versions_.find(version_key(tab_name, rid));
    if (it == versions_.end()) {
        return nullptr;
    }
    for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
        if (!rit->aborted && !rit->old_image) {
            return &(*rit);
        }
    }
    return nullptr;
}

bool TransactionManager::version_visible(const Version &version, Transaction *txn) const {
    if (version.aborted || version.old_image) {
        return false;
    }
    if (txn != nullptr && version.txn_id == txn->get_transaction_id()) {
        return true;
    }
    return version.committed && version.commit_ts <= txn->get_start_ts();
}

TransactionManager::Version *TransactionManager::visible_version(const std::string &tab_name, const Rid &rid,
                                                                 Transaction *txn) {
    auto it = versions_.find(version_key(tab_name, rid));
    if (it == versions_.end()) {
        return nullptr;
    }
    for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit) {
        if (version_visible(*rit, txn)) {
            return &(*rit);
        }
    }
    return nullptr;
}

std::vector<std::pair<Rid, RmRecord>> TransactionManager::get_visible_records(const std::string &tab_name,
                                                                               RmFileHandle *fh, Context *context) {
    std::lock_guard<std::mutex> lock(mvcc_latch_);
    std::unordered_set<Rid> seen;
    for (RmScan scan(fh); !scan.is_end(); scan.next()) {
        Rid rid = scan.rid();
        ensure_base_version(tab_name, rid, fh, context);
        seen.insert(rid);
    }
    for (auto &entry : versions_) {
        for (auto &version : entry.second) {
            (void)version;
        }
    }

    std::vector<std::pair<Rid, RmRecord>> result;
    for (auto &entry : versions_) {
        auto &list = entry.second;
        if (list.empty()) {
            continue;
        }
        const Rid rid = list.front().rid;
        if (entry.first.rfind(tab_name + "#", 0) != 0) {
            continue;
        }
        Version *visible = visible_version(tab_name, rid, context->txn_);
        if (visible != nullptr && !visible->deleted) {
            result.emplace_back(rid, visible->record);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        if (a.first.page_no != b.first.page_no) {
            return a.first.page_no < b.first.page_no;
        }
        return a.first.slot_no < b.first.slot_no;
    });
    return result;
}

std::unique_ptr<RmRecord> TransactionManager::get_visible_record(const std::string &tab_name, const Rid &rid,
                                                                  RmFileHandle *fh, Context *context) {
    std::lock_guard<std::mutex> lock(mvcc_latch_);
    ensure_base_version(tab_name, rid, fh, context);
    Version *visible = visible_version(tab_name, rid, context->txn_);
    if (visible == nullptr || visible->deleted) {
        return nullptr;
    }
    return std::make_unique<RmRecord>(visible->record);
}

void TransactionManager::check_write_conflict(const std::string &tab_name, const Rid &rid, Transaction *txn) {
    std::lock_guard<std::mutex> lock(mvcc_latch_);
    Version *latest = latest_version(tab_name, rid);
    if (latest == nullptr || txn == nullptr) {
        return;
    }
    if (latest->txn_id == txn->get_transaction_id()) {
        return;
    }
    if (!latest->committed) {
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
    }
    if (latest->commit_ts > txn->get_start_ts()) {
        throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
    }
}

void TransactionManager::check_insert_conflict(const std::string &tab_name, const RmRecord &record, Transaction *txn) {
    if (txn == nullptr) {
        return;
    }
    const auto &tab = sm_manager_->db_.get_table(tab_name);
    if (tab.indexes.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mvcc_latch_);
    for (auto &entry : versions_) {
        if (entry.first.rfind(tab_name + "#", 0) != 0 || entry.second.empty()) {
            continue;
        }
        Version *latest = nullptr;
        for (auto rit = entry.second.rbegin(); rit != entry.second.rend(); ++rit) {
            if (!rit->aborted && !rit->old_image) {
                latest = &(*rit);
                break;
            }
        }
        if (latest == nullptr || latest->txn_id == txn->get_transaction_id()) {
            continue;
        }

        bool same_logical_record = false;
        if (!tab.indexes.empty()) {
            for (auto &index : tab.indexes) {
                auto lhs = make_index_key(record, index);
                auto rhs = make_index_key(latest->record, index);
                if (memcmp(lhs.data(), rhs.data(), index.col_tot_len) == 0) {
                    same_logical_record = true;
                    break;
                }
            }
        } else if (record.size == latest->record.size &&
                   memcmp(record.data, latest->record.data, record.size) == 0) {
            same_logical_record = true;
        }

        if (!same_logical_record) {
            continue;
        }
        if (!latest->committed || latest->commit_ts > txn->get_start_ts()) {
            throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
        }
    }
}

void TransactionManager::append_insert_version(const std::string &tab_name, const Rid &rid, const RmRecord &record,
                                               Transaction *txn) {
    if (txn == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mvcc_latch_);
    std::string key = version_key(tab_name, rid);
    Version version;
    version.rid = rid;
    version.record = record;
    version.txn_id = txn->get_transaction_id();
    versions_[key].push_back(version);
    txn_version_keys_[txn->get_transaction_id()].insert(std::move(key));
}

void TransactionManager::append_update_version(const std::string &tab_name, const Rid &rid,
                                               const RmRecord &old_record, const RmRecord &new_record,
                                               Transaction *txn) {
    if (txn == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mvcc_latch_);
    std::string key = version_key(tab_name, rid);
    auto &list = versions_[key];
    if (list.empty()) {
        Version base;
        base.rid = rid;
        base.record = old_record;
        base.txn_id = INVALID_TXN_ID;
        base.commit_ts = 0;
        base.committed = true;
        list.push_back(base);
    }
    Version version;
    version.rid = rid;
    version.record = new_record;
    version.txn_id = txn->get_transaction_id();
    list.push_back(version);
    txn_version_keys_[txn->get_transaction_id()].insert(std::move(key));
}

void TransactionManager::append_delete_version(const std::string &tab_name, const Rid &rid,
                                               const RmRecord &old_record, Transaction *txn) {
    if (txn == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mvcc_latch_);
    std::string key = version_key(tab_name, rid);
    auto &list = versions_[key];
    if (list.empty()) {
        Version base;
        base.rid = rid;
        base.record = old_record;
        base.txn_id = INVALID_TXN_ID;
        base.commit_ts = 0;
        base.committed = true;
        list.push_back(base);
    }
    Version version;
    version.rid = rid;
    version.record = old_record;
    version.txn_id = txn->get_transaction_id();
    version.deleted = true;
    list.push_back(version);
    txn_version_keys_[txn->get_transaction_id()].insert(std::move(key));
}

bool TransactionManager::record_matches(const std::string &tab_name, const std::vector<Condition> &conds,
                                        const RmRecord &record) {
    const auto &cols = sm_manager_->db_.get_table(tab_name).cols;
    for (const auto &cond : conds) {
        auto col_it = std::find_if(cols.begin(), cols.end(), [&](const ColMeta &col) {
            return col.name == cond.lhs_col.col_name;
        });
        if (col_it == cols.end()) {
            continue;
        }
        const char *lhs = record.data + col_it->offset;
        const char *rhs = cond.is_rhs_val ? cond.rhs_val.raw->data : nullptr;
        int cmp = 0;
        if (col_it->type == TYPE_INT) {
            int lhs_val = *reinterpret_cast<const int *>(lhs);
            int rhs_val = *reinterpret_cast<const int *>(rhs);
            cmp = (lhs_val > rhs_val) - (lhs_val < rhs_val);
        } else if (col_it->type == TYPE_FLOAT) {
            float lhs_val = *reinterpret_cast<const float *>(lhs);
            float rhs_val = *reinterpret_cast<const float *>(rhs);
            cmp = (lhs_val > rhs_val) - (lhs_val < rhs_val);
        } else {
            cmp = strncmp(lhs, rhs, col_it->len);
        }
        bool ok = false;
        switch (cond.op) {
            case OP_EQ: ok = cmp == 0; break;
            case OP_NE: ok = cmp != 0; break;
            case OP_LT: ok = cmp < 0; break;
            case OP_GT: ok = cmp > 0; break;
            case OP_LE: ok = cmp <= 0; break;
            case OP_GE: ok = cmp >= 0; break;
        }
        if (!ok) {
            return false;
        }
    }
    return true;
}

bool TransactionManager::txn_relevant_for_ser(txn_id_t txn_id, Transaction *current) {
    if (current == nullptr || txn_id == current->get_transaction_id()) {
        return false;
    }
    auto txn_it = txn_map.find(txn_id);
    if (txn_it == txn_map.end()) {
        return true;
    }
    return txn_it->second->get_state() != TransactionState::ABORTED;
}

void TransactionManager::record_select_predicate(const std::string &tab_name, const std::vector<Condition> &conds,
                                                 Transaction *txn) {
    if (txn == nullptr || txn->get_isolation_level() != IsolationLevel::SERIALIZABLE) {
        return;
    }
    std::lock_guard<std::mutex> lock(mvcc_latch_);
    auto &state = ser_states_[txn->get_transaction_id()];
    state.predicates.push_back({tab_name, conds});
    for (auto &entry : versions_) {
        if (entry.first.rfind(tab_name + "#", 0) != 0) {
            continue;
        }
        for (auto &version : entry.second) {
            if (version.aborted || version.old_image || version.txn_id == txn->get_transaction_id()) {
                continue;
            }
            bool invisible = !version.committed || version.commit_ts > txn->get_start_ts();
            if (invisible && record_matches(tab_name, conds, version.record) &&
                txn_relevant_for_ser(version.txn_id, txn)) {
                if (add_rw_edge(txn->get_transaction_id(), version.txn_id)) {
                    throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
                }
            }
        }
    }
}

void TransactionManager::record_select_record(const std::string &tab_name, const Rid &rid, Transaction *txn) {
    if (txn == nullptr || txn->get_isolation_level() != IsolationLevel::SERIALIZABLE) {
        return;
    }
    std::lock_guard<std::mutex> lock(mvcc_latch_);
    ser_states_[txn->get_transaction_id()].read_records.insert({tab_name, rid_key(rid)});
}

void TransactionManager::check_ser_write(const std::string &tab_name, const Rid &rid, const RmRecord *old_record,
                                         const RmRecord *new_record, Transaction *txn) {
    if (txn == nullptr || txn->get_isolation_level() != IsolationLevel::SERIALIZABLE) {
        return;
    }
    std::lock_guard<std::mutex> lock(mvcc_latch_);
    for (auto &entry : ser_states_) {
        txn_id_t reader = entry.first;
        if (reader == txn->get_transaction_id() || !txn_relevant_for_ser(reader, txn)) {
            continue;
        }
        bool hit = entry.second.read_records.count({tab_name, rid_key(rid)}) > 0;
        if (!hit) {
            for (auto &pred : entry.second.predicates) {
                if (pred.tab_name != tab_name) {
                    continue;
                }
                if ((old_record != nullptr && record_matches(tab_name, pred.conds, *old_record)) ||
                    (new_record != nullptr && record_matches(tab_name, pred.conds, *new_record))) {
                    hit = true;
                    break;
                }
            }
        }
        if (hit && add_rw_edge(reader, txn->get_transaction_id())) {
            throw TransactionAbortException(txn->get_transaction_id(), AbortReason::DEADLOCK_PREVENTION);
        }
    }
}

bool TransactionManager::add_rw_edge(txn_id_t reader, txn_id_t writer) {
    if (reader == writer) {
        return false;
    }
    auto &outs = rw_edges_[reader];
    bool inserted = outs.insert(writer).second;
    if (!inserted) {
        return false;
    }
    return creates_rw_cycle(reader, writer) || creates_dangerous_structure(reader, writer);
}

bool TransactionManager::creates_rw_cycle(txn_id_t from, txn_id_t to) {
    std::queue<txn_id_t> queue;
    std::unordered_set<txn_id_t> visited;
    queue.push(to);
    visited.insert(to);
    while (!queue.empty()) {
        txn_id_t cur = queue.front();
        queue.pop();
        if (cur == from) {
            return true;
        }
        auto it = rw_edges_.find(cur);
        if (it == rw_edges_.end()) {
            continue;
        }
        for (txn_id_t next : it->second) {
            if (visited.insert(next).second) {
                queue.push(next);
            }
        }
    }
    return false;
}

bool TransactionManager::tout_committed_before_tin(txn_id_t tout, txn_id_t tin) const {
    auto tout_commit = committed_ts_.find(tout);
    if (tout_commit == committed_ts_.end()) {
        auto tout_it = ser_states_.find(tout);
        if (tout_it == ser_states_.end() || !tout_it->second.committed) {
            return false;
        }
        timestamp_t tout_ts = tout_it->second.commit_ts;
        auto tin_commit = committed_ts_.find(tin);
        if (tin_commit == committed_ts_.end()) {
            auto tin_it = ser_states_.find(tin);
            return tin_it == ser_states_.end() || !tin_it->second.committed;
        }
        return tout_ts < tin_commit->second;
    }
    auto tin_commit = committed_ts_.find(tin);
    if (tin_commit == committed_ts_.end()) {
        auto tin_it = ser_states_.find(tin);
        return tin_it == ser_states_.end() || !tin_it->second.committed;
    }
    return tout_commit->second < tin_commit->second;
}

bool TransactionManager::creates_dangerous_structure(txn_id_t reader, txn_id_t writer) {
    auto middle_out = rw_edges_.find(writer);
    if (middle_out != rw_edges_.end()) {
        for (txn_id_t tout : middle_out->second) {
            if (tout == reader || tout_committed_before_tin(tout, reader)) {
                return true;
            }
        }
    }

    for (const auto &entry : rw_edges_) {
        txn_id_t tin = entry.first;
        if (entry.second.count(reader) == 0) {
            continue;
        }
        if (writer == tin || tout_committed_before_tin(writer, tin)) {
            return true;
        }
    }
    return false;
}

void TransactionManager::cleanup_abort(Transaction *txn) {
    if (txn == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mvcc_latch_);
    txn_id_t txn_id = txn->get_transaction_id();
    auto key_it = txn_version_keys_.find(txn_id);
    if (key_it != txn_version_keys_.end()) {
        for (const auto &key : key_it->second) {
            auto version_it = versions_.find(key);
            if (version_it == versions_.end()) {
                continue;
            }
            for (auto &version : version_it->second) {
                if (version.txn_id != txn_id) {
                    continue;
                }
                version.aborted = true;
            }
        }
        txn_version_keys_.erase(key_it);
    }
    ser_states_.erase(txn_id);
    committed_ts_.erase(txn_id);
    rw_edges_.erase(txn_id);
    for (auto &entry : rw_edges_) {
        entry.second.erase(txn_id);
    }
}

std::vector<txn_id_t> TransactionManager::active_txn_ids(txn_id_t exclude_txn_id) {
    std::unique_lock<std::mutex> lock(latch_);
    std::vector<txn_id_t> active;
    active.reserve(txn_map.size());
    for (const auto &entry : txn_map) {
        if (entry.first == exclude_txn_id) {
            continue;
        }
        Transaction *txn = entry.second;
        if (txn != nullptr && txn->get_state() != TransactionState::COMMITTED &&
            txn->get_state() != TransactionState::ABORTED) {
            active.push_back(entry.first);
        }
    }
    return active;
}

std::vector<CheckpointTxnInfo> TransactionManager::active_txn_infos(txn_id_t exclude_txn_id) {
    std::unique_lock<std::mutex> lock(latch_);
    std::vector<CheckpointTxnInfo> active;
    active.reserve(txn_map.size());
    for (const auto &entry : txn_map) {
        if (entry.first == exclude_txn_id) {
            continue;
        }
        Transaction *txn = entry.second;
        if (txn != nullptr && txn->get_state() != TransactionState::COMMITTED &&
            txn->get_state() != TransactionState::ABORTED) {
            lsn_t first_write_lsn = txn->get_first_write_lsn();
            if (first_write_lsn != INVALID_LSN) {
                active.push_back(CheckpointTxnInfo{entry.first, first_write_lsn});
            }
        }
    }
    return active;
}

void TransactionManager::clear_table_versions(const std::string &tab_name) {
    std::lock_guard<std::mutex> lock(mvcc_latch_);
    const std::string prefix = tab_name + "#";
    for (auto it = versions_.begin(); it != versions_.end();) {
        if (it->first.rfind(prefix, 0) == 0) {
            it = versions_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto &entry : txn_version_keys_) {
        for (auto it = entry.second.begin(); it != entry.second.end();) {
            if (it->rfind(prefix, 0) == 0) {
                it = entry.second.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto &entry : ser_states_) {
        auto &reads = entry.second.read_records;
        for (auto it = reads.begin(); it != reads.end();) {
            if (it->first == tab_name) {
                it = reads.erase(it);
            } else {
                ++it;
            }
        }
        auto &predicates = entry.second.predicates;
        predicates.erase(std::remove_if(predicates.begin(), predicates.end(),
                                        [&](const PredicateRead &pred) { return pred.tab_name == tab_name; }),
                         predicates.end());
    }
}
