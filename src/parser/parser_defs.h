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

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "ast.h"
#include "defs.h"

int yyparse();

typedef struct yy_buffer_state *YY_BUFFER_STATE;

YY_BUFFER_STATE yy_scan_string(const char *str);

void yy_delete_buffer(YY_BUFFER_STATE buffer);

namespace parser_support {

struct NormalizedSelectSql {
    std::string sql;
    bool is_explain_analyze = false;
    std::map<std::string, std::string> tab_display_names;
};

inline std::string lower_sql(std::string s) {
    for (char &ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

inline bool is_identifier_token(const std::string &token) {
    if (token.empty() || !(std::isalpha(static_cast<unsigned char>(token[0])) || token[0] == '_')) {
        return false;
    }
    for (char ch : token) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')) {
            return false;
        }
    }
    return true;
}

inline bool is_reserved_from_token(const std::string &token) {
    std::string low = lower_sql(token);
    return low == "join" || low == "on" || low == "where" || low == "order" || low == "by" ||
           low == "and" || low == "as" || low == ",";
}

inline std::vector<std::string> tokenize_sql(const std::string &sql) {
    std::vector<std::string> tokens;
    for (size_t i = 0; i < sql.size();) {
        unsigned char ch = static_cast<unsigned char>(sql[i]);
        if (std::isspace(ch) || sql[i] == '\0') {
            ++i;
            continue;
        }
        if (sql[i] == '\'') {
            size_t j = i + 1;
            while (j < sql.size() && sql[j] != '\'') {
                ++j;
            }
            if (j < sql.size()) {
                ++j;
            }
            tokens.push_back(sql.substr(i, j - i));
            i = j;
            continue;
        }
        if (std::isalpha(ch) || sql[i] == '_') {
            size_t j = i + 1;
            while (j < sql.size()) {
                unsigned char c = static_cast<unsigned char>(sql[j]);
                if (!(std::isalnum(c) || sql[j] == '_')) {
                    break;
                }
                ++j;
            }
            tokens.push_back(sql.substr(i, j - i));
            i = j;
            continue;
        }
        if (std::isdigit(ch) ||
            ((sql[i] == '+' || sql[i] == '-') && i + 1 < sql.size() &&
             std::isdigit(static_cast<unsigned char>(sql[i + 1])))) {
            size_t j = i + 1;
            while (j < sql.size()) {
                unsigned char c = static_cast<unsigned char>(sql[j]);
                if (!(std::isdigit(c) || sql[j] == '.')) {
                    break;
                }
                ++j;
            }
            tokens.push_back(sql.substr(i, j - i));
            i = j;
            continue;
        }
        if (i + 1 < sql.size()) {
            std::string two = sql.substr(i, 2);
            if (two == ">=" || two == "<=" || two == "<>") {
                tokens.push_back(two);
                i += 2;
                continue;
            }
        }
        tokens.push_back(sql.substr(i, 1));
        ++i;
    }
    return tokens;
}

inline std::string join_sql_tokens(const std::vector<std::string> &tokens, size_t begin, size_t end) {
    std::ostringstream os;
    for (size_t i = begin; i < end; ++i) {
        if (i > begin) {
            os << ' ';
        }
        os << tokens[i];
    }
    return os.str();
}

inline std::vector<std::string> rewrite_alias_tokens(
    std::vector<std::string> tokens, const std::map<std::string, std::string> &alias_to_table) {
    for (size_t i = 0; i + 2 < tokens.size(); ++i) {
        if (tokens[i + 1] == "." && is_identifier_token(tokens[i])) {
            auto it = alias_to_table.find(tokens[i]);
            if (it != alias_to_table.end()) {
                tokens[i] = it->second;
            }
        }
    }
    return tokens;
}

inline std::optional<NormalizedSelectSql> normalize_select_sql(const std::string &sql) {
    auto tokens = tokenize_sql(sql);
    while (!tokens.empty() && tokens.back() == ";") {
        tokens.pop_back();
    }
    if (tokens.empty()) {
        return std::nullopt;
    }

    NormalizedSelectSql result;
    size_t select_pos = 0;
    if (tokens.size() >= 3 && lower_sql(tokens[0]) == "explain" && lower_sql(tokens[1]) == "analyze") {
        result.is_explain_analyze = true;
        select_pos = 2;
    }
    if (select_pos >= tokens.size() || lower_sql(tokens[select_pos]) != "select") {
        return std::nullopt;
    }

    size_t from_pos = tokens.size();
    for (size_t i = select_pos + 1; i < tokens.size(); ++i) {
        if (lower_sql(tokens[i]) == "from") {
            from_pos = i;
            break;
        }
    }
    if (from_pos == tokens.size()) {
        return std::nullopt;
    }

    size_t where_pos = tokens.size();
    size_t order_pos = tokens.size();
    for (size_t i = from_pos + 1; i < tokens.size(); ++i) {
        std::string low = lower_sql(tokens[i]);
        if (low == "where") {
            where_pos = i;
            break;
        }
        if (low == "order") {
            order_pos = i;
            break;
        }
    }
    if (where_pos != tokens.size()) {
        for (size_t i = where_pos + 1; i < tokens.size(); ++i) {
            if (lower_sql(tokens[i]) == "order") {
                order_pos = i;
                break;
            }
        }
    }

    size_t from_end = std::min(where_pos, order_pos);
    std::vector<std::string> tables;
    std::map<std::string, std::string> alias_to_table;
    std::vector<std::vector<std::string>> on_conds;

    auto add_table = [&](const std::string &table, const std::string &alias) {
        if (std::find(tables.begin(), tables.end(), table) == tables.end()) {
            tables.push_back(table);
        }
        alias_to_table[table] = table;
        alias_to_table[alias] = table;
        result.tab_display_names[table] = alias;
    };

    auto parse_table_ref = [&](size_t &i) -> bool {
        if (i >= from_end || !is_identifier_token(tokens[i])) {
            return false;
        }
        std::string table = tokens[i++];
        std::string alias = table;
        if (i < from_end && lower_sql(tokens[i]) == "as") {
            ++i;
            if (i < from_end && is_identifier_token(tokens[i])) {
                alias = tokens[i++];
            }
        } else if (i < from_end && is_identifier_token(tokens[i]) && !is_reserved_from_token(tokens[i])) {
            alias = tokens[i++];
        }
        add_table(table, alias);
        return true;
    };

    size_t i = from_pos + 1;
    if (!parse_table_ref(i)) {
        return std::nullopt;
    }
    while (i < from_end) {
        std::string low = lower_sql(tokens[i]);
        if (tokens[i] == ",") {
            ++i;
            if (!parse_table_ref(i)) {
                return std::nullopt;
            }
        } else if (low == "join") {
            ++i;
            if (!parse_table_ref(i)) {
                return std::nullopt;
            }
            if (i < from_end && lower_sql(tokens[i]) == "on") {
                ++i;
                size_t cond_begin = i;
                while (i < from_end && lower_sql(tokens[i]) != "join") {
                    ++i;
                }
                if (cond_begin < i) {
                    std::vector<std::string> cond(tokens.begin() + cond_begin, tokens.begin() + i);
                    on_conds.push_back(std::move(cond));
                }
            }
        } else {
            ++i;
        }
    }

    auto selector_tokens = rewrite_alias_tokens(
        std::vector<std::string>(tokens.begin() + select_pos + 1, tokens.begin() + from_pos), alias_to_table);
    std::vector<std::vector<std::string>> all_conds;
    for (auto &cond : on_conds) {
        all_conds.push_back(rewrite_alias_tokens(std::move(cond), alias_to_table));
    }
    if (where_pos != tokens.size()) {
        size_t where_end = order_pos == tokens.size() ? tokens.size() : order_pos;
        std::vector<std::string> where_tokens(tokens.begin() + where_pos + 1, tokens.begin() + where_end);
        if (!where_tokens.empty()) {
            all_conds.push_back(rewrite_alias_tokens(std::move(where_tokens), alias_to_table));
        }
    }
    std::vector<std::string> order_tokens;
    if (order_pos != tokens.size()) {
        order_tokens.assign(tokens.begin() + order_pos, tokens.end());
        order_tokens = rewrite_alias_tokens(std::move(order_tokens), alias_to_table);
    }

    std::ostringstream normalized;
    normalized << "SELECT " << join_sql_tokens(selector_tokens, 0, selector_tokens.size()) << " FROM ";
    for (size_t table_idx = 0; table_idx < tables.size(); ++table_idx) {
        if (table_idx > 0) {
            normalized << ", ";
        }
        normalized << tables[table_idx];
    }
    if (!all_conds.empty()) {
        normalized << " WHERE ";
        for (size_t cond_idx = 0; cond_idx < all_conds.size(); ++cond_idx) {
            if (cond_idx > 0) {
                normalized << " AND ";
            }
            normalized << join_sql_tokens(all_conds[cond_idx], 0, all_conds[cond_idx].size());
        }
    }
    if (!order_tokens.empty()) {
        normalized << " " << join_sql_tokens(order_tokens, 0, order_tokens.size());
    }
    normalized << ";";
    result.sql = normalized.str();
    return result;
}

inline YY_BUFFER_STATE scan_string(const char *str) {
    ast::pending_is_explain_analyze = false;
    ast::pending_tab_display_names.clear();

    auto normalized_select = normalize_select_sql(str == nullptr ? std::string() : std::string(str));
    if (normalized_select.has_value()) {
        ast::pending_is_explain_analyze = normalized_select->is_explain_analyze;
        ast::pending_tab_display_names = normalized_select->tab_display_names;
        return ::yy_scan_string(normalized_select->sql.c_str());
    }
    return ::yy_scan_string(str == nullptr ? "" : str);
}

}  // namespace parser_support

#define yy_scan_string(str) parser_support::scan_string(str)
