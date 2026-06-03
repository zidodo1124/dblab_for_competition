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

#include <cstring>

class AbstractExecutor {
   public:
    Rid _abstract_rid;

    Context *context_;

    virtual ~AbstractExecutor() = default;

    virtual size_t tupleLen() const { return 0; };

    virtual const std::vector<ColMeta> &cols() const {
        std::vector<ColMeta> *_cols = nullptr;
        return *_cols;
    };

    virtual std::string getType() { return "AbstractExecutor"; };

    virtual void beginTuple(){};

    virtual void nextTuple(){};

    virtual bool is_end() const { return true; };

    virtual Rid &rid() = 0;

    virtual std::unique_ptr<RmRecord> Next() = 0;

    virtual ColMeta get_col_offset(const TabCol &target) { return ColMeta();};

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
