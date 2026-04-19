#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <errno.h>
#include "client_socket.h"
#include "config.h"
#include "client_command_handle.h"   // 新增头文件
#include "log.h"

// 函数作用：从配置文件中读出客户端要连接的服务器 IP 和端口。
// 参数 ip：输出参数，用来保存读出来的 IP。
// 参数 port：输出参数，用来保存读出来的端口。
// 返回值：无。
void load_server_config(char *ip, char *port) {
    // 从 config.ini 中读取 ip= 后面的值。
    get_target("ip", ip);
    printf("ip=%s\n", ip);

    // 从 config.ini 中读取 port= 后面的值。
    get_target("port", port);
    printf("port=%s\n", port);
}

// 函数作用：客户端主函数。
// 参数 argc：命令行参数个数，这里没有实际使用。
// 参数 argv：命令行参数数组，这里没有实际使用。
// 返回值：程序正常结束时返回 0。
int main(int argc, char *argv[])
{
    // 这两行只是为了消除“未使用参数”警告。
    (void)argc;
    (void)argv;

    // 先初始化日志。
    // 否则后面如果 connect 失败，ERROR_CHECK 里打印日志时可能没有输出目标。
    init_log("INFO", NULL);

    // 准备两个字符数组，分别保存服务器 IP 和端口。
    char ip[64] = {0};
    char port[64] = {0};

    // 从配置文件中读取 IP 和端口。
    load_server_config(ip, port);

    // sock_fd 就是客户端和服务端通信用的 socket。
    int sock_fd = 0;

    // 主动连接到服务端。
    init_socket(&sock_fd, ip, port);

    // input 用来保存用户每次输入的一整行命令。
    char input[512];

    // 客户端进入命令循环。
    while (1) {
        // 打印命令提示符。
        printf("> ");

        // 立刻把提示符刷到终端上，避免缓冲区里还没显示。
        fflush(stdout);

        // fgets 从标准输入读一整行。
        // 如果返回 NULL，通常表示输入结束，例如按下 Ctrl+D。
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }

        // fgets 通常会把末尾的 '\n' 一起读进来。
        // 这里把它替换成 '\0'，让字符串更方便后续处理。
        input[strcspn(input, "\n")] = '\0';

        // 用户输入 quit 或 exit 时，客户端主动退出。
        if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0) {
            printf("再见！\n");
            break;
        }

        // 如果用户只输入了一个空行，就继续下一轮循环。
        if (strlen(input) == 0) {
            continue;
        }

        // 真正的命令发送、上传下载、结果接收，都交给 process_command 去做。
        process_command(sock_fd, input);
    }

    // 退出前关闭 socket。
    close(sock_fd);

    // 关闭日志系统。
    close_log();
    return 0;
}
