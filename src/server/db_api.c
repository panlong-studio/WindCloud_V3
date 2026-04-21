#include "db_api.h"
#include <mysql/mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static MYSQL *conn = NULL;

// ---------- 辅助函数：执行查询并返回单行结果 ----------
static MYSQL_RES* db_query(const char *sql) {
    if (mysql_query(conn, sql) != 0) return NULL;
    return mysql_store_result(conn);
}

// 根据路径查询记录（内部使用）
static bool query_path(int user_id, const char *path, int *id, int *parent_id, int *file_id, int *type) {
    char sql[512];
    snprintf(sql, sizeof(sql),
        "SELECT id, parent_id, file_id, type FROM paths WHERE user_id=%d AND path='%s'",
        user_id, path);
    MYSQL_RES *res = db_query(sql);
    if (!res) return false;
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) { mysql_free_result(res); return false; }
    if (id)        *id = atoi(row[0]);
    if (parent_id) *parent_id = atoi(row[1]);
    if (file_id)   *file_id = row[2] ? atoi(row[2]) : -1;
    if (type)      *type = atoi(row[3]);
    mysql_free_result(res);
    return true;
}

// ---------- 初始化与清理 ----------
bool db_init(void) {
    conn = mysql_init(NULL);
    return conn && mysql_real_connect(conn, "localhost", "root", "123456", "netdisk", 0, NULL, 0);
}

void db_cleanup(void) {
    if (conn) { mysql_close(conn); conn = NULL; }
}

// ---------- 用户认证 ----------
bool db_user_auth(const char *username, const char *pass_hash, int *user_id) {
    char sql[256];
    snprintf(sql, sizeof(sql),
        "SELECT id FROM users WHERE username='%s' AND password_hash='%s'",
        username, pass_hash);
    MYSQL_RES *res = db_query(sql);
    if (!res) return false;
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) { mysql_free_result(res); return false; }
    *user_id = atoi(row[0]);
    mysql_free_result(res);
    return true;
}

// ---------- 路径解析 ----------
bool db_resolve_path(const ClientContext *ctx, const char *input, char *real_path, size_t size) {
    if (input[0] == '/') {
        strncpy(real_path, input, size-1);
        real_path[size-1] = '\0';
    } else {
        snprintf(real_path, size, "%s/%s", ctx->current_path, input);
    }
    // 简单处理：暂不支持 .. 和 . 规范化
    return true;
}

bool db_path_exists(const ClientContext *ctx, const char *path, bool *is_dir) {
    int type;
    if (!query_path(ctx->user_id, path, NULL, NULL, NULL, &type)) return false;
    *is_dir = (type == 1);
    return true;
}

// ---------- 目录操作 ----------
DirEntry* db_list_dir(const ClientContext *ctx, const char *path, int *count) {
    const char *target = path ? path : ctx->current_path;
    int parent_id;
    if (!query_path(ctx->user_id, target, NULL, &parent_id, NULL, NULL)) return NULL;

    char sql[512];
    snprintf(sql, sizeof(sql),
        "SELECT file_name, type FROM paths WHERE user_id=%d AND parent_id=%d",
        ctx->user_id, parent_id);
    MYSQL_RES *res = db_query(sql);
    if (!res) return NULL;

    int n = mysql_num_rows(res);
    DirEntry *entries = malloc(n * sizeof(DirEntry));
    if (!entries) { mysql_free_result(res); return NULL; }

    for (int i = 0; i < n; i++) {
        MYSQL_ROW row = mysql_fetch_row(res);
        strncpy(entries[i].name, row[0], 255);
        entries[i].is_dir = (atoi(row[1]) == 1);
        entries[i].size = 0;
    }
    *count = n;
    mysql_free_result(res);
    return entries;
}

bool db_mkdir(const ClientContext *ctx, const char *path) {
    bool is_dir;
    if (db_path_exists(ctx, path, &is_dir)) return false; // 已存在

    char parent[PATH_MAX_LEN], name[32];
    const char *slash = strrchr(path, '/');
    if (!slash || slash == path) return false;
    strncpy(parent, path, slash - path);
    parent[slash - path] = '\0';
    strncpy(name, slash+1, 31); name[31] = '\0';

    int parent_id;
    if (!query_path(ctx->user_id, parent, NULL, &parent_id, NULL, NULL)) return false;

    char sql[512];
    snprintf(sql, sizeof(sql),
        "INSERT INTO paths (user_id, path, file_id, parent_id, file_name, type) "
        "VALUES (%d, '%s', NULL, %d, '%s', 1)",
        ctx->user_id, path, parent_id, name);
    return mysql_query(conn, sql) == 0;
}

