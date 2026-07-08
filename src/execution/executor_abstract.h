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
#include "common/common.h"
#include "index/ix.h"
#include "system/sm.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <sstream>

class AbstractExecutor {
   public:
    Rid _abstract_rid;

    Context *context_;
    size_t rows_ = 0;

    virtual ~AbstractExecutor() = default;

    virtual size_t tupleLen() const { return 0; };

    virtual const std::vector<ColMeta> &cols() const {
        std::vector<ColMeta> *_cols = nullptr;
        return *_cols;
    };

    virtual std::string getType() { return "AbstractExecutor"; };

    virtual std::string explain(int depth = 0) const { return ""; }

    virtual std::vector<std::string> tables() const { return {}; }

    virtual void bind_outer_tuple(const std::vector<Condition> &, const std::vector<ColMeta> &, const RmRecord *) {}

    virtual std::string format_record_value(const ColMeta &col, const char *rec_buf) const {
        if (col.type == TYPE_INT) {
            return std::to_string(*(int *)rec_buf);
        }
        if (col.type == TYPE_FLOAT) {
            return std::to_string(*(float *)rec_buf);
        }
        std::string col_str((char *)rec_buf, col.len);
        col_str.resize(strlen(col_str.c_str()));
        return col_str;
    }

    size_t rows() const { return rows_; }

    virtual void beginTuple(){};

    virtual void nextTuple(){};

    virtual bool is_end() const { return true; };

    virtual Rid &rid() = 0;

    virtual std::unique_ptr<RmRecord> Next() = 0;

    virtual ColMeta get_col_offset(const TabCol &target) { return ColMeta();};

    static std::string indent(int depth) {
        return std::string(depth, '\t');
    }

    static std::string display_table(const std::map<std::string, std::string> &display_names,
                                     const std::string &tab_name) {
        auto it = display_names.find(tab_name);
        return it == display_names.end() ? tab_name : it->second;
    }

    static std::string format_col(const TabCol &col, const std::map<std::string, std::string> &display_names) {
        std::string tab_name = display_table(display_names, col.tab_name);
        return tab_name.empty() ? col.col_name : tab_name + "." + col.col_name;
    }

    static std::string format_value(const Value &val) {
        if (!val.display_val.empty()) {
            return val.display_val;
        }
        std::ostringstream os;
        if (val.type == TYPE_INT) {
            os << val.int_val;
        } else if (val.type == TYPE_FLOAT) {
            os << val.float_val;
        } else {
            os << "'" << val.str_val << "'";
        }
        return os.str();
    }

    static std::string op_to_string(CompOp op) {
        switch (op) {
            case OP_EQ: return "=";
            case OP_NE: return "<>";
            case OP_LT: return "<";
            case OP_GT: return ">";
            case OP_LE: return "<=";
            case OP_GE: return ">=";
        }
        return "";
    }

    static std::string format_condition(const Condition &cond,
                                        const std::map<std::string, std::string> &display_names) {
        std::string rhs = cond.is_rhs_val ? format_value(cond.rhs_val) : format_col(cond.rhs_col, display_names);
        return format_col(cond.lhs_col, display_names) + op_to_string(cond.op) + rhs;
    }

    static std::string join_strings(std::vector<std::string> vals, bool sort_vals = true) {
        if (sort_vals) {
            std::sort(vals.begin(), vals.end());
        }
        std::ostringstream os;
        for (size_t i = 0; i < vals.size(); ++i) {
            if (i > 0) {
                os << ", ";
            }
            os << vals[i];
        }
        return os.str();
    }

    static std::vector<std::string> sorted_condition_strings(
        const std::vector<Condition> &conds, const std::map<std::string, std::string> &display_names) {
        std::vector<std::string> cond_strs;
        cond_strs.reserve(conds.size());
        for (const auto &cond : conds) {
            cond_strs.push_back(format_condition(cond, display_names));
        }
        std::sort(cond_strs.begin(), cond_strs.end());
        return cond_strs;
    }

    std::vector<ColMeta>::const_iterator get_col(const std::vector<ColMeta> &rec_cols, const TabCol &target) {
        auto pos = std::find_if(rec_cols.begin(), rec_cols.end(), [&](const ColMeta &col) {
            return col.tab_name == target.tab_name && col.name == target.col_name;
        });
        if (pos == rec_cols.end()) {
            throw ColumnNotFoundError(target.tab_name + '.' + target.col_name);
        }
        return pos;
    }

    bool eval_cond(const std::vector<ColMeta> &rec_cols, const Condition &cond, const RmRecord *rec) {
        auto lhs_col = get_col(rec_cols, cond.lhs_col);
        const char *lhs = rec->data + lhs_col->offset;
        const char *rhs = nullptr;
        std::unique_ptr<RmRecord> rhs_raw;
        if (cond.is_rhs_val) {
            rhs_raw = std::make_unique<RmRecord>(lhs_col->len);
            if (cond.rhs_val.type == TYPE_INT) {
                *(int *)rhs_raw->data = cond.rhs_val.int_val;
            } else if (cond.rhs_val.type == TYPE_FLOAT) {
                *(float *)rhs_raw->data = cond.rhs_val.float_val;
            } else {
                memset(rhs_raw->data, 0, lhs_col->len);
                memcpy(rhs_raw->data, cond.rhs_val.str_val.c_str(), cond.rhs_val.str_val.size());
            }
            rhs = rhs_raw->data;
        } else {
            auto rhs_col = get_col(rec_cols, cond.rhs_col);
            rhs = rec->data + rhs_col->offset;
        }

        int cmp = 0;
        if (lhs_col->type == TYPE_INT) {
            int lhs_val = *(int *)lhs;
            int rhs_val = *(int *)rhs;
            cmp = (lhs_val > rhs_val) - (lhs_val < rhs_val);
        } else if (lhs_col->type == TYPE_FLOAT) {
            float lhs_val = *(float *)lhs;
            float rhs_val = *(float *)rhs;
            cmp = (lhs_val > rhs_val) - (lhs_val < rhs_val);
        } else {
            cmp = strncmp(lhs, rhs, lhs_col->len);
        }

        switch (cond.op) {
            case OP_EQ: return cmp == 0;
            case OP_NE: return cmp != 0;
            case OP_LT: return cmp < 0;
            case OP_GT: return cmp > 0;
            case OP_LE: return cmp <= 0;
            case OP_GE: return cmp >= 0;
        }
        return false;
    }

    bool eval_conds(const std::vector<ColMeta> &rec_cols, const std::vector<Condition> &conds, const RmRecord *rec) {
        for (const auto &cond : conds) {
            if (!eval_cond(rec_cols, cond, rec)) {
                return false;
            }
        }
        return true;
    }
};
