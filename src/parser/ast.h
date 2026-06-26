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

#include <vector>
#include <string>
#include <memory>
#include <map>

enum JoinType {
    INNER_JOIN, LEFT_JOIN, RIGHT_JOIN, FULL_JOIN
};
namespace ast {

enum SvType {
    SV_TYPE_INT, SV_TYPE_FLOAT, SV_TYPE_STRING, SV_TYPE_BOOL
};

enum SvCompOp {
    SV_OP_EQ, SV_OP_NE, SV_OP_LT, SV_OP_GT, SV_OP_LE, SV_OP_GE
};

enum OrderByDir {
    OrderBy_DEFAULT,
    OrderBy_ASC,
    OrderBy_DESC
};

enum AggType {
    AGG_COUNT,
    AGG_MAX,
    AGG_MIN,
    AGG_SUM,
    AGG_AVG
};

enum SetKnobType {
    EnableNestLoop, EnableSortMerge
};

// Base class for tree nodes
struct TreeNode {
    virtual ~TreeNode() = default;  // enable polymorphism
};

struct Help : public TreeNode {
};

struct ShowTables : public TreeNode {
};

struct ShowIndex : public TreeNode {
    std::string tab_name;

    ShowIndex(std::string tab_name_) : tab_name(std::move(tab_name_)) {}
};

struct TxnBegin : public TreeNode {
};

struct TxnCommit : public TreeNode {
};

struct TxnAbort : public TreeNode {
};

struct TxnRollback : public TreeNode {
};

struct TypeLen : public TreeNode {
    SvType type;
    int len;

    TypeLen(SvType type_, int len_) : type(type_), len(len_) {}
};

struct Field : public TreeNode {
};

struct ColDef : public Field {
    std::string col_name;
    std::shared_ptr<TypeLen> type_len;

    ColDef(std::string col_name_, std::shared_ptr<TypeLen> type_len_) :
            col_name(std::move(col_name_)), type_len(std::move(type_len_)) {}
};

struct CreateTable : public TreeNode {
    std::string tab_name;
    std::vector<std::shared_ptr<Field>> fields;

    CreateTable(std::string tab_name_, std::vector<std::shared_ptr<Field>> fields_) :
            tab_name(std::move(tab_name_)), fields(std::move(fields_)) {}
};

struct DropTable : public TreeNode {
    std::string tab_name;

    DropTable(std::string tab_name_) : tab_name(std::move(tab_name_)) {}
};

struct DescTable : public TreeNode {
    std::string tab_name;

    DescTable(std::string tab_name_) : tab_name(std::move(tab_name_)) {}
};

struct CreateIndex : public TreeNode {
    std::string tab_name;
    std::vector<std::string> col_names;

    CreateIndex(std::string tab_name_, std::vector<std::string> col_names_) :
            tab_name(std::move(tab_name_)), col_names(std::move(col_names_)) {}
};

struct DropIndex : public TreeNode {
    std::string tab_name;
    std::vector<std::string> col_names;

    DropIndex(std::string tab_name_, std::vector<std::string> col_names_) :
            tab_name(std::move(tab_name_)), col_names(std::move(col_names_)) {}
};

struct Expr : public TreeNode {
};

struct Value : public Expr {
};

struct IntLit : public Value {
    int val;

    IntLit(int val_) : val(val_) {}
};

struct FloatLit : public Value {
    float val;

    FloatLit(float val_) : val(val_) {}
};

struct StringLit : public Value {
    std::string val;

    StringLit(std::string val_) : val(std::move(val_)) {}
};

struct BoolLit : public Value {
    bool val;

    BoolLit(bool val_) : val(val_) {}
};

struct Col : public Expr {
    std::string tab_name;
    std::string col_name;

    Col(std::string tab_name_, std::string col_name_) :
            tab_name(std::move(tab_name_)), col_name(std::move(col_name_)) {}
};

struct AggFunc : public Expr {
    AggType type;
    std::shared_ptr<Col> col;
    bool is_star;

