%{
#include "ast.h"
#include "yacc.tab.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <memory>
#include <string>

int yylex(YYSTYPE *yylval, YYLTYPE *yylloc);

void yyerror(YYLTYPE *locp, const char* s) {
    std::cerr << "Parser Error at line " << locp->first_line << " column " << locp->first_column << ": " << s << std::endl;
}

using namespace ast;

static std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

static bool parse_compatible_isolation_identifier(const std::string &level, bool *serializable) {
    std::string normalized = lower_copy(level);
    if (normalized == "read_committed" || normalized == "read_uncommitted" ||
        normalized == "repeatable_read") {
        *serializable = false;
        return true;
    }
    return false;
}

static bool parse_compatible_isolation_pair(
    const std::string &first, const std::string &second, bool *serializable) {
    std::string left = lower_copy(first);
    std::string right = lower_copy(second);
    if (left == "read" && (right == "committed" || right == "uncommitted")) {
        *serializable = false;
        return true;
    }
    if (left == "repeatable" && right == "read") {
        *serializable = false;
        return true;
    }
    return false;
}

static std::shared_ptr<Value> negate_numeric_value(std::shared_ptr<Value> value) {
    if (auto int_lit = std::dynamic_pointer_cast<IntLit>(value)) {
        return std::make_shared<IntLit>(-int_lit->val);
    }
    if (auto float_lit = std::dynamic_pointer_cast<FloatLit>(value)) {
        return std::make_shared<FloatLit>(-float_lit->val);
    }
    return value;
}
%}

// request a pure (reentrant) parser
%define api.pure full
// enable location in error handler
%locations
// enable verbose syntax error message
%define parse.error verbose

// keywords
%token SHOW TABLES CREATE TABLE DROP DESC INSERT INTO VALUES DELETE FROM ASC ORDER BY
WHERE UPDATE SET SELECT INT CHAR FLOAT INDEX AND JOIN EXIT HELP TXN_BEGIN TXN_COMMIT TXN_ABORT TXN_ROLLBACK ORDER_BY ENABLE_NESTLOOP ENABLE_SORTMERGE STATIC_CHECKPOINT
TRANSACTION ISOLATION LEVEL SNAPSHOT SNAPSHOT_ISOLATION SERIALIZABLE SESSION CHARACTERISTICS
GROUP HAVING AS COUNT MAX MIN SUM AVG LIMIT UNION
// non-keywords
%token LEQ NEQ GEQ T_EOF

// type-specific tokens
%token <sv_str> IDENTIFIER VALUE_STRING
%token <sv_int> VALUE_INT
%token <sv_float> VALUE_FLOAT
%token <sv_bool> VALUE_BOOL

// specify types for non-terminal symbol
%type <sv_node> stmt dbStmt ddl dml txnStmt setStmt
%type <sv_select_stmt> select_stmt
%type <sv_select_stmts> union_query
%type <sv_field> field
%type <sv_fields> fieldList
%type <sv_type_len> type
%type <sv_comp_op> op
%type <sv_expr> expr
%type <sv_val> value
%type <sv_vals> valueList
%type <sv_str> tbName colName opt_alias
%type <sv_strs> tableList colNameList
%type <sv_col> col
%type <sv_cols> colList opt_group_clause
%type <sv_agg_func> agg_func
%type <sv_select_item> select_item
%type <sv_select_items> selector select_item_list
%type <sv_expr> having_term
%type <sv_having_cond> having_condition
%type <sv_having_conds> having_clause opt_having_clause
%type <sv_set_clause> setClause
%type <sv_set_clauses> setClauses
%type <sv_cond> condition
%type <sv_conds> whereClause optWhereClause
%type <sv_orderby>  order_item
%type <sv_orderbys>  order_clause opt_order_clause
%type <sv_orderby_dir> opt_asc_desc
%type <sv_int> opt_limit_clause
%type <sv_setKnobType> set_knob_type
%type <sv_bool> isolationLevel

%%
start:
        stmt ';'
    {
        parse_tree = $1;
        YYACCEPT;
    }
    |   stmt
    {
        parse_tree = $1;
        YYACCEPT;
    }
    |   HELP
    {
        parse_tree = std::make_shared<Help>();
        YYACCEPT;
    }
    |   EXIT
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
    |   T_EOF
    {
        parse_tree = nullptr;
        YYACCEPT;
    }
    ;

