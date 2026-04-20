#include <stdio.h>
#include <mysql/mysql.h>    // MySQL C API 库
#include <string.h>
#include <openssl/sha.h>    // SHA256 加密库
#include <openssl/rand.h>   // 安全随机数库
#include <stdlib.h>
#include <time.h>
#include "config.h"
#include "auth.h"

/**
 * 将二进制数据转换为十六进制字符串
 * 例如：二进制 {0xAB, 0x12} -> 字符串 "ab12"
 */
void bin_to_hex(const unsigned char *bin, char *hex, int len) {
    for (int i = 0; i < len; i++) {
        // %02x 表示输出 2 位小写十六进制，不足补 0
        sprintf(hex + i * 2, "%02x", bin[i]);
    }
    hex[len * 2] = '\0'; // 字符串结束符
}

/**
 * 生成 16 字节（128位）的随机“盐”值
 * 作用：即使两个用户密码相同，由于盐不同，数据库存的哈希也不同，防止彩虹表攻击。
 */
void generate_salt(char *salt) {
    unsigned char random_bytes[16];
    
    // RAND_bytes 是 OpenSSL 提供的加密级随机数，比 C 标准库的 rand() 更安全
    if (RAND_bytes(random_bytes, sizeof(random_bytes)) != 1) {
        // 如果 OpenSSL 获取失败，则使用系统时间作为种子的普通随机数兜底
        srand(time(NULL));
        for (int i = 0; i < 16; i++) {
            random_bytes[i] = rand() % 256;
        }
    }
    
    // 将 16 字节二进制转为 32 位的十六进制字符串存储
    bin_to_hex(random_bytes, salt, 16);
}

/**
 * 计算：SHA256(用户密码 + 随机盐)
 * 这种方式目前是工业界存储密码的通用安全做法
 */
void hash_password_with_salt(const char *password, const char *salt, char *output) {
    char salted_password[256];
    unsigned char hash[SHA256_DIGEST_LENGTH]; // SHA256 结果固定为 32 字节
    
    // 拼接字符串：将密码和盐组合在一起
    snprintf(salted_password, sizeof(salted_password), "%s%s", password, salt);
    
    // 调用 OpenSSL 接口计算 SHA256 哈希值
    SHA256((unsigned char*)salted_password, strlen(salted_password), hash);
    
    // 将 32 字节哈希值转为 64 位十六进制字符串输出
    bin_to_hex(hash, output, SHA256_DIGEST_LENGTH);
}

/**
 * 登录处理函数
 */

 //接受信息格式  用户名/密码
void handle_login(int client_fd, const char *data, int *user_id) {
    // 1. 数据解析：通过 strtok 以 '/' 拆分用户名和密码
    // 假设输入格式为 "username/password"
    char *user_name = strtok((char *)data, "/");
    char *user_passwd = strtok(NULL, "\r\n\t");

    if (!user_name || !user_passwd) {
        send_msg(client_fd, "error: username or password missing");
        *user_id = -1;
        return;
    }

    // 2. 数据库连接初始化
    MYSQL *conn = mysql_init(NULL);
    if (!mysql_real_connect(conn, "localhost", "root", "", "testdb", 0, NULL, 0)) {
        send_msg(client_fd, "error: database connection failed");
        mysql_close(conn);
        *user_id = -1;
        return;
    }

    // 3. 构建查询语句：根据用户名查找用户的 ID、哈希后的密码和对应的盐
    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT id, password_hash, salt FROM users WHERE username='%s'",
             user_name);
    
    if (mysql_query(conn, sql)) {
        send_msg(client_fd, "error: database query failed");
        mysql_close(conn);
        *user_id = -1;
        return;
    }

    // 4. 获取查询结果
    MYSQL_RES *res = mysql_store_result(conn);
    if (mysql_num_rows(res) == 0) { // 用户名不存在
        send_msg(client_fd, "error: invalid username or password");
        mysql_free_result(res);
        mysql_close(conn);
        *user_id = -1;
        return;
    }

    // 5. 验证密码
    MYSQL_ROW row = mysql_fetch_row(res);
    int db_user_id = atoi(row[0]);
    char *stored_hash = row[1]; // 数据库存的哈希
    char *salt = row[2];        // 数据库存的盐

    char computed_hash[65];
    // 使用用户输入的密码 + 数据库取出的盐，重新计算一次哈希
    hash_password_with_salt(user_passwd, salt, computed_hash);

    // 比对计算结果与数据库存储结果
    if (strcmp(computed_hash, stored_hash) == 0) {
        *user_id = db_user_id;
        char msg[128];
        snprintf(msg, sizeof(msg), "success: login ok, user_id=%d", *user_id);
        send_msg(client_fd, msg);
    } else {
        send_msg(client_fd, "error: invalid username or password");
        *user_id = -1;
    }
    
    // 6. 释放资源
    mysql_free_result(res);
    mysql_close(conn);
}

/**
 * 注册处理函数
 */
void handle_register(int client_fd, const char *data, int *user_id) {
    // 1. 解析用户名和密码
    char *user_name = strtok((char *)data, "/");
    char *user_passwd = strtok(NULL, "\r\n\t");

    if (!user_name || !user_passwd) {
        send_msg(client_fd, "error: username or password missing");
        return;
    }
    
    // 2. 准备加密信息
    char salt[33];         // 存储 32 位盐字符串 + \0
    char password_hash[65]; // 存储 64 位哈希字符串 + \0
    
    generate_salt(salt);   // 随机生成一个盐
    hash_password_with_salt(user_passwd, salt, password_hash); // 计算加盐后的哈希

    // 3. 连接数据库
    MYSQL *conn = mysql_init(NULL);
    if (!mysql_real_connect(conn, "localhost", "root", "", "testdb", 0, NULL, 0)) {
        send_msg(client_fd, "error: database connection failed");
        mysql_close(conn);
        return;
    }

    // 4. 执行插入操作
    char sql[512];
    snprintf(sql, sizeof(sql),
             "INSERT INTO users (username, password_hash, salt) VALUES ('%s', '%s', '%s')",
             user_name, password_hash, salt);
    
    if (mysql_query(conn, sql) == 0) {
        send_msg(client_fd, "success: register ok");
    } else {
        // 如果用户名已存在（数据库中 username 字段应设为 UNIQUE），插入会失败
        send_msg(client_fd, "error: username already exists");
    }
    
    mysql_close(conn);
}