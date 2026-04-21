#ifndef DB_API_H
#define DB_API_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define PATH_MAX_LEN 255

// 客户端会话上下文
typedef struct {
    int user_id;
    char current_path[PATH_MAX_LEN];
    int parent_id;
} ClientContext;

// 目录条目（用于 ls）
typedef struct {
    char name[256];
    bool is_dir;
    int64_t size;
} DirEntry;

// 文件元信息
typedef struct {
    int file_id;
    int64_t size;
    char storage_path[512];
} FileInfo;

// 初始化与清理
bool db_init(void);
void db_cleanup(void);

// 用户认证
bool db_user_auth(const char *username, const char *pass_hash, int *user_id);

// 路径解析
bool db_resolve_path(const ClientContext *ctx, const char *input, char *real_path, size_t size);

// 检查路径是否存在，并判断类型
bool db_path_exists(const ClientContext *ctx, const char *path, bool *is_dir);

// 列出目录内容（调用者需 free(entries)）
DirEntry* db_list_dir(const ClientContext *ctx, const char *path, int *count);

// 创建目录
bool db_mkdir(const ClientContext *ctx, const char *path);

// 删除空目录
bool db_rmdir(const ClientContext *ctx, const char *path);

// 获取文件信息
bool db_file_info(const ClientContext *ctx, const char *path, FileInfo *info);

// 开始上传（返回新文件ID）
bool db_put_begin(const ClientContext *ctx, const char *path, int64_t size, int *file_id);

// 完成上传（成功时更新哈希，失败时清理）
bool db_put_complete(int file_id, bool success, const unsigned char *sha256);

// 删除文件
bool db_rm(const ClientContext *ctx, const char *path);

// 切换目录（更新 ctx）
bool db_cd(ClientContext *ctx, const char *path);

#endif