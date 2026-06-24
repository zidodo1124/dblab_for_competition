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

class ProjectionExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> prev_;        // 投影节点的儿子节点
    std::vector<ColMeta> cols_;                     // 需要投影的字段
    size_t len_;                                    // 字段总长度
    std::vector<size_t> sel_idxs_;
    std::vector<TabCol> sel_cols_;
    bool display_all_;
    std::map<std::string, std::string> tab_display_names_;

   public:
    ProjectionExecutor(std::unique_ptr<AbstractExecutor> prev, const std::vector<TabCol> &sel_cols,
                       bool display_all = false,
                       std::map<std::string, std::string> tab_display_names = {}) {
        prev_ = std::move(prev);
        sel_cols_ = sel_cols;
        display_all_ = display_all;
        tab_display_names_ = std::move(tab_display_names);

        size_t curr_offset = 0;
        auto &prev_cols = prev_->cols();
        for (auto &sel_col : sel_cols) {
            auto pos = get_col(prev_cols, sel_col);
            sel_idxs_.push_back(pos - prev_cols.begin());
            auto col = *pos;
            col.offset = curr_offset;
            curr_offset += col.len;
            cols_.push_back(col);
        }
        len_ = curr_offset;
    }

    void beginTuple() override {
        begin_count_run();
        prev_->beginTuple();
        if (!prev_->is_end()) {
            count_output_row();
        }
    }

    void nextTuple() override {
        prev_->nextTuple();
        if (!prev_->is_end()) {
            count_output_row();
        }
    }

    std::unique_ptr<RmRecord> Next() override {
        auto prev_rec = prev_->Next();
        auto rec = std::make_unique<RmRecord>(len_);
        auto &prev_cols = prev_->cols();
        for (size_t i = 0; i < sel_idxs_.size(); i++) {
            auto &src_col = prev_cols[sel_idxs_[i]];
            memcpy(rec->data + cols_[i].offset, prev_rec->data + src_col.offset, src_col.len);
        }
        return rec;
    }

    bool is_end() const override { return prev_->is_end(); }

    size_t tupleLen() const override { return len_; }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    ColMeta get_col_offset(const TabCol &target) override { return *get_col(cols_, target); }

    Rid &rid() override { return _abstract_rid; }

    std::vector<std::string> tables() const override { return prev_->tables(); }

    std::string explain(int depth = 0) const override {
        std::vector<std::string> col_strs;
        if (display_all_) {
            col_strs.push_back("*");
        } else {
            col_strs.reserve(sel_cols_.size());
            for (const auto &col : sel_cols_) {
                col_strs.push_back(format_col(col, tab_display_names_));
            }
        }
        std::ostringstream os;
        os << indent(depth) << "Project(columns=[" << join_strings(col_strs, !display_all_)
           << "], rows=" << rows_ << ")\n";
        os << prev_->explain(depth + 1);
        return os.str();
    }
};