stmt:
        dbStmt
    |   ddl
    |   dml
    |   txnStmt
    |   setStmt
    ;

txnStmt:
        TXN_BEGIN
    {
        $$ = std::make_shared<TxnBegin>();
    }
    |   TXN_COMMIT
    {
        $$ = std::make_shared<TxnCommit>();
    }
    |   TXN_ABORT
    {
        $$ = std::make_shared<TxnAbort>();
    }
    | TXN_ROLLBACK
    {
        $$ = std::make_shared<TxnRollback>();
    }
    ;

dbStmt:
        SHOW TABLES
    {
        $$ = std::make_shared<ShowTables>();
    }
    ;

setStmt:
        SET set_knob_type '=' VALUE_BOOL
    {
        $$ = std::make_shared<SetStmt>($2, $4);
    }
    |   SET TRANSACTION ISOLATION LEVEL isolationLevel
    {
        $$ = std::make_shared<SetTransactionIsolation>($5);
    }
    |   SET SESSION TRANSACTION ISOLATION LEVEL isolationLevel
    {
        $$ = std::make_shared<SetTransactionIsolation>($6);
    }
    |   SET SESSION CHARACTERISTICS AS TRANSACTION ISOLATION LEVEL isolationLevel
    {
        $$ = std::make_shared<SetTransactionIsolation>($8);
    }
    ;

isolationLevel:
        SNAPSHOT ISOLATION
    {
        $$ = false;
    }
    |   SNAPSHOT_ISOLATION
    {
        $$ = false;
    }
    |   SNAPSHOT
    {
        $$ = false;
    }
    |   SERIALIZABLE
    {
        $$ = true;
    }
    |   IDENTIFIER
    {
        bool serializable = false;
        if (!parse_compatible_isolation_identifier($1, &serializable)) {
            yyerror(&@$, "unsupported isolation level");
            YYERROR;
        }
        $$ = serializable;
    }
    |   IDENTIFIER IDENTIFIER
    {
        bool serializable = false;
        if (!parse_compatible_isolation_pair($1, $2, &serializable)) {
            yyerror(&@$, "unsupported isolation level");
            YYERROR;
        }
        $$ = serializable;
    }
    ;

ddl:
        CREATE TABLE tbName '(' fieldList ')'
    {
        $$ = std::make_shared<CreateTable>($3, $5);
    }
    |   CREATE STATIC_CHECKPOINT
    {
        $$ = std::make_shared<CreateStaticCheckpoint>();
    }
    |   DROP TABLE tbName
    {
        $$ = std::make_shared<DropTable>($3);
    }
    |   DESC tbName
    {
        $$ = std::make_shared<DescTable>($2);
    }
    |   CREATE INDEX tbName '(' colNameList ')'
    {
        $$ = std::make_shared<CreateIndex>($3, $5);
    }
    |   DROP INDEX tbName '(' colNameList ')'
    {
        $$ = std::make_shared<DropIndex>($3, $5);
    }
    ;

dml:
        INSERT INTO tbName VALUES '(' valueList ')'
    {
        $$ = std::make_shared<InsertStmt>($3, $6);
    }
    |   DELETE FROM tbName optWhereClause
    {
        $$ = std::make_shared<DeleteStmt>($3, $4);
    }
    |   UPDATE tbName SET setClauses optWhereClause
    {
        $$ = std::make_shared<UpdateStmt>($2, $4, $5);
    }
    |   select_stmt
    {
        $$ = $1;
    }
    |   SELECT selector FROM '(' union_query ')' AS tbName opt_order_clause opt_limit_clause
    {
        $$ = std::make_shared<UnionStmt>($5, $8, $9, $10 >= 0, $10);
    }
    ;

select_stmt:
        SELECT selector FROM tableList optWhereClause opt_group_clause opt_having_clause opt_order_clause opt_limit_clause
    {
        $$ = std::make_shared<SelectStmt>($2, $4, $5, $6, $7, $8, $9 >= 0, $9);
    }
    ;

union_query:
        select_stmt UNION select_stmt
    {
        $$ = std::vector<std::shared_ptr<SelectStmt>>{$1, $3};
    }
    |   union_query UNION select_stmt
    {
        $$.push_back($3);
    }
    ;

