/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "planner.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <memory>
#include <set>

#include "execution/executor_delete.h"
#include "execution/executor_filter.h"
#include "execution/executor_index_scan.h"
#include "execution/executor_insert.h"
#include "execution/executor_nestedloop_join.h"
#include "execution/executor_projection.h"
#include "execution/executor_seq_scan.h"
#include "execution/executor_update.h"
#include "index/ix.h"
#include "record_printer.h"

bool Planner::get_index_cols(std::string tab_name, std::vector<Condition> curr_conds, std::vector<std::string>& index_col_names) {
    index_col_names.clear();
    TabMeta& tab = sm_manager_->db_.get_table(tab_name);

    std::map<CompOp, CompOp> swap_op = {
        {OP_EQ, OP_EQ}, {OP_NE, OP_NE}, {OP_LT, OP_GT}, {OP_GT, OP_LT}, {OP_LE, OP_GE}, {OP_GE, OP_LE},
    };

    std::vector<Condition> normalized;
    for (auto cond : curr_conds) {
        if (!cond.is_rhs_val) {
            continue;
        }
        if (cond.lhs_col.tab_name == tab_name) {
            normalized.push_back(cond);
        } else if (cond.rhs_col.tab_name == tab_name) {
            std::swap(cond.lhs_col, cond.rhs_col);
            cond.op = swap_op.at(cond.op);
            normalized.push_back(cond);
        }
    }

    int best_prefix_len = 0;
    bool best_has_range = false;
    std::vector<std::string> best_cols;
    for (auto &index : tab.indexes) {
        int prefix_len = 0;
        bool has_range = false;
        for (auto &col : index.cols) {
            bool has_eq = false;
            bool has_range_on_col = false;
            for (auto &cond : normalized) {
                if (cond.lhs_col.col_name != col.name) {
                    continue;
                }
                if (cond.op == OP_EQ) {
                    has_eq = true;
                } else if (cond.op != OP_NE) {
                    has_range_on_col = true;
                }
            }
            if (has_eq) {
                prefix_len++;
                continue;
            }
            if (has_range_on_col) {
                prefix_len++;
                has_range = true;
            }
            break;
        }
        if (prefix_len > best_prefix_len ||
            (prefix_len == best_prefix_len && has_range && !best_has_range)) {
            best_prefix_len = prefix_len;
            best_has_range = has_range;
            best_cols.clear();
            for (auto &col : index.cols) {
                best_cols.push_back(col.name);
            }
        }
    }

    if (best_prefix_len == 0) {
        return false;
    }
    index_col_names = std::move(best_cols);
    return true;
}

/**
 * @brief 表算子条件谓词生成
 *
 * @param conds 条件
 * @param tab_names 表名
 * @return std::vector<Condition>
 */
std::vector<Condition> pop_conds(std::vector<Condition> &conds, std::string tab_names) {
    // auto has_tab = [&](const std::string &tab_name) {
    //     return std::find(tab_names.begin(), tab_names.end(), tab_name) != tab_names.end();
    // };
    std::vector<Condition> solved_conds;
    auto it = conds.begin();
    while (it != conds.end()) {
        if ((tab_names.compare(it->lhs_col.tab_name) == 0 && it->is_rhs_val) || (it->lhs_col.tab_name.compare(it->rhs_col.tab_name) == 0)) {
            solved_conds.emplace_back(std::move(*it));
            it = conds.erase(it);
        } else {
            it++;
        }
    }
    return solved_conds;
}

int push_conds(Condition *cond, std::shared_ptr<Plan> plan)
{
    if(auto x = std::dynamic_pointer_cast<ScanPlan>(plan))
    {
        if(x->tab_name_.compare(cond->lhs_col.tab_name) == 0) {
            return 1;
        } else if(x->tab_name_.compare(cond->rhs_col.tab_name) == 0){
            return 2;
        } else {
            return 0;
        }
    }
    else if(auto x = std::dynamic_pointer_cast<JoinPlan>(plan))
    {
        int left_res = push_conds(cond, x->left_);
        // 条件已经下推到左子节点
        if(left_res == 3){
            return 3;
        }
        int right_res = push_conds(cond, x->right_);
        // 条件已经下推到右子节点
        if(right_res == 3){
            return 3;
        }
        // 左子节点或右子节点有一个没有匹配到条件的列
        if(left_res == 0 || right_res == 0) {
            return left_res + right_res;
        }
        // 左子节点匹配到条件的右边
        if(left_res == 2) {
            // 需要将左右两边的条件变换位置
            std::map<CompOp, CompOp> swap_op = {
                {OP_EQ, OP_EQ}, {OP_NE, OP_NE}, {OP_LT, OP_GT}, {OP_GT, OP_LT}, {OP_LE, OP_GE}, {OP_GE, OP_LE},
            };
            std::swap(cond->lhs_col, cond->rhs_col);
            cond->op = swap_op.at(cond->op);
        }
        x->conds_.emplace_back(std::move(*cond));
        return 3;
    }
    return false;
}

