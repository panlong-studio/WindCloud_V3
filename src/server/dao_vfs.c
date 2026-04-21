#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dao_vfs.h"
#include "db_pool.h"
#include "log.h"

int dao_get_node_by_path(int user_id, const char *path, int *out_id, int *out_type) {
    // 根目录 "/" 是一个特殊情况。
    // 当前项目没有把每个用户的根目录真的插到 paths 表中，
    // 所以这里约定：
    // 1. 根目录的 id 当作 0
    // 2. 根目录一定是目录，所以 type=1
    if (strcmp(path, "/") == 0) {
        *out_id = 0; // 根目录的 parent_id 默认为 0
        *out_type = 1; // 1 表示目录
        return 0;
    }

    char sql[512];
    // 用“用户 id + 逻辑路径”唯一定位一个节点。
    // 注意：这里查的是逻辑路径，例如 "/doc/a.txt"，
    // 而不是 Linux 上真正的文件路径。
    snprintf(sql, sizeof(sql), "SELECT id, type FROM paths WHERE user_id=%d AND path='%s'", user_id, path);
    
    MYSQL_RES *res = db_execute_query(sql);
    if (res == NULL) return -1;

    MYSQL_ROW row = mysql_fetch_row(res);
    if (row != NULL) {
        // row[0] 是 id，row[1] 是 type。
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
    // ls 的本质，是查“当前目录的所有孩子节点”。
    // parent_id 就是“当前目录节点 id”。
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
            // 这里顺便用蓝色显示目录，方便客户端一眼看出文件和目录的区别。
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
    // 这是“通用节点创建”函数。
    // 它更适合 mkdir 这种只需要创建目录的场景。
    // 这里不写 file_id，是因为目录节点本来就不关联 files 表，
    // 普通文件如果要关联 files 表，应当走下面的 dao_create_file_node。
    snprintf(sql, sizeof(sql), 
        "INSERT INTO paths (user_id, path, parent_id, file_name, type) VALUES (%d, '%s', %d, '%s', %d)",
        user_id, full_path, parent_id, file_name, type);
    return db_execute_update(sql);
}

int dao_create_file_node(int user_id, const char *full_path, int parent_id, const char *file_name, int file_id) {
    char sql[1024];

    // 这个函数专门给“真实文件已经确定”的普通文件使用。
    // 关键点是把 paths.file_id 填上：
    // 这样逻辑路径就能和真实文件建立连接。
    snprintf(sql, sizeof(sql),
        "INSERT INTO paths (user_id, path, file_id, parent_id, file_name, type) "
        "VALUES (%d, '%s', %d, %d, '%s', 0)",
        user_id, full_path, file_id, parent_id, file_name);

    return db_execute_update(sql);
}

int dao_get_file_info_by_path(int user_id, const char *path, int *out_node_id, int *out_file_id) {
    char sql[512];
    // 下载时的第一跳：
    // 先通过“用户 + 逻辑路径”查出这个逻辑文件节点，
    // 再取出其中的 file_id。
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

    // 这里必须再做一次业务判断：
    // 1. 如果 type 不是 0，说明它是目录，不是普通文件，不能下载
    // 2. 如果 file_id 为空，说明这个逻辑节点还没有关联真实文件
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
    // 只要目录下面还能查到一个子节点，就说明目录非空。
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
    // 删除时同时带上 user_id，避免误删别的用户的数据。
    snprintf(sql, sizeof(sql), "DELETE FROM paths WHERE id=%d AND user_id=%d", node_id, user_id);
    return db_execute_update(sql);
}
