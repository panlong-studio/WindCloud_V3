#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <errno.h>
#include "handle.h"
#include "protocol.h"
#include "log.h"

#define SERVER_BASE_DIR "../test"
#define BUFFER_SIZE 4096

static const char *get_server_base_dir(void) {
    if (access(SERVER_BASE_DIR, F_OK) == 0) {
        return SERVER_BASE_DIR;
    }
    if (access("./test", F_OK) == 0) {
        return "./test";
    }
    return SERVER_BASE_DIR;
}

// 函数作用：向客户端发送一条普通文本消息。
// 参数 listen_fd：客户端连接 fd。
// 参数 msg：要发送的文本内容。
// 返回值：无。
void send_msg(int listen_fd, const char *msg) {
    // reply_packet 用来保存服务端要发回去的文本结构体。
    command_packet_t reply_packet;

    // 把命令类型写成 CMD_TYPE_REPLY，把文本写进 data。
    init_command_packet(&reply_packet, CMD_TYPE_REPLY, msg);

    // 直接按固定结构体大小发送。
    if (send_command_packet(listen_fd, &reply_packet) == -1) {
        LOG_WARN("发送响应失败，客户端fd=%d，消息=%s", listen_fd, msg);
        return;
    }

    LOG_DEBUG("响应发送成功，客户端fd=%d，消息=%s", listen_fd, msg);
}

// 函数作用：检查用户传来的路径参数是否基本合法。
// 参数 arg：用户输入的目录名或文件名。
// 返回值：合法返回 0，不合法返回 -1。
static int check_arg_path(const char *arg) {
    // 参数为空，或者是空字符串，都直接判为非法。
    if (arg == NULL || arg[0] == '\0') {
        return -1;
    }

    // 当前项目只做最基础的安全限制：
    // 只要出现 ..，就拒绝，避免跳出服务端根目录。
    if (strstr(arg, "..") != NULL) {
        return -1;
    }

    return 0;
}

// 函数作用：把当前虚拟路径转换成服务器真实路径。
// 参数 res：输出参数，用来保存最终拼好的真实路径。
// 参数 size：res 缓冲区大小。
// 参数 current_path：当前虚拟路径，例如 "/" 或 "/demo"。
// 返回值：成功返回 0，失败返回 -1。
static int get_current_real_path(char *res, int size, const char *current_path) {
    // 例如：
    // current_path = /demo
    // 最终拼出来就是 ../test/demo。
    const char *base_dir = get_server_base_dir();
    int ret = snprintf(res, size, "%s%s", base_dir, current_path);

    // snprintf 返回值 >= size，说明输出被截断了。
    if (ret < 0 || ret >= size) {
        return -1;
    }

    return 0;
}

// 函数作用：把“当前虚拟路径 + 参数”拼成最终真实路径。
// 参数 res：输出参数，保存最终路径。
// 参数 size：res 缓冲区大小。
// 参数 path：当前虚拟路径。
// 参数 arg：用户输入的文件名或目录名。
// 返回值：成功返回 0，失败返回 -1。
static int get_real_path(char *res, int size, const char *path, const char *arg) {
    // 先检查参数是否合法。
    if (check_arg_path(arg) == -1) {
        return -1;
    }

    // ret 用来保存 snprintf 的返回值。
    int ret = 0;

    const char *base_dir = get_server_base_dir();

    // 如果当前已经在根目录，就要拼成 ../test/xxx。
    if (strcmp(path, "/") == 0) {
        ret = snprintf(res, size, "%s/%s", base_dir, arg);
    } else {
        // 如果当前不在根目录，就要拼成 ../test/当前目录/参数。
        ret = snprintf(res, size, "%s%s/%s", base_dir, path, arg);
    }

    // 再做一次长度检查。
    if (ret < 0 || ret >= size) {
        return -1;
    }

    return 0;
}

// 函数作用：更新客户端看到的虚拟路径。
// 参数 current_path：当前虚拟路径，成功后会被改成新路径。
// 参数 size：current_path 缓冲区大小。
// 参数 arg：用户输入的目录名。
// 返回值：成功返回 0，失败返回 -1。
static int update_current_path(char *current_path, int size, const char *arg) {
    // new_path 是临时变量，先把新路径拼到这里，确认没问题后再写回 current_path。
    char new_path[512] = {0};
    int ret = 0;

    // 如果当前在根目录，进入 demo 后应该变成 /demo。
    if (strcmp(current_path, "/") == 0) {
        ret = snprintf(new_path, sizeof(new_path), "/%s", arg);
    } else {
        // 如果当前已经在 /demo，进入 abc 后应该变成 /demo/abc。
        ret = snprintf(new_path, sizeof(new_path), "%s/%s", current_path, arg);
    }

    // 检查新路径是否过长。
    if (ret < 0 || ret >= size) {
        return -1;
    }

    // 确认新路径安全后，再回写到 current_path。
    strcpy(current_path, new_path);
    return 0;
}

