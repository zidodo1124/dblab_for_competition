/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include <algorithm>
#include <limits>
#include <map>
#include <sstream>

#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "index/ix_scan.h"
#include "record/rm_scan.h"
#include "system/sm.h"

class IndexScanExecutor : public AbstractExecutor {
   private:
    std::string tab_name_;
    std::string logical_tab_name_;
    TabMeta tab_;
    std::vector<Condition> conds_;
    RmFileHandle *fh_;
    std::vector<ColMeta> cols_;
    size_t len_;
    std::vector<Condition> fed_conds_;
    std::vector<Condition> runtime_conds_;

    std::vector<std::string> index_col_names_;
    IndexMeta index_meta_;

    Rid rid_;
    std::vector<std::pair<Rid, RmRecord>> visible_records_;
    size_t cursor_ = 0;
    IxIndexHandle *ih_;

    SmManager *sm_manager_;

    void fill_min(char *buf, const ColMeta &col) {
        if (col.type == TYPE_INT) {
            int v = std::numeric_limits<int>::min();
            memcpy(buf, &v, sizeof(int));
        } else if (col.type == TYPE_FLOAT) {
            float v = -std::numeric_limits<float>::max();
            memcpy(buf, &v, sizeof(float));
        } else {
            memset(buf, 0, col.len);
        }
    }

    void fill_max(char *buf, const ColMeta &col) {
        if (col.type == TYPE_INT) {
            int v = std::numeric_limits<int>::max();
            memcpy(buf, &v, sizeof(int));
        } else if (col.type == TYPE_FLOAT) {
            float v = std::numeric_limits<float>::max();
            memcpy(buf, &v, sizeof(float));
        } else {
            memset(buf, 0x7f, col.len);
        }
    }

    void copy_value(char *buf, const Condition &cond, const ColMeta &col) {
        if (cond.rhs_val.raw != nullptr) {
            memcpy(buf, cond.rhs_val.raw->data, col.len);
            return;
        }
        if (col.type == TYPE_INT) {
            int v = cond.rhs_val.type == TYPE_FLOAT ? static_cast<int>(cond.rhs_val.float_val)
                                                     : cond.rhs_val.int_val;
            memcpy(buf, &v, sizeof(int));
        } else if (col.type == TYPE_FLOAT) {
            float v = cond.rhs_val.type == TYPE_INT ? static_cast<float>(cond.rhs_val.int_val)
                                                     : cond.rhs_val.float_val;
            memcpy(buf, &v, sizeof(float));
        } else {
            memset(buf, 0, col.len);
            memcpy(buf, cond.rhs_val.str_val.c_str(), std::min<int>(col.len, cond.rhs_val.str_val.size()));
        }
    }

    std::vector<Condition> active_conds() const {
        std::vector<Condition> conds = fed_conds_;
        conds.insert(conds.end(), runtime_conds_.begin(), runtime_conds_.end());
        return conds;
    }

    std::vector<Condition> normalized_value_conds() {
        auto conds = active_conds();
        std::vector<Condition> normalized;
        for (auto cond : conds) {
            if (!cond.is_rhs_val) {
                continue;
            }
            if (cond.lhs_col.tab_name == logical_tab_name_) {
                normalized.push_back(cond);
            }
        }
        return normalized;
    }

