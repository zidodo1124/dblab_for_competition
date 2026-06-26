/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#pragma once

#include <memory>

#include "executor_abstract.h"

class LimitExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> prev_;
    int limit_;
    int emitted_;

   public:
    LimitExecutor(std::unique_ptr<AbstractExecutor> prev, int limit) {
        prev_ = std::move(prev);
        limit_ = limit;
        emitted_ = 0;
    }

    void beginTuple() override {
        emitted_ = 0;
        rows_ = 0;
        prev_->beginTuple();
        if (!is_end()) {
            rows_++;
        }
    }

    void nextTuple() override {
        if (emitted_ < limit_) {
            emitted_++;
        }
        if (emitted_ < limit_) {
            prev_->nextTuple();
        }
        if (!is_end()) {
            rows_++;
        }
    }

    bool is_end() const override { return emitted_ >= limit_ || prev_->is_end(); }

    std::unique_ptr<RmRecord> Next() override { return prev_->Next(); }

    size_t tupleLen() const override { return prev_->tupleLen(); }

    const std::vector<ColMeta> &cols() const override { return prev_->cols(); }

    ColMeta get_col_offset(const TabCol &target) override { return prev_->get_col_offset(target); }

    Rid &rid() override { return _abstract_rid; }

    std::string explain(int depth = 0) const override { return prev_->explain(depth); }
};
