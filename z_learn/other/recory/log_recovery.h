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

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "log_manager.h"
#include "storage/disk_manager.h"
#include "storage/buffer_pool_manager.h"
#include "system/sm_manager.h"

class RecoveryManager {
public:
    RecoveryManager(DiskManager* disk_manager, BufferPoolManager* buffer_pool_manager, SmManager* sm_manager) {
        disk_manager_ = disk_manager;
        buffer_pool_manager_ = buffer_pool_manager;
        sm_manager_ = sm_manager;
    }

    void analyze();
    void redo();
    void undo();

private:
    struct RecoveryLogEntry {
        LogType type{LogType::begin};
        txn_id_t txn_id{INVALID_TXN_ID};
        std::string table_name;
        Rid rid{-1, -1};
        RmRecord old_record;
        RmRecord new_record;
    };

    DiskManager* disk_manager_;
    BufferPoolManager* buffer_pool_manager_;
    SmManager* sm_manager_;

    std::vector<RecoveryLogEntry> logs_;
    std::unordered_set<txn_id_t> committed_txns_;
    std::unordered_set<txn_id_t> loser_txns_;

    int GetRestartOffset();
    void RebuildIndexes();
    void FlushTables();
    bool IsRecordPresent(RmFileHandle *fh, const Rid &rid);
    void ApplyInsert(const RecoveryLogEntry &entry);
    void ApplyDelete(const RecoveryLogEntry &entry);
    void ApplyUpdate(const RecoveryLogEntry &entry, bool use_new_value);
};
