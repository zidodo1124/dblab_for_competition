/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include <netinet/in.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "errors.h"
#include "optimizer/optimizer.h"
#include "recovery/log_recovery.h"
#include "optimizer/plan.h"
#include "optimizer/planner.h"
#include "portal.h"
#include "analyze/analyze.h"

#define SOCK_PORT 8765
#define MAX_CONN_LIMIT 8

static bool should_exit = false;

// 构建全局所需的管理器对象
auto disk_manager = std::make_unique<DiskManager>();
auto buffer_pool_manager = std::make_unique<BufferPoolManager>(BUFFER_POOL_SIZE, disk_manager.get());
auto rm_manager = std::make_unique<RmManager>(disk_manager.get(), buffer_pool_manager.get());
auto ix_manager = std::make_unique<IxManager>(disk_manager.get(), buffer_pool_manager.get());
auto sm_manager = std::make_unique<SmManager>(disk_manager.get(), buffer_pool_manager.get(), rm_manager.get(), ix_manager.get());
auto lock_manager = std::make_unique<LockManager>();
auto txn_manager = std::make_unique<TransactionManager>(lock_manager.get(), sm_manager.get());
auto planner = std::make_unique<Planner>(sm_manager.get());
auto optimizer = std::make_unique<Optimizer>(sm_manager.get(), planner.get());
auto ql_manager = std::make_unique<QlManager>(sm_manager.get(), txn_manager.get(), planner.get());
auto log_manager = std::make_unique<LogManager>(disk_manager.get());
auto recovery = std::make_unique<RecoveryManager>(disk_manager.get(), buffer_pool_manager.get(), sm_manager.get());
auto portal = std::make_unique<Portal>(sm_manager.get());
auto analyze = std::make_unique<Analyze>(sm_manager.get());
pthread_mutex_t *buffer_mutex;
pthread_mutex_t *sockfd_mutex;

static jmp_buf jmpbuf;

std::string trim_copy(const std::string &s) {
    auto begin = std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    });
    auto end = std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base();
    if (begin >= end) {
        return "";
    }
    return std::string(begin, end);
}

std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return s;
}

bool parse_output_file_command(const std::string &sql, bool *enabled) {
    std::string normalized = trim_copy(sql);
    if (!normalized.empty() && normalized.back() == ';') {
        normalized.pop_back();
    }
    std::istringstream iss(normalized);
    std::string set_kw;
    std::string output_kw;
    std::string value_kw;
    std::string extra;
    if (!(iss >> set_kw >> output_kw >> value_kw) || (iss >> extra)) {
        return false;
    }
    if (lower_copy(set_kw) != "set" || lower_copy(output_kw) != "output_file") {
        return false;
    }
    std::string value = lower_copy(value_kw);
    if (value == "off" || value == "false" || value == "0") {
        *enabled = false;
        return true;
    }
    if (value == "on" || value == "true" || value == "1") {
        *enabled = true;
        return true;
    }
    return false;
}

std::optional<std::pair<std::string, std::string>> parse_load_sql(const std::string &sql) {
    std::string normalized = trim_copy(sql);
    if (!normalized.empty() && normalized.back() == ';') {
        normalized.pop_back();
    }
    std::istringstream iss(normalized);
    std::string load_kw;
    std::string file_name;
    std::string into_kw;
    std::string table_name;
    std::string extra;
    if (!(iss >> load_kw >> file_name >> into_kw >> table_name) || (iss >> extra)) {
        return std::nullopt;
    }
    if (lower_copy(load_kw) != "load" || lower_copy(into_kw) != "into") {
        return std::nullopt;
    }
    return std::make_pair(file_name, table_name);
}