// 函数作用：把 command_packet_t 里的整型命令值转成枚举类型。
// 参数 cmd_packet：客户端发来的命令结构体。
// 返回值：对应的命令枚举值；如果参数为空，返回 CMD_TYPE_INVALID。
static cmd_type_t get_packet_cmd_type(const command_packet_t *cmd_packet) {
    // 防止空指针。
    if (cmd_packet == NULL) {
        return CMD_TYPE_INVALID;
    }

    // 这里本质上只是把 int 强制转成 cmd_type_t。
    return (cmd_type_t)cmd_packet->cmd_type;
}

// 函数作用：处理一个客户端连接上的所有命令请求。
// 参数 listen_fd：这里实际是某个客户端连接 fd。
// 返回值：无。
void handle_request(int listen_fd) {
    // 每个客户端连接都有自己独立的 current_path。
    // 初始进入连接时，默认都在虚拟根目录 /。
    char current_path[512] = "/";

    // 持续循环接收这个客户端发来的命令。
    while (1) {
        // cmd_packet 用来保存本轮收到的命令结构体。
        command_packet_t cmd_packet;

        // 每次都按固定大小完整接收一个命令结构体。
        if (recv_command_packet(listen_fd, &cmd_packet) <= 0) {
            // 返回 <= 0 说明客户端断开了，或者协议接收失败。
            LOG_INFO("客户端连接断开，客户端fd=%d，当前路径=%s", listen_fd, current_path);
            break;
        }

        // 取出本轮命令的枚举类型。
        cmd_type_t cmd_type = get_packet_cmd_type(&cmd_packet);
        LOG_DEBUG("收到客户端命令，客户端fd=%d，命令类型=%d，数据=%s", listen_fd, cmd_type, cmd_packet.data);

        // 这里使用 switch-case，而不再是长长的 if-else if。
        switch (cmd_type) {
        case CMD_TYPE_PWD:
            handle_pwd(listen_fd, current_path);
            break;

        case CMD_TYPE_CD:
            handle_cd(listen_fd, current_path, cmd_packet.data);
            break;

        case CMD_TYPE_LS:
            handle_ls(listen_fd, current_path);
            break;

        case CMD_TYPE_GETS:
            handle_gets(listen_fd, current_path, cmd_packet.data);
            break;

        case CMD_TYPE_PUTS:
            handle_puts(listen_fd, current_path, cmd_packet.data);
            break;

        case CMD_TYPE_RM:
            handle_rm(listen_fd, current_path, cmd_packet.data);
            break;

        case CMD_TYPE_MKDIR:
            handle_mkdir(listen_fd, current_path, cmd_packet.data);
            break;

        default:
            // 命令类型无法识别时，返回一个错误提示。
            LOG_WARN("命令类型无效，客户端fd=%d，命令类型=%d", listen_fd, cmd_type);
            send_msg(listen_fd, "指令错误!");
            break;
        }
    }
}

// 函数作用：处理 cd 命令。
// 参数 listen_fd：客户端连接 fd。
// 参数 current_path：当前虚拟路径，成功进入目录后会被修改。
// 参数 arg：用户输入的目录名。
// 返回值：无。
void handle_cd(int listen_fd, char *current_path, char *arg) {
    // 没有参数时，直接提示输入错误。
    if (arg == NULL || arg[0] == '\0') {
        LOG_WARN("切换目录缺少参数，客户端fd=%d", listen_fd);
        send_msg(listen_fd, "输入错误");
        return;
    }

    // 当前项目把 cd .. 简化成“直接回到根目录”。
    if (strcmp(arg, "..") == 0) {
        strcpy(current_path, "/");
        LOG_INFO("客户端切换回根目录，客户端fd=%d", listen_fd);
        send_msg(listen_fd, "已返回根目录");
        return;
    }

    // real_path 用来保存拼好的真实路径。
    char real_path[1024] = {0};

    // 先把当前虚拟路径和目录名拼成真实路径。
    if (get_real_path(real_path, sizeof(real_path), current_path, arg) == -1) {
        LOG_WARN("切换目录路径非法，客户端fd=%d，当前路径=%s，参数=%s", listen_fd, current_path, arg);
        send_msg(listen_fd, "路径非法或过长");
        return;
    }

    // 用 opendir 试探这个目录是否真的存在。
    DIR *dir = opendir(real_path);
    if (dir == NULL) {
        LOG_WARN("切换目录失败，客户端fd=%d，路径=%s，错误码=%d", listen_fd, real_path, errno);
        send_msg(listen_fd, "目录不存在！");
        return;
    }

    // 目录打开成功后要及时关闭。
    closedir(dir);

    // 更新客户端看到的虚拟路径。
    if (update_current_path(current_path, 512, arg) == -1) {
        LOG_WARN("切换目录路径过长，客户端fd=%d，当前路径=%s，参数=%s", listen_fd, current_path, arg);
        send_msg(listen_fd, "路径过长");
        return;
    }

    LOG_INFO("切换目录成功，客户端fd=%d，当前路径=%s", listen_fd, current_path);
    send_msg(listen_fd, "进入目录成功");
}

