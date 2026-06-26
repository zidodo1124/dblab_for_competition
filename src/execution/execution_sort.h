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
#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class SortExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> prev_;
    std::vector<ColMeta> order_cols_;
    std::vector<bool> is_descs_;
    std::vector<std::unique_ptr<RmRecord>> tuples_;
    size_t cursor_;

    int compare_tuple(const RmRecord *lhs, const RmRecord *rhs, const ColMeta &cols) const {
        const char *lhs_data = lhs->data + cols.offset;
        const char *rhs_data = rhs->data + cols.offset;
        if (cols.type == TYPE_INT) {
            int lhs_val = *reinterpret_cast<const int *>(lhs_data);
            int rhs_val = *reinterpret_cast<const int *>(rhs_data);
            return (lhs_val > rhs_val) - (lhs_val < rhs_val);
        }
        if (cols.type == TYPE_FLOAT) {
            float lhs_val = *reinterpret_cast<const float *>(lhs_data);
            float rhs_val = *reinterpret_cast<const float *>(rhs_data);
            return (lhs_val > rhs_val) - (lhs_val < rhs_val);
        }
        return strncmp(lhs_data, rhs_data, cols.len);
    }

   public:
    SortExecutor(std::unique_ptr<AbstractExecutor> prev, TabCol sel_cols, bool is_desc) {
        prev_ = std::move(prev);
        order_cols_.push_back(prev_->get_col_offset(sel_cols));
        is_descs_.push_back(is_desc);
        cursor_ = 0;
    }

    SortExecutor(std::unique_ptr<AbstractExecutor> prev, const std::vector<OrderByItem> &order_bys) {
        prev_ = std::move(prev);
        for (const auto &order : order_bys) {
            order_cols_.push_back(prev_->get_col_offset(order.col));
            is_descs_.push_back(order.is_desc);
        }
        cursor_ = 0;
    }

    void beginTuple() override {
        tuples_.clear();
        cursor_ = 0;
        rows_ = 0;

        for (prev_->beginTuple(); !prev_->is_end(); prev_->nextTuple()) {
            tuples_.push_back(prev_->Next());
        }
        rows_ = tuples_.size();

        std::stable_sort(tuples_.begin(), tuples_.end(),
                         [&](const std::unique_ptr<RmRecord> &lhs, const std::unique_ptr<RmRecord> &rhs) {
                             for (size_t i = 0; i < order_cols_.size(); ++i) {
                                 int cmp = compare_tuple(lhs.get(), rhs.get(), order_cols_[i]);
                                 if (cmp != 0) {
                                     return is_descs_[i] ? cmp > 0 : cmp < 0;
                                 }
                             }
                             return false;
                         });
    }

    void nextTuple() override {
        if (cursor_ < tuples_.size()) {
            ++cursor_;
        }
    }

    std::unique_ptr<RmRecord> Next() override {
        return std::make_unique<RmRecord>(*tuples_[cursor_]);
    }

    bool is_end() const override { return cursor_ >= tuples_.size(); }

    size_t tupleLen() const override { return prev_->tupleLen(); }

    const std::vector<ColMeta> &cols() const override { return prev_->cols(); }

    ColMeta get_col_offset(const TabCol &target) override { return prev_->get_col_offset(target); }

    Rid &rid() override { return _abstract_rid; }

    std::string explain(int depth = 0) const override { return prev_->explain(depth); }
};
