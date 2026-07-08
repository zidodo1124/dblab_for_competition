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

#include <mutex>
#include <memory>
#include <vector>
#include <iostream>
#include <set>
#include <unordered_set>
#include "log_defs.h"
#include "common/config.h"
#include "record/rm_defs.h"

class SmManager;
class TransactionManager;

/* 日志记录对应操作的类型 */
enum LogType: int {
    UPDATE = 0,
    INSERT,
    DELETE,
    begin,
    commit,
    ABORT,
    CHECKPOINT
};
static std::string LogTypeStr[] = {
    "UPDATE",
    "INSERT",
    "DELETE",
    "BEGIN",
    "COMMIT",
    "ABORT",
    "CHECKPOINT"
};

/* 检查点日志中的活跃事务信息 */
struct ActiveTxnInfo {
    txn_id_t txn_id;
    lsn_t last_lsn;
};

class LogRecord {
public:
    LogType log_type_;         /* 日志对应操作的类型 */
    lsn_t lsn_;                /* 当前日志的lsn */
    uint32_t log_tot_len_;     /* 整个日志记录的长度 */
    txn_id_t log_tid_;         /* 创建当前日志的事务ID */
    lsn_t prev_lsn_;           /* 事务创建的前一条日志记录的lsn，用于undo */

    // 把日志记录序列化到dest中
    virtual void serialize (char* dest) const {
        memcpy(dest + OFFSET_LOG_TYPE, &log_type_, sizeof(LogType));
        memcpy(dest + OFFSET_LSN, &lsn_, sizeof(lsn_t));
        memcpy(dest + OFFSET_LOG_TOT_LEN, &log_tot_len_, sizeof(uint32_t));
        memcpy(dest + OFFSET_LOG_TID, &log_tid_, sizeof(txn_id_t));
        memcpy(dest + OFFSET_PREV_LSN, &prev_lsn_, sizeof(lsn_t));
    }
    // 从src中反序列化出一条日志记录
    virtual void deserialize(const char* src) {
        log_type_ = *reinterpret_cast<const LogType*>(src);
        lsn_ = *reinterpret_cast<const lsn_t*>(src + OFFSET_LSN);
        log_tot_len_ = *reinterpret_cast<const uint32_t*>(src + OFFSET_LOG_TOT_LEN);
        log_tid_ = *reinterpret_cast<const txn_id_t*>(src + OFFSET_LOG_TID);
        prev_lsn_ = *reinterpret_cast<const lsn_t*>(src + OFFSET_PREV_LSN);
    }
    // used for debug
    virtual void format_print() {
        std::cout << "log type: " << LogTypeStr[log_type_] << "\n";
        printf("  lsn: %d, log_tot_len: %d, log_tid: %lld, prev_lsn: %d\n",
               lsn_, log_tot_len_, log_tid_, prev_lsn_);
    }

    virtual ~LogRecord() = default;
};

class BeginLogRecord: public LogRecord {
public:
    BeginLogRecord() {
        log_type_ = LogType::begin;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }
    BeginLogRecord(txn_id_t txn_id) : BeginLogRecord() {
        log_tid_ = txn_id;
    }
    void serialize(char* dest) const override {
        LogRecord::serialize(dest);
    }
    void deserialize(const char* src) override {
        LogRecord::deserialize(src);
    }
    virtual void format_print() override {
        LogRecord::format_print();
    }
};

class CommitLogRecord: public LogRecord {
public:
    CommitLogRecord() {
        log_type_ = LogType::commit;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }
    CommitLogRecord(txn_id_t txn_id, lsn_t prev_lsn) : CommitLogRecord() {
        log_tid_ = txn_id;
        prev_lsn_ = prev_lsn;
    }
    void serialize(char* dest) const override {
        LogRecord::serialize(dest);
    }
    void deserialize(const char* src) override {
        LogRecord::deserialize(src);
    }
    void format_print() override {
        LogRecord::format_print();
    }
};

class AbortLogRecord: public LogRecord {
public:
    AbortLogRecord() {
        log_type_ = LogType::ABORT;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
    }
    AbortLogRecord(txn_id_t txn_id, lsn_t prev_lsn) : AbortLogRecord() {
        log_tid_ = txn_id;
        prev_lsn_ = prev_lsn;
    }
    void serialize(char* dest) const override {
        LogRecord::serialize(dest);
    }
    void deserialize(const char* src) override {
        LogRecord::deserialize(src);
    }
    void format_print() override {
        LogRecord::format_print();
    }
};

