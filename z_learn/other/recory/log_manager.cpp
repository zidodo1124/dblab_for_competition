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
#include "common/config.h"
#include "storage/disk_manager.h"
#include "system/sm_manager.h"
#include "transaction/transaction_manager.h"

/**
 * @description: 添加日志记录到日志缓冲区中，并返回日志记录号
 * @param {LogRecord*} log_record 要写入缓冲区的日志记录
 * @return {lsn_t} 返回该日志的日志记录号
 */
lsn_t LogManager::add_log_to_buffer(LogRecord* log_record) {
    std::unique_lock<std::mutex> lock(latch_);

    // 分配全局递增的LSN
    lsn_t lsn = global_lsn_.fetch_add(1);
    log_record->lsn_ = lsn;

    int record_len = log_record->log_tot_len_;

    // 如果缓冲区满，先刷盘
    if (log_buffer_.is_full(record_len)) {
        disk_manager_->write_log(log_buffer_.buffer_, log_buffer_.offset_);
        persist_lsn_ = lsn - 1;
        log_buffer_.offset_ = 0;
    }

    // 将日志记录序列化到缓冲区
    log_record->serialize(log_buffer_.buffer_ + log_buffer_.offset_);
    log_buffer_.offset_ += record_len;

    return lsn;
}

/**
 * @description: 把日志缓冲区的内容刷到磁盘中，由于目前只设置了一个缓冲区，因此需要阻塞其他日志操作
 */
void LogManager::flush_log_to_disk() {
    std::unique_lock<std::mutex> lock(latch_);
    if (log_buffer_.offset_ == 0) return;

    disk_manager_->write_log(log_buffer_.buffer_, log_buffer_.offset_);
    persist_lsn_ = global_lsn_.load() - 1;
    log_buffer_.offset_ = 0;
}

void LogManager::create_static_checkpoint(SmManager *sm_manager, TransactionManager *txn_mgr) {
    // 步骤1: 收集当前的活跃事务
    std::unordered_set<txn_id_t> active_txns;
    if (txn_mgr != nullptr) {
        active_txns = txn_mgr->snapshot_active_txns();
    }

    // 步骤2: 将日志缓冲区刷入磁盘
    flush_log_to_disk();
    int checkpoint_offset = disk_manager_->get_file_size(LOG_FILE_NAME);

    // 步骤3: 写入检查点记录（包含活跃事务信息）
    CheckpointLogRecord checkpoint_log(active_txns);
    add_log_to_buffer(&checkpoint_log);
    flush_log_to_disk();

    // 步骤4: 将数据库缓冲区的脏页刷入磁盘
    sm_manager->flush_meta();
    for (auto &file : sm_manager->fhs_) {
        file.second->flush_file_hdr();
        sm_manager->get_bpm()->flush_all_pages(file.second->GetFd());
    }
    for (auto &index_file : sm_manager->ihs_) {
        sm_manager->get_bpm()->flush_all_pages(index_file.second->GetFd());
    }

    // 步骤5: 将检查点记录的地址写入重启文件
    std::ofstream restart_file(RESTART_FILE_NAME, std::ios::out | std::ios::trunc);
    restart_file << checkpoint_offset;
}
