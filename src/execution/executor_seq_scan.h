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
#include "record/rm_scan.h"
#include "system/sm.h"

class SeqScanExecutor : public AbstractExecutor {
   private:
    std::string tab_name_;              // 表的名称
    std::string logical_tab_name_;
    std::string table_output_name_;
    std::vector<Condition> conds_;      // scan的条件
    RmFileHandle *fh_;                  // 表的数据文件句柄
    std::vector<ColMeta> cols_;         // scan后生成的记录的字段
    size_t len_;                        // scan后生成的每条记录的长度
    std::vector<Condition> fed_conds_;  // 同conds_，两个字段相同

    Rid rid_;
    std::unique_ptr<RmScan> scan_;
    std::unique_ptr<RmRecord> current_record_;
    std::vector<std::pair<Rid, RmRecord>> deleted_visible_records_;
    size_t deleted_cursor_ = 0;

    SmManager *sm_manager_;
    bool count_filtered_rows_;

    void advance_to_valid() {
        while (scan_ != nullptr && !scan_->is_end()) {
            rid_ = scan_->rid();
            auto physical_record = scan_->get_record();
            scan_->next();
            std::unique_ptr<RmRecord> rec =
                context_->txn_mgr_ == nullptr
                    ? std::move(physical_record)
                    : context_->txn_mgr_->get_visible_record(tab_name_, rid_, fh_, context_, std::move(physical_record));
            if (rec == nullptr) {
                continue;
            }
            if (!count_filtered_rows_) {
                rows_++;
            }
            if (eval_conds(cols_, fed_conds_, rec.get())) {
                if (count_filtered_rows_) {
                    rows_++;
                }
                current_record_ = std::move(rec);
                return;
            }
        }

        while (deleted_cursor_ < deleted_visible_records_.size()) {
            auto &entry = deleted_visible_records_[deleted_cursor_++];
            rid_ = entry.first;
            if (!count_filtered_rows_) {
                rows_++;
            }
            if (eval_conds(cols_, fed_conds_, &entry.second)) {
                if (count_filtered_rows_) {
                    rows_++;
                }
                current_record_ = std::make_unique<RmRecord>(entry.second);
                return;
            }
        }
        current_record_.reset();
    }

   public:
    SeqScanExecutor(SmManager *sm_manager, std::string tab_name, std::vector<Condition> conds, Context *context,
                    bool count_filtered_rows = false, std::string logical_tab_name = "",
                    std::string table_output_name = "") {
        sm_manager_ = sm_manager;
        tab_name_ = std::move(tab_name);
        logical_tab_name_ = logical_tab_name.empty() ? tab_name_ : std::move(logical_tab_name);
        table_output_name_ = table_output_name.empty() ? tab_name_ : std::move(table_output_name);
        conds_ = std::move(conds);
        TabMeta &tab = sm_manager_->db_.get_table(tab_name_);
        fh_ = sm_manager_->fhs_.at(tab_name_).get();
        cols_ = tab.cols;
        for (auto &col : cols_) {
            col.tab_name = logical_tab_name_;
        }
        len_ = cols_.back().offset + cols_.back().len;

        context_ = context;

        fed_conds_ = conds_;
        count_filtered_rows_ = count_filtered_rows;
    }

    void beginTuple() override {
        auto operation_guard = context_->txn_mgr_ != nullptr ? context_->txn_mgr_->acquire_operation_latch()
                                                             : std::shared_lock<std::shared_mutex>();
        auto table_guard = context_->txn_mgr_ != nullptr ? context_->txn_mgr_->acquire_table_read_latch(tab_name_)
                                                         : std::shared_lock<std::shared_mutex>();
        current_record_.reset();
        deleted_visible_records_.clear();
        deleted_cursor_ = 0;
        scan_ = std::make_unique<RmScan>(fh_);
        if (context_->txn_mgr_ != nullptr) {
            // RmScan cannot visit records deleted after this transaction's snapshot.
            // Keep only those exceptional, snapshot-visible versions in a small side list.
            for (auto &entry : context_->txn_mgr_->get_visible_version_records(tab_name_, context_)) {
                bool record_exists = true;
                try {
                    (void)fh_->get_record(entry.first, context_);
                } catch (const RecordNotFoundError &) {
                    record_exists = false;
                }
                if (!record_exists) {
                    deleted_visible_records_.push_back(std::move(entry));
                }
            }
        }
        if (context_->is_select_ && context_->txn_mgr_ != nullptr) {
            context_->txn_mgr_->record_select_predicate(tab_name_, fed_conds_, context_->txn_);
        }
        advance_to_valid();
    }

    void nextTuple() override {
        if (current_record_ != nullptr) {
            advance_to_valid();
        }
    }

    std::unique_ptr<RmRecord> Next() override {
        if (current_record_ == nullptr) {
            return nullptr;
        }
        if (context_->is_select_ && context_->txn_mgr_ != nullptr) {
            context_->txn_mgr_->record_select_record(tab_name_, rid_, context_->txn_);
        }
        return std::make_unique<RmRecord>(*current_record_);
    }

    bool is_end() const override { return current_record_ == nullptr; }

    size_t tupleLen() const override { return len_; }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    ColMeta get_col_offset(const TabCol &target) override { return *get_col(cols_, target); }

    Rid &rid() override { return rid_; }

    std::vector<std::string> tables() const override { return {table_output_name_}; }

    std::string explain(int depth = 0) const override {
        std::ostringstream os;
        os << indent(depth) << "Scan(table=" << tab_name_ << ", type=SeqScan, rows=" << rows_ << ")\n";
        return os.str();
    }
};
