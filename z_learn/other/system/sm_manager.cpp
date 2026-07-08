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

#include <iterator>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>

#include "common/config.h"
#include "common/context.h"
#include "errors.h"
#include "index/ix.h"
#include "record/rm.h"
#include "record/rm_scan.h"
#include "record_printer.h"

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
  if(chdir(db_name.c_str()) < 0) {
    throw UnixError();
  }
  std::ifstream ifs(DB_META_NAME);
  ifs >> db_;

  fhs_.clear();
  ihs_.clear();
  for (const auto &tab_entry : db_.tabs_) {
    const auto &tab_name = tab_entry.first;
    const auto &tab_meta = tab_entry.second;
    fhs_.emplace(tab_name, rm_manager_->open_file(tab_name));
    for (const auto &index_meta : tab_meta.indexes) {
      std::string idx_name = ix_manager_->get_index_name(tab_name, index_meta.cols);
      ihs_.emplace(idx_name, ix_manager_->open_index(tab_name, index_meta.cols));
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
  for (const auto& file : fhs_) {
    rm_manager_->close_file(file.second.get());
  }
  for (const auto& idx_file : ihs_) {
    ix_manager_->close_index(idx_file.second.get());
  }
  fhs_.clear();
  ihs_.clear();
  flush_meta();
}

/**
 * @description: 显示所有的表,通过测试需要将其结果写入到output.txt,详情看题目文档
 * @param {Context*} context 
 */
void SmManager::show_tables(Context* context) {
    std::fstream outfile;
    outfile.open("output.txt", std::ios::out | std::ios::app);
    outfile << "| Tables |\n";
    RecordPrinter printer(1);
    printer.print_separator(context);
    printer.print_record({"Tables"}, context);
    printer.print_separator(context);
    for (auto &entry : db_.tabs_) {
        auto &tab = entry.second;
        printer.print_record({tab.name}, context);
        outfile << "| " << tab.name << " |\n";
    }
    printer.print_separator(context);
    outfile.close();
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
 * @param {Context*} context 
 */
void SmManager::create_table(const std::string& tab_name, const std::vector<ColDef>& col_defs, Context* context) {
    if (db_.is_table(tab_name)) {
        throw TableExistsError(tab_name);
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
  if(!db_.is_table(tab_name)) {
    throw TableNotFoundError(tab_name);
  }

  auto idx_metas = db_.tabs_[tab_name].indexes;
  for (const auto& idx_meta: idx_metas) {
    
    std::string idx_name = ix_manager_->get_index_name(tab_name, idx_meta.cols);
    if(ihs_.count(idx_name)) {
      ix_manager_->close_index(ihs_.at(idx_name).get());
    }
    ihs_.erase(idx_name);
    ix_manager_->destroy_index(tab_name, idx_meta.cols);
  }
  
  if(fhs_.count(tab_name)) {
    rm_manager_->close_file(fhs_.at(tab_name).get());
  }
  rm_manager_->destroy_file(tab_name);
  fhs_.erase(tab_name);
  db_.tabs_.erase(tab_name);
}

/**
 * @description: 创建索引
 * @param {string&} tab_name 表的名称
 * @param {vector<string>&} col_names 索引包含的字段名称
 * @param {Context*} context
 */
void SmManager::create_index(const std::string& tab_name, const std::vector<std::string>& col_names, Context* context) {
  if(!db_.is_table(tab_name)) {
    throw TableNotFoundError(tab_name);
  }
  if (db_.tabs_[tab_name].is_index(col_names)) {
    throw IndexExistsError(tab_name, col_names);
  }
  // Create index meta
  int curr_offset = 0;
  IndexMeta index;
  TabMeta& tab = db_.get_table(tab_name);
  index.tab_name = tab_name;
  for (const std::string& idx_name : col_names) {
    auto target_col = tab.get_col(idx_name);
    index.cols.push_back(*target_col);
    curr_offset += target_col->len;
  }
  index.col_num = col_names.size();
  index.col_tot_len = curr_offset;

  //Check uniqueness
  auto fh = fhs_.at(tab_name).get();
  std::unordered_set<std::string> seen;
  for (RmScan scan(fh); !scan.is_end(); scan.next()) {
    auto record = fh->get_record(scan.rid(), context);
    std::string key(index.col_tot_len, '\0');
    int curr_offset = 0;
    for (const auto& col : index.cols) {
      memcpy(key.data() + curr_offset, record->data + col.offset, col.len);
      curr_offset += col.len;
    }
    if(!seen.insert(key).second) {
      throw RMDBError("Duplicate index key!");
    }
  }

  db_.tabs_[tab_name].indexes.push_back(index);
  std::string idx_name = ix_manager_->get_index_name(tab_name, index.cols);
  ix_manager_->create_index(tab_name, index.cols);
  ihs_.emplace(idx_name, ix_manager_->open_index(tab_name, index.cols));
  auto ih = ihs_.at(idx_name).get();
  for (RmScan scan(fh); !scan.is_end(); scan.next()) {
    auto record = fh->get_record(scan.rid(), context);
    std::string key(index.col_tot_len, '\0');
    int curr_offset = 0;
    for (const auto& col : index.cols) {
      memcpy(key.data() + curr_offset, record->data + col.offset, col.len);
      curr_offset += col.len;
    }
    ih->insert_entry(key.data(), scan.rid(), context == nullptr ? nullptr : context->txn_);
  }

  for (const std::string& idx_name : col_names) {
    auto target_col = tab.get_col(idx_name);
    target_col->index = true;
  }

  flush_meta();
  
}

/**
 * @description: 删除索引
 * @param {string&} tab_name 表名称
 * @param {vector<string>&} col_names 索引包含的字段名称
 * @param {Context*} context
 * Note: 不会修改index属性!
 */
void SmManager::drop_index(const std::string& tab_name, const std::vector<std::string>& col_names, Context* context) {
  if(!ix_manager_->exists(tab_name, col_names)) {
    throw IndexNotFoundError(tab_name, col_names);
  }
  auto it = db_.tabs_[tab_name].get_index_meta(col_names);
  db_.tabs_[tab_name].indexes.erase(it);
  auto index_name = ix_manager_->get_index_name(tab_name, col_names);
  ix_manager_->close_index(ihs_.at(index_name).get());
  ihs_.erase(index_name);
  ix_manager_->destroy_index(tab_name, col_names);

}

/**
 * @description: 删除索引
 * @param {string&} tab_name 表名称
 * @param {vector<ColMeta>&} 索引包含的字段元数据
 * @param {Context*} context
 * 不会修改index属性!
 */
void SmManager::drop_index(const std::string& tab_name, const std::vector<ColMeta>& cols, Context* context) {
  std::vector<std::string> col_names;
  col_names.reserve(cols.size());
  for (const auto& col : cols) {
    col_names.push_back(col.name);
  }
  drop_index(tab_name, col_names, context);
}

/* @description: 显示表索引的元数据
* @param tab_name
* @param context
*/
void SmManager::show_index(const std::string& tab_name, Context* context) {
  std::fstream outfile;
  outfile.open("output.txt", std::ios::out | std::ios::app);
  TabMeta &tab = db_.get_table(tab_name);
  RecordPrinter printer(3);
  printer.print_separator(context);
  printer.print_record({"Table", "Index", "Columns"}, context);
  printer.print_separator(context);
  for (const auto& index : tab.indexes) {
    std::string output_index_str = "(";
    for (auto it = index.cols.begin(); it != index.cols.end(); it++) {
      const auto& col = *it;
      output_index_str += col.name;
      output_index_str += (std::next(it) == index.cols.end()) ? ")" : ",";
    }
    printer.print_record({tab_name, "unique", output_index_str}, context);
    outfile << "| " << tab_name << " | " << "unique" << " | " << output_index_str << " |\n";
  }
  printer.print_separator(context);
  outfile.close();
}
