#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include "file_cmds.h"
#include "path_utils.h"
#include "session.h"
#include "log.h"

void handle_cd(int client_fd, char *current_path, char *arg) {
    if (arg == NULL || arg[0] == '\0') {
        LOG_WARN("切换目录缺少参数，客户端fd=%d", client_fd);
        send_msg(client_fd, "输入错误");
        return;
    }

    if (strcmp(arg, "..") == 0) {
        strcpy(current_path, "/");
        LOG_INFO("客户端切换回根目录，客户端fd=%d", client_fd);
        send_msg(client_fd, "已返回根目录");
        return;
    }

    char real_path[MAX_PATH_LEN] = {0};
    if (get_real_path(real_path, sizeof(real_path), current_path, arg) == -1) {
        LOG_WARN("切换目录路径非法，客户端fd=%d，当前路径=%s，参数=%s", client_fd, current_path, arg);
        send_msg(client_fd, "路径非法或过长");
        return;
    }

    DIR *dir = opendir(real_path);
    if (dir == NULL) {
        LOG_WARN("切换目录失败，客户端fd=%d，路径=%s，错误码=%d", client_fd, real_path, errno);
        send_msg(client_fd, "目录不存在！");
        return;
    }
    closedir(dir);

    if (update_current_path(current_path, 512, arg) == -1) {
        LOG_WARN("切换目录路径过长，客户端fd=%d，当前路径=%s，参数=%s", client_fd, current_path, arg);
        send_msg(client_fd, "路径过长");
        return;
    }

    LOG_INFO("切换目录成功，客户端fd=%d，当前路径=%s", client_fd, current_path);
    send_msg(client_fd, "进入目录成功");
}

void handle_ls(int client_fd, char *current_path) {
    char real_path[MAX_PATH_LEN] = {0};
    if (get_current_real_path(real_path, sizeof(real_path), current_path) == -1) {
        LOG_WARN("列目录路径过长，客户端fd=%d，当前路径=%s", client_fd, current_path);
        send_msg(client_fd, "路径过长");
        return;
    }

    DIR *dir = opendir(real_path);
    if (dir == NULL) {
        LOG_WARN("列目录失败，客户端fd=%d，路径=%s，错误码=%d", client_fd, real_path, errno);
        send_msg(client_fd, "目录打开失败");
        return;
    }

    struct dirent *file = NULL;
    char result[4096] = {0};
    int used = 0;

    while ((file = readdir(dir)) != NULL) {
        if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0) {
            continue;
        }

        int left = (int)sizeof(result) - used;
        int ret = snprintf(result + used, left, "%s ", file->d_name);

        if (ret < 0 || ret >= left) break;
        used += ret;
    }
    closedir(dir);

    if (used == 0) {
        LOG_INFO("目录为空，客户端fd=%d，路径=%s", client_fd, current_path);
        send_msg(client_fd, "当前目录为空");
        return;
    }

    LOG_INFO("列目录成功，客户端fd=%d，路径=%s", client_fd, current_path);
    send_msg(client_fd, result);
}

void handle_pwd(int client_fd, char *current_path) {
    LOG_INFO("处理显示当前路径命令，客户端fd=%d，当前路径=%s", client_fd, current_path);
    send_msg(client_fd, current_path);
}

void handle_rm(int client_fd, char *current_path, char *arg) {
    char real_path[MAX_PATH_LEN] = {0};
    if (get_real_path(real_path, sizeof(real_path), current_path, arg) == -1) {
        LOG_WARN("删除路径非法，客户端fd=%d，当前路径=%s，参数=%s", client_fd, current_path, arg);
        send_msg(client_fd, "路径非法或过长");
        return;
    }

    if (remove(real_path) == 0) {
        LOG_INFO("删除成功，客户端fd=%d，路径=%s", client_fd, real_path);
        send_msg(client_fd, "删除成功");
    } else {
        LOG_WARN("删除失败，客户端fd=%d，路径=%s，错误码=%d", client_fd, real_path, errno);
        send_msg(client_fd, "删除失败");
    }
}

void handle_mkdir(int client_fd, char *current_path, char *arg) {
    char real_path[MAX_PATH_LEN] = {0};
    if (get_real_path(real_path, sizeof(real_path), current_path, arg) == -1) {
        LOG_WARN("创建目录路径非法，客户端fd=%d，当前路径=%s，参数=%s", client_fd, current_path, arg);
        send_msg(client_fd, "路径非法或过长");
        return;
    }

    if (mkdir(real_path, 0755) == 0) {
        LOG_INFO("创建目录成功，客户端fd=%d，路径=%s", client_fd, real_path);
        send_msg(client_fd, "创建文件夹成功");
    } else {
        LOG_WARN("创建目录失败，客户端fd=%d，路径=%s，错误码=%d", client_fd, real_path, errno);
        send_msg(client_fd, "文件夹创建失败");
    }
}