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

bool is_valid_log_type(LogType type) {
    switch (type) {
        case LogType::begin:
        case LogType::commit:
        case LogType::ABORT:
        case LogType::INSERT:
        case LogType::DELETE:
        case LogType::UPDATE:
        case LogType::CHECKPOINT:
            return true;
    }
    return false;
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
        default: return nullptr;
    }
    record->deserialize(data);
    return record;
}

int read_restart_offset(DiskManager *disk_manager) {
    auto read_offset = [](const std::string &file_name, int *offset) {
        std::ifstream restart(file_name);
        if (!restart.good()) {
            return false;
        }
        return static_cast<bool>(restart >> *offset);
    };

    int offset = 0;
    if (!read_offset(RESTART_FILE_NAME, &offset)) {
        read_offset("restart.ckpt", &offset);
    }
    offset = std::max(0, offset);
    int file_size = disk_manager->get_file_size(LOG_FILE_NAME);
    if (file_size < 0) {
        return 0;
    }
    return std::min(offset, file_size);
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
    int file_size = disk_manager_->get_file_size(LOG_FILE_NAME);
    if (file_size <= 0) {
        return;
    }

    int scan_start = read_restart_offset(disk_manager_);
    bool restarted_for_checkpoint_active_txn = false;
    while (true) {
        buffer_.offset_ = 0;
        log_data_.clear();
        data_logs_.clear();
        undo_txns_.clear();
        redo_txns_.clear();

        bool restart_scan = false;
        int read_offset = scan_start;
        char header[LOG_HEADER_SIZE];
        while (read_offset + LOG_HEADER_SIZE <= file_size) {
            int header_read = disk_manager_->read_log(header, LOG_HEADER_SIZE, read_offset);
            if (header_read != LOG_HEADER_SIZE) {
                break;
            }

            auto len = *reinterpret_cast<uint32_t *>(header + OFFSET_LOG_TOT_LEN);
            auto type = *reinterpret_cast<LogType *>(header + OFFSET_LOG_TYPE);
            if (len < LOG_HEADER_SIZE || read_offset + static_cast<int>(len) > file_size ||
                !is_valid_log_type(type)) {
                break;
            }

            std::vector<char> raw(len);
            int bytes_read = disk_manager_->read_log(raw.data(), static_cast<int>(len), read_offset);
            if (bytes_read != static_cast<int>(len)) {
                break;
            }

            int stored_offset = static_cast<int>(log_data_.size());
            log_data_.insert(log_data_.end(), raw.begin(), raw.end());
            const char *record_data = log_data_.data() + stored_offset;
            auto tid = *reinterpret_cast<const txn_id_t *>(record_data + OFFSET_LOG_TID);
            auto lsn = *reinterpret_cast<const lsn_t *>(record_data + OFFSET_LSN);
            if (type == LogType::CHECKPOINT) {
                auto active_txns = checkpoint_active_txns(record_data);
                int active_restart = read_offset;
                for (const auto &active_txn : active_txns) {
                    if (active_txn.first_lsn != INVALID_LSN && active_txn.first_lsn < active_restart) {
                        active_restart = static_cast<int>(active_txn.first_lsn);
                    }
                }
                if (!restarted_for_checkpoint_active_txn && active_restart < scan_start) {
                    scan_start = active_restart;
                    restarted_for_checkpoint_active_txn = true;
                    restart_scan = true;
                    break;
                }

                std::unordered_set<txn_id_t> checkpoint_txns;
                checkpoint_txns.reserve(active_txns.size());
                for (const auto &active_txn : active_txns) {
                    if (active_txn.txn_id != INVALID_TXN_ID) {
                        checkpoint_txns.insert(active_txn.txn_id);
                    }
                }
                data_logs_.erase(std::remove_if(data_logs_.begin(), data_logs_.end(), [&](const LogRef &log_ref) {
                                     return checkpoint_txns.count(log_ref.tid) == 0;
                                 }),
                                 data_logs_.end());
                redo_txns_.clear();
                undo_txns_.clear();
                undo_txns_.insert(checkpoint_txns.begin(), checkpoint_txns.end());
            } else if (type == LogType::begin) {
                undo_txns_.insert(tid);
            } else if (type == LogType::commit) {
                undo_txns_.erase(tid);
                redo_txns_.insert(tid);
            } else if (type == LogType::ABORT) {
                undo_txns_.erase(tid);
            } else if (type == LogType::INSERT || type == LogType::DELETE || type == LogType::UPDATE) {
                undo_txns_.insert(tid);
                data_logs_.push_back({stored_offset, len, type, tid, lsn});
            }
            read_offset += static_cast<int>(len);
        }

        if (!restart_scan) {
            break;
        }
    }
}

/**
 * @description: 重做所有未落盘的操作
 */
void RecoveryManager::redo() {
    for (const auto &log_ref : data_logs_) {
        if (redo_txns_.count(log_ref.tid) > 0) {
            auto record = parse_log_record(log_data_.data() + log_ref.offset);
            if (record == nullptr) {
                continue;
            }
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
        if (record == nullptr) {
            continue;
        }
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
