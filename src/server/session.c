#include <stdio.h>
#include <string.h>
#include "session.h"
#include "protocol.h"
#include "log.h"
#include "file_cmds.h"
#include "file_transfer.h"
#include "auth.h"

void send_msg(int client_fd, const char *msg) {
    command_packet_t reply_packet;
    init_command_packet(&reply_packet, CMD_TYPE_REPLY, msg);

    if (send_command_packet(client_fd, &reply_packet) == -1) {
        LOG_WARN("发送响应失败，客户端fd=%d，消息=%s", client_fd, msg);
        return;
    }
    LOG_DEBUG("响应发送成功，客户端fd=%d，消息=%s", client_fd, msg);
}

static cmd_type_t get_packet_cmd_type(const command_packet_t *cmd_packet) {
    if (cmd_packet == NULL) {
        return CMD_TYPE_INVALID;
    }
    return (cmd_type_t)cmd_packet->cmd_type;
}

void handle_request(int client_fd) {
    ClientContext ctx;

    // 先把整个上下文清零，避免里面残留脏数据。
    memset(&ctx, 0, sizeof(ctx));

    // 新连接默认还没有登录。
    ctx.user_id = -1;

    // 初始虚拟路径固定为根目录。
    strcpy(ctx.current_path, "/");

    // 根目录约定 parent_id 为 0。
    ctx.parent_id = 0;

    while (1) {
        command_packet_t cmd_packet;

        if (recv_command_packet(client_fd, &cmd_packet) <= 0) {
            LOG_INFO("客户端连接断开，客户端fd=%d，当前路径=%s", client_fd, ctx.current_path);
            break;
        }

        cmd_type_t cmd_type = get_packet_cmd_type(&cmd_packet);
        LOG_DEBUG("收到客户端命令，客户端fd=%d，命令类型=%d，数据=%s", client_fd, cmd_type, cmd_packet.data);

        // 没登录时，除了登录和注册，别的命令都不允许执行。
        if(ctx.user_id == -1 && cmd_type != CMD_TYPE_LOGIN && cmd_type != CMD_TYPE_REGISTER) {
            LOG_WARN("未登录用户尝试执行命令，客户端fd=%d，命令类型=%d", client_fd, cmd_type);
            send_msg(client_fd, "请先登录!");
            continue;
        }

        switch (cmd_type) {
            case CMD_TYPE_LOGIN: {
                int old_user_id = ctx.user_id;
                handle_login(client_fd, cmd_packet.data, &ctx.user_id);

                // 登录成功后，把会话路径重新放回根目录。
                if (old_user_id == -1 && ctx.user_id != -1) {
                    strcpy(ctx.current_path, "/");
                    ctx.parent_id = 0;
                }
                break;
            }

            case CMD_TYPE_REGISTER: {
                int old_user_id = ctx.user_id;
                handle_register(client_fd, cmd_packet.data, &ctx.user_id);

                // 如果注册函数未来扩展成“注册后自动登录”，这里也能直接兼容。
                if (old_user_id == -1 && ctx.user_id != -1) {
                    strcpy(ctx.current_path, "/");
                    ctx.parent_id = 0;
                }
                break;
            }

            case CMD_TYPE_PWD:
                handle_pwd(client_fd, &ctx);
                break;
            case CMD_TYPE_CD:
                handle_cd(client_fd, &ctx, cmd_packet.data);
                break;
            case CMD_TYPE_LS:
                handle_ls(client_fd, &ctx);
                break;
            case CMD_TYPE_GETS:
                handle_gets(client_fd, &ctx, cmd_packet.data);
                break;
            case CMD_TYPE_PUTS:
                handle_puts(client_fd, &ctx, cmd_packet.data);
                break;
            case CMD_TYPE_RM:
                handle_rm(client_fd, &ctx, cmd_packet.data);
                break;
            case CMD_TYPE_MKDIR:
                handle_mkdir(client_fd, &ctx, cmd_packet.data);
                break;
            default:
                LOG_WARN("命令类型无效，客户端fd=%d，命令类型=%d", client_fd, cmd_type);
                send_msg(client_fd, "指令错误!");
                break;
        }
    }
}
