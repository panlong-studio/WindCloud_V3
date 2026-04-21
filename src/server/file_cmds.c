#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "file_cmds.h"
#include "dao_vfs.h"
#include "session.h"
#include "log.h"

// 内部工具函数：逻辑路径拼接 (不查硬盘)
static void build_abs_path(const char *current_path, const char *arg, char *result) {
    if (arg[0] == '/') { // 输入的是绝对路径
        strcpy(result, arg);
        return;
    }
    if (strcmp(current_path, "/") == 0) {
        sprintf(result, "/%s", arg);
    } else {
        sprintf(result, "%s/%s", current_path, arg);
    }
}

// 内部工具函数：获取父级逻辑路径
static void get_parent_path(const char *current_path, char *result) {
    if (strcmp(current_path, "/") == 0) {
        strcpy(result, "/");
        return;
    }
    strcpy(result, current_path);
    char *last_slash = strrchr(result, '/');
    if (last_slash == result) { // 类似 "/abc" -> 返回 "/"
        strcpy(result, "/");
    } else if (last_slash != NULL) { // 类似 "/abc/def" -> 返回 "/abc"
        *last_slash = '\0';
    }
}

// 内部工具函数：从全路径中提取 file_name
static void extract_file_name(const char *full_path, char *file_name) {
    const char *last_slash = strrchr(full_path, '/');
    if (last_slash != NULL) {
        strcpy(file_name, last_slash + 1);
    } else {
        strcpy(file_name, full_path);
    }
}

// ================== 业务命令实现 ==================

void handle_pwd(int client_fd, ClientContext *ctx) {
    LOG_INFO("客户端请求 pwd，fd=%d，当前路径=%s", client_fd, ctx->current_path);
    send_msg(client_fd, ctx->current_path);
}

void handle_ls(int client_fd, ClientContext *ctx) {
    char buf[4096] = {0};
    int ret = dao_list_dir(ctx->user_id, ctx->parent_id, buf);
    
    if (ret < 0) {
        send_msg(client_fd, "服务器查询目录失败");
    } else if (ret == 0) {
        send_msg(client_fd, "当前目录为空");
    } else {
        send_msg(client_fd, buf);
    }
}

void handle_cd(int client_fd, ClientContext *ctx, char *arg) {
    if (arg == NULL || arg[0] == '\0') {
        send_msg(client_fd, "cd 命令缺少参数");
        return;
    }

    char target_path[256] = {0};

    // 处理 cd .. 
    if (strcmp(arg, "..") == 0) {
        get_parent_path(ctx->current_path, target_path);
    } 
    // 处理 cd .
    else if (strcmp(arg, ".") == 0) {
        send_msg(client_fd, "进入目录成功");
        return;
    } 
    // 处理普通目录或绝对路径
    else {
        build_abs_path(ctx->current_path, arg, target_path);
    }

    // 查库判断目标路径是否存在，且必须是目录 (type=1)
    int target_id, node_type;
    if (dao_get_node_by_path(ctx->user_id, target_path, &target_id, &node_type) != 0) {
        send_msg(client_fd, "错误：目录不存在");
        return;
    }

    if (node_type != 1) {
        send_msg(client_fd, "错误：目标不是一个目录");
        return;
    }

    // 更新 Context 状态
    strcpy(ctx->current_path, target_path);
    ctx->parent_id = target_id;
    
    LOG_INFO("cd 成功，当前上下文：用户=%d, 路径=%s, 节点id=%d", ctx->user_id, ctx->current_path, ctx->parent_id);
    send_msg(client_fd, "进入目录成功");
}

void handle_mkdir(int client_fd, ClientContext *ctx, char *arg) {
    char target_path[256] = {0};
    char file_name[64] = {0};
    
    build_abs_path(ctx->current_path, arg, target_path);
    extract_file_name(target_path, file_name);

    int tmp_id, tmp_type;
    if (dao_get_node_by_path(ctx->user_id, target_path, &tmp_id, &tmp_type) == 0) {
        send_msg(client_fd, "错误：该文件或目录已存在");
        return;
    }

    if (dao_create_node(ctx->user_id, target_path, ctx->parent_id, file_name, 1) == 0) {
        send_msg(client_fd, "创建文件夹成功");
    } else {
        send_msg(client_fd, "创建文件夹失败，服务器内部错误");
    }
}

// 新增功能：创建空文件
void handle_touch(int client_fd, ClientContext *ctx, char *arg) {
    char target_path[256] = {0};
    char file_name[64] = {0};
    
    build_abs_path(ctx->current_path, arg, target_path);
    extract_file_name(target_path, file_name);

    int tmp_id, tmp_type;
    if (dao_get_node_by_path(ctx->user_id, target_path, &tmp_id, &tmp_type) == 0) {
        send_msg(client_fd, "错误：该文件或目录已存在");
        return;
    }

    // type = 0 表示创建的是文件
    if (dao_create_node(ctx->user_id, target_path, ctx->parent_id, file_name, 0) == 0) {
        send_msg(client_fd, "创建文件成功");
    } else {
        send_msg(client_fd, "创建文件失败，服务器内部错误");
    }
}

void handle_rm(int client_fd, ClientContext *ctx, char *arg) {
    char target_path[256] = {0};
    build_abs_path(ctx->current_path, arg, target_path);

    int target_id, node_type;
    if (dao_get_node_by_path(ctx->user_id, target_path, &target_id, &node_type) != 0) {
        send_msg(client_fd, "错误：文件不存在");
        return;
    }

    if (node_type == 1) {
        send_msg(client_fd, "错误：目标是目录，请使用 rmdir 命令");
        return;
    }

    if (dao_delete_node(ctx->user_id, target_id) == 0) {
        send_msg(client_fd, "文件删除成功");
    } else {
        send_msg(client_fd, "文件删除失败，数据库异常");
    }
}

// 新增功能：删除目录
void handle_rmdir(int client_fd, ClientContext *ctx, char *arg) {
    char target_path[256] = {0};
    build_abs_path(ctx->current_path, arg, target_path);

    int target_id, node_type;
    if (dao_get_node_by_path(ctx->user_id, target_path, &target_id, &node_type) != 0) {
        send_msg(client_fd, "错误：目录不存在");
        return;
    }

    if (node_type == 0) {
        send_msg(client_fd, "错误：目标是文件，请使用 rm 命令");
        return;
    }

    // 检查目录是否为空
    if (!dao_is_dir_empty(ctx->user_id, target_id)) {
        send_msg(client_fd, "错误：目录非空，无法删除");
        return;
    }

    if (dao_delete_node(ctx->user_id, target_id) == 0) {
        send_msg(client_fd, "空目录删除成功");
    } else {
        send_msg(client_fd, "目录删除失败，数据库异常");
    }
}