fieldList:
        field
    {
        $$ = std::vector<std::shared_ptr<Field>>{$1};
    }
    |   fieldList ',' field
    {
        $$.push_back($3);
    }
    ;

colNameList:
        colName
    {
        $$ = std::vector<std::string>{$1};
    }
    | colNameList ',' colName
    {
        $$.push_back($3);
    }
    ;

field:
        colName type
    {
        $$ = std::make_shared<ColDef>($1, $2);
    }
    ;

type:
        INT
    {
        $$ = std::make_shared<TypeLen>(SV_TYPE_INT, sizeof(int));
    }
    |   CHAR '(' VALUE_INT ')'
    {
        $$ = std::make_shared<TypeLen>(SV_TYPE_STRING, $3);
    }
    |   FLOAT
    {
        $$ = std::make_shared<TypeLen>(SV_TYPE_FLOAT, sizeof(float));
    }
    ;

valueList:
        value
    {
        $$ = std::vector<std::shared_ptr<Value>>{$1};
    }
    |   valueList ',' value
    {
        $$.push_back($3);
    }
    ;

value:
        VALUE_INT
    {
        $$ = std::make_shared<IntLit>($1);
    }
    |   VALUE_FLOAT
    {
        $$ = std::make_shared<FloatLit>($1);
    }
    |   VALUE_STRING
    {
        $$ = std::make_shared<StringLit>($1);
    }
    |   VALUE_BOOL
    {
        $$ = std::make_shared<BoolLit>($1);
    }
    ;

condition:
        col op expr
    {
        $$ = std::make_shared<BinaryExpr>($1, $2, $3);
    }
    |   agg_func op expr
    {
        $$ = std::make_shared<BinaryExpr>(std::make_shared<Col>("", "__invalid_where_agg__"), $2, $3);
    }
    |   col op agg_func
    {
        $$ = std::make_shared<BinaryExpr>($1, $2,
                std::static_pointer_cast<Expr>(std::make_shared<Col>("", "__invalid_where_agg__")));
    }
    |   value op agg_func
    {
        $$ = std::make_shared<BinaryExpr>(std::make_shared<Col>("", "__invalid_where_agg__"), $2,
                std::static_pointer_cast<Expr>(std::make_shared<Col>("", "__invalid_where_agg__")));
    }
    |   agg_func op agg_func
    {
        $$ = std::make_shared<BinaryExpr>(std::make_shared<Col>("", "__invalid_where_agg__"), $2,
                std::static_pointer_cast<Expr>(std::make_shared<Col>("", "__invalid_where_agg__")));
    }
    ;

optWhereClause:
        /* epsilon */ { /* ignore*/ }
    |   WHERE whereClause
    {
        $$ = $2;
    }
    ;

whereClause:
        condition 
    {
        $$ = std::vector<std::shared_ptr<BinaryExpr>>{$1};
    }
    |   whereClause AND condition
    {
        $$.push_back($3);
    }
    ;

col:
        tbName '.' colName
    {
        $$ = std::make_shared<Col>($1, $3);
    }
    |   colName
    {
        $$ = std::make_shared<Col>("", $1);
    }
    ;

colList:
        col
    {
        $$ = std::vector<std::shared_ptr<Col>>{$1};
    }
    |   colList ',' col
    {
        $$.push_back($3);
    }
    ;

op:
        '='
    {
        $$ = SV_OP_EQ;
    }
    |   '<'
    {
        $$ = SV_OP_LT;
    }
    |   '>'
    {
        $$ = SV_OP_GT;
    }
    |   NEQ
    {
        $$ = SV_OP_NE;
    }
    |   LEQ
    {
        $$ = SV_OP_LE;
    }
    |   GEQ
    {
        $$ = SV_OP_GE;
    }
    ;

expr:
        value
    {
        $$ = std::static_pointer_cast<Expr>($1);
    }
    |   col
    {
        $$ = std::static_pointer_cast<Expr>($1);
    }
    ;

setClauses:
        setClause
    {
        $$ = std::vector<std::shared_ptr<SetClause>>{$1};
    }
    |   setClauses ',' setClause
    {
        $$.push_back($3);
    }
    ;