std::vector<std::string> parse_csv_line(const std::string &line) {
    std::vector<std::string> fields;
    std::string cur;
    bool in_quote = false;
    char quote_char = '\0';
    for (size_t i = 0; i < line.size(); ++i) {
        char ch = line[i];
        if (in_quote) {
            if (ch == quote_char) {
                if (i + 1 < line.size() && line[i + 1] == quote_char) {
                    cur.push_back(ch);
                    ++i;
                } else {
                    in_quote = false;
                }
            } else {
                cur.push_back(ch);
            }
        } else if ((ch == '\'' || ch == '"') && trim_copy(cur).empty()) {
            in_quote = true;
            quote_char = ch;
        } else if (ch == ',') {
            fields.push_back(trim_copy(cur));
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    fields.push_back(trim_copy(cur));
    return fields;
}

std::vector<char> make_index_key_from_record(const RmRecord &rec, const IndexMeta &index) {
    std::vector<char> key(index.col_tot_len);
    int offset = 0;
    for (int i = 0; i < index.col_num; ++i) {
        memcpy(key.data() + offset, rec.data + index.cols[i].offset, index.cols[i].len);
        offset += index.cols[i].len;
    }
    return key;
}

void load_csv_into_table(const std::string &file_name, const std::string &table_name, Context *context) {
    TabMeta &tab = sm_manager->db_.get_table(table_name);
    RmFileHandle *fh = sm_manager->fhs_.at(table_name).get();
    std::ifstream input(file_name);
    if (!input.is_open()) {
        throw UnixError();
    }

    auto write_guard = txn_manager->acquire_write_latch();
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        std::vector<std::string> fields = parse_csv_line(line);
        if (fields.size() != tab.cols.size()) {
            throw InvalidValueCountError();
        }

        RmRecord rec(fh->get_file_hdr().record_size);
        for (size_t i = 0; i < tab.cols.size(); ++i) {
            const auto &col = tab.cols[i];
            const std::string &field = fields[i];
            char *dest = rec.data + col.offset;
            if (col.type == TYPE_INT) {
                int value = std::stoi(field);
                memcpy(dest, &value, sizeof(int));
            } else if (col.type == TYPE_FLOAT) {
                float value = std::stof(field);
                memcpy(dest, &value, sizeof(float));
            } else {
                std::string value = field;
                if (value.size() > static_cast<size_t>(col.len)) {
                    throw StringOverflowError();
                }
                memset(dest, 0, col.len);
                memcpy(dest, value.data(), value.size());
            }
        }

        std::vector<std::vector<char>> keys;
        keys.reserve(tab.indexes.size());
        for (auto &index : tab.indexes) {
            auto ih = sm_manager->ihs_.at(sm_manager->get_ix_manager()->get_index_name(table_name, index.cols)).get();
            auto key = make_index_key_from_record(rec, index);
            std::vector<Rid> existing;
            if (ih->get_value(key.data(), &existing, context == nullptr ? nullptr : context->txn_)) {
                throw UniqueConstraintViolationError(table_name);
            }
            keys.push_back(std::move(key));
        }

        Rid rid = fh->insert_record(rec.data, nullptr);
        for (size_t i = 0; i < tab.indexes.size(); ++i) {
            auto &index = tab.indexes[i];
            auto ih = sm_manager->ihs_.at(sm_manager->get_ix_manager()->get_index_name(table_name, index.cols)).get();
            ih->insert_entry(keys[i].data(), rid, context == nullptr ? nullptr : context->txn_);
        }
    }

    fh->flush_file_hdr();
    buffer_pool_manager->flush_dirty_pages(std::vector<int>{fh->GetFd()}, false);
    for (auto &index : tab.indexes) {
        auto ih = sm_manager->ihs_.at(sm_manager->get_ix_manager()->get_index_name(table_name, index.cols)).get();
        ih->flush_file_hdr();
        buffer_pool_manager->flush_dirty_pages(std::vector<int>{ih->get_fd()}, false);
    }
}

std::optional<std::string> parse_show_index_sql(const std::string &sql) {
    std::string normalized = sql;
    for (char &ch : normalized) {
        if (ch == ';') {
            ch = ' ';
        }
    }
    std::istringstream iss(normalized);
    std::string show;
    std::string index;
    std::string from;
    std::string tab_name;
    std::string extra;
    if (!(iss >> show >> index >> from >> tab_name) || (iss >> extra)) {
        return std::nullopt;
    }
    auto lower = [](std::string s) {
        for (char &ch : s) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return s;
    };
    auto valid_identifier = [](const std::string &s) {
        if (s.empty() || !std::isalpha(static_cast<unsigned char>(s[0]))) {
            return false;
        }
        for (char ch : s) {
            if (ch != '_' && !std::isalnum(static_cast<unsigned char>(ch))) {
                return false;
            }
        }
        return true;
    };
    if (lower(show) == "show" && lower(index) == "index" && lower(from) == "from" && valid_identifier(tab_name)) {
        return tab_name;
    }
    return std::nullopt;
}

bool should_report_parse_failure(const char *sql) {
    std::string s = sql == nullptr ? "" : sql;
    auto begin = std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    });
    if (begin == s.end()) {
        return false;
    }
    std::string trimmed(begin, s.end());
    return trimmed.rfind("--", 0) != 0;
}