class InsertLogRecord: public LogRecord {
public:
    InsertLogRecord() {
        log_type_ = LogType::INSERT;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
        table_name_ = nullptr;
    }
    InsertLogRecord(txn_id_t txn_id, const RmRecord& insert_value, const Rid& rid, const std::string& table_name,
                    lsn_t prev_lsn)
        : InsertLogRecord() {
        log_tid_ = txn_id;
        prev_lsn_ = prev_lsn;
        insert_value_ = insert_value;
        rid_ = rid;
        log_tot_len_ += sizeof(int);
        log_tot_len_ += insert_value_.size;
        log_tot_len_ += sizeof(Rid);
        table_name_size_ = table_name.length();
        table_name_ = new char[table_name_size_ + 1];
        memcpy(table_name_, table_name.c_str(), table_name_size_ + 1);
        log_tot_len_ += sizeof(size_t) + table_name_size_;
    }

    void serialize(char* dest) const override {
        LogRecord::serialize(dest);
        int offset = OFFSET_LOG_DATA;
        memcpy(dest + offset, &insert_value_.size, sizeof(int));
        offset += sizeof(int);
        memcpy(dest + offset, insert_value_.data, insert_value_.size);
        offset += insert_value_.size;
        memcpy(dest + offset, &rid_, sizeof(Rid));
        offset += sizeof(Rid);
        memcpy(dest + offset, &table_name_size_, sizeof(size_t));
        offset += sizeof(size_t);
        memcpy(dest + offset, table_name_, table_name_size_);
    }
    void deserialize(const char* src) override {
        LogRecord::deserialize(src);
        insert_value_.Deserialize(src + OFFSET_LOG_DATA);
        int offset = OFFSET_LOG_DATA + insert_value_.size + sizeof(int);
        rid_ = *reinterpret_cast<const Rid*>(src + offset);
        offset += sizeof(Rid);
        table_name_size_ = *reinterpret_cast<const size_t*>(src + offset);
        offset += sizeof(size_t);
        table_name_ = new char[table_name_size_ + 1];
        memcpy(table_name_, src + offset, table_name_size_);
        table_name_[table_name_size_] = '\0';
    }
    void format_print() override {
        printf("INSERT record\n");
        LogRecord::format_print();
        printf("  rid: page=%d, slot=%d, table: %s\n", rid_.page_no, rid_.slot_no, table_name_);
    }

    RmRecord insert_value_;     // 插入的记录
    Rid rid_;                   // 记录插入的位置
    char* table_name_;          // 插入记录的表名称
    size_t table_name_size_;    // 表名称的大小
};

class DeleteLogRecord: public LogRecord {
public:
    DeleteLogRecord() {
        log_type_ = LogType::DELETE;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
        table_name_ = nullptr;
    }
    DeleteLogRecord(txn_id_t txn_id, const RmRecord& deleted_value, const Rid& rid, const std::string& table_name,
                    lsn_t prev_lsn)
        : DeleteLogRecord() {
        log_tid_ = txn_id;
        prev_lsn_ = prev_lsn;
        deleted_value_ = deleted_value;
        rid_ = rid;
        log_tot_len_ += sizeof(int);
        log_tot_len_ += deleted_value_.size;
        log_tot_len_ += sizeof(Rid);
        table_name_size_ = table_name.length();
        table_name_ = new char[table_name_size_ + 1];
        memcpy(table_name_, table_name.c_str(), table_name_size_ + 1);
        log_tot_len_ += sizeof(size_t) + table_name_size_;
    }