setClause:
        colName '=' value
    {
        $$ = std::make_shared<SetClause>($1, $3);
    }
    |   colName '=' colName '+' value
    {
        $$ = std::make_shared<SetClause>($1, $3, $5);
    }
    |   colName '=' colName '-' value
    {
        $$ = std::make_shared<SetClause>($1, $3, negate_numeric_value($5));
    }
    |   colName '=' colName value
    {
        $$ = std::make_shared<SetClause>($1, $3, $4);
    }
    ;

selector:
        '*'
    {
        $$ = {};
    }
    |   select_item_list
    ;

select_item_list:
        select_item
    {
        $$ = std::vector<std::shared_ptr<SelectItem>>{$1};
    }
    |   select_item_list ',' select_item
    {
        $$.push_back($3);
    }
    ;

select_item:
        col opt_alias
    {
        $$ = std::make_shared<SelectItem>($1, $2);
    }
    |   agg_func opt_alias
    {
        $$ = std::make_shared<SelectItem>($1, $2);
    }
    ;

opt_alias:
        AS colName
    {
        $$ = $2;
    }
    |   /* epsilon */
    {
        $$ = "";
    }
    ;

agg_func:
        COUNT '(' col ')'
    {
        $$ = std::make_shared<AggFunc>(AGG_COUNT, $3, false);
    }
    |   COUNT '(' '*' ')'
    {
        $$ = std::make_shared<AggFunc>(AGG_COUNT, nullptr, true);
    }
    |   MAX '(' col ')'
    {
        $$ = std::make_shared<AggFunc>(AGG_MAX, $3, false);
    }
    |   MIN '(' col ')'
    {
        $$ = std::make_shared<AggFunc>(AGG_MIN, $3, false);
    }
    |   SUM '(' col ')'
    {
        $$ = std::make_shared<AggFunc>(AGG_SUM, $3, false);
    }
    |   AVG '(' col ')'
    {
        $$ = std::make_shared<AggFunc>(AGG_AVG, $3, false);
    }
    ;

opt_group_clause:
        GROUP BY colList
    {
        $$ = $3;
    }
    |   /* epsilon */
    {
        $$ = {};
    }
    ;

opt_having_clause:
        HAVING having_clause
    {
        $$ = $2;
    }
    |   /* epsilon */
    {
        $$ = {};
    }
    ;

having_clause:
        having_condition
    {
        $$ = std::vector<std::shared_ptr<HavingExpr>>{$1};
    }
    |   having_clause AND having_condition
    {
        $$.push_back($3);
    }
    ;

having_condition:
        having_term op having_term
    {
        $$ = std::make_shared<HavingExpr>($1, $2, $3);
    }
    ;

having_term:
        value
    {
        $$ = std::static_pointer_cast<Expr>($1);
    }
    |   col
    {
        $$ = std::static_pointer_cast<Expr>($1);
    }
    |   agg_func
    {
        $$ = std::static_pointer_cast<Expr>($1);
    }
    ;

tableList:
        tbName
    {
        $$ = std::vector<std::string>{$1};
    }
    |   tableList ',' tbName
    {
        $$.push_back($3);
    }
    |   tableList JOIN tbName
    {
        $$.push_back($3);
    }
    ;

opt_order_clause:
    ORDER BY order_clause      
    { 
        $$ = $3; 
    }
    |   /* epsilon */ { $$ = {}; }
    ;

order_clause:
      order_item
    {
        $$ = std::vector<std::shared_ptr<OrderBy>>{$1};
    }
    | order_clause ',' order_item
    {
        $$.push_back($3);
    }
    ;

order_item:
      col  opt_asc_desc 
    { 
        $$ = std::make_shared<OrderBy>($1, $2);
    }
    ;   

opt_limit_clause:
        LIMIT VALUE_INT
    {
        $$ = $2;
    }
    |   /* epsilon */
    {
        $$ = -1;
    }
    ;

opt_asc_desc:
    ASC          { $$ = OrderBy_ASC;     }
    |  DESC      { $$ = OrderBy_DESC;    }
    |       { $$ = OrderBy_DEFAULT; }
    ;    

set_knob_type:
    ENABLE_NESTLOOP { $$ = EnableNestLoop; }
    |   ENABLE_SORTMERGE { $$ = EnableSortMerge; }
    ;

tbName: IDENTIFIER;

colName: IDENTIFIER;
%%
