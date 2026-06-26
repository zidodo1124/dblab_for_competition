/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2. */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "executor_abstract.h"

class AggregateExecutor : public AbstractExecutor {
   private:
    struct AggState {
        long long count = 0;
        double sum = 0;
        bool has_value = false;
        int int_val = 0;
        float float_val = 0;
    };

    struct GroupState {
        std::unique_ptr<RmRecord> sample;
        std::vector<AggState> aggs;
    };

    struct TermValue {
        ColType type = TYPE_INT;
        bool is_null = false;
        int int_val = 0;
        float float_val = 0;
        std::string str_val;
    };

    std::unique_ptr<AbstractExecutor> prev_;
    std::vector<SelectItem> select_items_;
    std::vector<TabCol> group_cols_;
    std::vector<HavingCondition> having_conds_;
    std::vector<OrderByItem> order_bys_;
    bool has_limit_;
    int limit_;

    std::vector<AggCall> agg_calls_;
    std::vector<ColType> agg_types_;
    std::vector<ColMeta> cols_;
    size_t len_ = 0;
    std::vector<std::unique_ptr<RmRecord>> rows_buf_;
    size_t cursor_ = 0;

    static std::string agg_key(const AggCall &call) {
        std::ostringstream os;
        os << call.type << "|" << call.is_star << "|" << call.col.tab_name << "|" << call.col.col_name;
        return os.str();
    }

    int ensure_agg_call(const AggCall &call) {
        std::string key = agg_key(call);
        for (size_t i = 0; i < agg_calls_.size(); ++i) {
            if (agg_key(agg_calls_[i]) == key) {
                return static_cast<int>(i);
            }
        }
        agg_calls_.push_back(call);
        agg_types_.push_back(agg_output_type(call));
        return static_cast<int>(agg_calls_.size() - 1);
    }

    int agg_index(const AggCall &call) const {
        std::string key = agg_key(call);
        for (size_t i = 0; i < agg_calls_.size(); ++i) {
            if (agg_key(agg_calls_[i]) == key) {
                return static_cast<int>(i);
            }
        }
        throw InternalError("Aggregate call not found");
    }

    ColType agg_output_type(const AggCall &call) const {
        if (call.type == AGG_COUNT) {
            return TYPE_INT;
        }
        if (call.type == AGG_AVG) {
            return TYPE_FLOAT;
        }
        auto col = prev_->get_col_offset(call.col);
        return col.type;
    }

    int agg_output_len(ColType type) const {
        if (type == TYPE_INT) {
            return sizeof(int);
        }
        if (type == TYPE_FLOAT) {
            return sizeof(float);
        }
        return 0;
    }

    void build_output_cols() {
        int offset = 0;
        for (const auto &item : select_items_) {
            ColMeta col;
            col.tab_name = "";
            col.name = item.output_name;
            col.index = false;
            col.offset = offset;
            if (item.is_agg) {
                ColType type = agg_output_type(item.agg);
                col.type = type;
                col.len = agg_output_len(type);
            } else {
                auto src_col = prev_->get_col_offset(item.col);
                col.type = src_col.type;
                col.len = src_col.len;
            }
            offset += col.len;
            cols_.push_back(col);
        }
        len_ = offset;
    }

    std::string group_key(const RmRecord *rec) {
        std::string key;
        for (const auto &group_col : group_cols_) {
            auto col = prev_->get_col_offset(group_col);
            key.append(rec->data + col.offset, col.len);
            key.push_back('\0');
        }
        return key;
    }

    void update_agg(AggState &state, const AggCall &call, const RmRecord *rec) {
        if (call.type == AGG_COUNT) {
            state.count++;
            return;
        }
        auto col = prev_->get_col_offset(call.col);
        double val = 0;
        if (col.type == TYPE_INT) {
            val = *reinterpret_cast<const int *>(rec->data + col.offset);
        } else if (col.type == TYPE_FLOAT) {
            val = *reinterpret_cast<const float *>(rec->data + col.offset);
        }

        if (call.type == AGG_SUM || call.type == AGG_AVG) {
            state.sum += val;
            state.count++;
            return;
        }

        if (!state.has_value ||
            (call.type == AGG_MAX && val > (col.type == TYPE_INT ? state.int_val : state.float_val)) ||
            (call.type == AGG_MIN && val < (col.type == TYPE_INT ? state.int_val : state.float_val))) {
            state.has_value = true;
            if (col.type == TYPE_INT) {
                state.int_val = static_cast<int>(val);
            } else {
                state.float_val = static_cast<float>(val);
            }
        }
    }