bool db_rmdir(const ClientContext *ctx, const char *path) {
    int dir_id, type;
    if (!query_path(ctx->user_id, path, &dir_id, NULL, NULL, &type) || type != 1) return false;

    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM paths WHERE parent_id=%d", dir_id);
    MYSQL_RES *res = db_query(sql);
    if (!res) return false;
    MYSQL_ROW row = mysql_fetch_row(res);
    int cnt = row ? atoi(row[0]) : 0;
    mysql_free_result(res);
    if (cnt > 0) return false; // 非空

    snprintf(sql, sizeof(sql), "DELETE FROM paths WHERE id=%d", dir_id);
    return mysql_query(conn, sql) == 0;
}

// ---------- 文件操作 ----------
bool db_file_info(const ClientContext *ctx, const char *path, FileInfo *info) {
    int file_id, type;
    if (!query_path(ctx->user_id, path, NULL, NULL, &file_id, &type) || type != 0) return false;

    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT size FROM files WHERE id=%d", file_id);
    MYSQL_RES *res = db_query(sql);
    if (!res) return false;
    MYSQL_ROW row = mysql_fetch_row(res);
    if (!row) { mysql_free_result(res); return false; }
    info->file_id = file_id;
    info->size = atoll(row[0]);
    snprintf(info->storage_path, sizeof(info->storage_path), "/data/%d.dat", file_id);
    mysql_free_result(res);
    return true;
}

bool db_put_begin(const ClientContext *ctx, const char *path, int64_t size, int *file_id) {
    bool is_dir;
    if (db_path_exists(ctx, path, &is_dir)) return false;

    char parent[PATH_MAX_LEN], name[32];
    const char *slash = strrchr(path, '/');
    strncpy(parent, path, slash - path); parent[slash - path] = '\0';
    strncpy(name, slash+1, 31); name[31] = '\0';

    int parent_id;
    if (!query_path(ctx->user_id, parent, NULL, &parent_id, NULL, NULL)) return false;

    char sql[512];
    snprintf(sql, sizeof(sql), "INSERT INTO files (size, count) VALUES (%lld, 1)", size);
    if (mysql_query(conn, sql) != 0) return false;
    int fid = mysql_insert_id(conn);

    snprintf(sql, sizeof(sql),
        "INSERT INTO paths (user_id, path, file_id, parent_id, file_name, type) "
        "VALUES (%d, '%s', %d, %d, '%s', 0)",
        ctx->user_id, path, fid, parent_id, name);
    if (mysql_query(conn, sql) != 0) {
        snprintf(sql, sizeof(sql), "DELETE FROM files WHERE id=%d", fid);
        mysql_query(conn, sql);
        return false;
    }
    *file_id = fid;
    return true;
}

bool db_put_complete(int file_id, bool success, const unsigned char *sha256) {
    char sql[512];
    if (!success) {
        snprintf(sql, sizeof(sql), "DELETE FROM files WHERE id=%d", file_id);
        mysql_query(conn, sql);
        snprintf(sql, sizeof(sql), "DELETE FROM paths WHERE file_id=%d", file_id);
        mysql_query(conn, sql);
        return true;
    }
    char hex[65];
    for (int i=0; i<32; i++) sprintf(hex+2*i, "%02x", sha256[i]);
    snprintf(sql, sizeof(sql),
        "UPDATE files SET sha256sum=UNHEX('%s') WHERE id=%d", hex, file_id);
    return mysql_query(conn, sql) == 0;
}

bool db_rm(const ClientContext *ctx, const char *path) {
    int file_id, type;
    if (!query_path(ctx->user_id, path, NULL, NULL, &file_id, &type) || type != 0) return false;

    char sql[256];
    snprintf(sql, sizeof(sql), "DELETE FROM paths WHERE user_id=%d AND path='%s'", ctx->user_id, path);
    if (mysql_query(conn, sql) != 0) return false;

    snprintf(sql, sizeof(sql), "UPDATE files SET count = count - 1 WHERE id=%d", file_id);
    mysql_query(conn, sql);

    snprintf(sql, sizeof(sql), "SELECT count FROM files WHERE id=%d", file_id);
    MYSQL_RES *res = db_query(sql);
    if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && atoi(row[0]) == 0) {
            snprintf(sql, sizeof(sql), "DELETE FROM files WHERE id=%d", file_id);
            mysql_query(conn, sql);
        }
        mysql_free_result(res);
    }
    return true;
}

// ---------- 切换目录 ----------
bool db_cd(ClientContext *ctx, const char *path) {
    int dir_id, parent_id, type;
    if (!query_path(ctx->user_id, path, &dir_id, &parent_id, NULL, &type) || type != 1) return false;
    strncpy(ctx->current_path, path, PATH_MAX_LEN-1);
    ctx->parent_id = dir_id;
    return true;
}
