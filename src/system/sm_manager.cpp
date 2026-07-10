/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "sm_manager.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>

#include "index/ix.h"
#include "common/config.h"
#include "record/rm.h"
#include "record/rm_scan.h"
#include "record_printer.h"
#include "transaction/transaction_manager.h"

namespace {

std::vector<std::string> split_csv_line(const std::string &line) {
    std::vector<std::string> fields;
    std::string field;
    bool in_quote = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char ch = line[i];
        if (ch == '"') {
            if (in_quote && i + 1 < line.size() && line[i + 1] == '"') {
                field.push_back('"');
                ++i;
            } else {
                in_quote = !in_quote;
            }
        } else if (ch == ',' && !in_quote) {
            fields.push_back(std::move(field));
            field.clear();
        } else {
            field.push_back(ch);
        }
    }
    if (!field.empty() && field.back() == '\r') {
        field.pop_back();
    }
    fields.push_back(std::move(field));
    return fields;
}

bool is_header_line(const std::vector<std::string> &fields, const TabMeta &tab) {
    if (fields.size() != tab.cols.size()) {
        return false;
    }
    for (size_t i = 0; i < fields.size(); ++i) {
        if (fields[i] != tab.cols[i].name) {
            return false;
        }
    }
    return true;
}

int parse_int_field(const std::string &field, const std::string &file_name, size_t line_no) {
    char *end = nullptr;
    errno = 0;
    long value = std::strtol(field.c_str(), &end, 10);
    if (errno != 0 || end == field.c_str() || *end != '\0') {
        throw InternalError("Invalid int in load file " + file_name + " at line " + std::to_string(line_no));
    }
    return static_cast<int>(value);
}

float parse_float_field(const std::string &field, const std::string &file_name, size_t line_no) {
    char *end = nullptr;
    errno = 0;
    float value = std::strtof(field.c_str(), &end);
    if (errno != 0 || end == field.c_str() || *end != '\0') {
        throw InternalError("Invalid float in load file " + file_name + " at line " + std::to_string(line_no));
    }
    return value;
}

std::vector<char> make_index_key(const RmRecord &rec, const IndexMeta &index) {
    std::vector<char> key(index.col_tot_len);
    int offset = 0;
    for (const auto &col : index.cols) {
        memcpy(key.data() + offset, rec.data + col.offset, col.len);
        offset += col.len;
    }
    return key;
}

}  // namespace

/**
 * @description: 判断是否为一个文件夹
 * @return {bool} 返回是否为一个文件夹
 * @param {string&} db_name 数据库文件名称，与文件夹同名
 */
