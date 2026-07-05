/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "log_recovery.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>

#include "index/ix.h"

namespace {

std::string table_name_from_log(const char *name, size_t size) {
    return std::string(name, size);
}

std::unique_ptr<LogRecord> parse_log_record(const char *data) {
    LogType type = *reinterpret_cast<const LogType *>(data + OFFSET_LOG_TYPE);
    std::unique_ptr<LogRecord> record;
    switch (type) {
        case LogType::begin: record = std::make_unique<BeginLogRecord>(); break;
        case LogType::commit: record = std::make_unique<CommitLogRecord>(); break;
        case LogType::ABORT: record = std::make_unique<AbortLogRecord>(); break;
        case LogType::INSERT: record = std::make_unique<InsertLogRecord>(); break;
        case LogType::DELETE: record = std::make_unique<DeleteLogRecord>(); break;
        case LogType::UPDATE: record = std::make_unique<UpdateLogRecord>(); break;
        case LogType::CHECKPOINT: record = std::make_unique<CheckpointLogRecord>(); break;
    }
    record->deserialize(data);
    return record;
}

std::vector<char> make_index_key(const RmRecord &rec, const IndexMeta &index) {
    std::vector<char> key(index.col_tot_len);
    int offset = 0;
    for (int i = 0; i < index.col_num; ++i) {
        memcpy(key.data() + offset, rec.data + index.cols[i].offset, index.cols[i].len);
        offset += index.cols[i].len;
    }
    return key;
}

bool record_exists(RmFileHandle *fh, const Rid &rid) {
    try {
        return fh->is_record(rid);
    } catch (...) {
        return false;
    }
}

bool table_has_indexes(SmManager *sm_manager, const std::string &tab_name) {
    return !sm_manager->db_.get_table(tab_name).indexes.empty();
}

bool page_lsn_covers(RmFileHandle *fh, const Rid &rid, lsn_t lsn) {
    try {
        lsn_t page_lsn = fh->get_page_lsn(rid.page_no);
        return page_lsn != INVALID_LSN && page_lsn >= lsn;
    } catch (...) {
        return false;
    }
}

std::vector<CheckpointTxnInfo> checkpoint_active_txns(const char *data) {
    auto len = *reinterpret_cast<const uint32_t *>(data + OFFSET_LOG_TOT_LEN);
    if (len <= LOG_HEADER_SIZE) {
        return {};
    }
    int offset = OFFSET_LOG_DATA;
    auto active_count = *reinterpret_cast<const uint32_t *>(data + offset);
    offset += sizeof(uint32_t);
    int remaining = static_cast<int>(len) - offset;
    int new_payload_size = static_cast<int>(active_count * (sizeof(txn_id_t) + sizeof(lsn_t)));
    int old_payload_size = static_cast<int>(active_count * sizeof(txn_id_t));
    if (remaining < old_payload_size) {
        return {};
    }
    std::vector<CheckpointTxnInfo> active_txns;
    active_txns.reserve(active_count);
    if (remaining >= new_payload_size) {
        for (uint32_t i = 0; i < active_count; ++i) {
            CheckpointTxnInfo active_txn;
            active_txn.txn_id = *reinterpret_cast<const txn_id_t *>(data + offset);
            offset += sizeof(txn_id_t);
            active_txn.first_lsn = *reinterpret_cast<const lsn_t *>(data + offset);
            offset += sizeof(lsn_t);
            active_txns.push_back(active_txn);
        }
    } else {
        for (uint32_t i = 0; i < active_count; ++i) {
            CheckpointTxnInfo active_txn;
            active_txn.txn_id = *reinterpret_cast<const txn_id_t *>(data + offset);
            offset += sizeof(txn_id_t);
            active_txn.first_lsn = INVALID_LSN;
            active_txns.push_back(active_txn);
        }
    }
    return active_txns;
}

void insert_index_entries(SmManager *sm_manager, const std::string &tab_name, const RmRecord &rec, const Rid &rid,
                          Transaction *txn = nullptr) {
    auto &tab = sm_manager->db_.get_table(tab_name);
    for (auto &index : tab.indexes) {
        auto ih = sm_manager->ihs_.at(sm_manager->get_ix_manager()->get_index_name(tab_name, index.cols)).get();
        auto key = make_index_key(rec, index);
        std::vector<Rid> existing;
        if (!ih->get_value(key.data(), &existing, txn)) {
            ih->insert_entry(key.data(), rid, txn);
        }
    }
}

void delete_index_entries(SmManager *sm_manager, const std::string &tab_name, const RmRecord &rec,
                          Transaction *txn = nullptr) {
    auto &tab = sm_manager->db_.get_table(tab_name);
    for (auto &index : tab.indexes) {
        auto ih = sm_manager->ihs_.at(sm_manager->get_ix_manager()->get_index_name(tab_name, index.cols)).get();
        auto key = make_index_key(rec, index);
        ih->delete_entry(key.data(), txn);
    }
}

}  // namespace