void sigint_handler(int signo) {
    should_exit = true;
    log_manager->flush_log_to_disk();
    std::cout << "The Server receive Crtl+C, will been closed\n";
    longjmp(jmpbuf, 1);
}

// 判断当前正在执行的是显式事务还是单条SQL语句的事务，并更新事务ID
void SetTransaction(txn_id_t *txn_id, Context *context, IsolationLevel session_isolation) {
    context->txn_ = txn_manager->get_transaction(*txn_id);
    if(context->txn_ == nullptr || context->txn_->get_state() == TransactionState::COMMITTED ||
        context->txn_->get_state() == TransactionState::ABORTED) {
        context->txn_ = txn_manager->begin(nullptr, context->log_mgr_);
        context->txn_->set_isolation_level(session_isolation);
        *txn_id = context->txn_->get_transaction_id();
        context->txn_->set_txn_mode(false);
    }
}

void execute_parse_tree(std::shared_ptr<ast::TreeNode> parse_tree, char *data_send, int &offset, txn_id_t *txn_id,
                        Context *context) {
    try {
        std::shared_ptr<Query> query = analyze->do_analyze(std::move(parse_tree));
        std::shared_ptr<Plan> plan = optimizer->plan_query(query, context);
        std::shared_ptr<PortalStmt> portalStmt = portal->start(plan, context);
        portal->run(portalStmt, ql_manager.get(), txn_id, context);
        portal->drop();
    } catch (TransactionAbortException &e) {
        std::string str = "abort\n";
        memcpy(data_send, str.c_str(), str.length());
        data_send[str.length()] = '\0';
        offset = str.length();

        txn_manager->abort(context->txn_, log_manager.get());
        std::cout << e.GetInfo() << std::endl;

        if (output_file_enabled()) {
            std::fstream outfile;
            outfile.open("output.txt", std::ios::out | std::ios::app);
            outfile << str;
            outfile.close();
        }
    } catch (RMDBError &e) {
        std::cerr << e.what() << std::endl;

        memcpy(data_send, e.what(), e.get_msg_len());
        data_send[e.get_msg_len()] = '\n';
        data_send[e.get_msg_len() + 1] = '\0';
        offset = e.get_msg_len() + 1;

        if (output_file_enabled()) {
            std::fstream outfile;
            outfile.open("output.txt",std::ios::out | std::ios::app);
            outfile << "failure\n";
            outfile.close();
        }
    }
}