    TermValue agg_value(const AggCall &call, const AggState &state) const {
        TermValue val;
        ColType type = agg_types_[agg_index(call)];
        val.type = type;
        if (call.type == AGG_COUNT) {
            val.int_val = static_cast<int>(state.count);
        } else if (call.type == AGG_AVG) {
            if (state.count == 0) {
                val.is_null = true;
            } else {
                val.float_val = static_cast<float>(state.sum / state.count);
            }
        } else if (call.type == AGG_SUM) {
            if (state.count == 0) {
                val.is_null = true;
                return val;
            }
            if (type == TYPE_INT) {
                val.int_val = static_cast<int>(state.sum);
            } else {
                val.float_val = static_cast<float>(state.sum);
            }
        } else {
            if (!state.has_value) {
                val.is_null = true;
                return val;
            }
            if (type == TYPE_INT) {
                val.int_val = state.int_val;
            } else {
                val.float_val = state.float_val;
            }
        }
        return val;
    }

    TermValue col_value(const TabCol &target, const RmRecord *rec) const {
        TermValue val;
        auto col = prev_->get_col_offset(target);
        val.type = col.type;
        if (col.type == TYPE_INT) {
            val.int_val = *reinterpret_cast<const int *>(rec->data + col.offset);
        } else if (col.type == TYPE_FLOAT) {
            val.float_val = *reinterpret_cast<const float *>(rec->data + col.offset);
        } else {
            val.str_val.assign(rec->data + col.offset, col.len);
            val.str_val.resize(strlen(val.str_val.c_str()));
        }
        return val;
    }

    TermValue eval_term(const HavingTerm &term, const GroupState &group) const {
        if (term.is_agg) {
            int idx = agg_index(term.agg);
            return agg_value(term.agg, group.aggs[idx]);
        }
        if (term.is_col) {
            return col_value(term.col, group.sample.get());
        }
        TermValue val;
        val.type = term.val.type;
        val.int_val = term.val.int_val;
        val.float_val = term.val.float_val;
        val.str_val = term.val.str_val;
        return val;
    }

    bool compare_terms(TermValue lhs, CompOp op, TermValue rhs) const {
        if (lhs.is_null || rhs.is_null) {
            return false;
        }
        int cmp = 0;
        if (lhs.type == TYPE_FLOAT || rhs.type == TYPE_FLOAT) {
            float l = lhs.type == TYPE_FLOAT ? lhs.float_val : static_cast<float>(lhs.int_val);
            float r = rhs.type == TYPE_FLOAT ? rhs.float_val : static_cast<float>(rhs.int_val);
            cmp = (l > r) - (l < r);
        } else if (lhs.type == TYPE_INT && rhs.type == TYPE_INT) {
            cmp = (lhs.int_val > rhs.int_val) - (lhs.int_val < rhs.int_val);
        } else {
            cmp = lhs.str_val.compare(rhs.str_val);
        }

        switch (op) {
            case OP_EQ: return cmp == 0;
            case OP_NE: return cmp != 0;
            case OP_LT: return cmp < 0;
            case OP_GT: return cmp > 0;
            case OP_LE: return cmp <= 0;
            case OP_GE: return cmp >= 0;
        }
        return false;
    }

    bool pass_having(const GroupState &group) const {
        for (const auto &cond : having_conds_) {
            if (!compare_terms(eval_term(cond.lhs, group), cond.op, eval_term(cond.rhs, group))) {
                return false;
            }
        }
        return true;
    }

    void write_term(char *dest, ColType type, const TermValue &val) const {
        if (val.is_null) {
            if (type == TYPE_INT) {
                *reinterpret_cast<int *>(dest) = std::numeric_limits<int>::min();
            } else if (type == TYPE_FLOAT) {
                *reinterpret_cast<float *>(dest) = std::numeric_limits<float>::quiet_NaN();
            }
            return;
        }
        if (type == TYPE_INT) {
            int out = val.type == TYPE_FLOAT ? static_cast<int>(val.float_val) : val.int_val;
            *reinterpret_cast<int *>(dest) = out;
        } else if (type == TYPE_FLOAT) {
            float out = val.type == TYPE_FLOAT ? val.float_val : static_cast<float>(val.int_val);
            *reinterpret_cast<float *>(dest) = out;
        }
    }