    void serialize(char* dest) const override {
        LogRecord::serialize(dest);
        int offset = OFFSET_LOG_DATA;
        memcpy(dest + offset, &deleted_value_.size, sizeof(int));
        offset += sizeof(int);
        memcpy(dest + offset, deleted_value_.data, deleted_value_.size);
        offset += deleted_value_.size;
        memcpy(dest + offset, &rid_, sizeof(Rid));
        offset += sizeof(Rid);
        memcpy(dest + offset, &table_name_size_, sizeof(size_t));
        offset += sizeof(size_t);
        memcpy(dest + offset, table_name_, table_name_size_);
    }
    void deserialize(const char* src) override {
        LogRecord::deserialize(src);
        deleted_value_.Deserialize(src + OFFSET_LOG_DATA);
        int offset = OFFSET_LOG_DATA + deleted_value_.size + sizeof(int);
        rid_ = *reinterpret_cast<const Rid*>(src + offset);
        offset += sizeof(Rid);
        table_name_size_ = *reinterpret_cast<const size_t*>(src + offset);
        offset += sizeof(size_t);
        table_name_ = new char[table_name_size_ + 1];
        memcpy(table_name_, src + offset, table_name_size_);
        table_name_[table_name_size_] = '\0';
    }
    void format_print() override {
        printf("DELETE record\n");
        LogRecord::format_print();
        printf("  rid: page=%d, slot=%d, table: %s\n", rid_.page_no, rid_.slot_no, table_name_);
    }

    RmRecord deleted_value_;    // 被删除的记录（用于undo）
    Rid rid_;                   // 记录被删除的位置
    char* table_name_;
    size_t table_name_size_;
};

class UpdateLogRecord: public LogRecord {
public:
    UpdateLogRecord() {
        log_type_ = LogType::UPDATE;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
        table_name_ = nullptr;
    }
    UpdateLogRecord(txn_id_t txn_id, const RmRecord& old_value, const RmRecord& new_value, const Rid& rid,
                    const std::string& table_name, lsn_t prev_lsn)
        : UpdateLogRecord() {
        log_tid_ = txn_id;
        prev_lsn_ = prev_lsn;
        old_value_ = old_value;
        new_value_ = new_value;
        rid_ = rid;
        log_tot_len_ += 2 * sizeof(int);
        log_tot_len_ += old_value_.size;
        log_tot_len_ += new_value_.size;
        log_tot_len_ += sizeof(Rid);
        table_name_size_ = table_name.length();
        table_name_ = new char[table_name_size_ + 1];
        memcpy(table_name_, table_name.c_str(), table_name_size_ + 1);
        log_tot_len_ += sizeof(size_t) + table_name_size_;
    }

    void serialize(char* dest) const override {
        LogRecord::serialize(dest);
        int offset = OFFSET_LOG_DATA;
        // old value
        memcpy(dest + offset, &old_value_.size, sizeof(int));
        offset += sizeof(int);
        memcpy(dest + offset, old_value_.data, old_value_.size);
        offset += old_value_.size;
        // new value
        memcpy(dest + offset, &new_value_.size, sizeof(int));
        offset += sizeof(int);
        memcpy(dest + offset, new_value_.data, new_value_.size);
        offset += new_value_.size;
        // rid
        memcpy(dest + offset, &rid_, sizeof(Rid));
        offset += sizeof(Rid);
        // table name
        memcpy(dest + offset, &table_name_size_, sizeof(size_t));
        offset += sizeof(size_t);
        memcpy(dest + offset, table_name_, table_name_size_);
    }
    void deserialize(const char* src) override {
        LogRecord::deserialize(src);
        int offset = OFFSET_LOG_DATA;
        old_value_.Deserialize(src + offset);
        offset += old_value_.size + sizeof(int);
        new_value_.Deserialize(src + offset);
        offset += new_value_.size + sizeof(int);
        rid_ = *reinterpret_cast<const Rid*>(src + offset);
        offset += sizeof(Rid);
        table_name_size_ = *reinterpret_cast<const size_t*>(src + offset);
        offset += sizeof(size_t);
        table_name_ = new char[table_name_size_ + 1];
        memcpy(table_name_, src + offset, table_name_size_);
        table_name_[table_name_size_] = '\0';
    }
    void format_print() override {
        printf("UPDATE record\n");
        LogRecord::format_print();
        printf("  rid: page=%d, slot=%d, table: %s\n", rid_.page_no, rid_.slot_no, table_name_);
    }

    RmRecord old_value_;        // 更新前的值（用于undo）
    RmRecord new_value_;        // 更新后的值（用于redo）
    Rid rid_;
    char* table_name_;
    size_t table_name_size_;
};

