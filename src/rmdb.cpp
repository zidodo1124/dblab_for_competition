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
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <vector>

#include "errors.h"
#include "optimizer/optimizer.h"
#include "recovery/log_recovery.h"
#include "optimizer/plan.h"
#include "optimizer/planner.h"
#include "portal.h"
#include "analyze/analyze.h"
#include "common/config.h"

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

bool rmdb_verbose_logging() {
    const char *verbose = std::getenv("RMDB_VERBOSE");
    return verbose != nullptr && std::string(verbose) == "1";
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

std::optional<std::pair<std::string, std::string>> parse_load_sql(const std::string &sql) {
    std::string normalized = sql;
    for (char &ch : normalized) {
        if (ch == ';') {
            ch = ' ';
        }
    }
    std::istringstream iss(normalized);
    std::string load;
    std::string file_name;
    std::string into;
    std::string tab_name;
    std::string extra;
    if (!(iss >> load >> file_name >> into >> tab_name) || (iss >> extra)) {
        return std::nullopt;
    }
    auto lower = [](std::string s) {
        for (char &ch : s) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return s;
    };
    auto valid_identifier = [](const std::string &s) {
        if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) {
            return false;
        }
        for (char ch : s) {
            if (ch != '_' && !std::isalnum(static_cast<unsigned char>(ch))) {
                return false;
            }
        }
        return true;
    };
    auto strip_quotes = [](std::string s) {
        if (s.size() >= 2 && ((s.front() == '\'' && s.back() == '\'') ||
                              (s.front() == '"' && s.back() == '"'))) {
            return s.substr(1, s.size() - 2);
        }
        return s;
    };
    if (lower(load) != "load" || lower(into) != "into" || !valid_identifier(tab_name)) {
        return std::nullopt;
    }
    return std::make_pair(strip_quotes(file_name), tab_name);
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

std::string normalized_command(const char *sql) {
    std::string s = sql == nullptr ? "" : sql;
    for (char &ch : s) {
        if (ch == ';') {
            ch = ' ';
        } else {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
    }
    std::istringstream iss(s);
    std::ostringstream os;
    std::string word;
    bool first = true;
    while (iss >> word) {
        if (!first) {
            os << ' ';
        }
        os << word;
        first = false;
    }
    return os.str();
}

bool handle_fast_utility_command(const char *sql, char *data_send, int &offset) {
    std::string command = normalized_command(sql);
    if (command == "set output_file off") {
        output_file_enabled.store(false, std::memory_order_relaxed);
        data_send[0] = '\0';
        offset = 0;
        return true;
    }
    if (command == "set output_file on") {
        output_file_enabled.store(true, std::memory_order_relaxed);
        data_send[0] = '\0';
        offset = 0;
        return true;
    }
    return false;
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

        if (output_file_enabled.load(std::memory_order_relaxed)) {
            std::fstream outfile;
            outfile.open("output.txt", std::ios::out | std::ios::app);
            outfile << str;
            outfile.close();
        }
    } catch (RMDBError &e) {
        std::cerr << e.what() << std::endl;

        if (context->txn_ != nullptr &&
            context->txn_->get_state() != TransactionState::COMMITTED &&
            context->txn_->get_state() != TransactionState::ABORTED) {
            txn_manager->abort(context->txn_, log_manager.get());
        }

        memcpy(data_send, e.what(), e.get_msg_len());
        data_send[e.get_msg_len()] = '\n';
        data_send[e.get_msg_len() + 1] = '\0';
        offset = e.get_msg_len() + 1;

        if (output_file_enabled.load(std::memory_order_relaxed)) {
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

    bool verbose = rmdb_verbose_logging();
    if (verbose) {
        std::string output = "establish client connection, sockfd: " + std::to_string(fd) + "\n";
        std::cout << output;
    }

    while (true) {
        if (verbose) {
            std::cout << "Waiting for request..." << std::endl;
        }
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
        
        if (verbose) {
            printf("i_recvBytes: %d \n ", i_recvBytes);
        }

        if (strcmp(data_recv, "exit") == 0) {
            std::cout << "Client exit." << std::endl;
            break;
        }
        if (strcmp(data_recv, "crash") == 0) {
            std::cout << "Server crash" << std::endl;
            exit(1);
        }

        if (verbose) {
            std::cout << "Read from client " << fd << ": " << data_recv << std::endl;
        }

        memset(data_send, '\0', BUFFER_LENGTH);
        offset = 0;

        if (handle_fast_utility_command(data_recv, data_send, offset)) {
            if (write(fd, data_send, offset + 1) == -1) {
                break;
            }
            continue;
        }

        // 开启事务，初始化系统所需的上下文信息（包括事务对象指针、锁管理器指针、日志管理器指针、存放结果的buffer、记录结果长度的变量）
        Context *context = new Context(lock_manager.get(), log_manager.get(), nullptr, data_send, &offset,
                                       txn_manager.get(), &session_isolation);
        SetTransaction(&txn_id, context, session_isolation);

        // 用于判断是否已经调用了yy_delete_buffer来删除buf
        bool finish_analyze = false;
        auto load_command = parse_load_sql(data_recv);
        auto show_index_table = parse_show_index_sql(data_recv);
        if (load_command.has_value()) {
            finish_analyze = true;
            try {
                sm_manager->load_table(load_command->first, load_command->second, context);
            } catch (RMDBError &e) {
                std::cerr << e.what() << std::endl;
                memcpy(data_send, e.what(), e.get_msg_len());
                data_send[e.get_msg_len()] = '\n';
                data_send[e.get_msg_len() + 1] = '\0';
                offset = e.get_msg_len() + 1;
                if (output_file_enabled.load(std::memory_order_relaxed)) {
                    std::fstream outfile;
                    outfile.open("output.txt", std::ios::out | std::ios::app);
                    outfile << "failure\n";
                    outfile.close();
                }
            }
        } else if (show_index_table.has_value()) {
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
                    if (output_file_enabled.load(std::memory_order_relaxed)) {
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
    if (verbose) {
        std::cout << "Terminating current client_connection..." << std::endl;
    }
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
        if (rmdb_verbose_logging()) {
            std::cout << "Waiting for new connection..." << std::endl;
        }
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