    void build_index_range(Iid &lower, Iid &upper) {
        auto normalized = normalized_value_conds();
        std::vector<char> low_key(index_meta_.col_tot_len);
        std::vector<char> high_key(index_meta_.col_tot_len);
        int offset = 0;
        bool lower_inclusive = true;
        bool upper_inclusive = true;
        bool stopped = false;

        for (int i = 0; i < index_meta_.col_num; ++i) {
            const auto &col = index_meta_.cols[i];
            char *low = low_key.data() + offset;
            char *high = high_key.data() + offset;
            fill_min(low, col);
            fill_max(high, col);

            if (!stopped) {
                Condition *eq = nullptr;
                Condition *gt = nullptr;
                Condition *ge = nullptr;
                Condition *lt = nullptr;
                Condition *le = nullptr;
                for (auto &cond : normalized) {
                    if (cond.lhs_col.col_name != col.name) {
                        continue;
                    }
                    if (cond.op == OP_EQ) {
                        eq = &cond;
                    } else if (cond.op == OP_GT) {
                        gt = &cond;
                    } else if (cond.op == OP_GE) {
                        ge = &cond;
                    } else if (cond.op == OP_LT) {
                        lt = &cond;
                    } else if (cond.op == OP_LE) {
                        le = &cond;
                    }
                }

                if (eq != nullptr) {
                    copy_value(low, *eq, col);
                    copy_value(high, *eq, col);
                } else {
                    if (gt != nullptr || ge != nullptr) {
                        Condition *bound = ge != nullptr ? ge : gt;
                        copy_value(low, *bound, col);
                        lower_inclusive = (bound->op == OP_GE);
                    }
                    if (lt != nullptr || le != nullptr) {
                        Condition *bound = le != nullptr ? le : lt;
                        copy_value(high, *bound, col);
                        upper_inclusive = (bound->op == OP_LE);
                    }
                    stopped = true;
                }
            }
            offset += col.len;
        }

        std::vector<ColType> col_types;
        std::vector<int> col_lens;
        for (auto &col : index_meta_.cols) {
            col_types.push_back(col.type);
            col_lens.push_back(col.len);
        }
        int range_cmp = ix_compare(low_key.data(), high_key.data(), col_types, col_lens);
        if (range_cmp > 0 || (range_cmp == 0 && (!lower_inclusive || !upper_inclusive))) {
            lower = ih_->leaf_end();
            upper = lower;
            return;
        }

        lower = lower_inclusive ? ih_->lower_bound(low_key.data()) : ih_->upper_bound(low_key.data());
        upper = upper_inclusive ? ih_->upper_bound(high_key.data()) : ih_->lower_bound(high_key.data());
    }

    void advance_to_valid() {
        while (cursor_ < visible_records_.size()) {
            rid_ = visible_records_[cursor_].first;
            rows_++;
            auto conds = active_conds();
            if (eval_conds(cols_, conds, &visible_records_[cursor_].second)) {
                return;
            }
            cursor_++;
        }
    }

    bool make_runtime_condition(const Condition &join_cond, const std::vector<ColMeta> &outer_cols,
                                const RmRecord *outer_rec, Condition &runtime_cond) {
        if (join_cond.is_rhs_val || join_cond.op != OP_EQ) {
            return false;
        }

        TabCol inner_col;
        TabCol outer_col;
        if (join_cond.lhs_col.tab_name == logical_tab_name_) {
            inner_col = join_cond.lhs_col;
            outer_col = join_cond.rhs_col;
        } else if (join_cond.rhs_col.tab_name == logical_tab_name_) {
            inner_col = join_cond.rhs_col;
            outer_col = join_cond.lhs_col;
        } else {
            return false;
        }

        const auto outer_meta = get_col(outer_cols, outer_col);
        runtime_cond.lhs_col = inner_col;
        runtime_cond.op = OP_EQ;
        runtime_cond.is_rhs_val = true;
        runtime_cond.rhs_col = {};
        runtime_cond.rhs_val.raw = nullptr;
        if (outer_meta->type == TYPE_INT) {
            runtime_cond.rhs_val.set_int(*(int *)(outer_rec->data + outer_meta->offset));
        } else if (outer_meta->type == TYPE_FLOAT) {
            runtime_cond.rhs_val.set_float(*(float *)(outer_rec->data + outer_meta->offset));
        } else {
            std::string value(outer_rec->data + outer_meta->offset, outer_meta->len);
            value.resize(strlen(value.c_str()));
            runtime_cond.rhs_val.set_str(value);
        }
        runtime_cond.rhs_val.init_raw(outer_meta->len);
        return true;
    }

