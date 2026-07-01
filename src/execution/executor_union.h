/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#pragma once

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "executor_abstract.h"

class UnionExecutor : public AbstractExecutor {
   private:
    std::vector<std::unique_ptr<AbstractExecutor>> children_;
    std::vector<OrderByItem> order_bys_;
    bool has_limit_;
    int limit_;
    std::string alias_;

    std::vector<ColMeta> cols_;
    size_t len_ = 0;
    std::vector<std::unique_ptr<RmRecord>> rows_buf_;
    size_t cursor_ = 0;

    static bool is_numeric(ColType type) { return type == TYPE_INT || type == TYPE_FLOAT; }

    static bool compatible(ColType lhs, ColType rhs) {
        if (lhs == rhs) {
            return true;
        }
        return (is_numeric(lhs) && is_numeric(rhs)) || (lhs == TYPE_STRING && rhs == TYPE_STRING);
    }

    static ColType common_type(ColType lhs, ColType rhs) {
        if (lhs == TYPE_STRING || rhs == TYPE_STRING) {
            return TYPE_STRING;
        }
        if (lhs == TYPE_FLOAT || rhs == TYPE_FLOAT) {
            return TYPE_FLOAT;
        }
        return TYPE_INT;
    }

    static int common_len(ColType type, int lhs_len, int rhs_len) {
        if (type == TYPE_STRING) {
            return std::max(lhs_len, rhs_len);
        }
        if (type == TYPE_FLOAT) {
            return sizeof(float);
        }
        return sizeof(int);
    }

    void build_output_schema() {
        if (children_.size() < 2) {
            throw RMDBError("failure");
        }
        const auto &first_cols = children_[0]->cols();
        cols_ = first_cols;
        for (auto &col : cols_) {
            col.tab_name = alias_;
        }

        for (size_t child_idx = 1; child_idx < children_.size(); ++child_idx) {
            const auto &child_cols = children_[child_idx]->cols();
            if (child_cols.size() != cols_.size()) {
                throw RMDBError("failure");
            }
            for (size_t col_idx = 0; col_idx < cols_.size(); ++col_idx) {
                if (!compatible(cols_[col_idx].type, child_cols[col_idx].type)) {
                    throw RMDBError("failure");
                }
                ColType promoted_type = common_type(cols_[col_idx].type, child_cols[col_idx].type);
                cols_[col_idx].type = promoted_type;
                cols_[col_idx].len = common_len(promoted_type, cols_[col_idx].len, child_cols[col_idx].len);
            }
        }

        len_ = 0;
        for (auto &col : cols_) {
            col.offset = len_;
            len_ += col.len;
        }
    }

    void validate_order_bys() {
        for (auto &order : order_bys_) {
            auto col = get_col_offset(order.col);
            order.col = {.tab_name = col.tab_name, .col_name = col.name};
        }
    }

    std::unique_ptr<RmRecord> promote_record(const RmRecord *src, const std::vector<ColMeta> &src_cols) const {
        auto out = std::make_unique<RmRecord>(len_);
        for (size_t i = 0; i < cols_.size(); ++i) {
            const auto &src_col = src_cols[i];
            const auto &dst_col = cols_[i];
            const char *src_data = src->data + src_col.offset;
            char *dst_data = out->data + dst_col.offset;
            if (dst_col.type == TYPE_STRING) {
                memset(dst_data, 0, dst_col.len);
                memcpy(dst_data, src_data, std::min(src_col.len, dst_col.len));
            } else if (dst_col.type == TYPE_FLOAT) {
                float val = src_col.type == TYPE_FLOAT ? *reinterpret_cast<const float *>(src_data)
                                                       : static_cast<float>(*reinterpret_cast<const int *>(src_data));
                *reinterpret_cast<float *>(dst_data) = val;
            } else {
                *reinterpret_cast<int *>(dst_data) = *reinterpret_cast<const int *>(src_data);
            }
        }
        return out;
    }

    int compare_record_col(const RmRecord *lhs, const RmRecord *rhs, const ColMeta &col) const {
        const char *l = lhs->data + col.offset;
        const char *r = rhs->data + col.offset;
        if (col.type == TYPE_INT) {
            int lv = *reinterpret_cast<const int *>(l);
            int rv = *reinterpret_cast<const int *>(r);
            return (lv > rv) - (lv < rv);
        }
        if (col.type == TYPE_FLOAT) {
            float lv = *reinterpret_cast<const float *>(l);
            float rv = *reinterpret_cast<const float *>(r);
            return (lv > rv) - (lv < rv);
        }
        return strncmp(l, r, col.len);
    }

    void sort_and_limit_rows() {
        if (!order_bys_.empty()) {
            std::stable_sort(rows_buf_.begin(), rows_buf_.end(),
                             [&](const std::unique_ptr<RmRecord> &lhs, const std::unique_ptr<RmRecord> &rhs) {
                                 for (const auto &order : order_bys_) {
                                     ColMeta col = get_col_offset(order.col);
                                     int cmp = compare_record_col(lhs.get(), rhs.get(), col);
                                     if (cmp != 0) {
                                         return order.is_desc ? cmp > 0 : cmp < 0;
                                     }
                                 }
                                 return false;
                             });
        }
        if (has_limit_ && limit_ >= 0 && static_cast<size_t>(limit_) < rows_buf_.size()) {
            rows_buf_.resize(limit_);
        }
    }

   public:
    UnionExecutor(std::vector<std::unique_ptr<AbstractExecutor>> children, std::vector<OrderByItem> order_bys,
                  bool has_limit, int limit, std::string alias) {
        children_ = std::move(children);
        order_bys_ = std::move(order_bys);
        has_limit_ = has_limit;
        limit_ = limit;
        alias_ = std::move(alias);
        build_output_schema();
        validate_order_bys();
    }

    void beginTuple() override {
        rows_buf_.clear();
        cursor_ = 0;
        rows_ = 0;
        std::unordered_set<std::string> seen;

        for (auto &child : children_) {
            const auto &child_cols = child->cols();
            for (child->beginTuple(); !child->is_end(); child->nextTuple()) {
                auto tuple = child->Next();
                auto promoted = promote_record(tuple.get(), child_cols);
                std::string key(promoted->data, promoted->size);
                if (seen.insert(std::move(key)).second) {
                    rows_buf_.push_back(std::move(promoted));
                }
            }
        }

        sort_and_limit_rows();
        rows_ = rows_buf_.size();
    }

    void nextTuple() override {
        if (cursor_ < rows_buf_.size()) {
            ++cursor_;
        }
    }

    bool is_end() const override { return cursor_ >= rows_buf_.size(); }

    std::unique_ptr<RmRecord> Next() override { return std::make_unique<RmRecord>(*rows_buf_[cursor_]); }

    size_t tupleLen() const override { return len_; }

    const std::vector<ColMeta> &cols() const override { return cols_; }

    ColMeta get_col_offset(const TabCol &target) override {
        auto pos = std::find_if(cols_.begin(), cols_.end(), [&](const ColMeta &col) {
            if (!target.tab_name.empty() && target.tab_name != col.tab_name) {
                return false;
            }
            return col.name == target.col_name;
        });
        if (pos == cols_.end()) {
            throw ColumnNotFoundError(target.col_name);
        }
        return *pos;
    }

    Rid &rid() override { return _abstract_rid; }
};