std::shared_ptr<Plan> pop_scan(int *scantbl, std::string table, std::vector<std::string> &joined_tables, 
                std::vector<std::shared_ptr<Plan>> plans)
{
    for (size_t i = 0; i < plans.size(); i++) {
        auto x = std::dynamic_pointer_cast<ScanPlan>(plans[i]);
        if(x->tab_name_.compare(table) == 0)
        {
            scantbl[i] = 1;
            joined_tables.emplace_back(x->tab_name_);
            return plans[i];
        }
    }
    return nullptr;
}


std::shared_ptr<Query> Planner::logical_optimization(std::shared_ptr<Query> query, Context *context)
{
    
    //TODO 实现逻辑优化规则

    return query;
}

std::shared_ptr<Plan> Planner::physical_optimization(std::shared_ptr<Query> query, Context *context)
{
    std::shared_ptr<Plan> plan = make_one_rel(query);
    
    // 其他物理优化

    // 处理orderby
    plan = generate_sort_plan(query, std::move(plan)); 

    return plan;
}



std::shared_ptr<Plan> Planner::make_one_rel(std::shared_ptr<Query> query)
{
    auto x = std::dynamic_pointer_cast<ast::SelectStmt>(query->parse);
    std::vector<std::string> tables = query->tables;
    // // Scan table , 生成表算子列表tab_nodes
    std::vector<std::shared_ptr<Plan>> table_scan_executors(tables.size());
    for (size_t i = 0; i < tables.size(); i++) {
        auto curr_conds = pop_conds(query->conds, tables[i]);
        // int index_no = get_indexNo(tables[i], curr_conds);
        std::vector<std::string> index_col_names;
        bool index_exist = get_index_cols(tables[i], curr_conds, index_col_names);
        if (index_exist == false) {  // 该表没有索引
            index_col_names.clear();
            table_scan_executors[i] = 
                std::make_shared<ScanPlan>(T_SeqScan, sm_manager_, tables[i], curr_conds, index_col_names);
        } else {  // 存在索引
            table_scan_executors[i] =
                std::make_shared<ScanPlan>(T_IndexScan, sm_manager_, tables[i], curr_conds, index_col_names);
        }
    }
    // 只有一个表，不需要join。
    if(tables.size() == 1)
    {
        return table_scan_executors[0];
    }
    // 获取where条件
    auto conds = std::move(query->conds);
    std::shared_ptr<Plan> table_join_executors;
    
    int scantbl[tables.size()];
    for(size_t i = 0; i < tables.size(); i++)
    {
        scantbl[i] = -1;
    }
    // 假设在ast中已经添加了jointree，这里需要修改的逻辑是，先处理jointree，然后再考虑剩下的部分
    if(conds.size() >= 1)
    {
        // 有连接条件

        // 根据连接条件，生成第一层join
        std::vector<std::string> joined_tables(tables.size());
        auto it = conds.begin();
        while (it != conds.end()) {
            std::shared_ptr<Plan> left , right;
            left = pop_scan(scantbl, it->lhs_col.tab_name, joined_tables, table_scan_executors);
            right = pop_scan(scantbl, it->rhs_col.tab_name, joined_tables, table_scan_executors);
            std::vector<Condition> join_conds{*it};
            //建立join
            // 判断使用哪种join方式
            if(enable_nestedloop_join && enable_sortmerge_join) {
                // 默认nested loop join
                table_join_executors = std::make_shared<JoinPlan>(T_NestLoop, std::move(left), std::move(right), join_conds);
            } else if(enable_nestedloop_join) {
                table_join_executors = std::make_shared<JoinPlan>(T_NestLoop, std::move(left), std::move(right), join_conds);
            } else if(enable_sortmerge_join) {
                table_join_executors = std::make_shared<JoinPlan>(T_SortMerge, std::move(left), std::move(right), join_conds);
            } else {
                // error
                throw RMDBError("No join executor selected!");
            }

            // table_join_executors = std::make_shared<JoinPlan>(T_NestLoop, std::move(left), std::move(right), join_conds);
            it = conds.erase(it);
            break;
        }
        // 根据连接条件，生成第2-n层join
        it = conds.begin();
        while (it != conds.end()) {
            std::shared_ptr<Plan> left_need_to_join_executors = nullptr;
            std::shared_ptr<Plan> right_need_to_join_executors = nullptr;
            bool isneedreverse = false;
            if (std::find(joined_tables.begin(), joined_tables.end(), it->lhs_col.tab_name) == joined_tables.end()) {
                left_need_to_join_executors = pop_scan(scantbl, it->lhs_col.tab_name, joined_tables, table_scan_executors);
            }
            if (std::find(joined_tables.begin(), joined_tables.end(), it->rhs_col.tab_name) == joined_tables.end()) {
                right_need_to_join_executors = pop_scan(scantbl, it->rhs_col.tab_name, joined_tables, table_scan_executors);
                isneedreverse = true;
            } 

            if(left_need_to_join_executors != nullptr && right_need_to_join_executors != nullptr) {
                std::vector<Condition> join_conds{*it};
                std::shared_ptr<Plan> temp_join_executors = std::make_shared<JoinPlan>(T_NestLoop, 
                                                                    std::move(left_need_to_join_executors), 
                                                                    std::move(right_need_to_join_executors), 
                                                                    join_conds);
                table_join_executors = std::make_shared<JoinPlan>(T_NestLoop, std::move(temp_join_executors), 
                                                                    std::move(table_join_executors), 
                                                                    std::vector<Condition>());
            } else if(left_need_to_join_executors != nullptr || right_need_to_join_executors != nullptr) {
                if(isneedreverse) {
                    std::map<CompOp, CompOp> swap_op = {
                        {OP_EQ, OP_EQ}, {OP_NE, OP_NE}, {OP_LT, OP_GT}, {OP_GT, OP_LT}, {OP_LE, OP_GE}, {OP_GE, OP_LE},
                    };
                    std::swap(it->lhs_col, it->rhs_col);
                    it->op = swap_op.at(it->op);
                    left_need_to_join_executors = std::move(right_need_to_join_executors);
                }
                std::vector<Condition> join_conds{*it};
                table_join_executors = std::make_shared<JoinPlan>(T_NestLoop, std::move(left_need_to_join_executors), 
                                                                    std::move(table_join_executors), join_conds);
            } else {
                push_conds(std::move(&(*it)), table_join_executors);
            }
            it = conds.erase(it);
        }
    } else {
        table_join_executors = table_scan_executors[0];
        scantbl[0] = 1;
    }

    //连接剩余表
    for (size_t i = 0; i < tables.size(); i++) {
        if(scantbl[i] == -1) {
            table_join_executors = std::make_shared<JoinPlan>(T_NestLoop, std::move(table_join_executors),
                                                    std::move(table_scan_executors[i]), std::vector<Condition>());
        }
    }

    return table_join_executors;

}


