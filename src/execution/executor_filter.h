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

#include "executor_abstract.h"

class FilterExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> prev_;
    std::vector<Condition> conds_;
    std::map<std::string, std::string> tab_display_names_;

    void advance_to_match() {
        while (!prev_->is_end()) {
            auto rec = prev_->Next();
            if (eval_conds(prev_->cols(), conds_, rec.get())) {
                rows_++;
                return;
            }
            prev_->nextTuple();
        }
    }

   public:
    FilterExecutor(std::unique_ptr<AbstractExecutor> prev, std::vector<Condition> conds,
                   std::map<std::string, std::string> tab_display_names)
        : prev_(std::move(prev)),
          conds_(std::move(conds)),
          tab_display_names_(std::move(tab_display_names)) {}

    void beginTuple() override {
        prev_->beginTuple();
        advance_to_match();
    }

    void nextTuple() override {
        if (prev_->is_end()) {
            return;
        }
        prev_->nextTuple();
        advance_to_match();
    }

    std::unique_ptr<RmRecord> Next() override { return prev_->Next(); }

    bool is_end() const override { return prev_->is_end(); }

    size_t tupleLen() const override { return prev_->tupleLen(); }

    const std::vector<ColMeta> &cols() const override { return prev_->cols(); }

    ColMeta get_col_offset(const TabCol &target) override { return prev_->get_col_offset(target); }

    Rid &rid() override { return prev_->rid(); }

    std::vector<std::string> tables() const override { return prev_->tables(); }

    std::string explain(int depth = 0) const override {
        auto cond_strs = sorted_condition_strings(conds_, tab_display_names_);
        std::ostringstream os;
        os << indent(depth) << "Filter(condition=[" << join_strings(cond_strs, false)
           << "], rows=" << rows_ << ")\n";
        os << prev_->explain(depth + 1);
        return os.str();
    }
};
