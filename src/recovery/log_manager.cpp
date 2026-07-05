/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include <cstring>
#include <fstream>
#include "log_manager.h"
#include "system/sm_manager.h"
#include "storage/buffer_pool_manager.h"
#include "transaction/transaction.h"

void LogManager::ensure_lsn_initialized_unlocked() {
    if (next_lsn_ == INVALID_LSN) {
        next_lsn_ = disk_manager_->get_file_size(LOG_FILE_NAME) + log_buffer_.offset_;
    }
}

/**
 * @description: 添加日志记录到日志缓冲区中，并返回日志记录号
 * @param {LogRecord*} log_record 要写入缓冲区的日志记录
 * @return {lsn_t} 返回该日志的日志记录号
 */
lsn_t LogManager::add_log_to_buffer(LogRecord* log_record) {
    std::lock_guard<std::mutex> lock(latch_);
    if (log_buffer_.is_full(log_record->log_tot_len_)) {
        persist_lsn_ = disk_manager_->get_file_size(LOG_FILE_NAME) + log_buffer_.offset_ - 1;
        disk_manager_->write_log(log_buffer_.buffer_, log_buffer_.offset_);
        memset(log_buffer_.buffer_, 0, sizeof(log_buffer_.buffer_));
        log_buffer_.offset_ = 0;
    }
    log_record->serialize(log_buffer_.buffer_ + log_buffer_.offset_);
    log_buffer_.offset_ += static_cast<int>(log_record->log_tot_len_);
    return log_record->lsn_;
}

/**
 * @description: 把日志缓冲区的内容刷到磁盘中，由于目前只设置了一个缓冲区，因此需要阻塞其他日志操作
 */
void LogManager::flush_log_to_disk() {
    std::lock_guard<std::mutex> lock(latch_);
    if (log_buffer_.offset_ == 0) {
        return;
    }
    disk_manager_->write_log(log_buffer_.buffer_, log_buffer_.offset_);
    if (next_lsn_ != INVALID_LSN) {
        persist_lsn_ = next_lsn_ - 1;
    }
    memset(log_buffer_.buffer_, 0, sizeof(log_buffer_.buffer_));
    log_buffer_.offset_ = 0;
}

lsn_t LogManager::add_log_record(LogRecord* log_record, Transaction* txn) {
    std::lock_guard<std::mutex> lock(latch_);
    ensure_lsn_initialized_unlocked();
    if (log_buffer_.is_full(log_record->log_tot_len_)) {
        persist_lsn_ = next_lsn_ - 1;
        disk_manager_->write_log(log_buffer_.buffer_, log_buffer_.offset_);
        memset(log_buffer_.buffer_, 0, sizeof(log_buffer_.buffer_));
        log_buffer_.offset_ = 0;
    }
    log_record->lsn_ = next_lsn_;
    next_lsn_ += static_cast<lsn_t>(log_record->log_tot_len_);
    if (txn != nullptr) {
        log_record->prev_lsn_ = txn->get_prev_lsn();
        txn->set_prev_lsn(log_record->lsn_);
    }
    log_record->serialize(log_buffer_.buffer_ + log_buffer_.offset_);
    log_buffer_.offset_ += static_cast<int>(log_record->log_tot_len_);
    return log_record->lsn_;
}

void LogManager::create_static_checkpoint(BufferPoolManager* bpm, SmManager* sm_manager,
                                          std::vector<txn_id_t> active_txn_ids) {
    flush_log_to_disk();
    int checkpoint_offset = disk_manager_->get_file_size(LOG_FILE_NAME);
    int restart_offset = active_txn_ids.empty() ? checkpoint_offset : 0;
    CheckpointLogRecord checkpoint(std::move(active_txn_ids));
    add_log_record(&checkpoint);
    flush_log_to_disk();
    sm_manager->flush_meta();
    for (auto &entry : sm_manager->fhs_) {
        entry.second->flush_file_hdr();
        bpm->flush_dirty_pages(entry.second->GetFd());
    }
    for (auto &entry : sm_manager->ihs_) {
        bpm->flush_dirty_pages(entry.second->get_fd());
    }
    std::ofstream restart("restart.ckpt", std::ios::trunc);
    restart << restart_offset;
    restart.flush();
}