void *client_handler(void *sock_fd) {
    int fd = *((int *)sock_fd);
    pthread_mutex_unlock(sockfd_mutex);

    int i_recvBytes;
    // 接收客户端发送的请求
    char data_recv[BUFFER_LENGTH];
    // 需要返回给客户端的结果
    char *data_send = new char[BUFFER_LENGTH];
    // 需要返回给客户端的结果的长度
    int offset = 0;
    // 记录客户端当前正在执行的事务ID
    txn_id_t txn_id = INVALID_TXN_ID;
    IsolationLevel session_isolation = IsolationLevel::SERIALIZABLE;

    std::string output = "establish client connection, sockfd: " + std::to_string(fd) + "\n";
    std::cout << output;

    while (true) {
        std::cout << "Waiting for request..." << std::endl;
        memset(data_recv, 0, BUFFER_LENGTH);

        i_recvBytes = read(fd, data_recv, BUFFER_LENGTH);

        if (i_recvBytes == 0) {
            std::cout << "Maybe the client has closed" << std::endl;
            break;
        }
        if (i_recvBytes == -1) {
            std::cout << "Client read error!" << std::endl;
            break;
        }
        
        printf("i_recvBytes: %d \n ", i_recvBytes);

        if (strcmp(data_recv, "exit") == 0) {
            std::cout << "Client exit." << std::endl;
            break;
        }
        if (strcmp(data_recv, "crash") == 0) {
            std::cout << "Server crash" << std::endl;
            exit(1);
        }

        std::cout << "Read from client " << fd << ": " << data_recv << std::endl;

        memset(data_send, '\0', BUFFER_LENGTH);
        offset = 0;

        // 开启事务，初始化系统所需的上下文信息（包括事务对象指针、锁管理器指针、日志管理器指针、存放结果的buffer、记录结果长度的变量）
        Context *context = new Context(lock_manager.get(), log_manager.get(), nullptr, data_send, &offset,
                                       txn_manager.get(), &session_isolation);
        SetTransaction(&txn_id, context, session_isolation);

        bool output_file_on = true;
        if (parse_output_file_command(data_recv, &output_file_on)) {
            set_output_file_enabled(output_file_on);
            if(context->txn_->get_txn_mode() == false &&
               context->txn_->get_state() != TransactionState::COMMITTED &&
               context->txn_->get_state() != TransactionState::ABORTED)
            {
                txn_manager->commit(context->txn_, context->log_mgr_);
            }
            if (write(fd, data_send, offset + 1) == -1) {
                break;
            }
            continue;
        }

        if (auto load_stmt = parse_load_sql(data_recv)) {
            try {
                load_csv_into_table(load_stmt->first, load_stmt->second, context);
            } catch (RMDBError &e) {
                std::cerr << e.what() << std::endl;
                std::string str = "failure\n";
                memcpy(data_send, str.c_str(), str.length());
                data_send[str.length()] = '\0';
                offset = str.length();
                if (output_file_enabled()) {
                    std::fstream outfile;
                    outfile.open("output.txt", std::ios::out | std::ios::app);
                    outfile << str;
                    outfile.close();
                }
            } catch (std::exception &e) {
                std::cerr << e.what() << std::endl;
                std::string str = "failure\n";
                memcpy(data_send, str.c_str(), str.length());
                data_send[str.length()] = '\0';
                offset = str.length();
                if (output_file_enabled()) {
                    std::fstream outfile;
                    outfile.open("output.txt", std::ios::out | std::ios::app);
                    outfile << str;
                    outfile.close();
                }
            }
            if(context->txn_->get_txn_mode() == false &&
               context->txn_->get_state() != TransactionState::COMMITTED &&
               context->txn_->get_state() != TransactionState::ABORTED)
            {
                txn_manager->commit(context->txn_, context->log_mgr_);
            }
            if (write(fd, data_send, offset + 1) == -1) {
                break;
            }
            continue;
        }

        // 用于判断是否已经调用了yy_delete_buffer来删除buf
        bool finish_analyze = false;
        auto show_index_table = parse_show_index_sql(data_recv);
        if (show_index_table.has_value()) {
            auto parse_tree = std::make_shared<ast::ShowIndex>(*show_index_table);
            finish_analyze = true;
            execute_parse_tree(std::move(parse_tree), data_send, offset, &txn_id, context);
        } else {
            pthread_mutex_lock(buffer_mutex);
            ast::parse_tree.reset();
            YY_BUFFER_STATE buf = yy_scan_string(data_recv);
            if (yyparse() == 0) {
                if (ast::parse_tree != nullptr) {
                    auto parse_tree = ast::parse_tree;
                    ast::parse_tree.reset();
                    yy_delete_buffer(buf);
                    finish_analyze = true;
                    pthread_mutex_unlock(buffer_mutex);
                    execute_parse_tree(std::move(parse_tree), data_send, offset, &txn_id, context);
                }
            }
            if(finish_analyze == false) {
                ast::parse_tree.reset();
                yy_delete_buffer(buf);
                pthread_mutex_unlock(buffer_mutex);
                if (should_report_parse_failure(data_recv)) {
                    std::string str = "failure\n";
                    memcpy(data_send, str.c_str(), str.length());
                    data_send[str.length()] = '\0';
                    offset = str.length();
                    if (output_file_enabled()) {
                        std::fstream outfile;
                        outfile.open("output.txt", std::ios::out | std::ios::app);
                        outfile << str;
                        outfile.close();
                    }
                }
            }
        }
        // future TODO: 格式化 sql_handler.result, 传给客户端
        // send result with fixed format, use protobuf in the future
        if (write(fd, data_send, offset + 1) == -1) {
            break;
        }
        // 如果是单挑语句，需要按照一个完整的事务来执行，所以执行完当前语句后，自动提交事务
        if(context->txn_->get_txn_mode() == false &&
           context->txn_->get_state() != TransactionState::COMMITTED &&
           context->txn_->get_state() != TransactionState::ABORTED)
        {
            txn_manager->commit(context->txn_, context->log_mgr_);
        }
    }

    // Clear
    std::cout << "Terminating current client_connection..." << std::endl;
    close(fd);           // close a file descriptor.
    pthread_exit(NULL);  // terminate calling thread!
}

