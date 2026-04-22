#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include "client_command_handle.h"
#include "protocol.h"
#include "log.h"
#include "sha256_utils.h"

#define BUFFER_SIZE 4096

int handle_puts_command(int sock_fd, const char *arg) {
    LOG_INFO("客户端请求上传文件，文件=%s", arg);

    int fd = open(arg, O_RDONLY);
    if (fd == -1) {
        perror("打开文件失败");
        LOG_WARN("打开本地上传文件失败，文件=%s，错误码=%d", arg, errno);
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        perror("获取文件大小失败");
        LOG_ERROR("读取本地上传文件信息失败，文件=%s，错误码=%d", arg, errno);
        close(fd);
        return -1;
    }

    char file_hash[64] = {0};
    printf("正在计算文件哈希值...\n");
    if (get_file_sha256(arg, file_hash) == -1) {
        printf("计算文件哈希值失败\n");
        LOG_ERROR("计算文件哈希值失败，文件=%s", arg);
        close(fd);
        return -1;
    }

    LOG_DEBUG("计算文件哈希值成功，文件=%s，哈希=%s", arg, file_hash);

    command_packet_t cmd_packet;
    init_command_packet(&cmd_packet, CMD_TYPE_PUTS, arg);
    if (send_command_packet(sock_fd, &cmd_packet) == -1) {
        printf("发送命令失败\n");
        LOG_ERROR("发送上传命令失败，文件=%s", arg);
        close(fd);
        return -1;
    }

    file_packet_t client_file_packet;
    init_file_packet(&client_file_packet, CMD_TYPE_PUTS, arg, st.st_size, 0, file_hash);
    if (send_file_packet(sock_fd, &client_file_packet) == -1) {
        printf("发送文件信息失败\n");
        LOG_WARN("发送上传文件信息失败，文件=%s", arg);
        close(fd);
        return -1;
    }

    file_packet_t server_file_packet;
    if (recv_file_packet(sock_fd, &server_file_packet) <= 0) {
        printf("接收服务端断点信息失败\n");
        LOG_WARN("接收上传断点位置失败，文件=%s", arg);
        close(fd);
        return -1;
    }

    if (strcmp(server_file_packet.hash, file_hash) == 0) {
        printf("极速秒传成功。\n");
        LOG_INFO("上传已跳过，服务器文件完整，秒传完成，文件=%s，大小=%lld", arg, (long long)st.st_size);
        close(fd);
        return 0;
    }

    if (server_file_packet.offset > st.st_size) {
        server_file_packet.offset = 0;
    }

    LOG_DEBUG("上传断点信息，文件=%s，本地大小=%lld，偏移=%lld",
              arg,
              (long long)st.st_size,
              (long long)server_file_packet.offset);

    if (lseek(fd, server_file_packet.offset, SEEK_SET) == -1) {
        perror("移动文件指针失败");
        LOG_ERROR("定位本地上传文件失败，文件=%s，偏移=%lld，错误码=%d",
                  arg,
                  (long long)server_file_packet.offset,
                  errno);
        close(fd);
        return -1;
    }

    char buf[BUFFER_SIZE];
    off_t remaining = st.st_size - server_file_packet.offset;

    while (remaining > 0) {
        int once = BUFFER_SIZE;
        if (remaining < BUFFER_SIZE) {
            once = (int)remaining;
        }

        int nread = read(fd, buf, once);
        if (nread <= 0) {
            break;
        }

        if (send_full(sock_fd, buf, nread) == -1) {
            printf("上传中断，已经发送的内容由服务端自己保留。\n");
            LOG_WARN("上传中断，文件=%s，已发送=%lld，总大小=%lld",
                     arg,
                     (long long)(st.st_size - remaining),
                     (long long)st.st_size);
            close(fd);
            return 0;
        }

        remaining -= nread;
    }

    close(fd);
    recv_server_reply(sock_fd);
    LOG_INFO("上传成功，文件=%s", arg);
    return 0;
}
