#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mysql/mysql.h>
#include "dao_file.h"
#include "db_pool.h"

/**
 * @brief  根据 SHA-256 查询 files 表
 * @param  sha256sum 64 位十六进制 SHA-256 字符串
 * @param  out_file_id 输出参数，保存 files.id
 * @param  out_file_size 输出参数，保存 files.size
 * @return 找到返回 0，找不到返回 -1
 */
int dao_file_find_by_sha256(const char *sha256sum, int *out_file_id, off_t *out_file_size) {
    char sql[256];

    snprintf(sql, sizeof(sql),
        "SELECT id, size FROM files WHERE sha256sum=UNHEX('%s')",
        sha256sum);

    MYSQL_RES *res = db_execute_query(sql);
    if (res == NULL) {
        return -1;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (row == NULL) {
        mysql_free_result(res);
        return -1;
    }

    *out_file_id = atoi(row[0]);
    *out_file_size = (off_t)atoll(row[1]);

    mysql_free_result(res);
    return 0;
}

/**
 * @brief  插入新的真实文件记录
 * @param  sha256sum 64 位十六进制 SHA-256 字符串
 * @param  file_size 文件大小
 * @param  out_file_id 输出参数，保存新记录 id
 * @return 成功返回 0，失败返回 -1
 */
int dao_file_insert(const char *sha256sum, off_t file_size, int *out_file_id) {
    char sql[256];

    snprintf(sql, sizeof(sql),
        "INSERT INTO files (sha256sum, size, count) VALUES (UNHEX('%s'), %lld, 1)",
        sha256sum, (long long)file_size);

    if (db_execute_update(sql) != 0) {
        return -1;
    }

    return dao_file_find_by_sha256(sha256sum, out_file_id, &file_size);
}

/**
 * @brief  引用计数加 1
 * @param  file_id files 表主键 id
 * @return 成功返回 0，失败返回 -1
 */
int dao_file_add_ref_count(int file_id) {
    char sql[256];

    snprintf(sql, sizeof(sql),
        "UPDATE files SET count=count+1 WHERE id=%d",
        file_id);

    return db_execute_update(sql);
}

/**
 * @brief  根据 file_id 取出 SHA-256 和文件大小
 * @param  file_id files 表主键 id
 * @param  sha256sum_out 输出参数，保存 64 位十六进制字符串
 * @param  out_file_size 输出参数，保存文件大小
 * @return 成功返回 0，失败返回 -1
 */
int dao_file_get_info_by_id(int file_id, char *sha256sum_out, off_t *out_file_size) {
    char sql[256];

    snprintf(sql, sizeof(sql),
        "SELECT LOWER(HEX(sha256sum)), size FROM files WHERE id=%d",
        file_id);

    MYSQL_RES *res = db_execute_query(sql);
    if (res == NULL) {
        return -1;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (row == NULL) {
        mysql_free_result(res);
        return -1;
    }

    strcpy(sha256sum_out, row[0]);
    *out_file_size = (off_t)atoll(row[1]);

    mysql_free_result(res);
    return 0;
}
