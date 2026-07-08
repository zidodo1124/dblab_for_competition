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
#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "index/ix_index_handle.h"
#include "system/sm.h"
#include "recovery/log_manager.h"
#include <cassert>
#include <cstddef>
#include <cstring>
#include <vector>

class DeleteExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;                   // 表的元数据
    std::vector<Condition> conds_;  // delete的条件
    RmFileHandle *fh_;              // 表的数据文件句柄
    std::vector<Rid> rids_;         // 需要删除的记录的位置
    std::string tab_name_;          // 表名称
    SmManager *sm_manager_;

    //to debug
    bool satisfy_conds() {
    for (const auto & rid : rids_) {
      auto record = fh_->get_record(rid, context_);
      assert(eval_conds(tab_.cols, record->data, conds_));
    }
    return true;
  }

   public:
    DeleteExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<Condition> conds,
                   std::vector<Rid> rids, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = tab_name;
        tab_ = sm_manager_->db_.get_table(tab_name);
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        conds_ = conds;
        rids_ = rids;
        context_ = context;
    }

  std::unique_ptr<RmRecord> Next() override {
  // 先收集ih和ixmeta 再处理?
    for (const auto &rid : rids_) {
      std::unique_ptr<RmRecord> current_record;
      try {
        current_record = fh_->get_record(rid, context_);
      } catch (...) {
        continue;
      }

      // WAL: write DELETE log with old record before deleting
      if (context_ != nullptr && context_->log_mgr_ != nullptr && context_->txn_ != nullptr) {
          RmRecord old_rec_copy(*current_record);
          Rid rid_copy = rid;
          DeleteLogRecord delete_log(context_->txn_->get_transaction_id(), old_rec_copy,
                                      rid_copy, tab_name_, context_->txn_->get_prev_lsn());
          lsn_t lsn = context_->log_mgr_->add_log_to_buffer(&delete_log);
          context_->txn_->set_prev_lsn(lsn);
          context_->log_mgr_->flush_log_to_disk();
      }

      for (const auto& idx_meta : tab_.indexes) {
        std::string idx_name = sm_manager_->get_ix_manager()->get_index_name(tab_.name, idx_meta.cols);
        auto ih = sm_manager_->ihs_.at(idx_name).get();
        char* idx_key = new char[idx_meta.col_tot_len];
        int curr_offset = 0;
        for (const auto& col : idx_meta.cols) {
          memcpy(idx_key + curr_offset, current_record->data + col.offset, col.len);
          curr_offset += col.len;
        }
        ih->delete_entry(idx_key, context_ ? context_ ->txn_ : nullptr);
        delete[] idx_key;
      }
      try {
        fh_->delete_record(rid, nullptr);
      } catch (...) {
      }
    }
    return nullptr;
  }

    Rid &rid() override { return _abstract_rid; }
};
