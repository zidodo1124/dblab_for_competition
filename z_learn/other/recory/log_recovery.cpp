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

#include <fstream>
#include <vector>

#include "common/config.h"
#include "record/rm_scan.h"

namespace {

int ReadRecordSize(const char *src, int &offset) {
    int size = *reinterpret_cast<const int *>(src + offset);
    offset += sizeof(int);
    return size;
}

RmRecord ReadRecordImage(const char *src, int &offset) {
    int size = ReadRecordSize(src, offset);
    RmRecord record(size);
    memcpy(record.data, src + offset, size);
    offset += size;
    return record;
}

std::string ReadTableName(const char *src, int &offset) {
    size_t table_name_size = *reinterpret_cast<const size_t *>(src + offset);
    offset += sizeof(size_t);
    std::string table_name(src + offset, table_name_size);
    offset += static_cast<int>(table_name_size);
    return table_name;
}

}  // namespace

int RecoveryManager::GetRestartOffset() {
    if (!disk_manager_->is_file(RESTART_FILE_NAME)) {
        return 0;
    }
    std::ifstream restart_file(RESTART_FILE_NAME);
    int offset = 0;
    restart_file >> offset;
    return std::max(0, offset);
}

bool RecoveryManager::IsRecordPresent(RmFileHandle *fh, const Rid &rid) {
    try {
        return fh->is_record(rid);
    } catch (...) {
        return false;
    }
}

void RecoveryManager::ApplyInsert(const RecoveryLogEntry &entry) {
    auto fh = sm_manager_->fhs_.at(entry.table_name).get();
    if (IsRecordPresent(fh, entry.rid)) {
        fh->update_record(entry.rid, entry.new_record.data, nullptr);
    } else {
        fh->insert_record(entry.rid, entry.new_record.data);
    }
}

void RecoveryManager::ApplyDelete(const RecoveryLogEntry &entry) {
    auto fh = sm_manager_->fhs_.at(entry.table_name).get();
    if (IsRecordPresent(fh, entry.rid)) {
        fh->delete_record(entry.rid, nullptr);
    }
}

void RecoveryManager::ApplyUpdate(const RecoveryLogEntry &entry, bool use_new_value) {
    auto fh = sm_manager_->fhs_.at(entry.table_name).get();
    auto &record = use_new_value ? entry.new_record : entry.old_record;
    if (IsRecordPresent(fh, entry.rid)) {
        fh->update_record(entry.rid, record.data, nullptr);
    } else {
        fh->insert_record(entry.rid, record.data);
    }
}

void RecoveryManager::FlushTables() {
    sm_manager_->flush_meta();
    for (auto &file : sm_manager_->fhs_) {
        file.second->flush_file_hdr();
        buffer_pool_manager_->flush_all_pages(file.second->GetFd());
    }
}

void RecoveryManager::RebuildIndexes() {
    for (const auto &[tab_name, fh_ptr] : sm_manager_->fhs_) {
        const auto &tab = sm_manager_->db_.get_table(tab_name);
        auto fh = fh_ptr.get();
        for (const auto &index : tab.indexes) {
            std::string index_name = sm_manager_->get_ix_manager()->get_index_name(tab_name, index.cols);
            if (sm_manager_->ihs_.count(index_name) > 0) {
                sm_manager_->get_ix_manager()->close_index(sm_manager_->ihs_.at(index_name).get());
                sm_manager_->ihs_.erase(index_name);
            }
            if (sm_manager_->get_ix_manager()->exists(tab_name, index.cols)) {
                sm_manager_->get_ix_manager()->destroy_index(tab_name, index.cols);
            }
            sm_manager_->get_ix_manager()->create_index(tab_name, index.cols);
            sm_manager_->ihs_.emplace(index_name, sm_manager_->get_ix_manager()->open_index(tab_name, index.cols));
            auto ih = sm_manager_->ihs_.at(index_name).get();
            for (RmScan scan(fh); !scan.is_end(); scan.next()) {
                auto record = fh->get_record(scan.rid(), nullptr);
                std::string key(index.col_tot_len, '\0');
                int curr_offset = 0;
                for (const auto &col : index.cols) {
                    memcpy(key.data() + curr_offset, record->data + col.offset, col.len);
                    curr_offset += col.len;
                }
                ih->insert_entry(key.data(), scan.rid(), nullptr);
            }
        }
    }
}

