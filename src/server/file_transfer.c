#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <errno.h>
#include "file_transfer.h"
#include "path_utils.h"
#include "session.h"
#include "protocol.h"
#include "log.h"

#define BUFFER_SIZE 4096

void handle_gets(int client_fd, char *current_path, char *arg) {
    char real_path[MAX_PATH_LEN] = {0};
    file_packet_t server_file_packet;

    if (get_real_path(real_path, sizeof(real_path), current_path, arg) == -1) {
        LOG_WARN("下载路径非法，客户端fd=%d，当前路径=%s，参数=%s", client_fd, current_path, arg);
        init_file_packet(&server_file_packet, CMD_TYPE_GETS, arg, -1, 0);
        send_file_packet(client_fd, &server_file_packet);
        return;
    }

    int file_fd = open(real_path, O_RDONLY);
    if (file_fd == -1) {
        LOG_WARN("打开下载文件失败，客户端fd=%d，路径=%s，错误码=%d", client_fd, real_path, errno);
        init_file_packet(&server_file_packet, CMD_TYPE_GETS, arg, -1, 0);
        send_file_packet(client_fd, &server_file_packet);
        return;
    }

    struct stat st;
    if (fstat(file_fd, &st) == -1) {
        LOG_ERROR("读取下载文件状态失败，客户端fd=%d，路径=%s，错误码=%d", client_fd, real_path, errno);
        close(file_fd);
        init_file_packet(&server_file_packet, CMD_TYPE_GETS, arg, -1, 0);
        send_file_packet(client_fd, &server_file_packet);
        return;
    }

    init_file_packet(&server_file_packet, CMD_TYPE_GETS, arg, st.st_size, 0);
    if (send_file_packet(client_fd, &server_file_packet) == -1) {
        LOG_WARN("发送下载文件信息失败，客户端fd=%d，路径=%s", client_fd, real_path);
        close(file_fd);
        return;
    }

    file_packet_t client_file_packet;
    if (recv_file_packet(client_fd, &client_file_packet) <= 0) {
        LOG_WARN("接收下载断点位置失败，客户端fd=%d，路径=%s", client_fd, real_path);
        close(file_fd);
        return;
    }

    off_t offset = client_file_packet.offset;
    if (offset < 0 || offset > st.st_size) {
        LOG_WARN("下载断点位置无效，客户端fd=%d，偏移=%lld，大小=%lld", client_fd, (long long)offset, (long long)st.st_size);
        offset = 0;
    }
    LOG_DEBUG("下载断点信息，客户端fd=%d，路径=%s，偏移=%lld，大小=%lld", client_fd, real_path, (long long)offset, (long long)st.st_size);

    off_t remaining = st.st_size - offset;

    while (remaining > 0) {
        ssize_t sent = sendfile(client_fd, file_fd, &offset, remaining);
        if (sent < 0) {
            if (errno == EINTR) continue;
            LOG_WARN("下载发送中断，客户端fd=%d，路径=%s，已发送=%lld，错误码=%d", client_fd, real_path, (long long)(st.st_size - remaining), errno);
            break;
        }
        if (sent == 0) break;
        remaining -= sent;
    }

    close(file_fd);
    if (remaining == 0) {
        LOG_INFO("下载完成，客户端fd=%d，路径=%s，大小=%lld", client_fd, real_path, (long long)st.st_size);
    }
}

void handle_puts(int client_fd, char *current_path, char *arg) {
    char real_path[MAX_PATH_LEN] = {0};

    if (get_real_path(real_path, sizeof(real_path), current_path, arg) == -1) {
        LOG_WARN("上传路径非法，客户端fd=%d，参数=%s", client_fd, arg);
        send_msg(client_fd, "路径非法或过长");
        return;
    }

    file_packet_t client_file_packet;
    if (recv_file_packet(client_fd, &client_file_packet) <= 0) {
        LOG_WARN("接收上传文件信息失败，客户端fd=%d", client_fd);
        send_msg(client_fd, "接收文件信息失败");
        return;
    }

    int file_fd = open(real_path, O_RDWR | O_CREAT, 0666);
    if (file_fd == -1) {
        LOG_ERROR("打开上传目标文件失败，客户端fd=%d，错误码=%d", client_fd, errno);
        send_msg(client_fd, "服务端创建文件失败");
        return;
    }

    struct stat st;
    off_t local_size = 0;
    if (fstat(file_fd, &st) == 0) {
        local_size = st.st_size;
    }

    if (local_size > client_file_packet.file_size) {
        local_size = 0;
    }

    file_packet_t server_file_packet;
    init_file_packet(&server_file_packet, CMD_TYPE_PUTS, arg, client_file_packet.file_size, local_size);
    if (send_file_packet(client_fd, &server_file_packet) == -1) {
        close(file_fd);
        return;
    }

    off_t file_len = client_file_packet.file_size;
    off_t remaining = file_len - local_size;

    if (remaining <= 0) {
        LOG_INFO("上传已跳过，服务端文件完整，客户端fd=%d", client_fd);
        send_msg(client_fd, "服务器端文件已完整存在，无需续传。");
        close(file_fd);
        return;
    }

    if (ftruncate(file_fd, file_len) == -1) {
        send_msg(client_fd, "服务端扩展文件失败");
        close(file_fd);
        return;
    }

    if (file_len == 0) {
        send_msg(client_fd, "上传完成！");
        close(file_fd);
        return;
    }

    char *map_ptr = mmap(NULL, file_len, PROT_READ | PROT_WRITE, MAP_SHARED, file_fd, 0);
    if (map_ptr == MAP_FAILED) {
        send_msg(client_fd, "服务端内存映射失败");
        close(file_fd);
        return;
    }

    char *write_start = map_ptr + local_size;
    off_t received_count = 0;

    while (received_count < remaining) {
        int once = BUFFER_SIZE;
        if (remaining - received_count < BUFFER_SIZE) {
            once = (int)(remaining - received_count);
        }

        ssize_t ret = recv(client_fd, write_start + received_count, once, 0);
        if (ret <= 0) break;
        received_count += ret;
    }

    munmap(map_ptr, file_len);

    if (received_count < remaining) {
        ftruncate(file_fd, local_size + received_count);
        send_msg(client_fd, "传输中断，已保存当前进度。");
    } else {
        send_msg(client_fd, "上传完成！");
    }
    close(file_fd);
}