   public:
    IndexScanExecutor(SmManager *sm_manager, std::string tab_name, std::vector<Condition> conds,
                      std::vector<std::string> index_col_names, Context *context,
                      std::string logical_tab_name = "") {
        sm_manager_ = sm_manager;
        context_ = context;
        tab_name_ = std::move(tab_name);
        logical_tab_name_ = logical_tab_name.empty() ? tab_name_ : std::move(logical_tab_name);
        tab_ = sm_manager_->db_.get_table(tab_name_);
        conds_ = std::move(conds);
        index_col_names_ = std::move(index_col_names);
        index_meta_ = *(tab_.get_index_meta(index_col_names_));
        fh_ = sm_manager_->fhs_.at(tab_name_).get();
        ih_ = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, index_meta_.cols)).get();
        cols_ = tab_.cols;
        for (auto &col : cols_) {
            col.tab_name = logical_tab_name_;
        }
        len_ = cols_.back().offset + cols_.back().len;
        fed_conds_ = conds_;
    }

    void beginTuple() override {
        visible_records_.clear();
        cursor_ = 0;
        Iid lower;
        Iid upper;
        build_index_range(lower, upper);
        for (IxScan scan(ih_, lower, upper, sm_manager_->get_bpm()); !scan.is_end(); scan.next()) {
            Rid candidate = scan.rid();
            std::unique_ptr<RmRecord> rec =
                context_->txn_mgr_ != nullptr
                    ? context_->txn_mgr_->get_visible_record(tab_name_, candidate, fh_, context_)
                    : fh_->get_record(candidate, context_);
            if (rec != nullptr) {
                visible_records_.emplace_back(candidate, *rec);
            }
        }
        if (context_->is_select_ && context_->txn_mgr_ != nullptr) {
            context_->txn_mgr_->record_select_predicate(tab_name_, fed_conds_, context_->txn_);
        }
        advance_to_valid();
    }

    void nextTuple() override {
        if (cursor_ >= visible_records_.size()) {
            return;
        }
        cursor_++;
        advance_to_valid();
    }

    std::unique_ptr<RmRecord> Next() override {
        if (cursor_ >= visible_records_.size()) {
            return nullptr;
        }
        if (context_->is_select_ && context_->txn_mgr_ != nullptr) {
            context_->txn_mgr_->record_select_record(tab_name_, rid_, context_->txn_);
        }
        return std::make_unique<RmRecord>(visible_records_[cursor_].second);
    }

    bool is_end() const override { return cursor_ >= visible_records_.size(); }

    size_t tupleLen() const override { return len_; }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    ColMeta get_col_offset(const TabCol &target) override { return *get_col(cols_, target); }

    Rid &rid() override { return rid_; }

    std::vector<std::string> tables() const override { return {logical_tab_name_}; }

    bool set_join_key(const std::vector<Condition> &join_conds, const std::vector<ColMeta> &outer_cols,
                      const RmRecord *outer_rec) override {
        runtime_conds_.clear();
        bool matched = false;
        for (const auto &cond : join_conds) {
            Condition runtime_cond;
            if (make_runtime_condition(cond, outer_cols, outer_rec, runtime_cond)) {
                runtime_conds_.push_back(std::move(runtime_cond));
                matched = true;
            }
        }
        return matched;
    }

    std::string explain(int depth = 0) const override {
        std::ostringstream os;
        os << indent(depth) << "Scan(table=" << tab_name_ << ", type=IndexScan, using_index=(";
        for (size_t i = 0; i < index_col_names_.size(); ++i) {
            if (i > 0) {
                os << ", ";
            }
            os << index_col_names_[i];
        }
        os << "), rows=" << rows_ << ")\n";
        return os.str();
    }
};