// 函数作用：处理 ls 命令。
// 参数 listen_fd：客户端连接 fd。
// 参数 current_path：当前虚拟路径。
// 返回值：无。
void handle_ls(int listen_fd, char *current_path) {
    // real_path 保存当前目录对应的真实磁盘路径。
    char real_path[1024] = {0};
    if (get_current_real_path(real_path, sizeof(real_path), current_path) == -1) {
        LOG_WARN("列目录路径过长，客户端fd=%d，当前路径=%s", listen_fd, current_path);
        send_msg(listen_fd, "路径过长");
        return;
    }

    // 打开目录准备遍历。
    DIR *dir = opendir(real_path);
    if (dir == NULL) {
        LOG_WARN("列目录失败，客户端fd=%d，路径=%s，错误码=%d", listen_fd, real_path, errno);
        send_msg(listen_fd, "目录打开失败");
        return;
    }

    // file 每次指向一个目录项。
    struct dirent *file = NULL;

    // result 用来把所有文件名拼成一个字符串返回给客户端。
    char result[4096] = {0};

    // used 表示 result 目前已经用了多少字节。
    int used = 0;

    // 一项一项读取目录。
    while ((file = readdir(dir)) != NULL) {
        // "." 和 ".." 是系统自带目录项，这里不显示给用户。
        if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0) {
            continue;
        }

        // left 表示 result 当前还剩多少空间可以拼接。
        int left = (int)sizeof(result) - used;

        // 把当前文件名和一个空格拼接到 result 后面。
        int ret = snprintf(result + used, left, "%s ", file->d_name);

        // ret >= left 说明空间不够了，就停止继续拼接。
        if (ret < 0 || ret >= left) {
            break;
        }

        // 更新已使用长度。
        used += ret;
    }

    // 遍历完记得关闭目录流。
    closedir(dir);

    // 如果 used 还是 0，说明目录里没有普通可显示项。
    if (used == 0) {
        LOG_INFO("目录为空，客户端fd=%d，路径=%s", listen_fd, current_path);
        send_msg(listen_fd, "当前目录为空");
        return;
    }

    // 把目录列表发给客户端。
    LOG_INFO("列目录成功，客户端fd=%d，路径=%s", listen_fd, current_path);
    send_msg(listen_fd, result);
}

// 函数作用：处理 pwd 命令。
// 参数 listen_fd：客户端连接 fd。
// 参数 current_path：当前虚拟路径。
// 返回值：无。
void handle_pwd(int listen_fd, char *current_path) {
    // pwd 的逻辑最简单，直接把当前虚拟路径返回即可。
    LOG_INFO("处理显示当前路径命令，客户端fd=%d，当前路径=%s", listen_fd, current_path);
    send_msg(listen_fd, current_path);
}

// 函数作用：处理 rm 命令。
// 参数 listen_fd：客户端连接 fd。
// 参数 current_path：当前虚拟路径。
// 参数 arg：要删除的文件名。
// 返回值：无。
void handle_rm(int listen_fd, char *current_path, char *arg) {
    // real_path 保存最终要删除的真实文件路径。
    char real_path[1024] = {0};

    // 先拼出真实路径。
    if (get_real_path(real_path, sizeof(real_path), current_path, arg) == -1) {
        LOG_WARN("删除路径非法，客户端fd=%d，当前路径=%s，参数=%s", listen_fd, current_path, arg);
        send_msg(listen_fd, "路径非法或过长");
        return;
    }

    // remove 返回 0 表示删除成功。
    if (remove(real_path) == 0) {
        LOG_INFO("删除成功，客户端fd=%d，路径=%s", listen_fd, real_path);
        send_msg(listen_fd, "删除成功");
    } else {
        LOG_WARN("删除失败，客户端fd=%d，路径=%s，错误码=%d", listen_fd, real_path, errno);
        send_msg(listen_fd, "删除失败");
    }
}