    AggFunc(AggType type_, std::shared_ptr<Col> col_, bool is_star_) :
            type(type_), col(std::move(col_)), is_star(is_star_) {}
};

struct SelectItem : public TreeNode {
    bool is_agg;
    std::shared_ptr<Col> col;
    std::shared_ptr<AggFunc> agg;
    std::string alias;

    SelectItem(std::shared_ptr<Col> col_, std::string alias_) :
            is_agg(false), col(std::move(col_)), alias(std::move(alias_)) {}

    SelectItem(std::shared_ptr<AggFunc> agg_, std::string alias_) :
            is_agg(true), agg(std::move(agg_)), alias(std::move(alias_)) {}
};

struct SetClause : public TreeNode {
    std::string col_name;
    std::shared_ptr<Value> val;
    bool is_self_add = false;
    std::string rhs_col_name;

    SetClause(std::string col_name_, std::shared_ptr<Value> val_) :
            col_name(std::move(col_name_)), val(std::move(val_)) {}

    SetClause(std::string col_name_, std::string rhs_col_name_, std::shared_ptr<Value> val_) :
            col_name(std::move(col_name_)), val(std::move(val_)), is_self_add(true),
            rhs_col_name(std::move(rhs_col_name_)) {}
};

struct BinaryExpr : public TreeNode {
    std::shared_ptr<Col> lhs;
    SvCompOp op;
    std::shared_ptr<Expr> rhs;

    BinaryExpr(std::shared_ptr<Col> lhs_, SvCompOp op_, std::shared_ptr<Expr> rhs_) :
            lhs(std::move(lhs_)), op(op_), rhs(std::move(rhs_)) {}
};

struct HavingExpr : public TreeNode {
    std::shared_ptr<Expr> lhs;
    SvCompOp op;
    std::shared_ptr<Expr> rhs;

    HavingExpr(std::shared_ptr<Expr> lhs_, SvCompOp op_, std::shared_ptr<Expr> rhs_) :
            lhs(std::move(lhs_)), op(op_), rhs(std::move(rhs_)) {}
};

struct OrderBy : public TreeNode
{
    std::shared_ptr<Col> cols;
    OrderByDir orderby_dir;
    OrderBy( std::shared_ptr<Col> cols_, OrderByDir orderby_dir_) :
       cols(std::move(cols_)), orderby_dir(std::move(orderby_dir_)) {}
};

struct InsertStmt : public TreeNode {
    std::string tab_name;
    std::vector<std::shared_ptr<Value>> vals;

    InsertStmt(std::string tab_name_, std::vector<std::shared_ptr<Value>> vals_) :
            tab_name(std::move(tab_name_)), vals(std::move(vals_)) {}
};

struct DeleteStmt : public TreeNode {
    std::string tab_name;
    std::vector<std::shared_ptr<BinaryExpr>> conds;

    DeleteStmt(std::string tab_name_, std::vector<std::shared_ptr<BinaryExpr>> conds_) :
            tab_name(std::move(tab_name_)), conds(std::move(conds_)) {}
};

struct UpdateStmt : public TreeNode {
    std::string tab_name;
    std::vector<std::shared_ptr<SetClause>> set_clauses;
    std::vector<std::shared_ptr<BinaryExpr>> conds;

    UpdateStmt(std::string tab_name_,
               std::vector<std::shared_ptr<SetClause>> set_clauses_,
               std::vector<std::shared_ptr<BinaryExpr>> conds_) :
            tab_name(std::move(tab_name_)), set_clauses(std::move(set_clauses_)), conds(std::move(conds_)) {}
};

struct JoinExpr : public TreeNode {
    std::string left;
    std::string right;
    std::vector<std::shared_ptr<BinaryExpr>> conds;
    JoinType type;

