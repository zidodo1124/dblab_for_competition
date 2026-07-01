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
#include <string>
#include <string_view>
#include <unordered_set>

#include "index/ix.h"

namespace {

template <typename T>
T read_field(const char *data) {
    T value;
    memcpy(&value, data, sizeof(T));
    return value;
}

template <typename T>
bool read_field(const char *&pos, const char *end, T &value) {
    if (end - pos < static_cast<int>(sizeof(T))) {
        return false;
    }
    value = read_field<T>(pos);
    pos += sizeof(T);
    return true;
}

struct LogRecordView {
    LogType type;
    uint32_t len;
    txn_id_t tid;
    const char *data;
};

struct RecordSlice {
    int size;
    const char *data;
};

struct RecordLogView {
    RecordSlice record;
    Rid rid;
    std::string_view table;
};

struct UpdateLogView {
    RecordSlice old_record;
    RecordSlice new_record;
    Rid rid;
    std::string_view table;
};

bool read_log_view(const std::vector<char> &log_data, int offset, LogRecordView &view) {
    int data_size = static_cast<int>(log_data.size());
    if (offset < 0 || offset + LOG_HEADER_SIZE > data_size) {
        return false;
    }
    const char *base = log_data.data() + offset;
    uint32_t len = read_field<uint32_t>(base + OFFSET_LOG_TOT_LEN);
    if (len < LOG_HEADER_SIZE || len > static_cast<uint32_t>(data_size - offset)) {
        return false;
    }
    view.type = read_field<LogType>(base + OFFSET_LOG_TYPE);
    view.len = len;
    view.tid = read_field<txn_id_t>(base + OFFSET_LOG_TID);
    view.data = base;
    return true;
}

bool read_record_slice(const char *&pos, const char *end, RecordSlice &record) {
    int record_size = 0;
    if (!read_field(pos, end, record_size)) {
        return false;
    }
    if (record_size < 0 || end - pos < record_size) {
        return false;
    }
    record.size = record_size;
    record.data = pos;
    pos += record_size;
    return true;
}

bool read_table_name(const char *&pos, const char *end, std::string_view &table) {
    size_t table_size = 0;
    if (!read_field(pos, end, table_size)) {
        return false;
    }
    if (static_cast<size_t>(end - pos) < table_size) {
        return false;
    }
    table = std::string_view(pos, table_size);
    pos += table_size;
    return true;
}

bool parse_record_log(const LogRecordView &view, RecordLogView &record_log) {
    const char *pos = view.data + OFFSET_LOG_DATA;
    const char *end = view.data + view.len;
    return read_record_slice(pos, end, record_log.record) &&
           read_field(pos, end, record_log.rid) &&
           read_table_name(pos, end, record_log.table);
}

bool parse_update_log(const LogRecordView &view, UpdateLogView &update_log) {
    const char *pos = view.data + OFFSET_LOG_DATA;
    const char *end = view.data + view.len;
    return read_record_slice(pos, end, update_log.old_record) &&
           read_record_slice(pos, end, update_log.new_record) &&
           read_field(pos, end, update_log.rid) &&
           read_table_name(pos, end, update_log.table);
}

std::string table_name_from_view(std::string_view table) {
    return std::string(table.data(), table.size());
}

std::vector<char> make_index_key(const char *record_data, const IndexMeta &index) {
    std::vector<char> key(index.col_tot_len);
    int offset = 0;
    for (int i = 0; i < index.col_num; ++i) {
        memcpy(key.data() + offset, record_data + index.cols[i].offset, index.cols[i].len);
        offset += index.cols[i].len;
    }
    return key;
}

void insert_index_entries(SmManager *sm_manager, const std::string &tab_name, const char *record_data, const Rid &rid,
                          Transaction *txn = nullptr) {
    auto &tab = sm_manager->db_.get_table(tab_name);
    for (auto &index : tab.indexes) {
        auto ih = sm_manager->ihs_.at(sm_manager->get_ix_manager()->get_index_name(tab_name, index.cols)).get();
        auto key = make_index_key(record_data, index);
        ih->insert_entry(key.data(), rid, txn);
    }
}

void delete_index_entries(SmManager *sm_manager, const std::string &tab_name, const char *record_data,
                          Transaction *txn = nullptr) {
    auto &tab = sm_manager->db_.get_table(tab_name);
    for (auto &index : tab.indexes) {
        auto ih = sm_manager->ihs_.at(sm_manager->get_ix_manager()->get_index_name(tab_name, index.cols)).get();
        auto key = make_index_key(record_data, index);
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
    log_record_offsets_.clear();
    committed_txns_.clear();
    loser_txns_.clear();

    int offset = 0;
    std::ifstream restart("restart.ckpt");
    if (restart.good()) {
        restart >> offset;
    }
    int file_size = disk_manager_->get_file_size(LOG_FILE_NAME);
    if (file_size <= 0 || offset < 0 || file_size <= offset) {
        return;
    }

    log_data_.assign(file_size - offset, 0);
    int bytes_read = disk_manager_->read_log(log_data_.data(), static_cast<int>(log_data_.size()), offset);
    if (bytes_read <= 0) {
        log_data_.clear();
        return;
    }
    if (bytes_read < static_cast<int>(log_data_.size())) {
        log_data_.resize(std::max(0, bytes_read));
    }

    std::unordered_set<txn_id_t> active_txns;
    active_txns.reserve(log_data_.size() / LOG_HEADER_SIZE + 1);
    committed_txns_.reserve(active_txns.bucket_count());
    int log_offset = 0;
    while (true) {
        LogRecordView view{};
        if (!read_log_view(log_data_, log_offset, view)) {
            break;
        }

        log_record_offsets_.push_back(log_offset);
        switch (view.type) {
            case LogType::begin:
                active_txns.insert(view.tid);
                break;
            case LogType::commit:
                active_txns.erase(view.tid);
                committed_txns_.insert(view.tid);
                break;
            case LogType::ABORT:
                active_txns.erase(view.tid);
                break;
            default:
                break;
        }
        log_offset += static_cast<int>(view.len);
    }

    loser_txns_ = std::move(active_txns);
}

/**
 * @description: 重做所有未落盘的操作
 */
void RecoveryManager::redo() {
    for (int offset : log_record_offsets_) {
        LogRecordView view{};
        if (!read_log_view(log_data_, offset, view) || committed_txns_.count(view.tid) == 0) {
            continue;
        }

        if (view.type == LogType::INSERT) {
            RecordLogView insert_log{};
            if (!parse_record_log(view, insert_log)) {
                continue;
            }
            std::string tab_name = table_name_from_view(insert_log.table);
            auto fh = sm_manager_->fhs_.at(tab_name).get();
            try {
                fh->insert_record(insert_log.rid, const_cast<char *>(insert_log.record.data));
            } catch (...) {
            }
            insert_index_entries(sm_manager_, tab_name, insert_log.record.data, insert_log.rid);
        } else if (view.type == LogType::DELETE) {
            RecordLogView delete_log{};
            if (!parse_record_log(view, delete_log)) {
                continue;
            }
            std::string tab_name = table_name_from_view(delete_log.table);
            auto fh = sm_manager_->fhs_.at(tab_name).get();
            delete_index_entries(sm_manager_, tab_name, delete_log.record.data);
            try {
                fh->delete_record(delete_log.rid, nullptr);
            } catch (...) {
            }
        } else if (view.type == LogType::UPDATE) {
            UpdateLogView update_log{};
            if (!parse_update_log(view, update_log)) {
                continue;
            }
            std::string tab_name = table_name_from_view(update_log.table);
            auto fh = sm_manager_->fhs_.at(tab_name).get();
            delete_index_entries(sm_manager_, tab_name, update_log.old_record.data);
            try {
                fh->update_record(update_log.rid, const_cast<char *>(update_log.new_record.data), nullptr);
                insert_index_entries(sm_manager_, tab_name, update_log.new_record.data, update_log.rid);
            } catch (...) {
            }
        }
    }
}

/**
 * @description: 回滚未完成的事务
 */
void RecoveryManager::undo() {
    for (auto it = log_record_offsets_.rbegin(); it != log_record_offsets_.rend(); ++it) {
        LogRecordView view{};
        if (!read_log_view(log_data_, *it, view) || loser_txns_.count(view.tid) == 0) {
            continue;
        }

        if (view.type == LogType::INSERT) {
            RecordLogView insert_log{};
            if (!parse_record_log(view, insert_log)) {
                continue;
            }
            std::string tab_name = table_name_from_view(insert_log.table);
            auto fh = sm_manager_->fhs_.at(tab_name).get();
            delete_index_entries(sm_manager_, tab_name, insert_log.record.data);
            try {
                fh->delete_record(insert_log.rid, nullptr);
            } catch (...) {
            }
        } else if (view.type == LogType::DELETE) {
            RecordLogView delete_log{};
            if (!parse_record_log(view, delete_log)) {
                continue;
            }
            std::string tab_name = table_name_from_view(delete_log.table);
            auto fh = sm_manager_->fhs_.at(tab_name).get();
            try {
                fh->insert_record(delete_log.rid, const_cast<char *>(delete_log.record.data));
            } catch (...) {
            }
            insert_index_entries(sm_manager_, tab_name, delete_log.record.data, delete_log.rid);
        } else if (view.type == LogType::UPDATE) {
            UpdateLogView update_log{};
            if (!parse_update_log(view, update_log)) {
                continue;
            }
            std::string tab_name = table_name_from_view(update_log.table);
            auto fh = sm_manager_->fhs_.at(tab_name).get();
            delete_index_entries(sm_manager_, tab_name, update_log.new_record.data);
            try {
                fh->update_record(update_log.rid, const_cast<char *>(update_log.old_record.data), nullptr);
                insert_index_entries(sm_manager_, tab_name, update_log.old_record.data, update_log.rid);
            } catch (...) {
            }
        }
    }

    for (auto &entry : sm_manager_->fhs_) {
        entry.second->flush_file_hdr();
        buffer_pool_manager_->flush_all_pages(entry.second->GetFd());
    }
    for (auto &entry : sm_manager_->ihs_) {
        buffer_pool_manager_->flush_all_pages(entry.second->get_fd());
    }
}