    std::unique_ptr<RmRecord> make_output_record(const GroupState &group) const {
        auto rec = std::make_unique<RmRecord>(len_);
        for (size_t i = 0; i < select_items_.size(); ++i) {
            const auto &item = select_items_[i];
            const auto &out_col = cols_[i];
            char *dest = rec->data + out_col.offset;
            if (item.is_agg) {
                int idx = agg_index(item.agg);
                write_term(dest, out_col.type, agg_value(item.agg, group.aggs[idx]));
            } else {
                auto src_col = prev_->get_col_offset(item.col);
                memcpy(dest, group.sample->data + src_col.offset, src_col.len);
            }
        }
        return rec;
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
    AggregateExecutor(std::unique_ptr<AbstractExecutor> prev, std::vector<SelectItem> select_items,
                      std::vector<TabCol> group_cols, std::vector<HavingCondition> having_conds,
                      std::vector<OrderByItem> order_bys, bool has_limit, int limit) {
        prev_ = std::move(prev);
        select_items_ = std::move(select_items);
        group_cols_ = std::move(group_cols);
        having_conds_ = std::move(having_conds);
        order_bys_ = std::move(order_bys);
        has_limit_ = has_limit;
        limit_ = limit;

        for (const auto &item : select_items_) {
            if (item.is_agg) {
                ensure_agg_call(item.agg);
            }
        }
        for (const auto &cond : having_conds_) {
            if (cond.lhs.is_agg) {
                ensure_agg_call(cond.lhs.agg);
            }
            if (cond.rhs.is_agg) {
                ensure_agg_call(cond.rhs.agg);
            }
        }
        build_output_cols();
    }

    void beginTuple() override {
        rows_buf_.clear();
        cursor_ = 0;
        rows_ = 0;

        std::map<std::string, size_t> group_pos;
        std::vector<GroupState> groups;

        for (prev_->beginTuple(); !prev_->is_end(); prev_->nextTuple()) {
            auto tuple = prev_->Next();
            std::string key = group_cols_.empty() ? std::string("__all__") : group_key(tuple.get());
            auto it = group_pos.find(key);
            if (it == group_pos.end()) {
                GroupState group;
                group.sample = std::make_unique<RmRecord>(*tuple);
                group.aggs.resize(agg_calls_.size());
                groups.push_back(std::move(group));
                group_pos[key] = groups.size() - 1;
                it = group_pos.find(key);
            }
            auto &group = groups[it->second];
            for (size_t i = 0; i < agg_calls_.size(); ++i) {
                update_agg(group.aggs[i], agg_calls_[i], tuple.get());
            }
        }

        if (groups.empty() && group_cols_.empty()) {
            GroupState group;
            group.sample = std::make_unique<RmRecord>(prev_->tupleLen());
            group.aggs.resize(agg_calls_.size());
            groups.push_back(std::move(group));
        }

        for (const auto &group : groups) {
            if (pass_having(group)) {
                rows_buf_.push_back(make_output_record(group));
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
            if (!target.tab_name.empty() && col.tab_name != target.tab_name) {
                return false;
            }
            return col.name == target.col_name;
        });
        if (pos == cols_.end()) {
            throw ColumnNotFoundError(target.col_name);
        }
        return *pos;
    }

    std::string format_record_value(const ColMeta &col, const char *rec_buf) const override {
        if (col.type == TYPE_INT) {
            int val = *reinterpret_cast<const int *>(rec_buf);
            if (val == std::numeric_limits<int>::min()) {
                return "NULL";
            }
            return std::to_string(val);
        }
        if (col.type == TYPE_FLOAT) {
            float val = *reinterpret_cast<const float *>(rec_buf);
            if (std::isnan(val)) {
                return "NULL";
            }
            return std::to_string(val);
        }
        return AbstractExecutor::format_record_value(col, rec_buf);
    }

    Rid &rid() override { return _abstract_rid; }
};
