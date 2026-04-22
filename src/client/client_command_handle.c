#include <string.h>
#include "client_command_handle.h"
#include "protocol.h"
#include "log.h"

typedef struct {
    char cmd[100];
    char arg[200];
} client_input_t;

static void parse_input(const char *input, client_input_t *parsed) {
    memset(parsed, 0, sizeof(*parsed));
    sscanf(input, "%99s %199s", parsed->cmd, parsed->arg);
}

static int validate_command_args(const client_input_t *parsed) {
    if (strcmp(parsed->cmd, "gets") == 0 && parsed->arg[0] == '\0') {
        printf("用法: gets <文件名>\n");
        return -1;
    }

    if (strcmp(parsed->cmd, "puts") == 0 && parsed->arg[0] == '\0') {
        printf("用法: puts <文件名>\n");
        return -1;
    }

    if ((strcmp(parsed->cmd, "cd") == 0 ||
         strcmp(parsed->cmd, "rm") == 0 ||
         strcmp(parsed->cmd, "mkdir") == 0 ||
         strcmp(parsed->cmd, "touch") == 0 ||
         strcmp(parsed->cmd, "rmdir") == 0) &&
        parsed->arg[0] == '\0') {
        printf("该命令需要参数\n");
        return -1;
    }

    if ((strcmp(parsed->cmd, "puts") == 0 ||
         strcmp(parsed->cmd, "gets") == 0 ||
         strcmp(parsed->cmd, "mkdir") == 0 ||
         strcmp(parsed->cmd, "touch") == 0 ||
         strcmp(parsed->cmd, "rm") == 0 ||
         strcmp(parsed->cmd, "rmdir") == 0) &&
        strlen(parsed->arg) > MAX_VFS_NAME_LEN) {
        printf("文件名或目录名过长，最大长度为 30\n");
        return -1;
    }

    return 0;
}

static int handle_normal_command(int sock_fd, cmd_type_t cmd_type, const char *input, const char *arg) {
    command_packet_t cmd_packet;
    init_command_packet(&cmd_packet, cmd_type, arg);

    if (send_command_packet(sock_fd, &cmd_packet) == -1) {
        printf("发送命令失败\n");
        LOG_WARN("发送命令失败，输入=%s", input);
        return -1;
    }

    return recv_server_reply(sock_fd);
}

// 函数作用：接收服务端返回的普通文本响应。
// 参数 sock_fd：客户端和服务端通信的 socket。
// 返回值：无。
int recv_server_reply(int sock_fd) {
    // reply_packet 用来保存服务端发回来的文本结构体。
    command_packet_t reply_packet;

    // 按固定结构体大小完整接收。
    if (recv_command_packet(sock_fd, &reply_packet) <= 0) {
        printf("接收服务端响应失败\n");
        LOG_WARN("接收服务端响应失败，套接字=%d", sock_fd);
        return -1;
    }

    if(strstr(reply_packet.data, "成功") != NULL) {
        return 1;
    }

    // 把服务端返回的文本直接打印出来。
    printf("%s\n", reply_packet.data);
    LOG_DEBUG("收到服务端响应，套接字=%d，消息=%s", sock_fd, reply_packet.data);
    return 0;
}

// 函数作用：处理用户输入的一整条命令。
// 参数 sock_fd：客户端 socket。
// 参数 input：用户输入的一整行命令，例如 "puts a.txt"。
// 返回值：无。
int process_command(int sock_fd, const char *input) {
    client_input_t parsed;
    parse_input(input, &parsed);

    if (validate_command_args(&parsed) != 0) {
        return 0;
    }

    // 把字符串命令转换成枚举命令。
    // 后面客户端和服务端通信，都靠这个编号判断命令类型。
    cmd_type_t cmd_type = get_cmd_type(parsed.cmd);
    if (cmd_type == CMD_TYPE_INVALID) {
        printf("无效命令\n");
        LOG_WARN("无效命令，输入=%s", input);
        return 0;
    }

    if (cmd_type == CMD_TYPE_GETS) {
        return handle_gets_command(sock_fd, parsed.arg);
    }

    if (cmd_type == CMD_TYPE_PUTS) {
        return handle_puts_command(sock_fd, parsed.arg);
    }

    return handle_normal_command(sock_fd, cmd_type, input, parsed.arg);
}
