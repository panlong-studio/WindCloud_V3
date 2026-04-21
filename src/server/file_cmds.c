#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "file_cmds.h"
#include "dao_vfs.h"
#include "session.h"
#include "protocol.h"
#include "log.h"

// 内部工具函数：逻辑路径拼接（不查硬盘）
static void build_abs_path(const char *current_path,const char *arg,char *result){
    if(arg[0]=='/'){
        strcpy(result,arg);
        return;
    }
    if(strcmp(current_path,"/")==0){
        sprintf(result,"/%s",arg);
    }else{
        sprintf(result,"%s/%s",current_path,arg);
    }
}

// 内部工具函数：获取父级逻辑路径
static void get_parent_path(const char *current_path,char*result){
    if(strcmp(current_path,"/")==0){
        strcpy(result,"/");//如果当前路径是根目录，父级也是根目录
        return;
    }

    strcpy(result,current_path);
    char *last_slash=strrchr(result,'/');//找到最后一个斜杠的位置
    if(last_slash==result){//最后一个斜杠在开头，说明父级路径是根目录
        strcpy(result,"/");//如果父级路径是根目录，保持为"/"
    }else if(last_slash!=NULL){
        *last_slash='\0';//将最后一个斜杠替换为字符串结束符，得到父级路径
    }
}

//内部工具函数：从全路径中提取 file_name
static void extract_file_name(const char *full_path,char *file_name){
    const char *last_slash=strrchr(full_path,'/');
    if(last_slash!=NULL){
        strcpy(file_name,last_slash+1);//复制最后一个斜杠后的部分作为文件名
    }else{
        strcpy(file_name,full_path);//如果没有斜杠，整个路径就是文件名
    }
}

//=============业务命令实现==============

//---------pwd---------
void handle_pwd(int client_fd,ClientContext *ctx) {
    LOG_INFO("客户端请求pwd，客户端fd=%d，当前路径=%s", client_fd, ctx->current_path);
    send_msg(client_fd, ctx->current_path);
}

//---------ls---------
void handle_ls(int client_fd,ClientContext *ctx){
    char buf[4096]={0};
    int ret=dao_list_dir(ctx->user_id,ctx->parent_id,buf);

    if(ret<0){
        LOG_WARN("客户端请求ls失败，客户端fd=%d，当前路径=%s，错误码=%d", client_fd, ctx->current_path, ret);
        send_msg(client_fd, "服务器查询目录失败");
    }else if(ret==0){
        LOG_INFO("客户端请求ls，目录为空，客户端fd=%d，当前路径=%s", client_fd, ctx->current_path);
        send_msg(client_fd, "当前目录为空");
    }else{
        LOG_INFO("客户端请求ls成功，客户端fd=%d，当前路径=%s，返回条目数=%d", client_fd, ctx->current_path, ret);
        send_msg(client_fd, buf);
    }
}

//---------cd---------
void handle_cd(int client_fd, ClientContext *ctx, char *arg) {
    if (arg == NULL || arg[0] == '\0') {
        LOG_WARN("cd命令缺少参数，客户端fd=%d", client_fd);
        send_msg(client_fd, "cd 命令缺少参数");
        return;
    }

    char target_path[256] = {0};

    if (strcmp(arg, "..") == 0) {
        get_parent_path(ctx->current_path, target_path);
        // 这里只是拿到了父级路径，并没有真正切换，后面会继续验证和切换
    }else if(strcmp(arg, ".") == 0) {
        send_msg(client_fd, "进入目录成功");
        return;
    } else {
        build_abs_path(ctx->current_path, arg, target_path);
    }

    // 查数据库 验证目标路径是否存在且是目录
    //target_id 用于接收目标路径的 id，node_type 用于接收目标路径的类型（0 文件，1 目录）
    int target_id, node_type;
    if(dao_get_node_by_path(ctx->user_id,target_path,&target_id,&node_type)!=0){
        LOG_WARN("cd命令目标路径不存在，客户端fd=%d，目标路径=%s", client_fd, target_path);
        send_msg(client_fd, "目标路径不存在");
        return;
    }
    if(node_type!=1){
        LOG_WARN("cd命令目标路径不是目录，客户端fd=%d，目标路径=%s", client_fd, target_path);
        send_msg(client_fd, "目标路径不是目录");
        return;
    }
    // 如果验证通过，更新 Context 状态
    strcpy(ctx->current_path, target_path);
    ctx->parent_id=target_id;//更新父目录 ID cd后工作的目录就是目标路径了，所以父目录 ID 就是目标路径的 ID
    LOG_INFO("cd命令切换目录成功，客户端fd=%d，当前路径=%s", client_fd, ctx->current_path);
    send_msg(client_fd, "进入目录成功");
}

