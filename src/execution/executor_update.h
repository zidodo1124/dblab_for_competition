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
#include "system/sm.h"

class UpdateExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;
    std::vector<Condition> conds_;
    RmFileHandle *fh_;
    std::vector<Rid> rids_;
    std::string tab_name_;
    std::vector<SetClause> set_clauses_;
    SmManager *sm_manager_;

    std::vector<char> make_index_key(const RmRecord *rec, const IndexMeta &index) {
        std::vector<char> key(index.col_tot_len);
        int offset = 0;
        for (int i = 0; i < index.col_num; ++i) {
            memcpy(key.data() + offset, rec->data + index.cols[i].offset, index.cols[i].len);
            offset += index.cols[i].len;
        }
        return key;
    }

    bool rid_in_targets(const Rid &rid, const std::vector<Rid> &targets) {
        for (auto &target : targets) {
            if (target == rid) {
                return true;
            }
        }
        return false;
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
        auto write_guard = context_->txn_mgr_ != nullptr ? context_->txn_mgr_->acquire_write_latch()
                                                         : std::unique_lock<std::mutex>();
        std::vector<RmRecord> old_recs;
        std::vector<RmRecord> new_recs;
        std::vector<Rid> target_rids;
        old_recs.reserve(rids_.size());
        new_recs.reserve(rids_.size());
        target_rids.reserve(rids_.size());

        for (auto &rid : rids_) {
            if (context_->txn_mgr_ != nullptr) {
                context_->txn_mgr_->check_write_conflict(tab_name_, rid, context_->txn_);
            }
            auto old_rec = context_->txn_mgr_ != nullptr
                               ? context_->txn_mgr_->get_visible_record(tab_name_, rid, fh_, context_)
                               : fh_->get_record(rid, context_);
            if (old_rec == nullptr) {
                continue;
            }
            target_rids.push_back(rid);
            old_recs.emplace_back(*old_rec);
            new_recs.emplace_back(fh_->get_file_hdr().record_size);
            memcpy(new_recs.back().data, old_rec->data, new_recs.back().size);
            for (auto &set_clause : set_clauses_) {
                auto col = tab_.get_col(set_clause.lhs.col_name);
                Value val = set_clause.rhs;
                if (set_clause.is_self_add) {
                    if (col->type == TYPE_INT) {
                        int old_val = *reinterpret_cast<int *>(old_rec->data + col->offset);
                        val.set_int(old_val + set_clause.add_int);
                    } else if (col->type == TYPE_FLOAT) {
                        float old_val = *reinterpret_cast<float *>(old_rec->data + col->offset);
                        float delta = set_clause.add_is_float ? set_clause.add_float : static_cast<float>(set_clause.add_int);
                        val.set_float(old_val + delta);
                    }
                    val.raw = nullptr;
                }
                if (col->type == TYPE_FLOAT && val.type == TYPE_INT) {
                    val.set_float(static_cast<float>(val.int_val));
                    val.raw = nullptr;
                }
                if (col->type != val.type) {
                    throw IncompatibleTypeError(coltype2str(col->type), coltype2str(val.type));
                }
                if (val.raw == nullptr) {
                    val.init_raw(col->len);
                }
                memcpy(new_recs.back().data + col->offset, val.raw->data, col->len);
            }
        }

        if (context_->txn_mgr_ != nullptr) {
            for (auto &new_rec : new_recs) {
                context_->txn_mgr_->check_insert_conflict(tab_name_, new_rec, context_->txn_);
            }
        }

        for (auto &index : tab_.indexes) {
            auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
            std::vector<std::vector<char>> new_keys;
            new_keys.reserve(new_recs.size());
            for (size_t i = 0; i < new_recs.size(); ++i) {
                auto key = make_index_key(&new_recs[i], index);
                for (auto &prev_key : new_keys) {
                    if (memcmp(prev_key.data(), key.data(), index.col_tot_len) == 0) {
                        throw UniqueConstraintViolationError(tab_name_);
                    }
                }
                std::vector<Rid> existing;
                if (ih->get_value(key.data(), &existing, context_->txn_)) {
                    for (auto &exist_rid : existing) {
                        if (!rid_in_targets(exist_rid, target_rids)) {
                            throw UniqueConstraintViolationError(tab_name_);
                        }
                    }
                }
                new_keys.push_back(std::move(key));
            }
        }

        for (size_t i = 0; i < target_rids.size(); ++i) {
            if (context_->txn_mgr_ != nullptr) {
                context_->txn_mgr_->check_ser_write(tab_name_, target_rids[i], &old_recs[i], &new_recs[i], context_->txn_);
            }
            for (auto &index : tab_.indexes) {
                auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                auto key = make_index_key(&old_recs[i], index);
                ih->delete_entry(key.data(), context_->txn_);
            }
        }

        for (size_t i = 0; i < target_rids.size(); ++i) {
            lsn_t lsn = INVALID_LSN;
            if (context_->log_mgr_ != nullptr && context_->txn_ != nullptr) {
                UpdateLogRecord log_record(context_->txn_->get_transaction_id(), old_recs[i], new_recs[i],
                                           target_rids[i], tab_name_);
                lsn = context_->log_mgr_->add_log_record(&log_record, context_->txn_);
            }
            fh_->update_record(target_rids[i], new_recs[i].data, context_, lsn);
            if (context_->txn_ != nullptr) {
                if (context_->txn_mgr_ != nullptr) {
                    context_->txn_mgr_->append_update_version(tab_name_, target_rids[i], old_recs[i], new_recs[i], context_->txn_);
                }
                context_->txn_->append_write_record(new WriteRecord(WType::UPDATE_TUPLE, tab_name_, target_rids[i], old_recs[i]));
            }
        }

        for (size_t i = 0; i < target_rids.size(); ++i) {
            for (auto &index : tab_.indexes) {
                auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index.cols)).get();
                auto key = make_index_key(&new_recs[i], index);
                ih->insert_entry(key.data(), target_rids[i], context_->txn_);
            }
        }
        return nullptr;
    }

    Rid &rid() override { return _abstract_rid; }
};
