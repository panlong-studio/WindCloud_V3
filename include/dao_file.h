#ifndef _DAO_FILE_H_
#define _DAO_FILE_H_

#include <sys/types.h>

/**
 * @brief  根据 SHA-256 查询 files 表，判断这个真实文件是否已经存在
 * @param  sha256sum 64 位十六进制 SHA-256 字符串
 * @param  out_file_id 输出参数，保存 files 表主键 id
 * @param  out_file_size 输出参数，保存文件大小
 * @return 找到返回 0，找不到返回 -1
 */
int dao_file_find_by_sha256(const char *sha256sum, int *out_file_id, off_t *out_file_size);

/**
 * @brief  往 files 表中插入一条新的真实文件记录
 * @param  sha256sum 64 位十六进制 SHA-256 字符串
 * @param  file_size 文件大小
 * @param  out_file_id 输出参数，保存新插入记录的 id
 * @return 成功返回 0，失败返回 -1
 */
int dao_file_insert(const char *sha256sum, off_t file_size, int *out_file_id);

/**
 * @brief  将 files 表中某个文件的引用计数加 1
 * @param  file_id files 表主键 id
 * @return 成功返回 0，失败返回 -1
 */
int dao_file_add_ref_count(int file_id);

/**
 * @brief  根据 files 表 id 取出真实文件的 SHA-256 和大小
 * @param  file_id files 表主键 id
 * @param  sha256sum_out 输出参数，用来接收 64 位十六进制 SHA-256 字符串
 * @param  out_file_size 输出参数，用来接收文件大小
 * @return 成功返回 0，失败返回 -1
 */
int dao_file_get_info_by_id(int file_id, char *sha256sum_out, off_t *out_file_size);

#endif
