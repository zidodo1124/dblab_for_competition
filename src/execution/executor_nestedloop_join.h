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

class NestedLoopJoinExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> left_;    // 左儿子节点（需要join的表）
    std::unique_ptr<AbstractExecutor> right_;   // 右儿子节点（需要join的表）
    size_t len_;                                // join后获得的每条记录的长度
    std::vector<ColMeta> cols_;                 // join后获得的记录的字段

    std::vector<Condition> fed_conds_;          // join条件
    std::map<std::string, std::string> tab_display_names_;
    bool isend;

    std::unique_ptr<RmRecord> join_current_tuple() {
        auto left_rec = left_->Next();
        auto right_rec = right_->Next();
        auto rec = std::make_unique<RmRecord>(len_);
        memcpy(rec->data, left_rec->data, left_->tupleLen());
        memcpy(rec->data + left_->tupleLen(), right_rec->data, right_->tupleLen());
        return rec;
    }

    void advance_to_match() {
        while (!left_->is_end()) {
            while (!right_->is_end()) {
                auto rec = join_current_tuple();
                if (eval_conds(cols_, fed_conds_, rec.get())) {
                    isend = false;
                    count_output_row();
                    return;
                }
                right_->nextTuple();
            }
            left_->nextTuple();
            if (!left_->is_end()) {
                right_->beginTuple();
            }
        }
        isend = true;
    }

   public:
    NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left, std::unique_ptr<AbstractExecutor> right,
                            std::vector<Condition> conds,
                            std::map<std::string, std::string> tab_display_names = {}) {
        left_ = std::move(left);
        right_ = std::move(right);
        len_ = left_->tupleLen() + right_->tupleLen();
        cols_ = left_->cols();
        auto right_cols = right_->cols();
        for (auto &col : right_cols) {
            col.offset += left_->tupleLen();
        }

        cols_.insert(cols_.end(), right_cols.begin(), right_cols.end());
        isend = false;
        fed_conds_ = std::move(conds);
        tab_display_names_ = std::move(tab_display_names);

    }

    void beginTuple() override {
        begin_count_run();
        left_->beginTuple();
        if (left_->is_end()) {
            isend = true;
            return;
        }
        right_->beginTuple();
        if (right_->is_end()) {
            isend = true;
            return;
        }
        advance_to_match();
    }

    void nextTuple() override {
        if (isend) {
            return;
        }
        right_->nextTuple();
        advance_to_match();
    }

    std::unique_ptr<RmRecord> Next() override {
        return join_current_tuple();
    }

    bool is_end() const override { return isend; }

    size_t tupleLen() const override { return len_; }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    ColMeta get_col_offset(const TabCol &target) override { return *get_col(cols_, target); }

    Rid &rid() override { return _abstract_rid; }

    std::vector<std::string> tables() const override {
        auto left_tables = left_->tables();
        auto right_tables = right_->tables();
        left_tables.insert(left_tables.end(), right_tables.begin(), right_tables.end());
        std::sort(left_tables.begin(), left_tables.end());
        left_tables.erase(std::unique(left_tables.begin(), left_tables.end()), left_tables.end());
        return left_tables;
    }

    std::string explain(int depth = 0) const override {
        auto cond_strs = sorted_condition_strings(fed_conds_, tab_display_names_);
        std::ostringstream os;
        os << indent(depth) << "Join(tables=[" << join_strings(tables())
           << "], condition=[" << join_strings(cond_strs, false) << "], rows=" << rows_ << ")\n";
        os << left_->explain(depth + 1);
        os << right_->explain(depth + 1);
        return os.str();
    }
};