std::shared_ptr<Plan> Planner::generate_sort_plan(std::shared_ptr<Query> query, std::shared_ptr<Plan> plan)
{
    auto x = std::dynamic_pointer_cast<ast::SelectStmt>(query->parse);
    if(!x->has_sort) {
        return plan;
    }
    std::vector<std::string> tables = query->tables;
    std::vector<ColMeta> all_cols;
    for (auto &sel_tab_name : tables) {
        // 这里db_不能写成get_db(), 注意要传指针
        const auto &sel_tab_cols = sm_manager_->db_.get_table(sel_tab_name).cols;
        all_cols.insert(all_cols.end(), sel_tab_cols.begin(), sel_tab_cols.end());
    }
    TabCol order_col = {.tab_name = x->order->cols->tab_name, .col_name = x->order->cols->col_name};
    TabCol sel_col;
    bool found = false;
    for (auto &col : all_cols) {
        if (col.name != order_col.col_name) {
            continue;
        }
        if (!order_col.tab_name.empty() && col.tab_name != order_col.tab_name) {
            continue;
        }
        if (found && order_col.tab_name.empty()) {
            throw AmbiguousColumnError(order_col.col_name);
        }
        sel_col = {.tab_name = col.tab_name, .col_name = col.name};
        found = true;
    }
    if (!found) {
        throw ColumnNotFoundError(order_col.tab_name.empty() ? order_col.col_name
                                                            : order_col.tab_name + "." + order_col.col_name);
    }
    return std::make_shared<SortPlan>(T_Sort, std::move(plan), sel_col,
                                    x->order->orderby_dir == ast::OrderBy_DESC);
}


