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
    AG_COUNT, AG_MAX, AG_MIN, AG_SUM, AG_AVG
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
  ShowIndex(std::string tab_name_) : tab_name(std::move(tab_name_)){}

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
    std::string alias;  // for AS clause in SELECT

    Col(std::string tab_name_, std::string col_name_) :
            tab_name(std::move(tab_name_)), col_name(std::move(col_name_)) {}
    Col(std::string tab_name_, std::string col_name_, std::string alias_) :
            tab_name(std::move(tab_name_)), col_name(std::move(col_name_)), alias(std::move(alias_)) {}
};

struct AggExpr : public Expr {
    AggType agg_type;
    std::shared_ptr<Col> col;   // nullptr for COUNT(*)
    bool is_star;               // true for COUNT(*)
    std::string alias;          // for AS clause

    AggExpr(AggType agg_type_, std::shared_ptr<Col> col_, bool is_star_ = false) :
            agg_type(agg_type_), col(std::move(col_)), is_star(is_star_) {}
};

struct SetClause : public TreeNode {
    std::string col_name;
    std::shared_ptr<Value> val;

    SetClause(std::string col_name_, std::shared_ptr<Value> val_) :
            col_name(std::move(col_name_)), val(std::move(val_)) {}
};

struct BinaryExpr : public TreeNode {
    std::shared_ptr<Col> lhs;
    SvCompOp op;
    std::shared_ptr<Expr> rhs;

    BinaryExpr(std::shared_ptr<Col> lhs_, SvCompOp op_, std::shared_ptr<Expr> rhs_) :
            lhs(std::move(lhs_)), op(op_), rhs(std::move(rhs_)) {}
};

struct HavingBinaryExpr : public TreeNode {
    std::shared_ptr<AggExpr> lhs;
    SvCompOp op;
    std::shared_ptr<Value> rhs;

    HavingBinaryExpr(std::shared_ptr<AggExpr> lhs_, SvCompOp op_, std::shared_ptr<Value> rhs_) :
            lhs(std::move(lhs_)), op(op_), rhs(std::move(rhs_)) {}
};

struct OrderBy : public TreeNode
{
    std::vector<std::shared_ptr<Col>> cols;
    std::vector<OrderByDir> orderby_dirs;
    OrderBy( std::shared_ptr<Col> col, OrderByDir orderby_dir) {
        cols.push_back(std::move(col));
        orderby_dirs.push_back(orderby_dir);
    }
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

struct TableRefList : public TreeNode {
    std::vector<std::string> tabs;
    std::vector<std::shared_ptr<JoinExpr>> joins;
};

struct SelectStmt;

struct UnionStmt : public TreeNode {
    std::vector<std::shared_ptr<SelectStmt>> selects;
    bool has_sort;
    std::shared_ptr<OrderBy> order;
    int limit_num;

    UnionStmt(std::vector<std::shared_ptr<SelectStmt>> selects_,
              std::shared_ptr<OrderBy> order_,
              int limit_num_) :
            selects(std::move(selects_)), order(std::move(order_)), limit_num(limit_num_) {
        has_sort = (bool)order;
    }
};

struct SelectStmt : public TreeNode {
    std::vector<std::shared_ptr<Col>> cols;
    std::vector<std::shared_ptr<AggExpr>> agg_cols;    // aggregate expressions in SELECT
    std::vector<std::string> tabs;
    std::vector<std::shared_ptr<BinaryExpr>> conds;
    std::vector<std::shared_ptr<JoinExpr>> jointree;
    bool select_all;
    bool is_explain_analyze;

    bool has_sort;
    std::shared_ptr<OrderBy> order;

    bool has_group_by;
    std::vector<std::shared_ptr<Col>> group_by;         // GROUP BY columns

    std::vector<std::shared_ptr<HavingBinaryExpr>> having_conds; // HAVING conditions

    int limit_num;                                       // LIMIT clause, -1 for no limit

    bool from_union;
    std::shared_ptr<UnionStmt> union_stmt;
    std::string union_alias;


    SelectStmt(std::vector<std::shared_ptr<Col>> cols_,
               std::vector<std::string> tabs_,
               std::vector<std::shared_ptr<BinaryExpr>> conds_,
               std::shared_ptr<OrderBy> order_) :
            cols(std::move(cols_)), tabs(std::move(tabs_)), conds(std::move(conds_)),
            order(std::move(order_)) {
                select_all = cols.empty();
                is_explain_analyze = false;
                has_sort = (bool)order;
                has_group_by = false;
                limit_num = -1;
                from_union = false;
            }

    SelectStmt(std::vector<std::shared_ptr<Col>> cols_,
               std::vector<std::shared_ptr<AggExpr>> agg_cols_,
               std::shared_ptr<UnionStmt> union_stmt_,
               std::string union_alias_,
               std::shared_ptr<OrderBy> order_) :
            cols(std::move(cols_)), agg_cols(std::move(agg_cols_)), order(std::move(order_)),
            union_stmt(std::move(union_stmt_)), union_alias(std::move(union_alias_)) {
                select_all = cols.empty();
                is_explain_analyze = false;
                has_sort = (bool)order;
                has_group_by = false;
                limit_num = -1;
                from_union = true;
            }
};

// set enable_nestloop
struct SetStmt : public TreeNode {
    SetKnobType set_knob_type_;
    bool bool_val_;

    SetStmt(SetKnobType &type, bool bool_value) :
        set_knob_type_(type), bool_val_(bool_value) { }
};

// create static_checkpoint
struct CheckpointStmt : public TreeNode {
    CheckpointStmt() {}
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
    std::vector<std::shared_ptr<Expr>> sv_exprs;

    std::shared_ptr<Value> sv_val;
    std::vector<std::shared_ptr<Value>> sv_vals;

    std::shared_ptr<Col> sv_col;
    std::vector<std::shared_ptr<Col>> sv_cols;
    std::shared_ptr<SelectStmt> sv_select_stmt;
    std::vector<std::shared_ptr<SelectStmt>> sv_select_stmts;
    std::shared_ptr<UnionStmt> sv_union_stmt;

    std::shared_ptr<SetClause> sv_set_clause;
    std::vector<std::shared_ptr<SetClause>> sv_set_clauses;

    std::shared_ptr<BinaryExpr> sv_cond;
    std::vector<std::shared_ptr<BinaryExpr>> sv_conds;
    std::shared_ptr<HavingBinaryExpr> sv_having_cond;
    std::vector<std::shared_ptr<HavingBinaryExpr>> sv_having_conds;
    std::shared_ptr<TableRefList> sv_table_refs;

    std::shared_ptr<OrderBy> sv_orderby;

    std::shared_ptr<AggExpr> sv_agg_expr;
    std::vector<std::shared_ptr<AggExpr>> sv_agg_exprs;

    SetKnobType sv_setKnobType;
};

extern std::shared_ptr<ast::TreeNode> parse_tree;

}

#define YYSTYPE ast::SemValue
