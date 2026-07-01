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

bool Planner::get_index_cols(std::string tab_name, std::vector<Condition> curr_conds,
                             std::vector<std::string>& index_col_names, std::string logical_tab_name) {
    index_col_names.clear();
    TabMeta& tab = sm_manager_->db_.get_table(tab_name);
    if (logical_tab_name.empty()) {
        logical_tab_name = tab_name;
    }

    std::map<CompOp, CompOp> swap_op = {
        {OP_EQ, OP_EQ}, {OP_NE, OP_NE}, {OP_LT, OP_GT}, {OP_GT, OP_LT}, {OP_LE, OP_GE}, {OP_GE, OP_LE},
    };

    std::vector<Condition> normalized;
    for (auto cond : curr_conds) {
        if (!cond.is_rhs_val) {
            continue;
        }
        if (cond.lhs_col.tab_name == logical_tab_name) {
            normalized.push_back(cond);
        } else if (cond.rhs_col.tab_name == logical_tab_name) {
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
        if(x->logical_tab_name_.compare(cond->lhs_col.tab_name) == 0) {
            return 1;
        } else if(x->logical_tab_name_.compare(cond->rhs_col.tab_name) == 0){
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
        if(x->logical_tab_name_.compare(table) == 0)
        {
            scantbl[i] = 1;
            joined_tables.emplace_back(x->logical_tab_name_);
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
        std::string real_table = query->table_real_names.count(tables[i]) ? query->table_real_names.at(tables[i])
                                                                          : tables[i];
        bool index_exist = get_index_cols(real_table, curr_conds, index_col_names, tables[i]);
        if (index_exist == false) {  // 该表没有索引
            index_col_names.clear();
            table_scan_executors[i] =
                std::make_shared<ScanPlan>(T_SeqScan, sm_manager_, real_table, curr_conds, index_col_names, tables[i]);
        } else {  // 存在索引
            table_scan_executors[i] =
                std::make_shared<ScanPlan>(T_IndexScan, sm_manager_, real_table, curr_conds, index_col_names, tables[i]);
        }
    }
    // 只有一个表，不需要join。
    if(tables.size() == 1)
    {
        return table_scan_executors[0];
    }
    auto join_conds = std::move(query->conds);
    std::shared_ptr<Plan> table_join_executors = table_scan_executors[0];
    std::set<std::string> joined{tables[0]};
    std::vector<bool> used_cond(join_conds.size(), false);
    auto cond_tables = [](const Condition &cond) {
        std::set<std::string> result{cond.lhs_col.tab_name};
        if (!cond.is_rhs_val) {
            result.insert(cond.rhs_col.tab_name);
        }
        return result;
    };
    auto subset = [](const std::set<std::string> &small, const std::set<std::string> &large) {
        for (const auto &item : small) {
            if (large.find(item) == large.end()) {
                return false;
            }
        }
        return true;
    };

    for (size_t i = 1; i < tables.size(); ++i) {
        const auto &right_table = tables[i];
        std::set<std::string> new_joined = joined;
        new_joined.insert(right_table);
        std::vector<Condition> curr_join_conds;
        for (size_t cond_idx = 0; cond_idx < join_conds.size(); ++cond_idx) {
            if (used_cond[cond_idx]) {
                continue;
            }
            auto cond_tabs = cond_tables(join_conds[cond_idx]);
            if (subset(cond_tabs, new_joined) && cond_tabs.find(right_table) != cond_tabs.end()) {
                curr_join_conds.push_back(join_conds[cond_idx]);
                used_cond[cond_idx] = true;
            }
        }
        table_join_executors = std::make_shared<JoinPlan>(T_NestLoop, std::move(table_join_executors),
                                                          table_scan_executors[i], curr_join_conds);
        joined = std::move(new_joined);
    }

    if (auto root_join = std::dynamic_pointer_cast<JoinPlan>(table_join_executors)) {
        for (size_t cond_idx = 0; cond_idx < join_conds.size(); ++cond_idx) {
            if (!used_cond[cond_idx]) {
                root_join->conds_.push_back(join_conds[cond_idx]);
            }
        }
    }

    return table_join_executors;

}


std::shared_ptr<Plan> Planner::generate_sort_plan(std::shared_ptr<Query> query, std::shared_ptr<Plan> plan)
{
    if(query->order_bys.empty()) {
        return plan;
    }
    std::vector<std::string> tables = query->tables;
    std::vector<ColMeta> all_cols;
    for (auto &sel_tab_name : tables) {
        // 这里db_不能写成get_db(), 注意要传指针
        std::string real_table = query->table_real_names.count(sel_tab_name) ? query->table_real_names.at(sel_tab_name)
                                                                             : sel_tab_name;
        auto sel_tab_cols = sm_manager_->db_.get_table(real_table).cols;
        for (auto &col : sel_tab_cols) {
            col.tab_name = sel_tab_name;
            all_cols.push_back(col);
        }
    }
    std::vector<OrderByItem> order_bys;
    for (auto order : query->order_bys) {
        TabCol sel_col;
        bool found = false;
        for (auto &col : all_cols) {
            if (col.name != order.col.col_name) {
                continue;
            }
            if (!order.col.tab_name.empty() && col.tab_name != order.col.tab_name) {
                continue;
            }
            if (found && order.col.tab_name.empty()) {
                throw AmbiguousColumnError(order.col.col_name);
            }
            sel_col = {.tab_name = col.tab_name, .col_name = col.name};
            found = true;
        }
        if (!found) {
            throw ColumnNotFoundError(order.col.tab_name.empty() ? order.col.col_name
                                                                 : order.col.tab_name + "." + order.col.col_name);
        }
        order.col = sel_col;
        order_bys.push_back(order);
    }
    return std::make_shared<SortPlan>(T_Sort, std::move(plan), std::move(order_bys));
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

    if (query->has_aggregation) {
        std::shared_ptr<Plan> plannerRoot = make_one_rel(query);
        return std::make_shared<AggregatePlan>(T_Aggregate, std::move(plannerRoot),
                                               query->select_items, query->group_cols, query->having_conds,
                                               query->order_bys, query->has_limit, query->limit);
    }

    //物理优化
    auto sel_cols = query->cols;
    std::shared_ptr<Plan> plannerRoot = physical_optimization(query, context);
    plannerRoot = generate_sort_plan(query, std::move(plannerRoot));
    plannerRoot = std::make_shared<ProjectionPlan>(T_Projection, std::move(plannerRoot),
                                                        std::move(sel_cols));
    if (query->has_limit) {
        plannerRoot = std::make_shared<LimitPlan>(T_Limit, std::move(plannerRoot), query->limit);
    }

    return plannerRoot;
}

std::shared_ptr<Plan> Planner::generate_union_plan(std::shared_ptr<Query> query, Context *context) {
    std::vector<std::shared_ptr<Plan>> branch_plans;
    branch_plans.reserve(query->union_branches.size());
    for (auto &branch : query->union_branches) {
        branch_plans.push_back(generate_select_plan(std::move(branch), context));
    }
    return std::make_shared<UnionPlan>(T_Union, std::move(branch_plans), query->order_bys,
                                       query->has_limit, query->limit, query->union_alias);
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

void add_agg_source_col(std::map<std::string, std::vector<TabCol>> &pushed_cols, const AggCall &agg) {
    if (!agg.is_star) {
        add_col(pushed_cols[agg.col.tab_name], agg.col);
    }
}

void add_having_source_cols(std::map<std::string, std::vector<TabCol>> &pushed_cols,
                            const HavingTerm &term) {
    if (term.is_agg) {
        add_agg_source_col(pushed_cols, term.agg);
    } else if (term.is_col) {
        add_col(pushed_cols[term.col.tab_name], term.col);
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

TabCol resolve_query_col(const TabCol &target, const std::vector<std::string> &tables,
                         const std::map<std::string, std::string> &real_names, SmManager *sm_manager) {
    if (!target.tab_name.empty()) {
        auto real_it = real_names.find(target.tab_name);
        const std::string &real_table = real_it == real_names.end() ? target.tab_name : real_it->second;
        sm_manager->db_.get_table(real_table).get_col(target.col_name);
        return target;
    }

    TabCol resolved;
    bool found = false;
    for (const auto &tab_name : tables) {
        auto real_it = real_names.find(tab_name);
        const std::string &real_table = real_it == real_names.end() ? tab_name : real_it->second;
        const auto &tab = sm_manager->db_.get_table(real_table);
        if (std::find_if(tab.cols.begin(), tab.cols.end(), [&](const ColMeta &col) {
                return col.name == target.col_name;
            }) == tab.cols.end()) {
            continue;
        }
        if (found) {
            throw AmbiguousColumnError(target.col_name);
        }
        resolved = {.tab_name = tab_name, .col_name = target.col_name};
        found = true;
    }
    if (!found) {
        throw ColumnNotFoundError(target.col_name);
    }
    return resolved;
}

bool keeps_all_table_cols(const std::vector<TabCol> &cols, const TabMeta &tab,
                          const std::string &logical_tab_name) {
    if (cols.size() != tab.cols.size()) {
        return false;
    }
    for (const auto &col : tab.cols) {
        if (!has_col(cols, {.tab_name = logical_tab_name, .col_name = col.name})) {
            return false;
        }
    }
    return true;
}

std::vector<TabCol> aggregate_output_cols(const std::vector<SelectItem> &select_items,
                                          const std::map<std::string, std::string> &display_names) {
    std::vector<TabCol> cols;
    cols.reserve(select_items.size());
    for (const auto &item : select_items) {
        cols.push_back({.tab_name = "", .col_name = item.output_name});
    }
    sort_cols_for_display(cols, display_names);
    return cols;
}

bool is_subset(const std::set<std::string> &small, const std::set<std::string> &large) {
    for (const auto &item : small) {
        if (large.find(item) == large.end()) {
            return false;
        }
    }
    return true;
}

std::shared_ptr<Plan> build_join_plan_for_order(
    const std::vector<std::string> &join_order, const std::map<std::string, std::shared_ptr<Plan>> &table_plans,
    const std::vector<Condition> &join_conds, const std::map<std::string, std::string> &display_names,
    const std::vector<std::string> &join_cond_right_tables, bool append_unused_conds) {
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
            std::string owner_table =
                cond_idx < join_cond_right_tables.size() ? join_cond_right_tables[cond_idx] : std::string();
            bool natural_attach = is_subset(cond_tabs, new_joined) && cond_tabs.find(right_table) != cond_tabs.end();
            bool can_attach = false;
            if (!owner_table.empty()) {
                can_attach = (owner_table == right_table && is_subset(cond_tabs, new_joined)) || natural_attach;
            } else {
                can_attach = natural_attach;
            }
            if (can_attach) {
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

}  // namespace

std::shared_ptr<Plan> Planner::generate_explain_analyze_plan(std::shared_ptr<Query> query, Context *context) {
    std::map<std::string, std::vector<Condition>> local_conds;
    std::vector<Condition> join_conds;
    std::vector<std::string> join_cond_right_tables;
    size_t on_cond_idx = 0;
    for (size_t cond_idx = 0; cond_idx < query->conds.size(); ++cond_idx) {
        const auto &cond = query->conds[cond_idx];
        std::string join_owner_table;
        if (on_cond_idx < query->join_cond_right_tables.size()) {
            join_owner_table = query->join_cond_right_tables[on_cond_idx++];
        }
        auto tables = condition_tables(cond);
        if (tables.size() == 1) {
            local_conds[*tables.begin()].push_back(cond);
        } else {
            join_conds.push_back(cond);
            join_cond_right_tables.push_back(join_owner_table);
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
        if (query->has_aggregation) {
            for (const auto &item : query->select_items) {
                if (item.is_agg) {
                    add_agg_source_col(pushed_cols, item.agg);
                }
            }
            for (const auto &group_col : query->group_cols) {
                add_col(pushed_cols[group_col.tab_name], group_col);
            }
            for (const auto &cond : query->having_conds) {
                add_having_source_cols(pushed_cols, cond.lhs);
                add_having_source_cols(pushed_cols, cond.rhs);
            }
        }
        if (!query->has_aggregation) {
            for (const auto &order_by : query->order_bys) {
                TabCol order_col = resolve_query_col(order_by.col, query->tables, query->table_real_names, sm_manager_);
                add_col(pushed_cols[order_col.tab_name], order_col);
            }
        }
        for (auto &entry : pushed_cols) {
            sort_cols_for_display(entry.second, query->tab_display_names);
        }
    }

    std::map<std::string, std::shared_ptr<Plan>> table_plans;
    for (const auto &tab_name : query->tables) {
        std::vector<std::string> index_cols;
        std::string real_table = query->table_real_names.count(tab_name) ? query->table_real_names.at(tab_name)
                                                                         : tab_name;
        std::shared_ptr<Plan> plan =
            std::make_shared<ScanPlan>(T_SeqScan, sm_manager_, real_table, std::vector<Condition>(), index_cols,
                                       tab_name, real_table);
        if (!local_conds[tab_name].empty()) {
            plan = std::make_shared<FilterPlan>(T_Filter, std::move(plan), local_conds[tab_name],
                                                query->tab_display_names);
        }
        if (push_project && !pushed_cols[tab_name].empty()) {
            bool display_all = keeps_all_table_cols(pushed_cols[tab_name], sm_manager_->db_.get_table(real_table),
                                                    tab_name);
            plan = std::make_shared<ProjectionPlan>(T_Projection, std::move(plan), pushed_cols[tab_name],
                                                    display_all, query->tab_display_names);
        }
        table_plans[tab_name] = std::move(plan);
    }

    std::vector<std::string> join_order = query->tables;
    std::shared_ptr<Plan> root = build_join_plan_for_order(
        join_order, table_plans, join_conds, query->tab_display_names, join_cond_right_tables, true);
    if (query->has_aggregation) {
        root = std::make_shared<AggregatePlan>(T_Aggregate, std::move(root), query->select_items, query->group_cols,
                                               query->having_conds, query->order_bys, query->has_limit,
                                               query->limit);
        return std::make_shared<ProjectionPlan>(T_Projection, std::move(root),
                                                aggregate_output_cols(query->select_items,
                                                                      query->tab_display_names),
                                                false, query->tab_display_names);
    }
    root = generate_sort_plan(query, std::move(root));
    if (query->has_limit) {
        root = std::make_shared<LimitPlan>(T_Limit, std::move(root), query->limit);
    }

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
    } else if (std::dynamic_pointer_cast<ast::CreateStaticCheckpoint>(query->parse)) {
        plannerRoot = std::make_shared<OtherPlan>(T_CreateStaticCheckpoint, std::string());
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
    } else if (auto x = std::dynamic_pointer_cast<ast::UnionStmt>(query->parse)) {
        std::shared_ptr<Plan> union_plan = generate_union_plan(std::move(query), context);
        plannerRoot = std::make_shared<DMLPlan>(T_select, union_plan, std::string(), std::vector<Value>(),
                                                std::vector<Condition>(), std::vector<SetClause>());
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
