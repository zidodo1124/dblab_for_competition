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

#include <cstring>

#include "index/ix.h"
#include "record/rm_file_handle.h"
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
    if (txn == nullptr) {
        txn = new Transaction(next_txn_id_++);
    }
    txn->set_state(TransactionState::GROWING);
    txn->set_start_ts(next_timestamp_++);

    std::unique_lock<std::mutex> lock(latch_);
    TransactionManager::txn_map[txn->get_transaction_id()] = txn;
    return txn;
}

/**
 * @description: 事务的提交方法
 * @param {Transaction*} txn 需要提交的事务
 * @param {LogManager*} log_manager 日志管理器指针
 */
void TransactionManager::commit(Transaction* txn, LogManager* log_manager) {
    if (txn == nullptr) {
        return;
    }
    clear_write_set(txn);
    auto lock_set = txn->get_lock_set();
    while (!lock_set->empty()) {
        auto lock_data_id = *lock_set->begin();
        lock_manager_->unlock(txn, lock_data_id);
    }
    if (log_manager != nullptr) {
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
    if (txn == nullptr) {
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
        log_manager->flush_log_to_disk();
    }
    txn->set_state(TransactionState::ABORTED);
}