//---------mkdir---------
void handle_mkdir(int client_fd,ClientContext *ctx,char *arg){
    char target_path[256]={0};
    char file_name[64]={0};

    build_abs_path(ctx->current_path,arg,target_path);
    extract_file_name(target_path,file_name);

    int tmp_id,tmp_type;
    if(dao_get_node_by_path(ctx->user_id,target_path,&tmp_id,&tmp_type)==0){
        LOG_WARN("mkdir命令目标路径已存在，客户端fd=%d，目标路径=%s", client_fd, target_path);
        send_msg(client_fd, "目标路径已存在");
        return;
    }

    if(dao_create_node(ctx->user_id,target_path,ctx->parent_id,file_name,1)!=0){
        LOG_WARN("mkdir命令创建目录失败，客户端fd=%d，目标路径=%s", client_fd, target_path);
        send_msg(client_fd, "创建目录失败");
    }
}

//---------touch---------
void handle_touch(int client_fd,ClientContext *ctx,char *arg){
    char target_path[256]={0};
    char file_name[64]={0};

    build_abs_path(ctx->current_path,arg,target_path);
    extract_file_name(target_path,file_name);

    int tmp_id,tmp_type;
    if(dao_get_node_by_path(ctx->user_id,target_path,&tmp_id,&tmp_type)==0){
        LOG_WARN("touch命令目标路径已存在，客户端fd=%d，目标路径=%s", client_fd, target_path);
        send_msg(client_fd, "目标路径已存在");
        return;
    }

    // type=0 表示文件
    if(dao_create_node(ctx->user_id,target_path,ctx->parent_id,file_name,0)==0){
        LOG_INFO("touch命令创建文件成功，客户端fd=%d，目标路径=%s", client_fd, target_path);
        send_msg(client_fd, "创建文件成功");
    } else {
        LOG_WARN("touch命令创建文件失败，客户端fd=%d，目标路径=%s", client_fd, target_path);
        send_msg(client_fd, "创建文件失败");
    }
}

//---------rm---------
void handle_rm(int clietn_fd,ClientContext *ctx,char *arg){
    char target_path[256]={0};
    build_abs_path(ctx->current_path,arg,target_path);

    int target_id,node_type;
    if(dao_get_node_by_path(ctx->user_id,target_path,&target_id,&node_type)!=0){
        LOG_WARN("rm命令目标路径不存在，客户端fd=%d，目标路径=%s", clietn_fd, target_path);
        send_msg(clietn_fd, "目标路径不存在");
        return;
    }

    if(node_type==1){
        LOG_WARN("rm命令目标路径是目录，客户端fd=%d，目标路径=%s", clietn_fd, target_path);
        send_msg(clietn_fd, "目标路径是目录，请使用 rmdir 命令删除目录");
        return;
    }

    if(dao_delete_node(ctx->user_id,target_id)!=0){
        LOG_WARN("rm命令删除文件失败，客户端fd=%d，目标路径=%s", clietn_fd, target_path);
        send_msg(clietn_fd, "删除文件失败");
    } else {
        LOG_INFO("rm命令删除文件成功，客户端fd=%d，目标路径=%s", clietn_fd, target_path);
        send_msg(clietn_fd, "删除文件成功");
    }
}

//---------rmdir---------
void handle_rmdir(int client_fd,ClientContext *ctx,char *arg){
    char target_path[256]={0};
    build_abs_path(ctx->current_path,arg,target_path);

    int target_id,node_type;
    if(dao_get_node_by_path(ctx->user_id,target_path,&target_id,&node_type)!=0){
        LOG_WARN("rmdir命令目标路径不存在，客户端fd=%d，目标路径=%s", client_fd, target_path);
        send_msg(client_fd, "目标路径不存在");
        return;
    }

    // type=0 是文件
    if(node_type==0){
        LOG_WARN("rmdir命令目标路径是文件，客户端fd=%d，目标路径=%s", client_fd, target_path);
        send_msg(client_fd, "目标路径是文件，请使用 rm 命令删除文件");
        return;
    }

    // 检查目录是否为空
    int empty_status=dao_is_dir_empty(ctx->user_id,target_id);

    if(empty_status==-1){
        LOG_WARN("rmdir命令检查目录是否为空失败，客户端fd=%d，目标路径=%s", client_fd, target_path);
        send_msg(client_fd, "服务器无法校验目录状态，无法删除");
        return;
    }
    if(empty_status==0){
        LOG_WARN("rmdir命令目标目录非空，客户端fd=%d，目标路径=%s", client_fd, target_path);
        send_msg(client_fd, "目标目录非空，请先删除目录下的内容");
        return;
    }
    //只有empty_status==1 目录为空时才会走到这里继续删除
    if(dao_delete_node(ctx->user_id,target_id)==0){
        LOG_INFO("rmdir命令删除目录成功，客户端fd=%d，目标路径=%s", client_fd, target_path);
        send_msg(client_fd, "删除目录成功");
    } else {
        LOG_WARN("rmdir命令删除目录失败，客户端fd=%d，目标路径=%s", client_fd, target_path);
        send_msg(client_fd, "删除目录失败,数据库异常");
    }
}