void start_server() {
    // init mutex
    buffer_mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    sockfd_mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(buffer_mutex, nullptr);
    pthread_mutex_init(sockfd_mutex, nullptr);

    int sockfd_server;
    int fd_temp;
    struct sockaddr_in s_addr_in {};

    // 初始化连接
    sockfd_server = socket(AF_INET, SOCK_STREAM, 0);  // ipv4,TCP
    assert(sockfd_server != -1);
    int val = 1;
    setsockopt(sockfd_server, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

    // before bind(), set the attr of structure sockaddr.
    memset(&s_addr_in, 0, sizeof(s_addr_in));
    s_addr_in.sin_family = AF_INET;
    s_addr_in.sin_addr.s_addr = htonl(INADDR_ANY);
    int sock_port = SOCK_PORT;
    if (const char *env_port = std::getenv("RMDB_PORT")) {
        int parsed_port = std::atoi(env_port);
        if (parsed_port > 0 && parsed_port <= 65535) {
            sock_port = parsed_port;
        }
    }
    s_addr_in.sin_port = htons(sock_port);
    fd_temp = bind(sockfd_server, (struct sockaddr *)(&s_addr_in), sizeof(s_addr_in));
    if (fd_temp == -1) {
        std::cout << "Bind error!" << std::endl;
        exit(1);
    }

    fd_temp = listen(sockfd_server, MAX_CONN_LIMIT);
    if (fd_temp == -1) {
        std::cout << "Listen error!" << std::endl;
        exit(1);
    }

    while (!should_exit) {
        std::cout << "Waiting for new connection..." << std::endl;
        pthread_t thread_id;
        struct sockaddr_in s_addr_client {};
        int client_length = sizeof(s_addr_client);

        if (setjmp(jmpbuf)) {
            std::cout << "Break from Server Listen Loop\n";
            break;
        }

        // Block here. Until server accepts a new connection.
        pthread_mutex_lock(sockfd_mutex);
        int sockfd = accept(sockfd_server, (struct sockaddr *)(&s_addr_client), (socklen_t *)(&client_length));
        if (sockfd == -1) {
            std::cout << "Accept error!" << std::endl;
            continue;  // ignore current socket ,continue while loop.
        }
        
        // 和客户端建立连接，并开启一个线程负责处理客户端请求
        if (pthread_create(&thread_id, nullptr, &client_handler, (void *)(&sockfd)) != 0) {
            std::cout << "Create thread fail!" << std::endl;
            break;  // break while loop
        }

    }

    // Clear
    std::cout << " Try to close all client-connection.\n";
    int ret = shutdown(sockfd_server, SHUT_WR);  // shut down the all or part of a full-duplex connection.
    if(ret == -1) { printf("%s\n", strerror(errno)); }
//    assert(ret != -1);
    sm_manager->close_db();
    std::cout << " DB has been closed.\n";
    std::cout << "Server shuts down." << std::endl;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        // 需要指定数据库名称
        std::cerr << "Usage: " << argv[0] << " <database>" << std::endl;
        exit(1);
    }

    signal(SIGINT, sigint_handler);
    try {
        std::cout << "\n"
                     "  _____  __  __ _____  ____  \n"
                     " |  __ \\|  \\/  |  __ \\|  _ \\ \n"
                     " | |__) | \\  / | |  | | |_) |\n"
                     " |  _  /| |\\/| | |  | |  _ < \n"
                     " | | \\ \\| |  | | |__| | |_) |\n"
                     " |_|  \\_\\_|  |_|_____/|____/ \n"
                     "\n"
                     "Welcome to RMDB!\n"
                     "Type 'help;' for help.\n"
                     "\n";
        // Database name is passed by args
        std::string db_name = argv[1];
        if (!sm_manager->is_dir(db_name)) {
            // Database not found, create a new one
            sm_manager->create_db(db_name);
        }
        buffer_pool_manager->set_log_manager(log_manager.get());
        // Open database
        sm_manager->open_db(db_name);

        // recovery database
        recovery->analyze();
        recovery->redo();
        recovery->undo();
        
        // 开启服务端，开始接受客户端连接
        start_server();
    } catch (RMDBError &e) {
        std::cerr << e.what() << std::endl;
        exit(1);
    }
    return 0;
}
