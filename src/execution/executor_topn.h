/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#pragma once

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include "executor_abstract.h"

class TopNExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> prev_;
    std::vector<ColMeta> order_cols_;
    std::vector<bool> is_descs_;
    size_t limit_;
    std::vector<std::unique_ptr<RmRecord>> tuples_;
    size_t cursor_ = 0;

    int compare_tuple(const RmRecord *lhs, const RmRecord *rhs, const ColMeta &col) const {
        const char *lhs_data = lhs->data + col.offset;
        const char *rhs_data = rhs->data + col.offset;
        if (col.type == TYPE_INT) {
            int lhs_val = *reinterpret_cast<const int *>(lhs_data);
            int rhs_val = *reinterpret_cast<const int *>(rhs_data);
            return (lhs_val > rhs_val) - (lhs_val < rhs_val);
        }
        if (col.type == TYPE_FLOAT) {
            float lhs_val = *reinterpret_cast<const float *>(lhs_data);
            float rhs_val = *reinterpret_cast<const float *>(rhs_data);
            return (lhs_val > rhs_val) - (lhs_val < rhs_val);
        }
        return strncmp(lhs_data, rhs_data, col.len);
    }

    bool comes_before(const RmRecord *lhs, const RmRecord *rhs) const {
        for (size_t i = 0; i < order_cols_.size(); ++i) {
            int cmp = compare_tuple(lhs, rhs, order_cols_[i]);
            if (cmp != 0) {
                return is_descs_[i] ? cmp > 0 : cmp < 0;
            }
        }
        return false;
    }

    void retain_candidate(std::unique_ptr<RmRecord> tuple) {
        if (limit_ == 0) {
            return;
        }
        if (tuples_.size() == limit_ && !comes_before(tuple.get(), tuples_.back().get())) {
            return;
        }
        auto pos = std::upper_bound(
            tuples_.begin(), tuples_.end(), tuple,
            [&](const std::unique_ptr<RmRecord> &value, const std::unique_ptr<RmRecord> &element) {
                return comes_before(value.get(), element.get());
            });
        tuples_.insert(pos, std::move(tuple));
        if (tuples_.size() > limit_) {
            tuples_.pop_back();
        }
    }

   public:
    TopNExecutor(std::unique_ptr<AbstractExecutor> prev, const std::vector<OrderByItem> &order_bys, int limit)
        : prev_(std::move(prev)), limit_(std::max(0, limit)) {
        for (const auto &order : order_bys) {
            order_cols_.push_back(prev_->get_col_offset(order.col));
            is_descs_.push_back(order.is_desc);
        }
    }

    void beginTuple() override {
        tuples_.clear();
        cursor_ = 0;
        rows_ = 0;
        for (prev_->beginTuple(); !prev_->is_end(); prev_->nextTuple()) {
            retain_candidate(prev_->Next());
        }
        rows_ = tuples_.size();
    }

    void nextTuple() override {
        if (cursor_ < tuples_.size()) {
            ++cursor_;
        }
    }

    std::unique_ptr<RmRecord> Next() override { return std::make_unique<RmRecord>(*tuples_[cursor_]); }

    bool is_end() const override { return cursor_ >= tuples_.size(); }

    size_t tupleLen() const override { return prev_->tupleLen(); }

    const std::vector<ColMeta> &cols() const override { return prev_->cols(); }

    ColMeta get_col_offset(const TabCol &target) override { return prev_->get_col_offset(target); }

    Rid &rid() override { return _abstract_rid; }

    std::string explain(int depth = 0) const override {
        std::ostringstream os;
        os << indent(depth) << "TopN(limit=" << limit_ << ", rows=" << rows_ << ")\n";
        os << prev_->explain(depth + 1);
        return os.str();
    }
};