    JoinExpr(std::string left_, std::string right_,
               std::vector<std::shared_ptr<BinaryExpr>> conds_, JoinType type_) :
            left(std::move(left_)), right(std::move(right_)), conds(std::move(conds_)), type(type_) {}
};

extern bool pending_is_explain_analyze;
extern std::map<std::string, std::string> pending_tab_display_names;
extern std::map<std::string, std::string> pending_tab_real_names;

struct SelectStmt : public TreeNode {
    std::vector<std::shared_ptr<Col>> cols;
    std::vector<std::shared_ptr<SelectItem>> select_items;
    std::vector<std::string> tabs;
    std::vector<std::shared_ptr<BinaryExpr>> conds;
    std::vector<std::shared_ptr<Col>> group_cols;
    std::vector<std::shared_ptr<HavingExpr>> having_conds;
    std::vector<std::shared_ptr<JoinExpr>> jointree;
    bool is_explain_analyze = false;
    bool select_all = false;
    bool has_agg = false;
    std::map<std::string, std::string> tab_display_names;
    std::map<std::string, std::string> tab_real_names;


    bool has_sort;
    std::shared_ptr<OrderBy> order;
    std::vector<std::shared_ptr<OrderBy>> order_items;
    bool has_limit = false;
    int limit = 0;


    SelectStmt(std::vector<std::shared_ptr<SelectItem>> select_items_,
               std::vector<std::string> tabs_,
               std::vector<std::shared_ptr<BinaryExpr>> conds_,
               std::vector<std::shared_ptr<Col>> group_cols_,
               std::vector<std::shared_ptr<HavingExpr>> having_conds_,
               std::vector<std::shared_ptr<OrderBy>> order_items_,
               bool has_limit_,
               int limit_) :
            select_items(std::move(select_items_)), tabs(std::move(tabs_)), conds(std::move(conds_)),
            group_cols(std::move(group_cols_)), having_conds(std::move(having_conds_)),
            order_items(std::move(order_items_)), has_limit(has_limit_), limit(limit_) {
                select_all = select_items.empty();
                for (auto &item : select_items) {
                    if (item->is_agg) {
                        has_agg = true;
                    } else {
                        cols.push_back(item->col);
                    }
                }
                has_sort = !order_items.empty();
                order = has_sort ? order_items.front() : nullptr;
                is_explain_analyze = pending_is_explain_analyze;
                tab_display_names = pending_tab_display_names;
                tab_real_names = pending_tab_real_names;
            }
};

// set enable_nestloop
struct SetStmt : public TreeNode {
    SetKnobType set_knob_type_;
    bool bool_val_;

    SetStmt(SetKnobType &type, bool bool_value) : 
        set_knob_type_(type), bool_val_(bool_value) { }
};

struct SetTransactionIsolation : public TreeNode {
    bool serializable;

    explicit SetTransactionIsolation(bool serializable_) : serializable(serializable_) {}
};

// Semantic value
struct SemValue {
    int sv_int;
    float sv_float;
    std::string sv_str;
    bool sv_bool;
    OrderByDir sv_orderby_dir;
    std::vector<std::string> sv_strs;

    std::shared_ptr<TreeNode> sv_node;

    SvCompOp sv_comp_op;

    std::shared_ptr<TypeLen> sv_type_len;

    std::shared_ptr<Field> sv_field;
    std::vector<std::shared_ptr<Field>> sv_fields;

    std::shared_ptr<Expr> sv_expr;

    std::shared_ptr<Value> sv_val;
    std::vector<std::shared_ptr<Value>> sv_vals;

    std::shared_ptr<Col> sv_col;
    std::vector<std::shared_ptr<Col>> sv_cols;
    AggType sv_agg_type;
    std::shared_ptr<AggFunc> sv_agg_func;
    std::shared_ptr<SelectItem> sv_select_item;
    std::vector<std::shared_ptr<SelectItem>> sv_select_items;
    std::shared_ptr<HavingExpr> sv_having_cond;
    std::vector<std::shared_ptr<HavingExpr>> sv_having_conds;

    std::shared_ptr<SetClause> sv_set_clause;
    std::vector<std::shared_ptr<SetClause>> sv_set_clauses;

    std::shared_ptr<BinaryExpr> sv_cond;
    std::vector<std::shared_ptr<BinaryExpr>> sv_conds;

    std::shared_ptr<OrderBy> sv_orderby;
    std::vector<std::shared_ptr<OrderBy>> sv_orderbys;

    SetKnobType sv_setKnobType;
};

extern std::shared_ptr<ast::TreeNode> parse_tree;

}

#define YYSTYPE ast::SemValue
