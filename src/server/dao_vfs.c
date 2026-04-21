#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dao_vfs.h"
#include "db_pool.h"
#include "log.h"

int dao_get_node_by_path(int user_id, const char *path, int *out_id, int *out_type) {
    if (strcmp(path, "/") == 0) {
        *out_id = 0; // 根目录的 parent_id 默认为 0
        *out_type = 1; // 1 表示目录
        return 0;
    }

    char sql[512];
    snprintf(sql, sizeof(sql), "SELECT id, type FROM paths WHERE user_id=%d AND path='%s'", user_id, path);
    
    MYSQL_RES *res = db_execute_query(sql);
    if (res == NULL) return -1;

    MYSQL_ROW row = mysql_fetch_row(res);
    if (row != NULL) {
        *out_id = atoi(row[0]);
        *out_type = atoi(row[1]);
        mysql_free_result(res);
        return 0;
    }
    
    mysql_free_result(res);
    return -1;
}

int dao_list_dir(int user_id, int parent_id, char *output_buf) {
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT file_name, type FROM paths WHERE user_id=%d AND parent_id=%d", user_id, parent_id);
    
    MYSQL_RES *res = db_execute_query(sql);
    if (res == NULL) return -1;

    int used = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != NULL) {
        // row[0] 是 file_name, row[1] 是 type
        const char *name = row[0];
        int type = atoi(row[1]);
        
        // 目录加 / 后缀以示区分
        if (type == 1) {
            used += sprintf(output_buf + used, "\033[1;34m%s/\033[0m ", name); // 蓝色带斜杠
        } else {
            used += sprintf(output_buf + used, "%s ", name);
        }
    }
    mysql_free_result(res);
    return used; // 返回拼接了多少个字符，0表示空目录
}

int dao_create_node(int user_id, const char *full_path, int parent_id, const char *file_name, int type) {
    char sql[1024];
    // 这里的 file_id 传 NULL，因为还没做文件实体关联。
    snprintf(sql, sizeof(sql), 
        "INSERT INTO paths (user_id, path, parent_id, file_name, type) VALUES (%d, '%s', %d, '%s', %d)",
        user_id, full_path, parent_id, file_name, type);
    return db_execute_update(sql);
}

int dao_create_file_node(int user_id, const char *full_path, int parent_id, const char *file_name, int file_id) {
    char sql[1024];

    snprintf(sql, sizeof(sql),
        "INSERT INTO paths (user_id, path, file_id, parent_id, file_name, type) "
        "VALUES (%d, '%s', %d, %d, '%s', 0)",
        user_id, full_path, file_id, parent_id, file_name);

    return db_execute_update(sql);
}

int dao_get_file_info_by_path(int user_id, const char *path, int *out_node_id, int *out_file_id) {
    char sql[512];
    snprintf(sql, sizeof(sql),
        "SELECT id, file_id, type FROM paths WHERE user_id=%d AND path='%s'",
        user_id, path);

    MYSQL_RES *res = db_execute_query(sql);
    if (res == NULL) {
        return -1;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (row == NULL) {
        mysql_free_result(res);
        return -1;
    }

    if (atoi(row[2]) != 0 || row[1] == NULL) {
        mysql_free_result(res);
        return -1;
    }

    *out_node_id = atoi(row[0]);
    *out_file_id = atoi(row[1]);

    mysql_free_result(res);
    return 0;
}

int dao_is_dir_empty(int user_id, int dir_id) {
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT id FROM paths WHERE user_id=%d AND parent_id=%d LIMIT 1", user_id, dir_id);
    MYSQL_RES *res = db_execute_query(sql);
    if (res == NULL) return 1; // 查库失败当作空处理
    
    MYSQL_ROW row = mysql_fetch_row(res);
    int empty = (row == NULL) ? 1 : 0;
    mysql_free_result(res);
    return empty;
}

int dao_delete_node(int user_id, int node_id) {
    char sql[256];
    snprintf(sql, sizeof(sql), "DELETE FROM paths WHERE id=%d AND user_id=%d", node_id, user_id);
    return db_execute_update(sql);
}