void RecoveryManager::analyze() {
    logs_.clear();
    committed_txns_.clear();
    loser_txns_.clear();

    int offset = GetRestartOffset();
    char header[LOG_HEADER_SIZE];
    while (true) {
        int header_read = disk_manager_->read_log(header, LOG_HEADER_SIZE, offset);
        if (header_read != LOG_HEADER_SIZE) {
            break;
        }
        auto log_type = *reinterpret_cast<LogType *>(header + OFFSET_LOG_TYPE);
        auto log_tot_len = *reinterpret_cast<uint32_t *>(header + OFFSET_LOG_TOT_LEN);
        if (log_type < LogType::UPDATE || log_type > LogType::CHECKPOINT ||
            log_tot_len < LOG_HEADER_SIZE || log_tot_len > LOG_BUFFER_SIZE) {
            break;
        }
        std::vector<char> raw(log_tot_len);
        int bytes_read = disk_manager_->read_log(raw.data(), log_tot_len, offset);
        if (bytes_read != static_cast<int>(log_tot_len)) {
            break;
        }
        offset += log_tot_len;

        RecoveryLogEntry entry;
        entry.type = log_type;
        entry.txn_id = *reinterpret_cast<txn_id_t *>(raw.data() + OFFSET_LOG_TID);
        int data_offset = OFFSET_LOG_DATA;
        switch (log_type) {
            case LogType::INSERT: {
                entry.new_record = ReadRecordImage(raw.data(), data_offset);
                entry.rid = *reinterpret_cast<Rid *>(raw.data() + data_offset);
                data_offset += sizeof(Rid);
                entry.table_name = ReadTableName(raw.data(), data_offset);
                loser_txns_.insert(entry.txn_id);
                logs_.push_back(entry);
                break;
            }
            case LogType::DELETE: {
                entry.old_record = ReadRecordImage(raw.data(), data_offset);
                entry.rid = *reinterpret_cast<Rid *>(raw.data() + data_offset);
                data_offset += sizeof(Rid);
                entry.table_name = ReadTableName(raw.data(), data_offset);
                loser_txns_.insert(entry.txn_id);
                logs_.push_back(entry);
                break;
            }
            case LogType::UPDATE: {
                entry.old_record = ReadRecordImage(raw.data(), data_offset);
                entry.new_record = ReadRecordImage(raw.data(), data_offset);
                entry.rid = *reinterpret_cast<Rid *>(raw.data() + data_offset);
                data_offset += sizeof(Rid);
                entry.table_name = ReadTableName(raw.data(), data_offset);
                loser_txns_.insert(entry.txn_id);
                logs_.push_back(entry);
                break;
            }
            case LogType::begin: {
                loser_txns_.insert(entry.txn_id);
                logs_.push_back(entry);
                break;
            }
            case LogType::commit: {
                committed_txns_.insert(entry.txn_id);
                loser_txns_.erase(entry.txn_id);
                logs_.push_back(entry);
                break;
            }
            case LogType::ABORT: {
                committed_txns_.erase(entry.txn_id);
                logs_.push_back(entry);
                break;
            }
            case LogType::CHECKPOINT: {
                // 解析检查点中的活跃事务信息
                int cp_offset = OFFSET_LOG_DATA;
                int active_count = *reinterpret_cast<const int *>(raw.data() + cp_offset);
                cp_offset += sizeof(int);
                // 从检查点开始，清空之前的状态
                logs_.clear();
                loser_txns_.clear();
                committed_txns_.clear();
                // 将检查点时刻的活跃事务加入undo list
                for (int i = 0; i < active_count; i++) {
                    auto *info = reinterpret_cast<const ActiveTxnInfo *>(raw.data() + cp_offset);
                    cp_offset += sizeof(ActiveTxnInfo);
                    loser_txns_.insert(info->txn_id);
                }
                break;
            }
        }
    }
}

void RecoveryManager::redo() {
    for (const auto &entry : logs_) {
        if (committed_txns_.count(entry.txn_id) == 0) {
            continue;
        }
        switch (entry.type) {
            case LogType::INSERT:
                ApplyInsert(entry);
                break;
            case LogType::DELETE:
                ApplyDelete(entry);
                break;
            case LogType::UPDATE:
                ApplyUpdate(entry, true);
                break;
            default:
                break;
        }
    }
}

void RecoveryManager::undo() {
    for (auto it = logs_.rbegin(); it != logs_.rend(); ++it) {
        const auto &entry = *it;
        if (loser_txns_.count(entry.txn_id) == 0) {
            continue;
        }
        switch (entry.type) {
            case LogType::INSERT:
                ApplyDelete(entry);
                break;
            case LogType::DELETE:
                ApplyInsert({LogType::INSERT, entry.txn_id, entry.table_name, entry.rid, RmRecord(), entry.old_record});
                break;
            case LogType::UPDATE:
                ApplyUpdate(entry, false);
                break;
            default:
                break;
        }
    }
    RebuildIndexes();
    FlushTables();
}