// 函数作用：处理 mkdir 命令。
// 参数 listen_fd：客户端连接 fd。
// 参数 current_path：当前虚拟路径。
// 参数 arg：要创建的目录名。
// 返回值：无。
void handle_mkdir(int listen_fd, char *current_path, char *arg) {
    // real_path 保存新目录的真实路径。
    char real_path[1024] = {0};

    // 先拼真实路径。
    if (get_real_path(real_path, sizeof(real_path), current_path, arg) == -1) {
        LOG_WARN("创建目录路径非法，客户端fd=%d，当前路径=%s，参数=%s", listen_fd, current_path, arg);
        send_msg(listen_fd, "路径非法或过长");
        return;
    }

    // mkdir 返回 0 表示创建成功。
    if (mkdir(real_path, 0755) == 0) {
        LOG_INFO("创建目录成功，客户端fd=%d，路径=%s", listen_fd, real_path);
        send_msg(listen_fd, "创建文件夹成功");
    } else {
        LOG_WARN("创建目录失败，客户端fd=%d，路径=%s，错误码=%d", listen_fd, real_path, errno);
        send_msg(listen_fd, "文件夹创建失败");
    }
}

// 函数作用：处理 gets 命令，也就是下载。
// 参数 listen_fd：客户端连接 fd。
// 参数 current_path：当前虚拟路径。
// 参数 arg：要下载的文件名。
// 返回值：无。
void handle_gets(int listen_fd, char *current_path, char *arg) {
    // real_path 是服务端真实文件路径。
    char real_path[1024] = {0};

    // server_file_packet 用来发给客户端，告诉它文件信息。
    file_packet_t server_file_packet;

    // 如果路径非法，直接返回 file_size = -1，告诉客户端失败。
    if (get_real_path(real_path, sizeof(real_path), current_path, arg) == -1) {
        LOG_WARN("下载路径非法，客户端fd=%d，当前路径=%s，参数=%s", listen_fd, current_path, arg);
        init_file_packet(&server_file_packet, CMD_TYPE_GETS, arg, -1, 0);
        send_file_packet(listen_fd, &server_file_packet);
        return;
    }

    // 以只读方式打开目标文件。
    int file_fd = open(real_path, O_RDONLY);
    if (file_fd == -1) {
        LOG_WARN("打开下载文件失败，客户端fd=%d，路径=%s，错误码=%d", listen_fd, real_path, errno);
        init_file_packet(&server_file_packet, CMD_TYPE_GETS, arg, -1, 0);
        send_file_packet(listen_fd, &server_file_packet);
        return;
    }

    // st 用来保存文件状态。
    struct stat st;
    if (fstat(file_fd, &st) == -1) {
        LOG_ERROR("读取下载文件状态失败，客户端fd=%d，路径=%s，错误码=%d", listen_fd, real_path, errno);
        close(file_fd);
        init_file_packet(&server_file_packet, CMD_TYPE_GETS, arg, -1, 0);
        send_file_packet(listen_fd, &server_file_packet);
        return;
    }

    // 第一步：先把文件总大小和文件名发给客户端。
    init_file_packet(&server_file_packet, CMD_TYPE_GETS, arg, st.st_size, 0);
    if (send_file_packet(listen_fd, &server_file_packet) == -1) {
        LOG_WARN("发送下载文件信息失败，客户端fd=%d，路径=%s", listen_fd, real_path);
        close(file_fd);
        return;
    }

    // 第二步：再收客户端发回来的 offset。
    // 这个 offset 表示“客户端本地已经有多少字节了”。
    file_packet_t client_file_packet;
    if (recv_file_packet(listen_fd, &client_file_packet) <= 0) {
        LOG_WARN("接收下载断点位置失败，客户端fd=%d，路径=%s", listen_fd, real_path);
        close(file_fd);
        return;
    }

    // 取出断点位置。
    off_t offset = client_file_packet.offset;

    // 只要 offset 越界，就回退到 0，从头开始传。
    if (offset < 0 || offset > st.st_size) {
        LOG_WARN("下载断点位置无效，客户端fd=%d，偏移=%lld，大小=%lld",
                 listen_fd,
                 (long long)offset,
                 (long long)st.st_size);
        offset = 0;
    }
    LOG_DEBUG("下载断点信息，客户端fd=%d，路径=%s，偏移=%lld，大小=%lld",
              listen_fd,
              real_path,
              (long long)offset,
              (long long)st.st_size);

    // 计算还剩多少字节要传给客户端。
    off_t remaining = st.st_size - offset;

    // 只要 remaining > 0，就持续发送。
    while (remaining > 0) {
        // sendfile 会把 file_fd 指向的文件内容直接送到 socket。
        // 第三个参数 &offset 很关键：
        // 它表示从当前 offset 开始发，并且每发送成功一次，offset 会自动后移。
        ssize_t sent = sendfile(listen_fd, file_fd, &offset, remaining);

        // sent < 0 表示发送出错。
        if (sent < 0) {
            // 如果只是被信号打断，就继续发送。
            if (errno == EINTR) {
                continue;
            }
            LOG_WARN("下载发送中断，客户端fd=%d，路径=%s，已发送=%lld，总大小=%lld，错误码=%d",
                     listen_fd,
                     real_path,
                     (long long)(st.st_size - remaining),
                     (long long)st.st_size,
                     errno);
            break;
        }

        // sent == 0 通常表示没有更多内容可发，或者连接已异常结束。
        if (sent == 0) {
            break;
        }

        // 从剩余量里减掉本轮已发送字节数。
        remaining -= sent;
    }

    // 下载结束后关闭文件。
    close(file_fd);
    if (remaining == 0) {
        LOG_INFO("下载完成，客户端fd=%d，路径=%s，大小=%lld",
                 listen_fd,
                 real_path,
                 (long long)st.st_size);
    } else {
        LOG_WARN("下载未完成，客户端fd=%d，路径=%s，已发送=%lld，总大小=%lld",
                 listen_fd,
                 real_path,
                 (long long)(st.st_size - remaining),
                 (long long)st.st_size);
    }
}