class CheckpointLogRecord: public LogRecord {
public:
    CheckpointLogRecord() {
        log_type_ = LogType::CHECKPOINT;
        lsn_ = INVALID_LSN;
        log_tot_len_ = LOG_HEADER_SIZE;
        log_tid_ = INVALID_TXN_ID;
        prev_lsn_ = INVALID_LSN;
        active_txn_count_ = 0;
    }
    CheckpointLogRecord(const std::unordered_set<txn_id_t>& active_txns) : CheckpointLogRecord() {
        for (auto tid : active_txns) {
            ActiveTxnInfo info;
            info.txn_id = tid;
            info.last_lsn = INVALID_LSN;
            active_txns_.push_back(info);
        }
        active_txn_count_ = active_txns_.size();
        log_tot_len_ += sizeof(int);
        log_tot_len_ += active_txn_count_ * sizeof(ActiveTxnInfo);
    }

    void serialize(char* dest) const override {
        LogRecord::serialize(dest);
        int offset = OFFSET_LOG_DATA;
        memcpy(dest + offset, &active_txn_count_, sizeof(int));
        offset += sizeof(int);
        if (active_txn_count_ > 0) {
            memcpy(dest + offset, active_txns_.data(), active_txn_count_ * sizeof(ActiveTxnInfo));
        }
    }
    void deserialize(const char* src) override {
        LogRecord::deserialize(src);
        int offset = OFFSET_LOG_DATA;
        active_txn_count_ = *reinterpret_cast<const int*>(src + offset);
        offset += sizeof(int);
        active_txns_.resize(active_txn_count_);
        if (active_txn_count_ > 0) {
            memcpy(active_txns_.data(), src + offset, active_txn_count_ * sizeof(ActiveTxnInfo));
        }
    }
    void format_print() override {
        printf("CHECKPOINT record\n");
        LogRecord::format_print();
        printf("  active txn count: %d\n", active_txn_count_);
    }

    int active_txn_count_;
    std::vector<ActiveTxnInfo> active_txns_;
};

/* 日志记录工厂：从buffer中读取一条日志 */
static std::shared_ptr<LogRecord> create_log_record(const char* src) {
    LogType type = *reinterpret_cast<const LogType*>(src);
    std::shared_ptr<LogRecord> record;
    switch (type) {
        case LogType::begin:
            record = std::make_shared<BeginLogRecord>();
            break;
        case LogType::commit:
            record = std::make_shared<CommitLogRecord>();
            break;
        case LogType::ABORT:
            record = std::make_shared<AbortLogRecord>();
            break;
        case LogType::INSERT:
            record = std::make_shared<InsertLogRecord>();
            break;
        case LogType::DELETE:
            record = std::make_shared<DeleteLogRecord>();
            break;
        case LogType::UPDATE:
            record = std::make_shared<UpdateLogRecord>();
            break;
        case LogType::CHECKPOINT:
            record = std::make_shared<CheckpointLogRecord>();
            break;
        default:
            return nullptr;
    }
    record->deserialize(src);
    return record;
}

/* 日志缓冲区，只有一个buffer，因此需要阻塞地去把日志写入缓冲区中 */
class LogBuffer {
public:
    LogBuffer() {
        offset_ = 0;
        memset(buffer_, 0, sizeof(buffer_));
    }

    bool is_full(int append_size) {
        if(offset_ + append_size > LOG_BUFFER_SIZE)
            return true;
        return false;
    }

    char buffer_[LOG_BUFFER_SIZE+1];
    int offset_;    // 写入log的offset
};

/* 日志管理器，负责把日志写入日志缓冲区，以及把日志缓冲区中的内容写入磁盘中 */
class LogManager {
public:
    LogManager(DiskManager* disk_manager) { disk_manager_ = disk_manager; }

    lsn_t add_log_to_buffer(LogRecord* log_record);
    void flush_log_to_disk();
    void create_static_checkpoint(SmManager *sm_manager, TransactionManager *txn_mgr);

    LogBuffer* get_log_buffer() { return &log_buffer_; }
    lsn_t get_persist_lsn() { return persist_lsn_; }
    std::mutex& get_latch() { return latch_; }

    void set_flush_now(bool val) { flush_now_ = val; }

private:
    std::atomic<lsn_t> global_lsn_{0};  // 全局lsn，递增，用于为每条记录分发lsn
    std::mutex latch_;                  // 用于对log_buffer_的互斥访问
    LogBuffer log_buffer_;              // 日志缓冲区
    lsn_t persist_lsn_{0};              // 记录已经持久化到磁盘中的最后一条日志的日志号
    DiskManager* disk_manager_;
    bool flush_now_ = false;
};