bool SmManager::is_dir(const std::string& db_name) {
    struct stat st;
    return stat(db_name.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

/**
 * @description: 创建数据库，所有的数据库相关文件都放在数据库同名文件夹下
 * @param {string&} db_name 数据库名称
 */
void SmManager::create_db(const std::string& db_name) {
    if (is_dir(db_name)) {
        throw DatabaseExistsError(db_name);
    }
    //为数据库创建一个子目录
    std::string cmd = "mkdir " + db_name;
    if (system(cmd.c_str()) < 0) {  // 创建一个名为db_name的目录
        throw UnixError();
    }
    if (chdir(db_name.c_str()) < 0) {  // 进入名为db_name的目录
        throw UnixError();
    }
    //创建系统目录
    DbMeta *new_db = new DbMeta();
    new_db->name_ = db_name;

    // 注意，此处ofstream会在当前目录创建(如果没有此文件先创建)和打开一个名为DB_META_NAME的文件
    std::ofstream ofs(DB_META_NAME);

    // 将new_db中的信息，按照定义好的operator<<操作符，写入到ofs打开的DB_META_NAME文件中
    ofs << *new_db;  // 注意：此处重载了操作符<<

    delete new_db;

    // 创建日志文件
    disk_manager_->create_file(LOG_FILE_NAME);

    // 回到根目录
    if (chdir("..") < 0) {
        throw UnixError();
    }
}

/**
 * @description: 删除数据库，同时需要清空相关文件以及数据库同名文件夹
 * @param {string&} db_name 数据库名称，与文件夹同名
 */
void SmManager::drop_db(const std::string& db_name) {
    if (!is_dir(db_name)) {
        throw DatabaseNotFoundError(db_name);
    }
    std::string cmd = "rm -r " + db_name;
    if (system(cmd.c_str()) < 0) {
        throw UnixError();
    }
}

/**
 * @description: 打开数据库，找到数据库对应的文件夹，并加载数据库元数据和相关文件
 * @param {string&} db_name 数据库名称，与文件夹同名
 */
void SmManager::open_db(const std::string& db_name) {
    if (!is_dir(db_name)) {
        throw DatabaseNotFoundError(db_name);
    }
    if (chdir(db_name.c_str()) < 0) {
        throw UnixError();
    }

    std::ifstream ifs(DB_META_NAME);
    ifs >> db_;

    fhs_.clear();
    ihs_.clear();
    for (auto &entry : db_.tabs_) {
        auto &tab = entry.second;
        fhs_.emplace(tab.name, rm_manager_->open_file(tab.name));
        for (auto &index : tab.indexes) {
            std::string index_name = ix_manager_->get_index_name(tab.name, index.cols);
            ihs_.emplace(index_name, ix_manager_->open_index(tab.name, index.cols));
        }
    }
}

/**
 * @description: 把数据库相关的元数据刷入磁盘中
 */
void SmManager::flush_meta() {
    // 默认清空文件
    std::ofstream ofs(DB_META_NAME);
    ofs << db_;
}

/**
 * @description: 关闭数据库并把数据落盘
 */
void SmManager::close_db() {
    flush_meta();
    for (auto &entry : ihs_) {
        ix_manager_->close_index(entry.second.get());
    }
    ihs_.clear();
    for (auto &entry : fhs_) {
        rm_manager_->close_file(entry.second.get());
    }
    fhs_.clear();
    if (chdir("..") < 0) {
        throw UnixError();
    }
}

/**
 * @description: 显示所有的表,通过测试需要将其结果写入到output.txt,详情看题目文档
 * @param {Context*} context 
 */
void SmManager::show_tables(Context* context) {
    std::fstream outfile;
    bool write_output = output_file_enabled.load(std::memory_order_relaxed);
    if (write_output) {
        outfile.open("output.txt", std::ios::out | std::ios::app);
        outfile << "| Tables |\n";
    }
    RecordPrinter printer(1);
    printer.print_separator(context);
    printer.print_record({"Tables"}, context);
    printer.print_separator(context);
    for (auto &entry : db_.tabs_) {
        auto &tab = entry.second;
        printer.print_record({tab.name}, context);
        if (write_output) {
            outfile << "| " << tab.name << " |\n";
        }
    }
    printer.print_separator(context);
    if (write_output) {
        outfile.close();
    }
}

/**
 * @description: 显示某个表上的索引信息
 * @param {string&} tab_name 表名称
 * @param {Context*} context
 */
void SmManager::show_index(const std::string& tab_name, Context* context) {
    TabMeta &tab = db_.get_table(tab_name);

    std::fstream outfile;
    bool write_output = output_file_enabled.load(std::memory_order_relaxed);
    if (write_output) {
        outfile.open("output.txt", std::ios::out | std::ios::app);
    }
    for (auto &index : tab.indexes) {
        std::string cols = "(";
        for (int i = 0; i < index.col_num; ++i) {
            if (i > 0) {
                cols += ",";
            }
            cols += index.cols[i].name;
        }
        cols += ")";

        std::string line = "| " + tab_name + " | unique | " + cols + " |\n";
        if (write_output) {
            outfile << line;
        }
        if (context->ellipsis_ == false && *context->offset_ + line.length() < BUFFER_LENGTH) {
            memcpy(context->data_send_ + *(context->offset_), line.c_str(), line.length());
            *(context->offset_) += static_cast<int>(line.length());
        } else {
            context->ellipsis_ = true;
        }
    }
    if (write_output) {
        outfile.close();
    }
}

/**
 * @description: 显示表的元数据
 * @param {string&} tab_name 表名称
 * @param {Context*} context 
 */
void SmManager::desc_table(const std::string& tab_name, Context* context) {
    TabMeta &tab = db_.get_table(tab_name);

    std::vector<std::string> captions = {"Field", "Type", "Index"};
    RecordPrinter printer(captions.size());
    // Print header
    printer.print_separator(context);
    printer.print_record(captions, context);
    printer.print_separator(context);
    // Print fields
    for (auto &col : tab.cols) {
        std::vector<std::string> field_info = {col.name, coltype2str(col.type), col.index ? "YES" : "NO"};
        printer.print_record(field_info, context);
    }
    // Print footer
    printer.print_separator(context);
}

/**
 * @description: 创建表
 * @param {string&} tab_name 表的名称
 * @param {vector<ColDef>&} col_defs 表的字段
 * @param {Context*} context 
 */
void SmManager::create_table(const std::string& tab_name, const std::vector<ColDef>& col_defs, Context* context) {
    if (db_.is_table(tab_name)) {
        throw TableExistsError(tab_name);
    }
    if (context != nullptr && context->txn_mgr_ != nullptr) {
        context->txn_mgr_->clear_table_versions(tab_name);
    }
    // Create table meta
    int curr_offset = 0;
    TabMeta tab;
    tab.name = tab_name;
    for (auto &col_def : col_defs) {
        ColMeta col = {.tab_name = tab_name,
                       .name = col_def.name,
                       .type = col_def.type,
                       .len = col_def.len,
                       .offset = curr_offset,
                       .index = false};
        curr_offset += col_def.len;
        tab.cols.push_back(col);
    }
    // Create & open record file
    int record_size = curr_offset;  // record_size就是col meta所占的大小（表的元数据也是以记录的形式进行存储的）
    rm_manager_->create_file(tab_name, record_size);
    db_.tabs_[tab_name] = tab;
    // fhs_[tab_name] = rm_manager_->open_file(tab_name);
    fhs_.emplace(tab_name, rm_manager_->open_file(tab_name));

    flush_meta();
}

/**
 * @description: 删除表
 * @param {string&} tab_name 表的名称
 * @param {Context*} context
 */
void SmManager::drop_table(const std::string& tab_name, Context* context) {
    TabMeta &tab = db_.get_table(tab_name);
    if (context != nullptr && context->txn_mgr_ != nullptr) {
        context->txn_mgr_->clear_table_versions(tab_name);
    }

    for (auto &index : tab.indexes) {
        std::string index_name = ix_manager_->get_index_name(tab_name, index.cols);
        auto ih = ihs_.find(index_name);
        if (ih != ihs_.end()) {
            ix_manager_->close_index(ih->second.get());
            ihs_.erase(ih);
        }
        ix_manager_->destroy_index(tab_name, index.cols);
    }

    auto fh = fhs_.find(tab_name);
    if (fh != fhs_.end()) {
        rm_manager_->close_file(fh->second.get());
        fhs_.erase(fh);
    }
    rm_manager_->destroy_file(tab_name);
    db_.tabs_.erase(tab_name);
    flush_meta();
}

/**
 * @description: 创建索引
 * @param {string&} tab_name 表的名称
 * @param {vector<string>&} col_names 索引包含的字段名称
 * @param {Context*} context
 */
void SmManager::create_index(const std::string& tab_name, const std::vector<std::string>& col_names, Context* context) {
    TabMeta &tab = db_.get_table(tab_name);
    if (tab.is_index(col_names)) {
        throw IndexExistsError(tab_name, col_names);
    }

    std::vector<ColMeta> cols;
    int col_tot_len = 0;
    for (const auto &col_name : col_names) {
        auto col = tab.get_col(col_name);
        cols.push_back(*col);
        col_tot_len += col->len;
    }

    ix_manager_->create_index(tab_name, cols);
    std::string index_name = ix_manager_->get_index_name(tab_name, cols);
    ihs_.emplace(index_name, ix_manager_->open_index(tab_name, cols));

    IndexMeta index_meta = {.tab_name = tab_name, .col_tot_len = col_tot_len,
                            .col_num = static_cast<int>(cols.size()), .cols = cols};
    auto ih = ihs_.at(index_name).get();
    auto fh = fhs_.at(tab_name).get();
    try {
        for (RmScan scan(fh); !scan.is_end(); scan.next()) {
            auto rec = fh->get_record(scan.rid(), context);
            std::vector<char> key(col_tot_len);
            int offset = 0;
            for (auto &col : cols) {
                memcpy(key.data() + offset, rec->data + col.offset, col.len);
                offset += col.len;
            }
            std::vector<Rid> existing;
            if (ih->get_value(key.data(), &existing, context->txn_)) {
                throw UniqueConstraintViolationError(tab_name);
            }
            ih->insert_entry(key.data(), scan.rid(), context->txn_);
        }
    } catch (...) {
        auto ih_it = ihs_.find(index_name);
        if (ih_it != ihs_.end()) {
            ix_manager_->close_index(ih_it->second.get());
            ihs_.erase(ih_it);
        }
        ix_manager_->destroy_index(tab_name, cols);
        throw;
    }

    for (auto &col : cols) {
        tab.get_col(col.name)->index = true;
    }
    tab.indexes.push_back(std::move(index_meta));
    flush_meta();
}

/**
 * @description: 删除索引
 * @param {string&} tab_name 表名称
 * @param {vector<string>&} col_names 索引包含的字段名称
 * @param {Context*} context
 */
void SmManager::drop_index(const std::string& tab_name, const std::vector<std::string>& col_names, Context* context) {
    TabMeta &tab = db_.get_table(tab_name);
    auto index_meta = tab.get_index_meta(col_names);
    std::string index_name = ix_manager_->get_index_name(tab_name, col_names);
    auto ih = ihs_.find(index_name);
    if (ih != ihs_.end()) {
        ix_manager_->close_index(ih->second.get());
        ihs_.erase(ih);
    }
    ix_manager_->destroy_index(tab_name, col_names);
    for (auto &col : index_meta->cols) {
        bool still_indexed = false;
        for (auto it = tab.indexes.begin(); it != tab.indexes.end(); ++it) {
            if (it == index_meta) {
                continue;
            }
            for (auto &other_col : it->cols) {
                if (other_col.name == col.name) {
                    still_indexed = true;
                    break;
                }
            }
            if (still_indexed) {
                break;
            }
        }
        tab.get_col(col.name)->index = still_indexed;
    }
    tab.indexes.erase(index_meta);
    flush_meta();
}

/**
 * @description: 删除索引
 * @param {string&} tab_name 表名称
 * @param {vector<ColMeta>&} 索引包含的字段元数据
 * @param {Context*} context
 */
void SmManager::drop_index(const std::string& tab_name, const std::vector<ColMeta>& cols, Context* context) {
    std::vector<std::string> col_names;
    for (auto &col : cols) {
        col_names.push_back(col.name);
    }
    drop_index(tab_name, col_names, context);
}

size_t SmManager::load_table(const std::string& file_name, const std::string& tab_name, Context* context) {
    TabMeta &tab = db_.get_table(tab_name);
    auto fh = fhs_.at(tab_name).get();
    std::ifstream input(file_name);
    if (!input.is_open()) {
        throw FileNotFoundError(file_name);
    }

    auto write_guard = context != nullptr && context->txn_mgr_ != nullptr
                       ? context->txn_mgr_->acquire_write_latch()
                       : std::unique_lock<std::shared_mutex>();

    std::string line;
    size_t line_no = 0;
    size_t loaded = 0;
    bool first_data_line = true;
    while (std::getline(input, line)) {
        ++line_no;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        std::vector<std::string> fields = split_csv_line(line);
        if (first_data_line && is_header_line(fields, tab)) {
            first_data_line = false;
            continue;
        }
        first_data_line = false;
        if (fields.size() != tab.cols.size()) {
            throw InternalError("Invalid value count in load file " + file_name +
                                " at line " + std::to_string(line_no));
        }

        RmRecord rec(fh->get_file_hdr().record_size);
        for (size_t i = 0; i < tab.cols.size(); ++i) {
            const auto &col = tab.cols[i];
            char *dest = rec.data + col.offset;
            if (col.type == TYPE_INT) {
                int value = parse_int_field(fields[i], file_name, line_no);
                memcpy(dest, &value, sizeof(value));
            } else if (col.type == TYPE_FLOAT) {
                float value = parse_float_field(fields[i], file_name, line_no);
                memcpy(dest, &value, sizeof(value));
            } else if (col.type == TYPE_STRING) {
                if (static_cast<int>(fields[i].size()) > col.len) {
                    throw StringOverflowError();
                }
                memset(dest, 0, col.len);
                memcpy(dest, fields[i].data(), fields[i].size());
            }
        }

        std::vector<std::vector<char>> keys;
        keys.reserve(tab.indexes.size());
        for (const auto &index : tab.indexes) {
            auto key = make_index_key(rec, index);
            auto ih = ihs_.at(ix_manager_->get_index_name(tab_name, index.cols)).get();
            std::vector<Rid> existing;
            if (ih->get_value(key.data(), &existing, context == nullptr ? nullptr : context->txn_)) {
                throw UniqueConstraintViolationError(tab_name);
            }
            keys.push_back(std::move(key));
        }

        Rid rid = fh->insert_record(rec.data, nullptr);
        for (size_t i = 0; i < tab.indexes.size(); ++i) {
            const auto &index = tab.indexes[i];
            auto ih = ihs_.at(ix_manager_->get_index_name(tab_name, index.cols)).get();
            ih->insert_entry(keys[i].data(), rid, context == nullptr ? nullptr : context->txn_);
        }
        ++loaded;
    }

    fh->flush_file_hdr();
    buffer_pool_manager_->flush_all_pages(fh->GetFd());
    for (const auto &index : tab.indexes) {
        auto ih = ihs_.at(ix_manager_->get_index_name(tab_name, index.cols)).get();
        ih->flush_file_hdr();
        buffer_pool_manager_->flush_all_pages(ih->get_fd());
    }
    return loaded;
}
