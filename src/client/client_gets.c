#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include "client_command_handle.h"
#include "protocol.h"
#include "log.h"

#define BUFFER_SIZE 4096

// 下载阶段会持续把网络数据写入本地文件，这里保证每一块都写完整。
static int write_file_full(int fd, const char *buf, int len) {
    int total = 0;

    while (total < len) {
        int ret = write(fd, buf + total, len - total);
        if (ret <= 0) {
            return -1;
        }
        total += ret;
    }

    return 0;
}

int handle_gets_command(int sock_fd, const char *arg) {
    LOG_INFO("客户端请求下载文件，文件=%s", arg);

    command_packet_t cmd_packet;
    init_command_packet(&cmd_packet, CMD_TYPE_GETS, arg);

    if (send_command_packet(sock_fd, &cmd_packet) == -1) {
        printf("发送命令失败\n");
        LOG_ERROR("发送下载命令失败，文件=%s", arg);
        return 0;
    }

    file_packet_t server_file_packet;
    if (recv_file_packet(sock_fd, &server_file_packet) <= 0) {
        printf("接收文件信息失败\n");
        LOG_WARN("接收下载文件信息失败，文件=%s", arg);
        return 0;
    }

    if (server_file_packet.file_size < 0) {
        printf("服务器文件不存在\n");
        LOG_WARN("服务端文件不存在，文件=%s", arg);
        return 0;
    }

    struct stat st;
    off_t local_size = 0;
    off_t request_offset = 0;
    int local_file_exists = 0;

    if (stat(arg, &st) == 0) {
        local_size = st.st_size;
        local_file_exists = 1;
    }

    if (local_size < server_file_packet.file_size) {
        request_offset = local_size;
    } else if (local_file_exists && local_size == server_file_packet.file_size) {
        request_offset = server_file_packet.file_size;
    } else {
        request_offset = 0;
    }

    LOG_DEBUG("下载断点信息，文件=%s，本地大小=%lld，服务端大小=%lld，偏移=%lld",
              arg,
              (long long)local_size,
              (long long)server_file_packet.file_size,
              (long long)request_offset);

    file_packet_t client_file_packet;
    init_file_packet(&client_file_packet,
                     CMD_TYPE_GETS,
                     arg,
                     server_file_packet.file_size,
                     request_offset,
                     NULL);

    if (send_file_packet(sock_fd, &client_file_packet) == -1) {
        printf("发送续传位置失败\n");
        LOG_WARN("发送下载断点位置失败，文件=%s", arg);
        return -1;
    }

    if (local_file_exists && request_offset == server_file_packet.file_size) {
        printf("文件已存在且完整，无需下载。\n");
        LOG_INFO("下载已跳过，本地文件完整，文件=%s，大小=%lld", arg, (long long)server_file_packet.file_size);
        return 0;
    }

    int fd = open(arg, O_WRONLY | O_CREAT, 0666);
    if (fd == -1) {
        perror("创建文件失败");
        LOG_ERROR("创建本地文件失败，文件=%s，错误码=%d", arg, errno);
        return -1;
    }

    if (request_offset == 0) {
        if (ftruncate(fd, 0) == -1) {
            perror("清空旧文件失败");
            LOG_ERROR("截断本地文件失败，文件=%s，错误码=%d", arg, errno);
            close(fd);
            return -1;
        }
    }

    if (lseek(fd, request_offset, SEEK_SET) == -1) {
        perror("移动文件指针失败");
        LOG_ERROR("定位本地文件失败，文件=%s，偏移=%lld，错误码=%d",
                  arg,
                  (long long)request_offset,
                  errno);
        close(fd);
        return -1;
    }

    char buf[BUFFER_SIZE];
    off_t remaining = server_file_packet.file_size - request_offset;

    while (remaining > 0) {
        int once = BUFFER_SIZE;
        if (remaining < BUFFER_SIZE) {
            once = (int)remaining;
        }

        if (recv_full(sock_fd, buf, once) <= 0) {
            printf("下载中断，已经保留当前进度。\n");
            LOG_WARN("下载中断，文件=%s，已保存=%lld，总大小=%lld",
                     arg,
                     (long long)(server_file_packet.file_size - remaining),
                     (long long)server_file_packet.file_size);
            close(fd);
            return 0;
        }

        if (write_file_full(fd, buf, once) == -1) {
            perror("写入本地文件失败");
            LOG_ERROR("写入本地文件失败，文件=%s，错误码=%d", arg, errno);
            close(fd);
            return -1;
        }

        remaining -= once;
    }

    close(fd);
    printf("下载成功: %s (%ld 字节)\n", arg, (long)server_file_packet.file_size);
    LOG_INFO("下载成功，文件=%s，大小=%lld", arg, (long long)server_file_packet.file_size);
    return 0;
}
