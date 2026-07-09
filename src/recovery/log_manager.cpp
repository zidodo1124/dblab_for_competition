/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include <cstdio>
#include <cstring>
#include <fstream>
#include "log_manager.h"
#include "system/sm_manager.h"
#include "storage/buffer_pool_manager.h"
#include "transaction/transaction.h"
#include "transaction/transaction_manager.h"

void LogManager::ensure_lsn_initialized_unlocked() {
    if (next_lsn_ == INVALID_LSN) {
        int file_size = disk_manager_->get_file_size(LOG_FILE_NAME);
        if (file_size < 0) {
            file_size = 0;
        }
        next_lsn_ = file_size + log_buffer_.offset_;
        persist_lsn_ = file_size == 0 ? INVALID_LSN : static_cast<lsn_t>(file_size - 1);
    }
}

/**
 * @description: 添加日志记录到日志缓冲区中，并返回日志记录号
 * @param {LogRecord*} log_record 要写入缓冲区的日志记录
 * @return {lsn_t} 返回该日志的日志记录号
 */
lsn_t LogManager::add_log_to_buffer(LogRecord* log_record) {
    return add_log_record(log_record);
}

/**
 * @description: 把日志缓冲区的内容刷到磁盘中，由于目前只设置了一个缓冲区，因此需要阻塞其他日志操作
 */
void LogManager::flush_log_to_disk() {
    std::lock_guard<std::mutex> lock(latch_);
    ensure_lsn_initialized_unlocked();
    if (log_buffer_.offset_ == 0) {
        return;
    }
    disk_manager_->write_log(log_buffer_.buffer_, log_buffer_.offset_);
    if (next_lsn_ != INVALID_LSN) {
        persist_lsn_ = next_lsn_ - 1;
    }
    log_buffer_.offset_ = 0;
}

void LogManager::flush_log_to_lsn(lsn_t lsn) {
    std::lock_guard<std::mutex> lock(latch_);
    ensure_lsn_initialized_unlocked();
    if (lsn == INVALID_LSN || lsn <= persist_lsn_) {
        return;
    }
    if (log_buffer_.offset_ > 0) {
        disk_manager_->write_log(log_buffer_.buffer_, log_buffer_.offset_);
        persist_lsn_ = next_lsn_ - 1;
        log_buffer_.offset_ = 0;
    }
}

lsn_t LogManager::get_persist_lsn() {
    std::lock_guard<std::mutex> lock(latch_);
    ensure_lsn_initialized_unlocked();
    return persist_lsn_;
}

lsn_t LogManager::add_log_record(LogRecord* log_record, Transaction* txn) {
    std::lock_guard<std::mutex> lock(latch_);
    ensure_lsn_initialized_unlocked();
    if (log_buffer_.is_full(log_record->log_tot_len_)) {
        disk_manager_->write_log(log_buffer_.buffer_, log_buffer_.offset_);
        persist_lsn_ = next_lsn_ - 1;
        log_buffer_.offset_ = 0;
    }
    log_record->lsn_ = next_lsn_;
    next_lsn_ += static_cast<lsn_t>(log_record->log_tot_len_);
    if (txn != nullptr) {
        log_record->prev_lsn_ = txn->get_prev_lsn();
        if (txn->get_first_lsn() == INVALID_LSN) {
            txn->set_first_lsn(log_record->lsn_);
        }
        if ((log_record->log_type_ == LogType::INSERT || log_record->log_type_ == LogType::DELETE ||
             log_record->log_type_ == LogType::UPDATE) &&
            txn->get_first_write_lsn() == INVALID_LSN) {
            txn->set_first_write_lsn(log_record->lsn_);
        }
        txn->set_prev_lsn(log_record->lsn_);
    }
    log_record->serialize(log_buffer_.buffer_ + log_buffer_.offset_);
    log_buffer_.offset_ += static_cast<int>(log_record->log_tot_len_);
    return log_record->lsn_;
}

void LogManager::create_static_checkpoint(BufferPoolManager* bpm, SmManager* sm_manager, TransactionManager* txn_mgr,
                                          Transaction* current_txn) {
    auto write_guard = txn_mgr != nullptr ? txn_mgr->acquire_write_latch() : std::unique_lock<std::mutex>();
    flush_log_to_disk();
    int checkpoint_offset = disk_manager_->get_file_size(LOG_FILE_NAME);
    bool current_is_implicit = current_txn != nullptr && !current_txn->get_txn_mode();
    txn_id_t exclude_txn_id = current_is_implicit ? current_txn->get_transaction_id() : INVALID_TXN_ID;
    std::vector<CheckpointTxnInfo> active_txns =
        txn_mgr != nullptr ? txn_mgr->active_txn_infos(exclude_txn_id) : std::vector<CheckpointTxnInfo>();
    CheckpointLogRecord checkpoint(std::move(active_txns));
    add_log_record(&checkpoint);
    flush_log_to_disk();
    sm_manager->flush_meta();
    std::vector<int> flush_fds;
    flush_fds.reserve(sm_manager->fhs_.size() + sm_manager->ihs_.size());
    for (auto &entry : sm_manager->fhs_) {
        entry.second->flush_file_hdr();
        flush_fds.push_back(entry.second->GetFd());
    }
    for (auto &entry : sm_manager->ihs_) {
        flush_fds.push_back(entry.second->get_fd());
    }
    if (bpm != nullptr) {
        bpm->flush_dirty_pages(flush_fds);
    }
    for (const char *restart_name : {"db.restart", "restart.ckpt"}) {
        std::string tmp_name = std::string(restart_name) + ".tmp";
        std::ofstream restart(tmp_name, std::ios::trunc);
        restart << checkpoint_offset;
        restart.flush();
        restart.close();
        std::rename(tmp_name.c_str(), restart_name);
    }
}
