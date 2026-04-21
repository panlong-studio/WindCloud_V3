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

    memset(&ctx, 0, sizeof(ctx));

    ctx.user_id = -1; // 初始状态未登录
    strcpy(ctx.current_path, "/"); // 初始路径为根目录
    ctx.parent_id = 0; // 根目录的 parent_id 是 0

    while (1) {
        command_packet_t cmd_packet;

        if (recv_command_packet(client_fd, &cmd_packet) <= 0) {
            LOG_INFO("客户端连接断开，客户端fd=%d，当前路径=%s", client_fd, ctx.current_path);
            break;
        }

        cmd_type_t cmd_type = get_packet_cmd_type(&cmd_packet);
        LOG_DEBUG("收到客户端命令，客户端fd=%d，命令类型=%d，数据=%s", client_fd, cmd_type, cmd_packet.data);

        if(ctx.user_id == -1 && cmd_type != CMD_TYPE_LOGIN && cmd_type != CMD_TYPE_REGISTER) {
            LOG_WARN("未登录用户尝试执行命令，客户端fd=%d，命令类型=%d", client_fd, cmd_type);
            send_msg(client_fd, "请先登录!");
            continue;
        }

        switch (cmd_type) {
            case CMD_TYPE_LOGIN:{
                int old_user_id = ctx.user_id;
                handle_login(client_fd, cmd_packet.data, &ctx.user_id);
                if(ctx.user_id != -1 && old_user_id == -1) {
                    strcpy(ctx.current_path, "/");
                    ctx.parent_id = 0;
                    LOG_INFO("用户登录成功，客户端fd=%d，用户ID=%d", client_fd, ctx.user_id);
                } else if(ctx.user_id == -1) {
                    LOG_INFO("用户登录失败，客户端fd=%d", client_fd);
                }
                break;
            }

            case CMD_TYPE_REGISTER:{
                int old_user_id = ctx.user_id;
                handle_register(client_fd, cmd_packet.data, &ctx.user_id);
                //注册成功修改会话状态 之后扩展成注册后自动登录也可兼容
                if(old_user_id==-1&& ctx.user_id!=-1){
                    strcpy(ctx.current_path,"/");
                    ctx.parent_id=0;
                    LOG_INFO("用户注册成功，客户端fd=%d，用户ID=%d",client_fd,ctx.user_id);
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
            case CMD_TYPE_TOUCH:
                handle_touch(client_fd, &ctx, cmd_packet.data);
                break;
            case CMD_TYPE_RM:
                handle_rm(client_fd, &ctx, cmd_packet.data);
                break;
            case CMD_TYPE_MKDIR:
                handle_mkdir(client_fd, &ctx, cmd_packet.data);
                break;
            case CMD_TYPE_RMDIR:
                handle_rmdir(client_fd, &ctx, cmd_packet.data);
                break;
            default:
                LOG_WARN("命令类型无效，客户端fd=%d，命令类型=%d", client_fd, cmd_type);
                send_msg(client_fd, "指令错误!");
                break;
        }
    }
}