/**
 * @brief select plan 生成
 *
 * @param sel_cols select plan 选取的列
 * @param tab_names select plan 目标的表
 * @param conds select plan 选取条件
 */
std::shared_ptr<Plan> Planner::generate_select_plan(std::shared_ptr<Query> query, Context *context) {
    //逻辑优化
    query = logical_optimization(std::move(query), context);

    //物理优化
    auto sel_cols = query->cols;
    std::shared_ptr<Plan> plannerRoot = physical_optimization(query, context);
    plannerRoot = std::make_shared<ProjectionPlan>(T_Projection, std::move(plannerRoot), 
                                                        std::move(sel_cols));

    return plannerRoot;
}

namespace {

std::set<std::string> condition_tables(const Condition &cond) {
    std::set<std::string> tables;
    tables.insert(cond.lhs_col.tab_name);
    if (!cond.is_rhs_val) {
        tables.insert(cond.rhs_col.tab_name);
    }
    return tables;
}

bool has_col(const std::vector<TabCol> &cols, const TabCol &target) {
    return std::find_if(cols.begin(), cols.end(), [&](const TabCol &col) {
        return col.tab_name == target.tab_name && col.col_name == target.col_name;
    }) != cols.end();
}

void add_col(std::vector<TabCol> &cols, const TabCol &target) {
    if (!has_col(cols, target)) {
        cols.push_back(target);
    }
}

std::string display_table(const std::map<std::string, std::string> &display_names, const std::string &tab_name) {
    auto it = display_names.find(tab_name);
    return it == display_names.end() ? tab_name : it->second;
}

std::string display_col_key(const TabCol &col, const std::map<std::string, std::string> &display_names) {
    return display_table(display_names, col.tab_name) + "." + col.col_name;
}

void sort_cols_for_display(std::vector<TabCol> &cols, const std::map<std::string, std::string> &display_names) {
    std::sort(cols.begin(), cols.end(), [&](const TabCol &lhs, const TabCol &rhs) {
        return display_col_key(lhs, display_names) < display_col_key(rhs, display_names);
    });
}

bool is_subset(const std::set<std::string> &small, const std::set<std::string> &large) {
    for (const auto &item : small) {
        if (large.find(item) == large.end()) {
            return false;
        }
    }
    return true;
}

bool connects_to_joined(const Condition &cond, const std::set<std::string> &joined,
                        const std::string &candidate) {
    auto tables = condition_tables(cond);
    if (tables.find(candidate) == tables.end()) {
        return false;
    }
    for (const auto &table : tables) {
        if (table != candidate && joined.find(table) != joined.end()) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<Plan> build_join_plan_for_order(
    const std::vector<std::string> &join_order, const std::map<std::string, std::shared_ptr<Plan>> &table_plans,
    const std::vector<Condition> &join_conds, const std::map<std::string, std::string> &display_names,
    bool append_unused_conds) {
    std::shared_ptr<Plan> root = table_plans.at(join_order[0]);
    std::set<std::string> joined{join_order[0]};
    std::vector<bool> used_join_cond(join_conds.size(), false);

    for (size_t i = 1; i < join_order.size(); ++i) {
        const auto &right_table = join_order[i];
        std::set<std::string> new_joined = joined;
        new_joined.insert(right_table);
        std::vector<Condition> curr_join_conds;
        for (size_t cond_idx = 0; cond_idx < join_conds.size(); ++cond_idx) {
            if (used_join_cond[cond_idx]) {
                continue;
            }
            auto cond_tabs = condition_tables(join_conds[cond_idx]);
            if (is_subset(cond_tabs, new_joined) && cond_tabs.find(right_table) != cond_tabs.end()) {
                curr_join_conds.push_back(join_conds[cond_idx]);
                used_join_cond[cond_idx] = true;
            }
        }
        root = std::make_shared<JoinPlan>(T_NestLoop, std::move(root), table_plans.at(right_table),
                                          curr_join_conds, display_names);
        joined = std::move(new_joined);
    }

    if (append_unused_conds) {
        auto root_join = std::dynamic_pointer_cast<JoinPlan>(root);
        for (size_t cond_idx = 0; cond_idx < join_conds.size(); ++cond_idx) {
            if (!used_join_cond[cond_idx]) {
                root_join->conds_.push_back(join_conds[cond_idx]);
            }
        }
    }
    return root;
}

std::unique_ptr<AbstractExecutor> make_count_executor(std::shared_ptr<Plan> plan, SmManager *sm_manager,
                                                       Context *context) {
    if (auto x = std::dynamic_pointer_cast<ProjectionPlan>(plan)) {
        return std::make_unique<ProjectionExecutor>(make_count_executor(x->subplan_, sm_manager, context),
                                                    x->sel_cols_, x->display_all_, x->tab_display_names_);
    }
    if (auto x = std::dynamic_pointer_cast<FilterPlan>(plan)) {
        return std::make_unique<FilterExecutor>(make_count_executor(x->subplan_, sm_manager, context),
                                                x->conds_, x->tab_display_names_);
    }
    if (auto x = std::dynamic_pointer_cast<ScanPlan>(plan)) {
        return std::make_unique<SeqScanExecutor>(sm_manager, x->tab_name_, x->conds_, context);
    }
    if (auto x = std::dynamic_pointer_cast<JoinPlan>(plan)) {
        return std::make_unique<NestedLoopJoinExecutor>(make_count_executor(x->left_, sm_manager, context),
                                                        make_count_executor(x->right_, sm_manager, context),
                                                        x->conds_, x->tab_display_names_);
    }
    throw InternalError("Unsupported explain count plan");
}

size_t count_plan_rows(std::shared_ptr<Plan> plan, SmManager *sm_manager, Context *context) {
    auto executor = make_count_executor(std::move(plan), sm_manager, context);
    size_t rows = 0;
    for (executor->beginTuple(); !executor->is_end(); executor->nextTuple()) {
        auto tuple = executor->Next();
        (void)tuple;
        ++rows;
    }
    return rows;
}

std::string order_key(const std::vector<std::string> &order) {
    std::ostringstream os;
    for (const auto &tab : order) {
        os << tab << '\n';
    }
    return os.str();
}

std::vector<std::string> choose_min_result_join_order(
    const std::vector<std::string> &tables, const std::vector<Condition> &join_conds,
    const std::map<std::string, std::shared_ptr<Plan>> &table_plans,
    const std::map<std::string, std::string> &display_names, SmManager *sm_manager, Context *context) {
    if (tables.size() <= 2) {
        return tables;
    }

    auto plan_rows = [&](const std::vector<std::string> &order) {
        return count_plan_rows(build_join_plan_for_order(order, table_plans, join_conds, display_names, false),
                               sm_manager, context);
    };

    std::vector<std::string> order;
    std::vector<std::string> remaining = tables;

    bool found_pair = false;
    size_t best_pair_rows = 0;
    std::vector<std::string> best_pair;
    for (const auto &lhs : tables) {
        for (const auto &rhs : tables) {
            if (lhs == rhs) {
                continue;
            }
            bool connected = false;
            for (const auto &cond : join_conds) {
                auto cond_tabs = condition_tables(cond);
                if (cond_tabs.find(lhs) != cond_tabs.end() && cond_tabs.find(rhs) != cond_tabs.end()) {
                    connected = true;
                    break;
                }
            }
            if (!connected) {
                continue;
            }
            std::vector<std::string> candidate{lhs, rhs};
            size_t rows = plan_rows(candidate);
            if (!found_pair || rows < best_pair_rows ||
                (rows == best_pair_rows && order_key(candidate) < order_key(best_pair))) {
                found_pair = true;
                best_pair_rows = rows;
                best_pair = std::move(candidate);
            }
        }
    }

    if (found_pair) {
        order = std::move(best_pair);
        for (const auto &tab : order) {
            remaining.erase(std::remove(remaining.begin(), remaining.end(), tab), remaining.end());
        }
    }

    while (!remaining.empty()) {
        std::vector<std::string> best_order;
        size_t best_rows = 0;
        bool found = false;
        for (const auto &candidate : remaining) {
            std::set<std::string> joined(order.begin(), order.end());
            bool connected = order.empty();
            for (const auto &cond : join_conds) {
                if (connects_to_joined(cond, joined, candidate)) {
                    connected = true;
                    break;
                }
            }
            if (!connected) {
                continue;
            }
            auto candidate_order = order;
            candidate_order.push_back(candidate);
            size_t rows = plan_rows(candidate_order);
            if (!found || rows < best_rows ||
                (rows == best_rows && order_key(candidate_order) < order_key(best_order))) {
                found = true;
                best_rows = rows;
                best_order = std::move(candidate_order);
            }
        }
        if (!found) {
            std::sort(remaining.begin(), remaining.end());
            best_order = order;
            best_order.push_back(remaining.front());
        }
        auto chosen = best_order.back();
        order = std::move(best_order);
        remaining.erase(std::remove(remaining.begin(), remaining.end(), chosen), remaining.end());
    }

    return order;
}

}  // namespace

std::shared_ptr<Plan> Planner::generate_explain_analyze_plan(std::shared_ptr<Query> query, Context *context) {
    std::map<std::string, std::vector<Condition>> local_conds;
    std::vector<Condition> join_conds;

    for (const auto &cond : query->conds) {
        auto tables = condition_tables(cond);
        if (tables.size() == 1) {
            local_conds[*tables.begin()].push_back(cond);
        } else {
            join_conds.push_back(cond);
        }
    }

    std::map<std::string, std::vector<TabCol>> pushed_cols;
    bool push_project = !query->select_all && query->tables.size() > 1;
    if (push_project) {
        for (const auto &col : query->cols) {
            add_col(pushed_cols[col.tab_name], col);
        }
        for (const auto &cond : join_conds) {
            add_col(pushed_cols[cond.lhs_col.tab_name], cond.lhs_col);
            add_col(pushed_cols[cond.rhs_col.tab_name], cond.rhs_col);
        }
        for (auto &entry : pushed_cols) {
            sort_cols_for_display(entry.second, query->tab_display_names);
        }
    }

    std::map<std::string, std::shared_ptr<Plan>> table_plans;
    for (const auto &tab_name : query->tables) {
        std::vector<std::string> index_cols;
        std::shared_ptr<Plan> plan =
            std::make_shared<ScanPlan>(T_SeqScan, sm_manager_, tab_name, std::vector<Condition>(), index_cols);
        if (!local_conds[tab_name].empty()) {
            plan = std::make_shared<FilterPlan>(T_Filter, std::move(plan), local_conds[tab_name],
                                                query->tab_display_names);
        }
        if (push_project && !pushed_cols[tab_name].empty()) {
            plan = std::make_shared<ProjectionPlan>(T_Projection, std::move(plan), pushed_cols[tab_name], false,
                                                    query->tab_display_names);
        }
        table_plans[tab_name] = std::move(plan);
    }

    std::vector<std::string> join_order = choose_min_result_join_order(
        query->tables, join_conds, table_plans, query->tab_display_names, sm_manager_, context);
    std::shared_ptr<Plan> root = build_join_plan_for_order(
        join_order, table_plans, join_conds, query->tab_display_names, true);

    auto root_cols = query->cols;
    if (!query->select_all) {
        sort_cols_for_display(root_cols, query->tab_display_names);
    }
    return std::make_shared<ProjectionPlan>(T_Projection, std::move(root), root_cols, query->select_all,
                                            query->tab_display_names);
}

// 生成DDL语句和DML语句的查询执行计划
std::shared_ptr<Plan> Planner::do_planner(std::shared_ptr<Query> query, Context *context)
{
    std::shared_ptr<Plan> plannerRoot;
    if (auto x = std::dynamic_pointer_cast<ast::CreateTable>(query->parse)) {
        // create table;
        std::vector<ColDef> col_defs;
        for (auto &field : x->fields) {
            if (auto sv_col_def = std::dynamic_pointer_cast<ast::ColDef>(field)) {
                ColDef col_def = {.name = sv_col_def->col_name,
                                  .type = interp_sv_type(sv_col_def->type_len->type),
                                  .len = sv_col_def->type_len->len};
                col_defs.push_back(col_def);
            } else {
                throw InternalError("Unexpected field type");
            }
        }
        plannerRoot = std::make_shared<DDLPlan>(T_CreateTable, x->tab_name, std::vector<std::string>(), col_defs);
    } else if (auto x = std::dynamic_pointer_cast<ast::DropTable>(query->parse)) {
        // drop table;
        plannerRoot = std::make_shared<DDLPlan>(T_DropTable, x->tab_name, std::vector<std::string>(), std::vector<ColDef>());
    } else if (auto x = std::dynamic_pointer_cast<ast::CreateIndex>(query->parse)) {
        // create index;
        plannerRoot = std::make_shared<DDLPlan>(T_CreateIndex, x->tab_name, x->col_names, std::vector<ColDef>());
    } else if (auto x = std::dynamic_pointer_cast<ast::DropIndex>(query->parse)) {
        // drop index
        plannerRoot = std::make_shared<DDLPlan>(T_DropIndex, x->tab_name, x->col_names, std::vector<ColDef>());
    } else if (auto x = std::dynamic_pointer_cast<ast::InsertStmt>(query->parse)) {
        // insert;
        plannerRoot = std::make_shared<DMLPlan>(T_Insert, std::shared_ptr<Plan>(),  x->tab_name,  
                                                    query->values, std::vector<Condition>(), std::vector<SetClause>());
    } else if (auto x = std::dynamic_pointer_cast<ast::DeleteStmt>(query->parse)) {
        // delete;
        // 生成表扫描方式
        std::shared_ptr<Plan> table_scan_executors;
        // 只有一张表，不需要进行物理优化了
        // int index_no = get_indexNo(x->tab_name, query->conds);
        std::vector<std::string> index_col_names;
        bool index_exist = get_index_cols(x->tab_name, query->conds, index_col_names);
        
        if (index_exist == false) {  // 该表没有索引
            index_col_names.clear();
            table_scan_executors = 
                std::make_shared<ScanPlan>(T_SeqScan, sm_manager_, x->tab_name, query->conds, index_col_names);
        } else {  // 存在索引
            table_scan_executors =
                std::make_shared<ScanPlan>(T_IndexScan, sm_manager_, x->tab_name, query->conds, index_col_names);
        }

        plannerRoot = std::make_shared<DMLPlan>(T_Delete, table_scan_executors, x->tab_name,  
                                                std::vector<Value>(), query->conds, std::vector<SetClause>());
    } else if (auto x = std::dynamic_pointer_cast<ast::UpdateStmt>(query->parse)) {
        // update;
        // 生成表扫描方式
        std::shared_ptr<Plan> table_scan_executors;
        // 只有一张表，不需要进行物理优化了
        // int index_no = get_indexNo(x->tab_name, query->conds);
        std::vector<std::string> index_col_names;
        bool index_exist = get_index_cols(x->tab_name, query->conds, index_col_names);

        if (index_exist == false) {  // 该表没有索引
        index_col_names.clear();
            table_scan_executors = 
                std::make_shared<ScanPlan>(T_SeqScan, sm_manager_, x->tab_name, query->conds, index_col_names);
        } else {  // 存在索引
            table_scan_executors =
                std::make_shared<ScanPlan>(T_IndexScan, sm_manager_, x->tab_name, query->conds, index_col_names);
        }
        plannerRoot = std::make_shared<DMLPlan>(T_Update, table_scan_executors, x->tab_name,
                                                     std::vector<Value>(), query->conds, 
                                                     query->set_clauses);
    } else if (auto x = std::dynamic_pointer_cast<ast::SelectStmt>(query->parse)) {

        std::shared_ptr<plannerInfo> root = std::make_shared<plannerInfo>(x);
        // 生成select语句的查询执行计划
        if (query->is_explain_analyze) {
            std::shared_ptr<Plan> projection = generate_explain_analyze_plan(std::move(query), context);
            plannerRoot = std::make_shared<DMLPlan>(T_ExplainAnalyze, projection, std::string(), std::vector<Value>(),
                                                    std::vector<Condition>(), std::vector<SetClause>());
        } else {
            std::shared_ptr<Plan> projection = generate_select_plan(std::move(query), context);
            plannerRoot = std::make_shared<DMLPlan>(T_select, projection, std::string(), std::vector<Value>(),
                                                    std::vector<Condition>(), std::vector<SetClause>());
        }
    } else {
        throw InternalError("Unexpected AST root");
    }
    return plannerRoot;
}