/**
 * @description: analyze阶段，需要获得脏页表（DPT）和未完成的事务列表（ATT）
 */
void RecoveryManager::analyze() {
    buffer_.offset_ = 0;
    log_data_.clear();
    data_logs_.clear();
    undo_txns_.clear();
    redo_txns_.clear();
    int offset = 0;
    std::ifstream restart("restart.ckpt");
    if (restart.good()) {
        restart >> offset;
    }
    int file_size = disk_manager_->get_file_size(LOG_FILE_NAME);
    if (file_size <= offset) {
        log_data_.clear();
        return;
    }
    log_data_.assign(file_size - offset, 0);
    int bytes_read = disk_manager_->read_log(log_data_.data(), static_cast<int>(log_data_.size()), offset);
    if (bytes_read < static_cast<int>(log_data_.size())) {
        log_data_.resize(std::max(0, bytes_read));
    }

    int log_offset = 0;
    while (log_offset + LOG_HEADER_SIZE <= static_cast<int>(log_data_.size())) {
        auto len = *reinterpret_cast<uint32_t *>(log_data_.data() + log_offset + OFFSET_LOG_TOT_LEN);
        if (len == 0 || log_offset + static_cast<int>(len) > static_cast<int>(log_data_.size())) {
            break;
        }
        auto type = *reinterpret_cast<LogType *>(log_data_.data() + log_offset + OFFSET_LOG_TYPE);
        auto tid = *reinterpret_cast<txn_id_t *>(log_data_.data() + log_offset + OFFSET_LOG_TID);
        auto lsn = *reinterpret_cast<lsn_t *>(log_data_.data() + log_offset + OFFSET_LSN);
        if (type == LogType::CHECKPOINT) {
            undo_txns_.clear();
            auto active_txns = checkpoint_active_txns(log_data_.data() + log_offset);
            for (const auto &active_txn : active_txns) {
                if (active_txn.txn_id != INVALID_TXN_ID) {
                    undo_txns_.insert(active_txn.txn_id);
                }
            }
        } else if (type == LogType::begin) {
            undo_txns_.insert(tid);
        } else if (type == LogType::commit) {
            undo_txns_.erase(tid);
            redo_txns_.insert(tid);
        } else if (type == LogType::ABORT) {
            undo_txns_.erase(tid);
        } else if (type == LogType::INSERT || type == LogType::DELETE || type == LogType::UPDATE) {
            data_logs_.push_back({log_offset, len, type, tid, lsn});
        }
        log_offset += static_cast<int>(len);
    }
}

/**
 * @description: 重做所有未落盘的操作
 */
