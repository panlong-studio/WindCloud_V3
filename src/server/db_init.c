#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mysql/mysql.h>
#include "db_init.h"
#include "log.h"

int init_database(const char* host, const char* user, const char* pwd, const char* db_name) {
    MYSQL *conn = mysql_init(NULL);
    if (conn == NULL) {
        LOG_ERROR("MySQL 初始化失败");
        return -1;
    }

    // 1. 先不指定数据库名进行连接，为了能执行 CREATE DATABASE
    if (mysql_real_connect(conn, host, user, pwd, NULL, 0, NULL, 0) == NULL) {
        LOG_ERROR("MySQL 连接失败: %s", mysql_error(conn));
        mysql_close(conn);
        return -1;
    }

    // 2. 创建数据库（如果不存在）并设置字符集为 utf8mb4
    char query[256];
    snprintf(query, sizeof(query), "CREATE DATABASE IF NOT EXISTS %s DEFAULT CHARACTER SET utf8mb4", db_name);
    if (mysql_query(conn, query)) {
        LOG_ERROR("创建数据库失败: %s", mysql_error(conn));
        mysql_close(conn);
        return -1;
    }

    // 3. 切换到我们刚才创建/已存在的数据库
    if (mysql_select_db(conn, db_name)) {
        LOG_ERROR("选择数据库失败: %s", mysql_error(conn));
        mysql_close(conn);
        return -1;
    }

    // 4. 定义建表语句（带有 IF NOT EXISTS 和索引优化）
    const char *create_users_table = 
        "CREATE TABLE IF NOT EXISTS users ("
        "id INT AUTO_INCREMENT PRIMARY KEY, "
        "username VARCHAR(20) NOT NULL UNIQUE, "
        "password_hash CHAR(64) NOT NULL, "
        "salt CHAR(32) NOT NULL"
        ") ENGINE=InnoDB;";

    const char *create_files_table = 
        "CREATE TABLE IF NOT EXISTS files ("
        "id INT AUTO_INCREMENT PRIMARY KEY, "
        "sha256sum BINARY(32) NOT NULL UNIQUE, "
        "size BIGINT, "
        "count INT NOT NULL DEFAULT 0"
        ") ENGINE=InnoDB;";

    const char *create_paths_table = 
        "CREATE TABLE IF NOT EXISTS paths ("
        "id INT AUTO_INCREMENT PRIMARY KEY, "
        "user_id INT NOT NULL, "
        "path VARCHAR(255) NOT NULL, "
        "file_id INT NULL, "
        "parent_id INT NOT NULL, "
        "file_name VARCHAR(30) NOT NULL, "
        "type TINYINT NOT NULL, "
        "UNIQUE KEY (user_id, path), "
        "INDEX idx_user_parent (user_id, parent_id)"
        ") ENGINE=InnoDB;";

    // 5. 依次执行建表
    if (mysql_query(conn, create_users_table)) {
        LOG_ERROR("创建 users 表失败: %s", mysql_error(conn));
        mysql_close(conn);
        return -1;
    }
    
    if (mysql_query(conn, create_files_table)) {
        LOG_ERROR("创建 files 表失败: %s", mysql_error(conn));
        mysql_close(conn);
        return -1;
    }

    if (mysql_query(conn, create_paths_table)) {
        LOG_ERROR("创建 paths 表失败: %s", mysql_error(conn));
        mysql_close(conn);
        return -1;
    }

    LOG_INFO("数据库 [%s] 及数据表初始化校验完成", db_name);

    // 6. 初始化完毕，关闭这个临时连接（工作线程后续会创建自己的连接）
    mysql_close(conn);
    return 0;
}