// 函数作用：处理 puts 命令，也就是上传。
// 参数 listen_fd：客户端连接 fd。
// 参数 current_path：当前虚拟路径。
// 参数 arg：目标文件名。
// 返回值：无。
void handle_puts(int listen_fd, char *current_path, char *arg) {
    // real_path 保存服务端准备写入的真实路径。
    char real_path[1024] = {0};

    // 路径不合法时直接拒绝。
    if (get_real_path(real_path, sizeof(real_path), current_path, arg) == -1) {
        LOG_WARN("上传路径非法，客户端fd=%d，当前路径=%s，参数=%s", listen_fd, current_path, arg);
        send_msg(listen_fd, "路径非法或过长");
        return;
    }

    // client_file_packet 用来接收客户端发来的文件信息。
    file_packet_t client_file_packet;

    // 第一步：先完整接收客户端发来的文件结构体。
    if (recv_file_packet(listen_fd, &client_file_packet) <= 0) {
        LOG_WARN("接收上传文件信息失败，客户端fd=%d，路径=%s", listen_fd, real_path);
        send_msg(listen_fd, "接收文件信息失败");
        return;
    }

    // 打开或创建服务端目标文件。
    int file_fd = open(real_path, O_RDWR | O_CREAT, 0666);
    if (file_fd == -1) {
        LOG_ERROR("打开上传目标文件失败，客户端fd=%d，路径=%s，错误码=%d", listen_fd, real_path, errno);
        send_msg(listen_fd, "服务端创建文件失败");
        return;
    }

    // st 用来读取当前服务端文件状态。
    struct stat st;

    // local_size 表示服务端本地已经有多少字节。
    off_t local_size = 0;

    // 如果 fstat 成功，就把当前文件大小取出来。
    if (fstat(file_fd, &st) == 0) {
        local_size = st.st_size;
    } else {
        LOG_WARN("读取上传目标文件状态失败，客户端fd=%d，路径=%s，错误码=%d", listen_fd, real_path, errno);
    }

    // 如果服务端旧文件比客户端新文件还大，说明这个旧文件不可信。
    // 这时把续传断点重置为 0。
    if (local_size > client_file_packet.file_size) {
        LOG_WARN("上传断点已重置，客户端fd=%d，路径=%s，本地大小=%lld，客户端大小=%lld",
                 listen_fd,
                 real_path,
                 (long long)local_size,
                 (long long)client_file_packet.file_size);
        local_size = 0;
    }
    LOG_DEBUG("上传断点信息，客户端fd=%d，路径=%s，偏移=%lld，总大小=%lld",
              listen_fd,
              real_path,
              (long long)local_size,
              (long long)client_file_packet.file_size);

    // 第二步：把服务端当前已有大小通过 offset 发回客户端。
    file_packet_t server_file_packet;
    init_file_packet(&server_file_packet,
                     CMD_TYPE_PUTS,
                     arg,
                     client_file_packet.file_size,
                     local_size);

    if (send_file_packet(listen_fd, &server_file_packet) == -1) {
        LOG_WARN("发送上传断点位置失败，客户端fd=%d，路径=%s", listen_fd, real_path);
        close(file_fd);
        return;
    }

    // file_len 是客户端文件总大小。
    off_t file_len = client_file_packet.file_size;

    // remaining 表示服务端这次还需要继续接收多少字节。
    off_t remaining = file_len - local_size;

    // 如果 remaining <= 0，说明服务端已经是完整文件了。
    if (remaining <= 0) {
        LOG_INFO("上传已跳过，服务端文件完整，客户端fd=%d，路径=%s，大小=%lld",
                 listen_fd,
                 real_path,
                 (long long)file_len);
        send_msg(listen_fd, "服务器端文件已完整存在，无需续传。");
        close(file_fd);
        return;
    }

    // 在 mmap 之前，必须先把文件扩展到目标大小。
    // 否则后面往映射区尾部写数据时可能出错。
    if (ftruncate(file_fd, file_len) == -1) {
        LOG_ERROR("扩展上传文件失败，客户端fd=%d，路径=%s，大小=%lld，错误码=%d",
                  listen_fd,
                  real_path,
                  (long long)file_len,
                  errno);
        send_msg(listen_fd, "服务端扩展文件失败");
        close(file_fd);
        return;
    }

    // 空文件是一个特殊情况。
    // 文件总长度为 0 时，不需要 mmap，也不需要继续收数据。
    if (file_len == 0) {
        LOG_INFO("上传成功，客户端fd=%d，路径=%s，大小=0", listen_fd, real_path);
        send_msg(listen_fd, "上传完成！");
        close(file_fd);
        return;
    }

    // map_ptr 指向“整个文件映射到内存后的起始地址”。
    char *map_ptr = mmap(NULL, file_len, PROT_READ | PROT_WRITE, MAP_SHARED, file_fd, 0);
    if (map_ptr == MAP_FAILED) {
        LOG_ERROR("映射上传文件失败，客户端fd=%d，路径=%s，错误码=%d", listen_fd, real_path, errno);
        send_msg(listen_fd, "服务端内存映射失败");
        close(file_fd);
        return;
    }

    // write_start 指向真正开始写入的位置，也就是断点处。
    char *write_start = map_ptr + local_size;

    // received_count 表示本次已经成功收到了多少字节。
    off_t received_count = 0;

    // 只要还没收满，就一轮轮 recv。
    while (received_count < remaining) {
        // once 表示这一轮最多收多少字节。
        int once = BUFFER_SIZE;

        // 最后一轮可能不足 4096，所以要按剩余量调整。
        if (remaining - received_count < BUFFER_SIZE) {
            once = (int)(remaining - received_count);
        }

        // 这里直接把 socket 数据收进映射区。
        // write_start + received_count 表示“从还没写入的位置继续收”。
        ssize_t ret = recv(listen_fd, write_start + received_count, once, 0);

        // ret <= 0 说明连接中断或接收失败。
        if (ret <= 0) {
            break;
        }

        // 统计本次已经收满了多少字节。
        received_count += ret;
    }

    // 传输结束后解除映射。
    munmap(map_ptr, file_len);

    // 如果中途断开，received_count 就会小于 remaining。
    // 这时要把文件截断到“真实已收到”的大小。
    if (received_count < remaining) {
        ftruncate(file_fd, local_size + received_count);
        LOG_WARN("上传中断，客户端fd=%d，路径=%s，已保存=%lld，总大小=%lld",
                 listen_fd,
                 real_path,
                 (long long)(local_size + received_count),
                 (long long)file_len);
        send_msg(listen_fd, "传输中断，已保存当前进度。");
    } else {
        LOG_INFO("上传成功，客户端fd=%d，路径=%s，大小=%lld",
                 listen_fd,
                 real_path,
                 (long long)file_len);
        send_msg(listen_fd, "上传完成！");
    }

    // 最后关闭文件。
    close(file_fd);
}