void RecoveryManager::redo() {
    for (const auto &log_ref : data_logs_) {
        if (redo_txns_.count(log_ref.tid) > 0) {
            auto record = parse_log_record(log_data_.data() + log_ref.offset);
            if (auto insert_log = dynamic_cast<InsertLogRecord *>(record.get())) {
                std::string tab_name = table_name_from_log(insert_log->table_name_, insert_log->table_name_size_);
                auto fh = sm_manager_->fhs_.at(tab_name).get();
                bool has_indexes = table_has_indexes(sm_manager_, tab_name);
                if (!has_indexes && page_lsn_covers(fh, insert_log->rid_, record->lsn_)) {
                    continue;
                }
                if (!record_exists(fh, insert_log->rid_)) {
                    fh->insert_record(insert_log->rid_, insert_log->insert_value_.data, record->lsn_);
                }
                if (has_indexes) {
                    insert_index_entries(sm_manager_, tab_name, insert_log->insert_value_, insert_log->rid_);
                }
            } else if (auto delete_log = dynamic_cast<DeleteLogRecord *>(record.get())) {
                std::string tab_name = table_name_from_log(delete_log->table_name_, delete_log->table_name_size_);
                auto fh = sm_manager_->fhs_.at(tab_name).get();
                bool has_indexes = table_has_indexes(sm_manager_, tab_name);
                if (!has_indexes && page_lsn_covers(fh, delete_log->rid_, record->lsn_)) {
                    continue;
                }
                if (has_indexes) {
                    delete_index_entries(sm_manager_, tab_name, delete_log->delete_value_);
                }
                if (record_exists(fh, delete_log->rid_)) {
                    fh->delete_record(delete_log->rid_, nullptr, record->lsn_);
                }
            } else if (auto update_log = dynamic_cast<UpdateLogRecord *>(record.get())) {
                std::string tab_name = table_name_from_log(update_log->table_name_, update_log->table_name_size_);
                auto fh = sm_manager_->fhs_.at(tab_name).get();
                bool has_indexes = table_has_indexes(sm_manager_, tab_name);
                if (!has_indexes && page_lsn_covers(fh, update_log->rid_, record->lsn_)) {
                    continue;
                }
                if (record_exists(fh, update_log->rid_)) {
                    if (has_indexes) {
                        delete_index_entries(sm_manager_, tab_name, update_log->old_value_);
                    }
                    fh->update_record(update_log->rid_, update_log->new_value_.data, nullptr, record->lsn_);
                    if (has_indexes) {
                        insert_index_entries(sm_manager_, tab_name, update_log->new_value_, update_log->rid_);
                    }
                }
            }
        }
    }
}

/**
 * @description: 回滚未完成的事务
 */
void RecoveryManager::undo() {
    for (auto it = data_logs_.rbegin(); it != data_logs_.rend(); ++it) {
        const auto &log_ref = *it;
        if (undo_txns_.count(log_ref.tid) == 0) {
            continue;
        }
        auto record = parse_log_record(log_data_.data() + log_ref.offset);
        if (auto insert_log = dynamic_cast<InsertLogRecord *>(record.get())) {
            std::string tab_name = table_name_from_log(insert_log->table_name_, insert_log->table_name_size_);
            auto fh = sm_manager_->fhs_.at(tab_name).get();
            if (record_exists(fh, insert_log->rid_)) {
                delete_index_entries(sm_manager_, tab_name, insert_log->insert_value_);
                fh->delete_record(insert_log->rid_, nullptr);
            }
        } else if (auto delete_log = dynamic_cast<DeleteLogRecord *>(record.get())) {
            std::string tab_name = table_name_from_log(delete_log->table_name_, delete_log->table_name_size_);
            auto fh = sm_manager_->fhs_.at(tab_name).get();
            if (!record_exists(fh, delete_log->rid_)) {
                fh->insert_record(delete_log->rid_, delete_log->delete_value_.data);
                insert_index_entries(sm_manager_, tab_name, delete_log->delete_value_, delete_log->rid_);
            }
        } else if (auto update_log = dynamic_cast<UpdateLogRecord *>(record.get())) {
            std::string tab_name = table_name_from_log(update_log->table_name_, update_log->table_name_size_);
            auto fh = sm_manager_->fhs_.at(tab_name).get();
            if (record_exists(fh, update_log->rid_)) {
                delete_index_entries(sm_manager_, tab_name, update_log->new_value_);
                fh->update_record(update_log->rid_, update_log->old_value_.data, nullptr);
                insert_index_entries(sm_manager_, tab_name, update_log->old_value_, update_log->rid_);
            }
        }
    }

    for (auto &entry : sm_manager_->fhs_) {
        entry.second->flush_file_hdr();
        buffer_pool_manager_->flush_all_pages(entry.second->GetFd());
    }
    for (auto &entry : sm_manager_->ihs_) {
        entry.second->flush_file_hdr();
        buffer_pool_manager_->flush_all_pages(entry.second->get_fd());
    }
}
