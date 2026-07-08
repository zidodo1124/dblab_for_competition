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
#include "common/common.h"
#include "defs.h"
#include "errors.h"
#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"
#include "recovery/log_manager.h"
#include <cassert>
#include <cstddef>
#include <cstring>
#include <string>
#include <unordered_set>

class UpdateExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;
    std::vector<Condition> conds_;
    RmFileHandle *fh_;
    std::vector<Rid> rids_;
    std::string tab_name_;
    std::vector<SetClause> set_clauses_;
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
    UpdateExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<SetClause> set_clauses,
                   std::vector<Condition> conds, std::vector<Rid> rids, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = tab_name;
        set_clauses_ = set_clauses;
        tab_ = sm_manager_->db_.get_table(tab_name);
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        conds_ = conds;
        rids_ = rids;
        context_ = context;
    }
  std::unique_ptr<RmRecord> Next() override {
    // 有一个重复 拒绝整句请求。
    for (const auto& idx_meta : tab_.indexes) {
      std::string idx_name = sm_manager_->get_ix_manager()->get_index_name(tab_.name, idx_meta.cols);
      auto ih = sm_manager_->ihs_.at(idx_name).get();
      std::unordered_set<std::string> key_set;
      for (const auto& rid : rids_) {
        std::string new_key(idx_meta.col_tot_len, '\0');
        std::unique_ptr<RmRecord> old_record;
        try {
          old_record = fh_->get_record(rid, context_);
        } catch (...) {
          continue;
        }
        auto new_record(*old_record);
        for (auto& set_clause : set_clauses_) {
          auto it = get_col(tab_.cols, set_clause.lhs);
          write_value_to_col(set_clause.rhs, *it, new_record.data + it->offset);
        }
        int curr_offset = 0;
        for (const auto& col : idx_meta.cols) {
          memcpy(new_key.data() + curr_offset, new_record.data + col.offset, col.len);
          curr_offset += col.len;
        }
        std::vector<Rid> rids;
        if(ih->get_value(new_key.data(), &rids, context_ ? context_->txn_ : nullptr)) {
          auto it = find_if(rids.begin(), rids.end(), [&](const Rid rid2){
            return rid2 == rid;
          });
          if(it == rids.end()) {
            throw RMDBError("Duplicate Index error");
          }
        }
        if(!key_set.insert(new_key).second) {
          throw RMDBError("Duplicate Index error");
        }
      }
    }



    // 更新索引
    for (const auto &rid : rids_) {

      std::unique_ptr<RmRecord> old_record;
      try {
        old_record = fh_->get_record(rid, context_);
      } catch (...) {
        continue;
      }
      auto new_record(*old_record);
      for (auto& set_clause : set_clauses_) {
        auto it = get_col(tab_.cols, set_clause.lhs);
        write_value_to_col(set_clause.rhs, *it, new_record.data + it->offset);
      }

      // WAL: write UPDATE log before touching indexes or data pages
      if (context_ != nullptr && context_->log_mgr_ != nullptr && context_->txn_ != nullptr) {
          UpdateLogRecord update_log(context_->txn_->get_transaction_id(),
                                     *old_record, new_record, rid, tab_name_,
                                     context_->txn_->get_prev_lsn());
          lsn_t lsn = context_->log_mgr_->add_log_to_buffer(&update_log);
          context_->txn_->set_prev_lsn(lsn);
          context_->log_mgr_->flush_log_to_disk();
      }

      for (const auto& idx_meta : tab_.indexes) {
        std::string idx_name = sm_manager_->get_ix_manager()->get_index_name(tab_.name, idx_meta.cols);
        char* old_key = new char[idx_meta.col_tot_len];
        char* new_key = new char[idx_meta.col_tot_len];
        int curr_offset = 0;
        for (const auto& col : idx_meta.cols) {
          memcpy(old_key + curr_offset, old_record->data + col.offset, col.len);
          memcpy(new_key + curr_offset, new_record.data + col.offset, col.len);
          curr_offset += col.len;
        }
        if(memcmp(old_key, new_key, idx_meta.col_tot_len) != 0) {
          auto ih = sm_manager_->ihs_.at(idx_name).get();
          ih->delete_entry(old_key, context_ ? context_ ->txn_ : nullptr);
          ih->insert_entry(new_key, rid, context_ ? context_->txn_ : nullptr);
        }
        delete[] new_key;
        delete[] old_key;
      }

      try {
        fh_->update_record(rid, new_record.data, context_);
      } catch (...) {
      }
    }
    return nullptr;
  }

    std::string getType() override{ return "UpdateExecutor"; };

    Rid &rid() override { return _abstract_rid; }
};
